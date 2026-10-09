#include "io/image_loader.h"
#include "qml/accounted_texture_factory.h"
#include "qml/thumbnail_image_provider.h"
#include "io/thumbnail_disk_cache.h"
#include "io/raw_image_decoder.h"
#include "io/directory_scanner.h"
#include "io/qt_image_decoder.h"
#include "io/camera_raw_decoder.h"
#include "dng_fixture.h"
#include "core/display_histogram.h"
#include "core/raw_plane_histogram.h"
#include "core/raw_plane_access.h"
#include <QColorSpace>
#include "browser/thumbnail_model.h"
#include "browser/thumbnail_filter_proxy_model.h"
#include "browser/file_clipboard.h"
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardItemModel>
#include <future>
#include <mutex>
#include <stdexcept>
using namespace mvpview;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void pump(const std::function<bool()>& done, int limit = 5000) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QThread::msleep(1); }
    require(done(), "event loop timeout");
}
class SlowDecoder final : public IImageDecoder {
public:
    mutable std::mutex mutex;
    mutable QStringList started;
    mutable std::atomic_int cancelled{0};
    bool canDecode(const QString&) const override { return true; }
    DecodeExecutionMode executionMode(const QString&) const override { return DecodeExecutionMode::Serialized; }
    DecodeResult decode(const DecodeRequest& request) const override {
        { std::lock_guard lock(mutex); started.append(QFileInfo(request.path).fileName()); }
        for (int i=0; i<40; ++i) { if (request.isCancelled()) { ++cancelled; return {{}, "Cancelled"}; } QThread::msleep(2); }
        auto frame = std::make_shared<ImageFrame>();
        frame->metadata.path=request.path; frame->metadata.sourceSize={4,4}; frame->descriptor.size={4,4};
        QImage image(4,4,QImage::Format_RGBA8888); image.fill(Qt::red); frame->storage=image;
        return {frame,{}};
    }
    QStringList order() const { std::lock_guard lock(mutex); return started; }
};
class ParallelProbeDecoder final : public IImageDecoder {
public:
    mutable std::atomic_int active{0}, peak{0};
    mutable std::atomic_int entered{0};
    mutable std::atomic_bool interactiveStarted{false};
    std::atomic_bool release{false};
    qsizetype workingBytes = 0;
    qsizetype interactiveWorkingBytes = -1;
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest& request) const override {
        ++entered;
        const bool interactive=QFileInfo(request.path).fileName()=="interactive.png";
        if (!request.prepareAllocation(interactive && interactiveWorkingBytes>=0
                ? interactiveWorkingBytes : workingBytes)) return {{}, "Cancelled"};
        const int count = ++active;
        int previous = peak.load();
        while (previous < count && !peak.compare_exchange_weak(previous, count)) {}
        if (QFileInfo(request.path).fileName() == "interactive.png") interactiveStarted = true;
        for (int i=0; i<500 && !release && !request.isCancelled(); ++i) QThread::msleep(2);
        --active;
        if (request.isCancelled()) return {{}, "Cancelled"};
        auto frame = std::make_shared<ImageFrame>();
        QImage image(4,4,QImage::Format_RGBA8888); image.fill(Qt::blue);
        frame->storage=image; frame->descriptor.size=image.size(); return {frame,{}};
    }
};
class UnavailableDecoder final : public IImageDecoder {
public:
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest&) const override { return {{}, "Unavailable"}; }
};
class SizedTexture final : public QSGTexture {
public:
    explicit SizedTexture(QSize size) : size_(size) {}
    qint64 comparisonKey() const override { return qint64(quintptr(this)); }
    QSize textureSize() const override { return size_; }
    bool hasAlphaChannel() const override { return true; }
    bool hasMipmaps() const override { return false; }
private:
    QSize size_;
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--export-dng"))
        return writeDngFixture(QString::fromLocal8Bit(argv[2])) ? 0 : 2;
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--sort-benchmark")) {
        QElapsedTimer scan; scan.start();
        auto records=DirectoryScanner::scan(QString::fromLocal8Bit(argv[2]));
        qInfo().noquote()<<QJsonDocument(QJsonObject{{"stage","scan"},{"elapsedMs",scan.elapsed()},
            {"items",records.size()}}).toJson(QJsonDocument::Compact);
        ImageLoader loader(std::make_shared<QtImageDecoder>()); ThumbnailModel model(&loader);
        ThumbnailFilterProxyModel proxy; proxy.setSourceModel(&model);
        QElapsedTimer initial; initial.start(); model.setFiles(std::move(records));
        const int rows=proxy.rowCount();
        qInfo().noquote()<<QJsonDocument(QJsonObject{{"stage","initial_mapping"},{"elapsedMs",initial.elapsed()},
            {"items",rows}}).toJson(QJsonDocument::Compact);
        for (auto mode : {BrowserSortMode::Type,BrowserSortMode::Size,BrowserSortMode::ModifiedTime,BrowserSortMode::Name}) {
            QElapsedTimer timer; timer.start(); proxy.setSortMode(mode); const int count=proxy.rowCount();
            qInfo().noquote()<<QJsonDocument(QJsonObject{{"stage","sort"},{"mode",int(mode)},
                {"elapsedMs",timer.elapsed()},{"items",count},
                {"first",proxy.index(0,0).data().toString()},
                {"last",proxy.index(count-1,0).data().toString()}}).toJson(QJsonDocument::Compact);
        }
        return rows ? 0 : 1;
    }
    try {
        QTemporaryDir temp(QDir::currentPath() + "/performance-test-XXXXXX"); require(temp.isValid(), "temporary directory");
        auto path = [&](QString name) { auto p=temp.filePath(name); QFile f(p); require(f.open(QIODevice::WriteOnly),"create file"); f.write("data"); return p; };
        const auto a=path("a.png"), b=path("b.png"), c=path("c.png");
        auto decoder=std::make_shared<SlowDecoder>();
        {
            require(nominalGpuBytes({3,2},4,4)==96 && nominalGpuBytes({},4)==0,
                    "GPU dimensions/sample count estimate");
            require(nominalGpuBytes({INT_MAX,INT_MAX},16,4)==std::numeric_limits<qsizetype>::max(),
                    "GPU size estimate overflowed");
            auto ledger=std::make_shared<PixelMemoryLedger>();
            auto first=std::make_unique<SizedTexture>(QSize(2,2));
            auto second=std::make_unique<SizedTexture>(QSize(3,1));
            AccountedTextureFactory::trackTexture(first.get(),ledger);
            AccountedTextureFactory::trackTexture(first.get(),ledger);
            AccountedTextureFactory::trackTexture(second.get(),ledger);
            require(ledger->bytes()==0 && ledger->gpuResources()->bytes()==28,
                    "GPU ownership failed to deduplicate resource identities or mixed CPU/GPU");
            first.reset();require(ledger->gpuResources()->bytes()==12,"destroyed GPU resource remained charged");
            second.reset();require(ledger->gpuResources()->bytes()==0,"GPU resource owners leaked");
            auto probe=std::make_shared<SlowDecoder>();ImageLoader loader(probe);loader.setMemoryBudget(100);
            loader.prefetchAdjacentImages({a,b,c},1,{32,32}); // Queued before GPU pressure exists.
            auto registration=loader.accountImagePixels({});
            auto texture=std::make_unique<SizedTexture>(QSize(100,1));
            AccountedTextureFactory::trackTexture(texture.get(),registration.ledger());
            auto preview=std::make_shared<ImageFrame>();preview->metadata.sourceSize={4,4};
            require(loader.residentPixelBytes()==0 && loader.nominalGpuPixelBytes()==400 &&
                    loader.residentResourceBytes()==400 && !loader.canAutomaticallyLoadFull({preview}),
                    "GPU pressure did not pause automatic full loading");
            loader.prefetchAdjacentImages({a,b,c},1,{32,32});
            QElapsedTimer interval;interval.start();
            while(interval.elapsed()<50){QCoreApplication::processEvents();QThread::msleep(1);}
            require(probe->order().isEmpty(),"GPU pressure started new or already queued image prefetch");
            texture.reset();require(loader.residentResourceBytes()==0 && loader.canAutomaticallyLoadFull({preview}),
                    "GPU pressure persisted after resource destruction");
        }
        {
            auto probe=std::make_shared<SlowDecoder>();ImageLoader loader(probe);loader.setMemoryBudget(100);
            auto registration=loader.accountImagePixels({});
            auto texture=std::make_unique<SizedTexture>(QSize(100,1));
            AccountedTextureFactory::trackTexture(texture.get(),registration.ledger());
            loader.updateViewport("pressure",{{a,20},{b,80}},false);
            bool nearby=false,visible=false;
            loader.request(1,{a,DecodePurpose::Thumbnail,{32,32}},[&](auto,const auto& result){nearby=bool(result.frame);},
                RequestOptions{LoadCategory::NearViewport});
            loader.request(2,{b,DecodePurpose::Thumbnail,{32,32}},[&](auto,const auto& result){visible=bool(result.frame);},
                RequestOptions{LoadCategory::VisibleThumbnail});
            pump([&]{return visible;});
            require(!nearby && probe->order()==QStringList{QFileInfo(b).fileName()},
                    "GPU pressure paused visible thumbnails or decoded nearby candidates");
            loader.updateViewport("pressure",{{a,80}},false);
            pump([&]{return nearby;});
            require(probe->order().size()==2,"visible promotion failed under GPU pressure");
        }
        for (const bool unavailable : {false,true}) {
            std::shared_ptr<const IImageDecoder> providerDecoder = unavailable
                ? std::shared_ptr<const IImageDecoder>(std::make_shared<UnavailableDecoder>())
                : std::shared_ptr<const IImageDecoder>(std::make_shared<SlowDecoder>());
            ImageLoader loader(providerDecoder); ThumbnailImageProvider provider(providerDecoder,&loader);
            std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(
                QUrl::toPercentEncoding(a),{128,128}));
            bool finished=false;
            QObject::connect(response.get(),&QQuickImageResponse::finished,&app,[&]{finished=true;});
            pump([&]{return finished;});
            std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
            require(factory && !factory->image().isNull(),"provider returned no image factory");
            response.reset();loader.clearCache();
            require(loader.residentPixelBytes()==factory->image().sizeInBytes(),
                    "provider/factory lifetime lost pixels or retained destroyed response pixels");
            factory.reset();
            require(loader.residentPixelBytes()==0,"provider left pixel owners after response/factory destruction");
        }
        {
            auto ledger=std::make_shared<PixelMemoryLedger>();
            auto frame=std::make_shared<ImageFrame>();
            QImage image(2,2,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(10,20,30,80));
            frame->storage=image;PixelStorageFootprint footprint;frame->appendPixelStorage(footprint);
            frame->pixelOwnership.attach(ledger,std::move(footprint));
            std::unique_ptr<QQuickTextureFactory> factory(AccountedTextureFactory::create(image,ledger));
            require(factory && factory->textureSize()==image.size() && factory->image()==image,
                    "accounted factory changed CPU image/size");
            require(ledger->bytes()==16,"factory counted shared decoder pixels twice");
            frame.reset();image={};
            require(ledger->bytes()==16,"Qt factory pixels vanished when the frame was released");
            factory.reset();require(ledger->bytes()==0,"Qt factory destruction retained CPU pixels");
            QImage high(2,2,QImage::Format_RGBA64);high.fill(QColor(20,30,40,90));
            std::unique_ptr<QQuickTextureFactory> expected(QQuickTextureFactory::textureFactoryForImage(high));
            factory.reset(AccountedTextureFactory::create(high,ledger));
            require(factory && expected && factory->image()==expected->image() &&
                    factory->textureByteCount()==expected->textureByteCount(),"factory altered native Qt conversion");
            require(ledger->bytes()==factory->image().sizeInBytes(),"factory conversion buffer not accounted");
            factory.reset();require(ledger->bytes()==0,"converted factory pixels leaked their account");
        }
        {
            auto tiny=std::make_shared<SlowDecoder>(); ImageLoader loader(tiny);loader.setMemoryBudget(100);
            QVector<ImageFramePtr> retained; int completed=0;
            for (const auto& file:QStringList{a,b,c}) loader.request(++completed,
                {file,DecodePurpose::Full},[&](auto,const auto& result){retained.append(result.frame);});
            pump([&]{return retained.size()==3;});
            require(loader.cachedBytes()==64 && loader.residentPixelBytes()==192,
                    "active frames were lost after LRU eviction");
            require(!loader.canAutomaticallyLoadFull({retained.first()}),
                    "automatic full ignored active resident pressure");
            const auto starts=tiny->order().size();
            loader.prefetchAdjacentImages({a,b,c},1,{32,32});
            const auto rawPath=temp.filePath("pressure.raw");QFile rawFile(rawPath);
            require(rawFile.open(QIODevice::WriteOnly) && rawFile.write(QByteArray(16,'x'))==16,"pressure RAW fixture");
            rawFile.close();RawImageParameters adjacent;adjacent.size={2,2};adjacent.format=RawPixelFormat::Raw16;
            loader.prefetchAdjacentRawFrames(rawPath,adjacent,{32,32});
            QElapsedTimer idle;idle.start();
            while (idle.elapsed()<50) { QCoreApplication::processEvents();QThread::msleep(1); }
            require(tiny->order().size()==starts,"prefetch ignored retained pixel pressure");
            loader.clearCache();retained.clear();
            require(loader.residentPixelBytes()==0,"session references did not release pixels");
        }
        {
            auto ledger=std::make_shared<PixelMemoryLedger>();
            auto frame=std::make_shared<ImageFrame>();
            frame->storage=QImage(2,2,QImage::Format_RGBA8888);
            PixelStorageFootprint first;frame->appendPixelStorage(first);
            frame->pixelOwnership.attach(ledger,std::move(first));
            auto changed=std::make_shared<ImageFrame>(*frame);
            changed->storage=QImage(2,2,QImage::Format_RGBA64);
            PixelStorageFootprint next;changed->appendPixelStorage(next);
            changed->pixelOwnership.attach(ledger,std::move(next));
            require(ledger->bytes()==48,"copied frame inherited stale pixel ownership");
            frame.reset(); require(ledger->bytes()==32,"released source frame retained its pixels");
            auto metadata=std::make_shared<ImageFrame>(*changed);
            PixelStorageFootprint same;metadata->appendPixelStorage(same);
            metadata->pixelOwnership.attach(ledger,std::move(same));
            changed.reset(); require(ledger->bytes()==32,"metadata frame lost shared resident pixels");
            std::weak_ptr<PixelMemoryLedger> alive=ledger;ledger.reset();
            require(!alive.expired(),"pixel owner did not preserve ledger lifetime");
            metadata.reset();require(alive.expired(),"last pixel owner retained its ledger");
        }
        {
            auto ledger=std::make_shared<PixelMemoryLedger>();
            auto sourceCache=std::make_unique<SourceFrameCache>(ledger);
            auto source=std::make_shared<SourceFrame>(); source->bytes=QByteArray(64,'x');
            sourceCache->put("sensor",source);
            auto planes=std::make_shared<PlaneBufferSet>(); planes->storage=source->bytes;
            planes->displayImage=QImage(2,2,QImage::Format_RGBA8888); planes->displayImage.fill(Qt::blue);
            auto frame=std::make_shared<ImageFrame>(); frame->storage=planes; frame->uploadImage=planes->displayImage;
            frame->uploadPlanes=planes;
            require(frame->byteSize()==80,"frame aliases counted pixels repeatedly");
            WeightedLruCache<ImageFrame> cache(512);
            cache.setObserver([ledger](const auto& image,bool added){
                PixelStorageFootprint footprint;image->appendPixelStorage(footprint);ledger->adjust(footprint,added);
            });
            cache.put("display",frame,frame->byteSize());
            auto metadata=std::make_shared<ImageFrame>(*frame);
            cache.put("metadata",metadata,metadata->byteSize());
            require(ledger->bytes()==80,"source/display/metadata storage not deduplicated");
            cache.erase("display"); require(ledger->bytes()==80,"shared eviction released live cache storage");
            auto uploaded=std::make_shared<ImageFrame>(*metadata);
            uploaded->uploadImage=QImage(2,2,QImage::Format_RGBA64); uploaded->uploadImage.fill(Qt::red);
            cache.put("metadata",uploaded,uploaded->byteSize());
            require(ledger->bytes()==112,"replacement did not account distinct upload pixels");
            auto detached=std::make_shared<ImageFrame>(*uploaded);
            auto uploadPlanes=std::make_shared<PlaneBufferSet>(*planes);
            uploadPlanes->storage[0]='y'; detached->uploadPlanes=uploadPlanes;
            cache.put("detached",detached,detached->byteSize());
            require(ledger->bytes()==176,"detached upload/source buffers were conflated");
            cache.erase("detached"); require(ledger->bytes()==112,"detached upload eviction lost shared source");
            PixelStorageFootprint shared; uploaded->appendPixelStorage(shared);
            auto concurrent=std::async(std::launch::async,[ledger,shared]{
                for (int i=0;i<1000;++i) { ledger->adjust(shared,true); ledger->adjust(shared,false); }
            });
            for (int i=0;i<1000;++i) { ledger->adjust(shared,true); ledger->adjust(shared,false); }
            concurrent.get(); require(ledger->bytes()==112,"concurrent source/cache ownership drifted");
            sourceCache->clear(); require(ledger->bytes()==112,"source eviction lost frame-owned bytes");
            sourceCache->put("sensor",source);
            cache.clear(); require(ledger->bytes()==64,"cache clear retained orphaned upload storage");
            sourceCache.reset(); require(ledger->bytes()==0,"cache destructor left ledger references");
        }
        {
            auto pressured=std::make_shared<ParallelProbeDecoder>();
            pressured->workingBytes=400LL*1024*1024;
            pressured->interactiveWorkingBytes=32LL*1024*1024;
            ImageLoader loader(pressured); QVector<LoadHandle> visible;
            for (int i=0;i<20;++i) visible.append(loader.request(i,
                {path(QString("visible-pressure%1.png").arg(i)),DecodePurpose::Preview},
                [](auto,const auto&){},RequestOptions{LoadCategory::VisibleThumbnail}));
            pump([&]{return pressured->entered.load()>=qBound(2,QThread::idealThreadCount()-1,6)-1;});
            bool delivered=false;
            QElapsedTimer interactionDelay; interactionDelay.start();
            loader.request(100,{path("interactive.png"),DecodePurpose::Preview},
                [&](auto,const auto& result){require(bool(result.frame),"pressured interactive result");delivered=true;},
                RequestOptions{LoadCategory::Interactive});
            pump([&]{return pressured->interactiveStarted.load();},800);
            qInfo()<<"Budget pressure interactive start:"<<interactionDelay.elapsed()<<"ms";
            for (const auto& handle:visible) handle.cancel();
            pressured->release=true;
            pump([&]{return delivered && pressured->active.load()==0;});
            require(pressured->peak.load()<=6,"pressure bypassed decode concurrency");
        }
        {
            auto constrained=std::make_shared<ParallelProbeDecoder>();
            constrained->workingBytes=400LL*1024*1024;
            ImageLoader loader(constrained); int completed=0;
            auto first=loader.request(1,{a,DecodePurpose::Preview},[&](auto,const auto&){++completed;});
            auto second=loader.request(2,{b,DecodePurpose::Preview},[&](auto,const auto&){++completed;});
            pump([&]{return constrained->active.load()==1;});
            QThread::msleep(60); QCoreApplication::processEvents();
            require(constrained->peak.load()==1,"decode working budget admitted concurrent oversized pixels");
            constrained->release=true;
            pump([&]{return completed==2;});
            require(constrained->peak.load()==1,"decode working reservation leaked across result transfer");
        }
        if (QGuiApplication::platformName()==QStringLiteral("offscreen")) {
            FileClipboard::setPaths({a},true);
            require(FileClipboard::hasFiles() && FileClipboard::contents().paths==QStringList{a} &&
                    FileClipboard::contents().cut,"file clipboard URL/cut compatibility");
            auto* ordinary=new QMimeData; ordinary->setText(QStringLiteral("ordinary text"));
            QGuiApplication::clipboard()->setMimeData(ordinary);
            require(!FileClipboard::hasFiles(),"clipboard change retained old files");
            auto* paths=new QMimeData; paths->setText(a+QLatin1Char('\n')+b);
            QGuiApplication::clipboard()->setMimeData(paths);
            require(FileClipboard::hasFiles() && FileClipboard::contents().paths==QStringList{a,b} &&
                    !FileClipboard::contents().cut,"plain path clipboard compatibility");
            FileClipboard::clear(); require(!FileClipboard::hasFiles(),"empty clipboard retained file state");
        }
        {
            const auto budget=std::make_shared<ResultBufferBudget>(100);
            auto blocker=budget->reserve(100,[]{return false;});
            auto earlier=std::async(std::launch::async,[budget]{
                return budget->reserve(80,[]{return false;},[]{return 10;});
            });
            pump([&]{return budget->snapshot().waiters==1;});
            std::atomic_int priority{0};
            auto promoted=std::async(std::launch::async,[budget,&priority]{
                return budget->reserve(80,[]{return false;},[&]{return priority.load();});
            });
            pump([&]{return budget->snapshot().waiters==2;});
            priority=100; blocker.reset();
            const auto ready=promoted.wait_for(std::chrono::seconds(1));
            if (ready!=std::future_status::ready) budget->close();
            require(ready==std::future_status::ready,"promoted working request did not wake");
            auto interactive=promoted.get();
            require(interactive && earlier.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,
                    "working budget did not promote a waiting visible request");
            interactive.reset(); auto remaining=earlier.get();
            require(bool(remaining),"working budget starved the remaining request");
        }
        {
            const auto budget=std::make_shared<ResultBufferBudget>(100);
            auto first=budget->reserve(60,[]{return false;});
            auto waiting=std::async(std::launch::async,[budget]{return budget->reserve(50,[]{return false;});});
            pump([&]{return budget->snapshot().waiters==1;});
            require(budget->snapshot().bytes==60,"result budget admitted excess bytes");
            first.reset(); auto second=waiting.get();
            require(second && budget->snapshot().bytes==50 && budget->snapshot().peakBytes<=100,"result reservation did not wake/release");
            std::atomic_bool cancelled=false;
            auto cancelWaiting=std::async(std::launch::async,[budget,&cancelled]{return budget->reserve(60,[&]{return cancelled.load();});});
            pump([&]{return budget->snapshot().waiters==1;}); cancelled=true;
            require(!cancelWaiting.get() && budget->snapshot().bytes==50,"result wait ignored cancellation");
            second.reset();
            auto oversized=budget->reserve(180,[]{return false;});
            require(oversized && budget->snapshot().bytes==180,"explicit oversized image rejected");
            auto closing=std::async(std::launch::async,[budget]{return budget->reserve(1,[]{return false;});});
            pump([&]{return budget->snapshot().waiters==1;}); budget->close();
            require(!closing.get(),"closing result budget left a worker waiting");
            oversized.reset(); require(budget->snapshot().bytes==0,"result reservation leaked bytes");
        }
        {
            auto parallel=std::make_shared<ParallelProbeDecoder>(); ImageLoader loader(parallel);
            QVector<LoadHandle> background; bool backgroundDelivered=false;
            for (int i=0;i<20;++i) background.append(loader.request(i,
                {path(QString("background%1.png").arg(i)),DecodePurpose::Preview,{32,32}},
                [&](auto,const auto&){backgroundDelivered=true;}, RequestOptions{LoadCategory::Background}));
            pump([&]{return parallel->active.load()>0;});
            bool foregroundDelivered=false;
            loader.request(100,{path("interactive.png"),DecodePurpose::Preview,{32,32}},
                [&](auto,const auto& result){require(bool(result.frame),"reserved slot result");foregroundDelivered=true;},
                RequestOptions{LoadCategory::Interactive});
            pump([&]{return parallel->interactiveStarted.load();},800);
            require(parallel->peak.load()<=6,"parallel decode cap");
            for (const auto& handle : background) handle.cancel();
            parallel->release=true;
            pump([&]{return foregroundDelivered && parallel->active.load()==0;});
            require(!backgroundDelivered,"cancelled background callback");
        }
        {
            ImageLoader loader(decoder); int completed=0;
            auto first=loader.request(1,{a,DecodePurpose::Preview,{32,32}},[&](auto,const auto& r){require(bool(r.frame),"first frame");++completed;},100);
            pump([&]{return !decoder->order().isEmpty();});
            auto obsolete=loader.request(2,{b,DecodePurpose::Preview,{32,32}},[&](auto,const auto&){throw std::runtime_error("cancelled callback");},0);
            obsolete.cancel();
            loader.request(3,{c,DecodePurpose::Preview,{32,32}},[&](auto,const auto& r){require(bool(r.frame),"visible frame");++completed;},100);
            pump([&]{return completed==2;});
            require(!decoder->order().contains("b.png"),"cancelled queued task was decoded");
        }
        {
            decoder->started.clear(); ImageLoader loader(decoder); int completed=0;
            auto cancelled=loader.request(1,{a,DecodePurpose::Full},[](auto,const auto&){throw std::runtime_error("cancelled result delivered");});
            loader.request(2,{a,DecodePurpose::Full},[&](auto,const auto& r){require(bool(r.frame),"shared result");++completed;});
            cancelled.cancel(); pump([&]{return completed==1;});
            require(decoder->order().size()==1,"shared request decoded twice");
            auto active=loader.request(3,{b,DecodePurpose::Full},[](auto,const auto&){throw std::runtime_error("abandoned callback");});
            pump([&]{return decoder->order().size()==2;}); active.cancel();
            loader.request(4,{b,DecodePurpose::Full},[&](auto,const auto& r){require(bool(r.frame),"reattached request aborted");++completed;});
            pump([&]{return completed==2;});
            require(decoder->cancelled>0,"running decoder did not cancel");
        }
        {
            decoder->started.clear(); ImageLoader loader(decoder); int completed=0;
            loader.request(0,{a,DecodePurpose::Full},[&](auto,const auto&){++completed;},100);
            pump([&]{return !decoder->order().isEmpty();});
            loader.request(1,{b,DecodePurpose::Preview,{256,256}},[&](auto,const auto&){++completed;},20);
            loader.request(2,{c,DecodePurpose::Preview,{256,256}},[&](auto,const auto&){++completed;},60);
            loader.request(3,{b,DecodePurpose::Preview,{256,256}},[&](auto,const auto&){++completed;},120);
            pump([&]{return completed==4;}); require(decoder->order().at(1)=="b.png","merged priority not promoted");
        }
        {
            decoder->started.clear(); ImageLoader loader(decoder); int completed=0;
            loader.updateViewport("one", {{a,80}}, true);
            auto analysis=loader.requestAnalysis([](const auto&){return QVariantMap{{"done",true}};},
                [&](const auto&){throw std::runtime_error("background ran during fast scroll");});
            loader.request(1,{a,DecodePurpose::Thumbnail,{256,256}},[&](auto,const auto& r){require(bool(r.frame),"visible scrolling result");++completed;});
            pump([&]{return completed==1;}); analysis.cancel();
            loader.updateViewport("one", {}, false);
            loader.requestAnalysis([](const auto&){return QVariantMap{{"done",true}};},
                [&](const auto& r){require(r.value("done").toBool(),"analysis result");++completed;});
            pump([&]{return completed==2;});
        }
        {
            ThumbnailDiskCache cache(temp.filePath("cache"));
            QImage image(128,128,QImage::Format_RGBA8888); image.fill(QColor(10,20,30,127));
            require(cache.store("alpha",image,{4000,3000},16),"cache write");
            auto read=cache.load("alpha"); require(!read.isNull() && read.pixelColor(0,0).alpha()==127,"alpha lost");
            require(read.text("mvpview.sourceWidth")=="4000","source dimensions lost");
            QImage opaque(384,384,QImage::Format_RGBA8888); opaque.fill(Qt::green);
            auto writer=std::async(std::launch::async,[&]{for(int i=0;i<80;++i) require(cache.store(QString::number(i),opaque,{4000,3000},8),"concurrent write");});
            for(int i=0;i<100;++i) require(!cache.load("alpha").isNull(),"read blocked/corrupt during writes");
            writer.get();
        }
        {
            auto raw=temp.filePath("frame.yuv"); QFile file(raw); require(file.open(QIODevice::WriteOnly),"raw create");
            QByteArray bytes(24, char(128)); bytes.append(QByteArray(24,char(200)));
            require(file.write(bytes)==bytes.size(),"raw write"); file.close();
            RawImageParameters parameters; parameters.size={4,4}; parameters.format=RawPixelFormat::NV12;
            DecodeRequest request{raw,DecodePurpose::Full,{},parameters}; request.sourceCache=std::make_shared<SourceFrameCache>();
            qsizetype rawWorking=0;
            request.reserveWorkingMemory=[&](qsizetype bytes){rawWorking=bytes;return false;};
            require(!RawImageDecoder{}.decode(request).frame && rawWorking>24 && request.sourceCache->cost()==0,
                    "RAW allocated source pixels before working admission");
            request.reserveWorkingMemory={};
            RawImageDecoder decoder; auto result=decoder.decode(request); require(bool(result.frame),"raw decode");
            require(request.sourceCache->cost()==24,"source cache not populated");
            {
                ImageLoader loader(std::make_shared<RawImageDecoder>()); loader.setMemoryBudget(100);
                ImageFramePtr retained; bool done=false;
                loader.request(400,{raw,DecodePurpose::Full,{},parameters},[&](auto,const auto& decoded){
                    retained=decoded.frame;done=true;
                });
                pump([&]{return done;});
                require(retained && retained->byteSize()==88 && loader.cachedBytes()==88,
                        "loader counted shared NV12 source bytes twice or evicted the frame");
                require(loader.isCached({raw,DecodePurpose::Full,{},parameters}),
                        "deduplicated RAW cache did not retain its admissible frame");
                require(loader.residentPixelBytes()==88,"resident source/display bytes counted twice");
                loader.clearCache();
                require(loader.cachedBytes()==0 && loader.residentPixelBytes()==88,
                        "evicted active frame disappeared from resident pixels");
                retained.reset();
                require(loader.residentPixelBytes()==0,"released active frame retained resident pixels");
            }
            require(DisplayHistogramAnalyzer::analyze(*result.frame).isValid(),"display histogram");
            require(!DisplayHistogramAnalyzer::analyze(*result.frame, 10000, []{return true;}).isValid(),"display analysis cancellation");
            require(RawPlaneHistogramAnalyzer::analyze(*result.frame).isValid(),"source histogram");
            const auto firstPlane=std::get<std::shared_ptr<const PlaneBufferSet>>(result.frame->storage);
            parameters.yuvMatrix=YuvMatrix::BT601; request.rawParameters=parameters;
            auto recolored=decoder.decode(request);
            require(recolored.frame && request.sourceCache->cost()==24 &&
                    std::get<std::shared_ptr<const PlaneBufferSet>>(recolored.frame->storage)->storage.constData()==firstPlane->storage.constData(),
                    "display transform reread RAW source bytes");
            parameters.format=RawPixelFormat::NV21; request.rawParameters=parameters;
            require(decoder.decode(request).frame && request.sourceCache->cost()==48,"RAW source layout identity");
            parameters.frameIndex=1; request.rawParameters=parameters;
            auto nextFrame=decoder.decode(request);
            require(nextFrame.frame && request.sourceCache->cost()==72,"RAW source frame identity");
            const auto sample=RawPlaneAccessor(*nextFrame.frame).yuvAtSourcePixel({0,0});
            require(sample && sample->y==200,"RAW cached source used another frame");
            request.activeConsumers=std::make_shared<std::atomic_int>(-1);
            require(!decoder.decode(request).frame,"raw cancellation");
        }
        {
            RawImageDecoder decoder;
            for (auto format : {RawPixelFormat::NV12, RawPixelFormat::Raw16}) {
                RawImageParameters parameters; parameters.size={1536,1024}; parameters.format=format;
                auto path=temp.filePath("large-display.raw"); QFile file(path);
                require(file.open(QIODevice::WriteOnly),"CPU full source fixture");
                QByteArray bytes(frameByteSize(parameters),char(128));
                require(file.write(bytes)==bytes.size(),"CPU full source write"); file.close();
                DecodeRequest request{path,DecodePurpose::Full,{},parameters};
                auto fallback=decoder.decode(request);
                request.requireDisplayImage=true;
                auto display=decoder.decode(request);
                require(fallback.frame && display.frame && fallback.frame->qImage()->width()==960 &&
                        display.frame->qImage()->size()==parameters.size,"RAW/YUV CPU full display lost native dimensions");
            }
            for (auto format : {RawPixelFormat::NV12, RawPixelFormat::NV21, RawPixelFormat::I420,
                                RawPixelFormat::P010, RawPixelFormat::MipiRaw10, RawPixelFormat::MipiRaw12, RawPixelFormat::Raw16}) {
                for (bool littleEndian : {true, false}) {
                    RawImageParameters parameters; parameters.size={8,4}; parameters.format=format;
                    parameters.littleEndian=littleEndian; parameters.orientation=ImageOrientation::Rotate90Clockwise;
                    auto path=temp.filePath("format.raw"); QFile file(path);
                    require(file.open(QIODevice::WriteOnly),"format fixture");
                    QByteArray bytes(frameByteSize(parameters), char(0)); file.write(bytes); file.close();
                    auto result=decoder.decode({path,DecodePurpose::Full,{},parameters});
                    require(bool(result.frame),"format decode");
                    RawPlaneAccessor accessor(*result.frame); require(accessor.isValid(),"source plane accessor");
                    require(accessor.displaySize()==QSize(4,8),"raw orientation");
                    require(parameters.isYuv() ? bool(accessor.yuvAtSourcePixel({0,0})) : bool(accessor.bayerAtSourcePixel({0,0})),"exact samples");
                }
            }
        }
        {
            QtImageDecoder decoder;
            for (QString suffix : {"png","jpg","bmp"}) {
                QImage image(8,4,QImage::Format_RGBA8888); image.fill(QColor(30,100,180));
                auto path=temp.filePath("encoded."+suffix); require(image.save(path),"encoded fixture");
                DecodeRequest refused{path,DecodePurpose::Full}; qsizetype encodedWorking=0;
                refused.reserveWorkingMemory=[&](qsizetype bytes){encodedWorking=bytes;return false;};
                require(!decoder.decode(refused).frame && encodedWorking>=8*4*16,
                        "encoded decode bypassed working admission");
                auto result=decoder.decode({path,DecodePurpose::Full});
                require(bool(result.frame) && result.frame->descriptor.size==QSize(8,4),"encoded decode");
                require(result.frame->metadata.metadataReaderName.isEmpty(),"EXIF delayed display");
            }
            QImage high(8,4,QImage::Format_RGBA64); high.fill(QColor::fromRgbF(.1234,.3456,.789));
            high.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
            auto path=temp.filePath("high.png"); require(high.save(path),"16bit fixture");
            auto result=decoder.decode({path,DecodePurpose::Full});
            require(bool(result.frame) && result.frame->descriptor.storageBits==16,"16bit preserved");
            ImageLoader loader(std::make_shared<QtImageDecoder>()); bool loaded=false;
            loader.request(1,{path,DecodePurpose::Full},[&](auto,const auto& r) {
                require(bool(r.frame) && r.frame->uploadImage.format()==QImage::Format_RGBA16FPx4,"worker upload conversion"); loaded=true;
            });
            pump([&]{return loaded;});
            ThumbnailModel model(&loader); const QFileInfo info(path);
            model.appendFiles({{path,info.fileName(),info.size(),info.lastModified(),false,"png"}});
            const auto originalUrl=model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString();
            int displayInvalidations=0, resets=0;
            QObject::connect(&model,&QAbstractItemModel::modelReset,[&]{++resets;});
            QObject::connect(&model,&QAbstractItemModel::dataChanged,[&](auto,auto,const auto& roles){
                require(roles==QList<int>{ThumbnailModel::ThumbnailUrlRole},"display settings invalidated unrelated model roles");
                ++displayInvalidations;
            });
            loader.clearCache();
            require(displayInvalidations==0 && originalUrl==model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString(),
                    "ordinary cache clearing churned image URLs");
            QtImageDecoder::setPreserveHighBitDepth(false); loader.clearCache();
            require(displayInvalidations==1 && originalUrl!=model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString(),
                    "display settings left Qt's image URL unchanged");
            loaded=false;
            loader.request(2,{path,DecodePurpose::Full},[&](auto,const auto& r){
                require(r.frame && r.frame->qImage()->depth()==32,"display settings reused old high-bit pixels"); loaded=true;
            });
            pump([&]{return loaded;});
            QtImageDecoder::setPreserveHighBitDepth(true); loader.clearCache();
            require(displayInvalidations==2 && resets==0 && model.rowCount()==1 &&
                    originalUrl==model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString(),
                    "display configuration identity is unstable or reset the directory");
        }
        if (CameraRawDecoder::isAvailable()) {
            const auto path=temp.filePath("sensor.dng");
            require(writeDngFixture(path),"DNG fixture");
            CameraRawDecoder decoder;
            DecodeRequest refused{path,DecodePurpose::Full}; qsizetype cameraWorking=0;
            refused.reserveWorkingMemory=[&](qsizetype bytes){cameraWorking=bytes;return false;};
            require(!decoder.decode(refused).frame && cameraWorking>=1536LL*1024*16,
                    "LibRaw unpack bypassed working admission");
            auto native=decoder.decode({path,DecodePurpose::Full});
            require(bool(native.frame) && native.frame->rawParameters.has_value(),"LibRaw DNG sensor decode");
            auto parameters=*native.frame->rawParameters;
            parameters.demosaic=false;
            DecodeRequest cfa{path,DecodePurpose::Full,{},parameters};
            cfa.sourceCache=std::make_shared<SourceFrameCache>();
            auto fresh=decoder.decode(cfa);
            auto cached=decoder.decode(cfa);
            require(bool(fresh.frame) && bool(cached.frame),"DNG source reuse");
            require(fresh.frame->qImage()->size()==QSize(1536,1024),"DNG full CFA dimensions");
            require(*fresh.frame->qImage()==*cached.frame->qImage(),"cached CFA changed full-resolution pixels");
            RawPlaneAccessor source(*cached.frame);
            auto sample=source.bayerAtSourcePixel({50,70});
            require(sample && sample->value==64+(50*31+70*11)%16000,"DNG exact sensor sample");
            DecodeRequest defaults{path,DecodePurpose::Full}; defaults.sourceCache=cfa.sourceCache;
            auto reset=decoder.decode(defaults);
            require(bool(reset.frame) && reset.frame->rawParameters->demosaic,"source retained a request override");
            require(*native.frame->qImage()==*reset.frame->qImage(),"cached file defaults changed developed pixels");
            DecodeRequest cpuFull{path,DecodePurpose::Full}; cpuFull.requireDisplayImage=true;
            auto freshCpu=decoder.decode(cpuFull);
            cpuFull.sourceCache=cfa.sourceCache;
            auto cachedCpu=decoder.decode(cpuFull);
            require(freshCpu.frame && cachedCpu.frame && freshCpu.frame->qImage()->size()==QSize(1536,1024),
                    "CPU full RAW display used the GPU fallback dimensions");
            require(*freshCpu.frame->qImage()==*cachedCpu.frame->qImage(),"cached CPU full RAW pixels changed");
            cfa.activeConsumers=std::make_shared<std::atomic_int>(-1);
            require(!decoder.decode(cfa).frame,"cancelled cached DNG source rendered");
            ImageLoader loader(std::make_shared<CameraRawDecoder>());
            ThumbnailModel model(&loader);
            const QFileInfo info(path);
            model.appendFiles({{path,info.fileName(),info.size(),info.lastModified(),false,"dng"}});
            const auto before=model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString();
            int invalidations=0;
            QObject::connect(&model,&QAbstractItemModel::dataChanged,
                [&](auto,auto,const auto& roles){if(roles.contains(ThumbnailModel::ThumbnailUrlRole)) ++invalidations;});
            ImageFramePtr first; bool thumbnailDone=false;
            loader.request(1,{path,DecodePurpose::Thumbnail,{64,64}},[&](auto,const auto& result){
                first=result.frame; thumbnailDone=true;
                if (!first) qCritical()<<"DNG thumbnail:"<<result.error;
            });
            pump([&]{return thumbnailDone;},10000);
            require(bool(first),"DNG thumbnail decode");
            require(first->qImage()->pixelColor(0,0).alpha()==255,"DNG thumbnail must be opaque");
            require(before==model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString() && invalidations==0,
                    "discovered RAW defaults changed thumbnail URL");
            bool memoryHit=false;
            loader.request(2,{path,DecodePurpose::Thumbnail,{64,64}},[&](auto,const auto& result){memoryHit=result.frame==first;});
            require(memoryHit,"discovered RAW defaults lost the decoded memory entry");
            ImageFramePtr gpuFull, cpuPreview, fullDisplay;
            bool gpuDone=false, previewDone=false, fullDone=false;
            loader.request(3,{path,DecodePurpose::Full},[&](auto,const auto& result){gpuFull=result.frame;gpuDone=true;});
            pump([&]{return gpuDone;},10000);
            require(gpuFull && gpuFull->qImage()->width()==960,"GPU RAW fallback fixture");
            DecodeRequest galleryPreview{path,DecodePurpose::Preview,{1536,1536}};
            galleryPreview.requireDisplayImage=true;
            loader.request(4,galleryPreview,[&](auto,const auto& result){cpuPreview=result.frame;previewDone=true;});
            pump([&]{return previewDone;},10000);
            require(cpuPreview && cpuPreview->qImage()->size()==QSize(1536,1024),"gallery reused an undersized GPU full fallback");
            loader.request(5,cpuFull,[&](auto,const auto& result){fullDisplay=result.frame;fullDone=true;});
            pump([&]{return fullDone;},10000);
            require(fullDisplay && *fullDisplay->qImage()==*cpuPreview->qImage(),"gallery full upgrade changed RAW display pixels");
            require(fullDisplay->uploadImage.isNull() && cpuPreview->uploadImage.isNull(),
                    "CPU-only gallery allocated an unused GPU upload buffer");
            memoryHit=false;
            loader.request(6,galleryPreview,[&](auto,const auto& result){memoryHit=result.frame==fullDisplay;});
            require(memoryHit && loader.isCached(galleryPreview),"gallery did not reuse its sufficient CPU full cache");
            auto edited=*loader.rawParameters(path); edited.demosaic=!edited.demosaic;
            loader.setRawParameters(path,edited);
            require(invalidations==1 && before!=model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString(),
                    "RAW editing did not invalidate thumbnail pixels");
        }
        {
            auto folder=temp.filePath("many"); QDir().mkpath(folder);
            for(int i=0;i<10000;++i) { QFile file(folder+QString("/image%1.png").arg(i)); require(file.open(QIODevice::WriteOnly),"scan fixture"); }
            DirectoryScanner scanner; scanner.setBatchBackpressure(true); int count=0; int first=0; bool finished=false;
            QObject::connect(&scanner,&DirectoryScanner::scanBatchReady,[&](auto,const auto& files,auto generation){if(!count) first=files.size();count+=files.size();scanner.acknowledgeBatch(generation);});
            QObject::connect(&scanner,&DirectoryScanner::scanFinished,[&](auto,const auto& files,auto){require(files.size()==10000,"scan count"); finished=true;});
            scanner.scanAsync(folder); pump([&]{return finished;},15000);
            require(first<=32 && count==10000,"progressive first batch");
            {
                DirectoryScanner blocked; blocked.setBatchBackpressure(true); int batches=0;
                QObject::connect(&blocked,&DirectoryScanner::scanBatchReady,[&](auto,auto,auto){++batches;});
                blocked.scanAsync(folder); pump([&]{return batches==8;});
                QElapsedTimer wait; wait.start();
                while (wait.elapsed()<100) { QCoreApplication::processEvents(); QThread::msleep(1); }
                require(batches==8,"bounded scan publication");
                blocked.cancel(); // Destruction must unblock a producer waiting for credits.
            }
            auto records=DirectoryScanner::scan(folder);
            ImageLoader loader(std::make_shared<QtImageDecoder>()); ThumbnailModel model(&loader); ThumbnailFilterProxyModel proxy;
            proxy.setSourceModel(&model); model.appendFiles(records);
            require(proxy.index(2,0).data().toString()=="image2.png","natural sorting");
            proxy.setSortMode(BrowserSortMode::Type);
            require(proxy.index(2,0).data().toString()=="image2.png","equal prepared types lost natural order");
            const auto types=temp.filePath("sort-types"); QDir().mkpath(types);
            for (const auto& name : {"image10.png","image2.png","image10.jpg","image2.jpg"}) {
                QFile file(types+"/"+name); require(file.open(QIODevice::WriteOnly),"type sort fixture");
            }
            QDir().mkpath(types+"/folder10"); QDir().mkpath(types+"/folder2");
            auto mixed=DirectoryScanner::scan(types); model.setFiles(mixed);
            const QStringList expected{"folder2","folder10","image2.jpg","image10.jpg","image2.png","image10.png"};
            for (int i=0;i<expected.size();++i)
                require(proxy.index(i,0).data().toString()==expected.at(i),"prepared mixed-type ordering");
            for (auto& record : mixed) {record.nameSortKey.reset();record.typeSortKey.reset();record.fileType.clear();}
            model.setFiles(mixed);
            for (int i=0;i<expected.size();++i)
                require(proxy.index(i,0).data().toString()==expected.at(i),"prepared ordering differs from uncached collation");
            QStandardItemModel generic;
            for (const auto& record : mixed) {
                auto* item=new QStandardItem(record.fileName);
                item->setData(record.isDirectory,ThumbnailModel::DirectoryRole);
                item->setData(record.isDirectory ? QStringLiteral("Folder") : QFileInfo(record.fileName).suffix(),ThumbnailModel::TypeRole);
                generic.appendRow(item);
            }
            proxy.setSourceModel(&generic);
            for (int i=0;i<expected.size();++i)
                require(proxy.index(i,0).data().toString()==expected.at(i),"source replacement retained stale thumbnail fields");
        }
        {
            const auto root=temp.filePath("recursive");
            for (const auto& relative : {"branch2/leaf/image.png", "branch10/image.png", "documents/note.txt", "root.png"}) {
                const auto name=root+"/"+relative;
                QDir().mkpath(QFileInfo(name).absolutePath());
                QFile file(name); require(file.open(QIODevice::WriteOnly),"recursive fixture");
            }
            const auto records=DirectoryScanner::scanImageFoldersRecursively(root);
            require(records.size()==4,"recursive folders and direct images count");
            require(records.first().fileName=="branch2" && records.last().fileName=="root.png","recursive natural order");
            for (const auto& record : records) {
                require(record.nameSortKey.has_value() && record.typeSortKey.has_value(),"recursive prepared keys");
                require(!record.fileName.startsWith("documents"),"document-only branch excluded");
            }
            require(DirectoryScanner::scanImageFoldersRecursively(root, std::make_shared<std::atomic_bool>(true)).isEmpty(),"recursive cancellation");
        }
        qInfo()<<"All performance regression tests passed";
        return 0;
    } catch(const std::exception& error) { qCritical()<<error.what(); return 1; }
}
