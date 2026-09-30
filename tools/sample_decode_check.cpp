#include "io/default_image_decoder.h"
#include "io/directory_scanner.h"
#include "io/image_decoder.h"
#include "io/raw_preset_store.h"
#include "io/supported_image_formats.h"
#include "raw_candidate_options.h"

#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <memory>
#include <optional>

namespace mvpview {
namespace {

constexpr QSize kPreviewSize{1920, 1200};

struct TimedDecode {
    DecodeResult result;
    qint64 nanoseconds = 0;
};

TimedDecode decodeTimed(const IImageDecoder& decoder, DecodeRequest request) {
    QElapsedTimer timer;
    timer.start();
    DecodeResult result = decoder.decode(request);
    return {std::move(result), timer.nsecsElapsed()};
}

std::optional<RawImageParameters>
rawParametersFor(const QString& path, const std::optional<RawImageParameters>& raw16Candidate) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QStringLiteral("raw") && suffix != QStringLiteral("yuv")) {
        return std::nullopt;
    }
    if (const auto stored = RawPresetStore::loadForFile(path)) {
        return stored;
    }
    const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
    if (!inferred.size.isEmpty()) {
        return inferred;
    }
    if (suffix == QStringLiteral("raw")) {
        return raw16Candidate;
    }
    return std::nullopt;
}

QString fingerprint(const ImageFrame& frame) {
    const QImage* image = frame.qImage();
    if (!image || image->isNull()) {
        return QStringLiteral("none");
    }
    const QByteArray bytes = QByteArray::fromRawData(
        reinterpret_cast<const char*>(image->constBits()), image->sizeInBytes());
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(16));
}

