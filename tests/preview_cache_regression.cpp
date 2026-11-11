#include "io/image_loader.h"
#include "io/thumbnail_disk_cache.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QThread>
#include <stdexcept>
#include <iostream>
using namespace mvpview;
void check(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
template<class F> void waitFor(F condition) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed()<10000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    check(condition(),"timeout");
}
class Decoder : public IImageDecoder {
public:
    mutable std::atomic_int decodes{0};
    mutable std::atomic_bool started{false}, hold{false};
    std::atomic_int variant{0};
    QString cacheIdentity() const override { return QString::number(variant.load()); }
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest& request) const override {
        ++decodes;
        if (request.purpose==DecodePurpose::Preview && request.maximumSize.width()==512) {
            started=true;
            while (hold && !request.isCancelled()) QThread::msleep(1);
        }
        auto frame=std::make_shared<ImageFrame>();
        frame->metadata.path=request.path; frame->metadata.sourceSize={1600,1200};
        frame->descriptor.size=request.purpose==DecodePurpose::Full ? QSize(1600,1200) : request.maximumSize;
        frame->rawParameters=request.rawParameters;
        QImage image(frame->descriptor.size,QImage::Format_RGBA8888); image.fill(QColor(20,40,60,128));
        frame->storage=std::move(image); return {frame,{}};
    }
};
ImageFramePtr load(ImageLoader& loader, DecodeRequest request) {
    bool done=false; ImageFramePtr frame;
    auto handle=loader.request(1,request,[&](quint64,const DecodeResult& r){frame=r.frame;done=true;});
    waitFor([&]{return done;}); check(bool(frame),"decode failed"); return frame;
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); QTemporaryDir dir;
    const QString path=dir.filePath("image.png"); QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("fixture")!=7) return 1;
    file.close();
    auto decoder=std::make_shared<Decoder>();
    ImageLoader loader(decoder,nullptr,std::make_shared<ThumbnailDiskCache>(dir.filePath("cache")));
    try {
        auto small=load(loader,{path,DecodePurpose::Preview,{512,512}});
        auto large=load(loader,{path,DecodePurpose::Preview,{1024,1024}});
        check(loader.cachedBytes()==large->byteSize(),"obsolete preview remains cached");
        check(small->qImage()->pixelColor(0,0)==QColor(20,40,60,128),"held preview was invalidated");
        auto full=load(loader,{path,DecodePurpose::Full});
        check(loader.cachedBytes()==full->byteSize(),"full frame retains redundant previews");
        const int count=decoder->decodes;
        check(load(loader,{path,DecodePurpose::Preview,{512,512}})==full,"preview did not reuse full frame");
        check(decoder->decodes==count,"reopen decoded again");
        decoder->variant=1;
        auto different=load(loader,{path,DecodePurpose::Preview,{512,512}});
        check(different!=full && decoder->decodes==count+1,"decoder identity was mixed");
        check(loader.cachedBytes()==full->byteSize()+different->byteSize(),"another decoder identity was evicted");
        small.reset();large.reset();full.reset();different.reset();loader.clearCache();
        decoder->hold=true;decoder->started=false;
        bool delivered=false;
        auto pending=loader.request(2,{path,DecodePurpose::Preview,{512,512}},[&](quint64,const DecodeResult& r){check(bool(r.frame),"late preview failed");delivered=true;});
        waitFor([&]{return decoder->started.load();});
        full=load(loader,{path,DecodePurpose::Full});decoder->hold=false;
        waitFor([&]{return delivered;});
        check(loader.cachedBytes()==full->byteSize(),"late preview reintroduced redundant cache");
        full.reset();loader.clearCache();
        small=load(loader,{path,DecodePurpose::Preview,{512,512}});
        auto custom=load(loader,{path,DecodePurpose::Preview,{2800,2800}});
        check(loader.cachedBytes()==small->byteSize()+custom->byteSize(),"non-reusable custom preview evicted canonical preview");
        custom.reset();small.reset();loader.clearCache();
        loader.setMemoryBudget(2LL*1024*1024);
        small=load(loader,{path,DecodePurpose::Preview,{512,512}});
        bool oversizedDone=false;
        auto oversized=loader.request(3,{path,DecodePurpose::Full},[&](quint64,const DecodeResult& r){
            check(bool(r.frame),"oversized decode failed");
            check(loader.isCached({path,DecodePurpose::Preview,{512,512}}),"uncached full frame evicted reusable preview");
            oversizedDone=true;
        });
        waitFor([&]{return oversizedDone;});
        small.reset();loader.clearCache();loader.setMemoryBudget(384LL*1024*1024);
        RawImageParameters raw;raw.size={1600,1200};
        small=load(loader,{path,DecodePurpose::Preview,{512,512},raw});
        full=load(loader,{path,DecodePurpose::Full,{},raw});
        check(loader.cachedBytes()==small->byteSize()+full->byteSize(),"RAW fallback representation was evicted");
        std::cout<<"Preview compaction preserves consumers, cache hits, decoder identities, late completions and RAW representations\n";
        return 0;
    } catch(const std::exception& e) {decoder->hold=false;std::cerr<<e.what()<<'\n';return 1;}
}
