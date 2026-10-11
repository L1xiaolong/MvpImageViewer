#include "io/raw_image_decoder.h"
#include "io/image_loader.h"
#include "io/thumbnail_disk_cache.h"
#include "io/raw_preset_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QtEndian>
#include <iostream>
#include <new>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace mvpview;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QString fixture(const QString& root, const RawImageParameters& parameters) {
    const qsizetype frameSize = frameByteSize(parameters);
    QByteArray bytes(parameters.headerOffset + frameSize * 2, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>((i * 37 + i / 17) % 256);
    const QString path = root + QStringLiteral("/fixture.yuv");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "fixture write failed");
    return path;
}

void pixelParity(const QString& root) {
    RawImageDecoder decoder;
    int cases = 0;
    for (const auto format : {RawPixelFormat::NV12, RawPixelFormat::NV21,
                             RawPixelFormat::I420, RawPixelFormat::P010}) {
        for (const auto size : {QSize(17, 13), QSize(32, 24)}) {
            for (int variant = 0; variant < 8; ++variant) {
                RawImageParameters p;
                p.size = size; p.format = format;
                p.headerOffset = 19; p.frameIndex = 1;
                p.rowStride = minimumRowStride(p) + 6;
                p.chromaStride = minimumChromaRowStride(p) + 8;
                p.littleEndian = (variant & 1) == 0;
                p.msbAligned = (variant & 2) == 0;
                p.orientation = static_cast<ImageOrientation>(variant % 4);
                p.chromaLocation = variant & 1 ? ChromaLocation::Left : ChromaLocation::Center;
                p.range = variant & 2 ? QuantizationRange::Full : QuantizationRange::Limited;
                p.yuvMatrix = static_cast<YuvMatrix>(variant % 3);
                const QString path = fixture(root, p);
                DecodeRequest streamed{path, DecodePurpose::Thumbnail, QSize(9, 7), p};
                streamed.sourceCache = std::make_shared<SourceFrameCache>();
                const auto reduced = decoder.decode(streamed);
                DecodeRequest retained{path, DecodePurpose::Full, QSize(9, 7), p};
                retained.requireDisplayImage = true;
                const auto reference = decoder.decode(retained);
                check(reduced.succeeded() && reference.succeeded(), "YUV conversion failed");
                check(*reduced.frame->qImage() == *reference.frame->qImage(), "streamed YUV pixel mismatch");
                check(streamed.sourceCache->cost() == 0, "thumbnail retained source frame");
                check(reduced.frame->sourceSamplesPending, "thumbnail probe status lost");
                check(!reference.frame->sourceSamplesPending, "full source samples missing");
                ++cases;
            }
        }
    }
    std::cout << "Pixel parity: " << cases << " cases passed\n";
}

void largeFrameBudget(const QString& root) {
    RawImageParameters p; p.size = {8000, 6000};
    const QString path = root + QStringLiteral("/large_8000_6000.yuv");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.resize(frameByteSize(p)), "large fixture failed");
    file.close();
    RawImageDecoder decoder;
    for (const auto purpose : {DecodePurpose::Thumbnail, DecodePurpose::Preview}) {
        DecodeRequest request{path, purpose, QSize(256, 256), p};
        request.sourceCache = std::make_shared<SourceFrameCache>();
        qsizetype allocation = 0;
        request.reserveWorkingMemory = [&](qsizetype bytes) { allocation = bytes; return true; };
        const auto result = decoder.decode(request);
        check(result.succeeded(), "large YUV preview failed");
        check(allocation < 4 * 1024 * 1024, "small YUV preview reserves a full frame");
        check(request.sourceCache->cost() == 0, "small YUV preview cached a full frame");
        check(result.frame->byteSize() < 1024 * 1024, "small preview retained large storage");
        std::cout << "8000x6000 preview reservation: " << allocation << " bytes\n";
    }
    DecodeRequest cancelled{path, DecodePurpose::Thumbnail, QSize(256, 256), p};
    cancelled.activeConsumers = std::make_shared<std::atomic_int>(0);
    check(!decoder.decode(cancelled).succeeded(), "cancelled request succeeded");
    cancelled.activeConsumers->store(1);
    cancelled.reserveWorkingMemory = [&](qsizetype) { cancelled.activeConsumers->store(0); return true; };
    check(!decoder.decode(cancelled).succeeded(), "streaming ignored cancellation");
    p.size = {8002, 6000};
    check(!decoder.decode({path, DecodePurpose::Thumbnail, QSize(256, 256), p}).succeeded(), "truncated file accepted");
    p.size = {8000, 6000};
    DecodeRequest changed{path, DecodePurpose::Thumbnail, QSize(256, 256), p};
    changed.reserveWorkingMemory = [&](qsizetype) {
        QFile shortened(path);
        return shortened.open(QIODevice::WriteOnly) && shortened.resize(100);
    };
    check(!decoder.decode(changed).succeeded(), "file shrinking during decode accepted");
}

