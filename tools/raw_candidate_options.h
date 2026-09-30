#pragma once

#include "core/raw_image_parameters.h"

#include <QRegularExpression>
#include <QStringList>

#include <optional>

namespace mvpview::tools {

inline std::optional<RawImageParameters> parseRaw16Candidate(const QString& text, bool msbAligned,
                                                             bool bigEndian) {
    static const QRegularExpression expression(
        QStringLiteral(R"(^(\d{1,6})x(\d{1,6}):(\d{1,2}):(rggb|grbg|gbrg|bggr)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool widthValid = false;
    bool heightValid = false;
    bool bitsValid = false;
    const int width = match.captured(1).toInt(&widthValid);
    const int height = match.captured(2).toInt(&heightValid);
    const int bits = match.captured(3).toInt(&bitsValid);
    if (!widthValid || !heightValid || !bitsValid || width <= 0 || height <= 0 || bits <= 0 ||
        bits > 16) {
        return std::nullopt;
    }

    RawImageParameters result;
    result.size = {width, height};
    result.format = RawPixelFormat::Raw16;
    result.validBitsOverride = bits;
    result.msbAligned = msbAligned;
    result.littleEndian = !bigEndian;
    const QString cfa = match.captured(4).toLower();
    if (cfa == QStringLiteral("grbg")) {
        result.bayerPattern = BayerPattern::GRBG;
    } else if (cfa == QStringLiteral("gbrg")) {
        result.bayerPattern = BayerPattern::GBRG;
    } else if (cfa == QStringLiteral("bggr")) {
        result.bayerPattern = BayerPattern::BGGR;
    }
    return result;
}

inline bool applyCandidateOrientationOption(const QStringList& arguments,
                                            std::optional<RawImageParameters>& candidate,
                                            QString& error) {
    const qsizetype optionIndex = arguments.indexOf(QStringLiteral("--orientation"));
    if (optionIndex < 0) {
        return true;
    }
    if (!candidate) {
        error = QStringLiteral("--orientation requires --candidate-raw16");
        return false;
    }
    if (optionIndex + 1 >= arguments.size()) {
        error = QStringLiteral("--orientation requires 0, 90, 180, or 270");
        return false;
    }

    const QString value = arguments.at(optionIndex + 1);
    if (value == QStringLiteral("0")) {
        candidate->orientation = ImageOrientation::Normal;
    } else if (value == QStringLiteral("90")) {
        candidate->orientation = ImageOrientation::Rotate90Clockwise;
    } else if (value == QStringLiteral("180")) {
        candidate->orientation = ImageOrientation::Rotate180;
    } else if (value == QStringLiteral("270")) {
        candidate->orientation = ImageOrientation::Rotate270Clockwise;
    } else {
        error = QStringLiteral("Invalid orientation; expected 0, 90, 180, or 270");
        return false;
    }
    return true;
}

} // namespace mvpview::tools