QStringList imageCandidates(const QString& directory) {
    QStringList paths;
    QDirIterator iterator(directory, QDir::Files | QDir::Readable | QDir::NoSymLinks);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (hasSupportedImageSuffix(path)) {
            paths.push_back(QFileInfo(path).absoluteFilePath());
        }
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(paths.begin(), paths.end(), [&collator](const QString& left, const QString& right) {
        return collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
    });
    return paths;
}

} // namespace
} // namespace mvpview

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QTextStream errors(stderr);
    const QStringList arguments = application.arguments();
    QString directory;
    std::optional<mvpview::RawImageParameters> raw16Candidate;
    const qsizetype candidateIndex = arguments.indexOf(QStringLiteral("--candidate-raw16"));
    if (candidateIndex >= 0) {
        if (candidateIndex + 1 >= arguments.size()) {
            errors << "--candidate-raw16 requires WIDTHxHEIGHT:VALID_BITS:CFA\n";
            return 1;
        }
        raw16Candidate = mvpview::tools::parseRaw16Candidate(
            arguments.at(candidateIndex + 1), arguments.contains(QStringLiteral("--msb-aligned")),
            arguments.contains(QStringLiteral("--big-endian")));
        if (!raw16Candidate) {
            errors << "Invalid RAW16 candidate; expected WIDTHxHEIGHT:1-16:RGGB|GRBG|GBRG|BGGR\n";
            return 1;
        }
    }
    QString optionError;
    if (!mvpview::tools::applyCandidateOrientationOption(arguments, raw16Candidate, optionError)) {
        errors << optionError << '\n';
        return 1;
    }
    const qsizetype orientationIndex = arguments.indexOf(QStringLiteral("--orientation"));
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        if (index == candidateIndex + 1 || index == orientationIndex + 1) {
            continue;
        }
        if (!arguments.at(index).startsWith(QLatin1Char('-'))) {
            directory = arguments.at(index);
            break;
        }
    }
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
        errors << "Usage: mvpview_sample_check [--allow-incomplete] "
                  "[--candidate-raw16 WIDTHxHEIGHT:VALID_BITS:CFA] [--msb-aligned] "
                  "[--big-endian] [--orientation 0|90|180|270] <image-directory>\n";
        return 1;
    }

    const bool allowIncomplete = arguments.contains(QStringLiteral("--allow-incomplete"));
    const QStringList paths = mvpview::imageCandidates(directory);
    const auto decoder = mvpview::createDefaultImageDecoder();
    QSet<QString> browserPaths;
    const QVector<mvpview::ImageFileRecord> browserFiles =
        mvpview::DirectoryScanner::scan(directory);
    for (const mvpview::ImageFileRecord& record : browserFiles) {
        browserPaths.insert(QFileInfo(record.path).absoluteFilePath());
    }
    int passed = 0;
    int failed = 0;
    int unsupported = 0;
    int needsParameters = 0;
    output
        << "Status\tFile\tFormat\tFullSize\tPreviewSize\tPreviewMs\tFullMs\tFullMiB\tSHA256-64\n";
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!decoder->canDecode(path)) {
            output << "UNSUPPORTED\t" << info.fileName() << '\t' << info.suffix().toUpper()
                   << "\t-\t-\t-\t-\t-\t-\n";
            ++unsupported;
            continue;
        }
        if (!browserPaths.contains(QFileInfo(path).absoluteFilePath())) {
            output << "FAILED_NOT_BROWSABLE\t" << info.fileName() << '\t' << info.suffix().toUpper()
                   << "\t-\t-\t-\t-\t-\t-\n";
            ++failed;
            continue;
        }

        const auto parameters = mvpview::rawParametersFor(path, raw16Candidate);
        const QString suffix = info.suffix().toLower();
        if ((suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) && !parameters) {
            output << "NEEDS_PARAMETERS\t" << info.fileName() << '\t' << info.suffix().toUpper()
                   << "\t-\t-\t-\t-\t-\t-\n";
            ++needsParameters;
            continue;
        }
        const mvpview::TimedDecode preview = mvpview::decodeTimed(
            *decoder, {path, mvpview::DecodePurpose::Preview, mvpview::kPreviewSize, parameters});
        const mvpview::TimedDecode full =
            mvpview::decodeTimed(*decoder, {path, mvpview::DecodePurpose::Full, {}, parameters});
        if (!preview.result.succeeded() || !full.result.succeeded() ||
            !preview.result.frame->qImage() || !full.result.frame->qImage()) {
            const QString message =
                !preview.result.succeeded() ? preview.result.error : full.result.error;
            output << "FAILED\t" << info.fileName() << '\t' << info.suffix().toUpper()
                   << "\t-\t-\t-\t-\t-\t" << message << '\n';
            ++failed;
            continue;
        }
        const mvpview::ImageFrame& previewFrame = *preview.result.frame;
        const mvpview::ImageFrame& fullFrame = *full.result.frame;
        output << "PASS\t" << info.fileName() << '\t' << fullFrame.metadata.format << '\t'
               << fullFrame.descriptor.size.width() << 'x' << fullFrame.descriptor.size.height()
               << '\t' << previewFrame.descriptor.size.width() << 'x'
               << previewFrame.descriptor.size.height() << '\t'
               << QString::number(preview.nanoseconds / 1'000'000.0, 'f', 2) << '\t'
               << QString::number(full.nanoseconds / 1'000'000.0, 'f', 2) << '\t'
               << QString::number(fullFrame.byteSize() / (1024.0 * 1024.0), 'f', 2) << '\t'
               << mvpview::fingerprint(fullFrame) << '\n';
        ++passed;
    }
    output << "Summary\tPASS=" << passed << "\tFAILED=" << failed << "\tUNSUPPORTED=" << unsupported
           << "\tNEEDS_PARAMETERS=" << needsParameters
           << "\tBROWSER_VISIBLE=" << browserFiles.size() << '\n';
    output.flush();
    if (failed > 0) {
        return 1;
    }
    return (unsupported > 0 || needsParameters > 0) && !allowIncomplete ? 2 : 0;
}