class FailingDecoder final : public IImageDecoder {
  public:
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest& request) const override {
        if (calls_.fetch_add(1) == 0) throw std::bad_alloc();
        return decoder_.decode(request);
    }
  private:
    mutable std::atomic_int calls_{0};
    RawImageDecoder decoder_;
};

void workerFailure(const QString& root) {
    RawImageParameters p; p.size = {32, 24};
    const QString path = fixture(root, p);
    ImageLoader loader(std::make_shared<FailingDecoder>(), nullptr,
                       std::make_shared<ThumbnailDiskCache>(root + QStringLiteral("/failure-cache")));
    QEventLoop loop;
    bool failed = false, recovered = false;
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    loader.request(1, {path, DecodePurpose::Thumbnail, QSize(16, 16), p},
        [&](quint64, const DecodeResult& first) {
            failed = !first.succeeded() && !first.error.isEmpty();
            loader.request(2, {path, DecodePurpose::Thumbnail, QSize(16, 16), p},
                [&](quint64, const DecodeResult& second) { recovered = second.succeeded(); loop.quit(); });
        });
    deadline.start(10000); loop.exec();
    check(failed && recovered, "worker allocation exception did not recover");
    std::cout << "Worker allocation failure delivered; next request succeeded\n";
}

void realDirectory(const QString& root, const QString& directory) {
    const auto files = QDir(directory).entryInfoList({QStringLiteral("*.yuv")}, QDir::Files, QDir::Name);
    check(!files.isEmpty(), "real directory has no YUV files");
    auto decoder = std::make_shared<RawImageDecoder>();
    ImageLoader loader(decoder, nullptr,
        std::make_shared<ThumbnailDiskCache>(root + QStringLiteral("/real-cache")));
    loader.setMemoryBudget(32 * 1024 * 1024);
    int completed = 0, failures = 0;
    qsizetype peakResident = 0;
    QEventLoop loop;
    QTimer monitor;
    QObject::connect(&monitor, &QTimer::timeout, &loop, [&] {
        peakResident = qMax(peakResident, loader.residentPixelBytes());
    });
    monitor.start(2);
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QElapsedTimer elapsed; elapsed.start();
    for (const auto& file : files) {
        const auto p = RawPresetStore::inferFromFileName(file.absoluteFilePath());
        check(frameByteSize(p) > 0, "could not infer real YUV geometry");
        loader.adoptRawParameters(file.absoluteFilePath(), p);
        loader.request(quint64(completed), {file.absoluteFilePath(), DecodePurpose::Thumbnail, QSize(256, 256), p},
            [&](quint64, const DecodeResult& result) {
                if (!result.succeeded()) ++failures;
                peakResident = qMax(peakResident, loader.residentPixelBytes());
                if (++completed == files.size()) loop.quit();
            }, RequestOptions{LoadCategory::VisibleThumbnail, 0, QStringLiteral("regression"), false});
    }
    deadline.start(120000); loop.exec();
    check(completed == files.size() && failures == 0, "real directory thumbnail stress failed");
    check(peakResident < 40 * 1024 * 1024, "real thumbnail resident memory exceeded bound");
    std::cout << "Real directory: " << completed << " thumbnails, peak tracked resident "
              << peakResident << " bytes, elapsed " << elapsed.elapsed() << " ms\n";
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Optional process commit limit reproduces memory pressure without changing
    // the machine's memory/pagefile settings or constraining other applications.
    HANDLE job = nullptr;
    if (app.arguments().size() >= 3) {
        bool valid = false;
        const auto mib = app.arguments().at(2).toULongLong(&valid);
        if (!valid || mib < 64 || mib > 4096) return 1;
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = SIZE_T(mib * 1024 * 1024);
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job, GetCurrentProcess())) return 1;
    }
#endif
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/yuv-regression-XXXXXX"));
    if (!temporary.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QCoreApplication::setOrganizationName(QStringLiteral("MvpViewRegression"));
    try {
        pixelParity(temporary.path());
        largeFrameBudget(temporary.path());
        workerFailure(temporary.path());
        if (app.arguments().size() >= 2) realDirectory(temporary.path(), app.arguments().at(1));
#ifdef Q_OS_WIN
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION usage{};
            QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &usage, sizeof(usage), nullptr);
            std::cout << "Peak process commit under job limit: " << usage.PeakProcessMemoryUsed << " bytes\n";
            CloseHandle(job);
        }
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
