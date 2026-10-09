#include "io/image_loader.h"
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
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
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
    mutable std::atomic_bool interactiveStarted{false};
    std::atomic_bool release{false};
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest& request) const override {
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
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--export-dng"))
        return writeDngFixture(QString::fromLocal8Bit(argv[2])) ? 0 : 2;
    try {
        QTemporaryDir temp(QDir::currentPath() + "/performance-test-XXXXXX"); require(temp.isValid(), "temporary directory");
        auto path = [&](QString name) { auto p=temp.filePath(name); QFile f(p); require(f.open(QIODevice::WriteOnly),"create file"); f.write("data"); return p; };
        const auto a=path("a.png"), b=path("b.png"), c=path("c.png");
        auto decoder=std::make_shared<SlowDecoder>();
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
            QByteArray bytes(24, char(128)); require(file.write(bytes)==bytes.size(),"raw write"); file.close();
            RawImageParameters parameters; parameters.size={4,4}; parameters.format=RawPixelFormat::NV12;
            DecodeRequest request{raw,DecodePurpose::Full,{},parameters}; request.sourceCache=std::make_shared<SourceFrameCache>();
            RawImageDecoder decoder; auto result=decoder.decode(request); require(bool(result.frame),"raw decode");
            require(request.sourceCache->cost()==24,"source cache not populated");
            require(DisplayHistogramAnalyzer::analyze(*result.frame).isValid(),"display histogram");
            require(!DisplayHistogramAnalyzer::analyze(*result.frame, 10000, []{return true;}).isValid(),"display analysis cancellation");
            require(RawPlaneHistogramAnalyzer::analyze(*result.frame).isValid(),"source histogram");
            parameters.yuvMatrix=YuvMatrix::BT601; request.rawParameters=parameters;
            require(bool(decoder.decode(request).frame),"parameter reuse decode");
            request.activeConsumers=std::make_shared<std::atomic_int>(-1);
            require(!decoder.decode(request).frame,"raw cancellation");
        }
        {
            RawImageDecoder decoder;
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
        }
        if (CameraRawDecoder::isAvailable()) {
            const auto path=temp.filePath("sensor.dng");
            require(writeDngFixture(path),"DNG fixture");
            CameraRawDecoder decoder;
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
