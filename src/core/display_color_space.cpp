#include "core/display_color_space.h"

#include <QImage>
#include <QList>

#include <array>
#include <atomic>
#include <cmath>

namespace ispview {
namespace {

std::atomic<int> currentSpace{static_cast<int>(DisplayColorSpace::Srgb)};

bool isKnownSpace(DisplayColorSpace space) {
    const int value = static_cast<int>(space);
    return value >= static_cast<int>(DisplayColorSpace::Srgb) &&
           value <= static_cast<int>(DisplayColorSpace::Bt2020);
}

// Saturated and neutral probes: the spaces the viewer supports differ by at least 83 8-bit levels
// on these, so a 4 level tolerance recognises a platform profile without ever confusing two of
// them (verified in the unit tests).
constexpr int kAgreementTolerance = 4;

const std::array<QColor, 10>& agreementProbes() {
    static const std::array<QColor, 10> probes{
        QColor(255, 0, 0),   QColor(0, 255, 0),   QColor(0, 0, 255), QColor(255, 255, 0),
        QColor(0, 255, 255), QColor(255, 0, 255), QColor(200, 60, 30), QColor(60, 200, 30),
        QColor(30, 60, 200), QColor(128, 128, 128)};
    return probes;
}

} // namespace

DisplayPrimaries displayPrimaries(DisplayColorSpace space) {
    switch (space) {
    case DisplayColorSpace::DisplayP3: return DisplayPrimaries::DisplayP3;
    case DisplayColorSpace::AdobeRgb: return DisplayPrimaries::AdobeRgb;
    case DisplayColorSpace::Bt2020: return DisplayPrimaries::Bt2020;
    case DisplayColorSpace::Srgb:
    default: return DisplayPrimaries::SrgbBt709;
    }
}

DisplayTransfer displayTransfer(DisplayColorSpace space) {
    switch (space) {
    case DisplayColorSpace::AdobeRgb: return DisplayTransfer::AdobeGamma1998;
    case DisplayColorSpace::Bt2020: return DisplayTransfer::Bt2020;
    case DisplayColorSpace::DisplayP3:
    case DisplayColorSpace::Srgb:
    default:
        // Display P3 pairs the P3 primaries with the sRGB transfer curve.
        return DisplayTransfer::Srgb;
    }
}

QColorSpace displayQColorSpace(DisplayColorSpace space) {
    switch (space) {
    case DisplayColorSpace::DisplayP3: return QColorSpace(QColorSpace::DisplayP3);
    case DisplayColorSpace::AdobeRgb: return QColorSpace(QColorSpace::AdobeRgb);
    case DisplayColorSpace::Bt2020: return QColorSpace(QColorSpace::Bt2020);
    case DisplayColorSpace::Srgb:
    default: return QColorSpace(QColorSpace::SRgb);
    }
}

QString displayPrimariesName(DisplayPrimaries primaries) {
    switch (primaries) {
    case DisplayPrimaries::DisplayP3: return QStringLiteral("Display P3 (DCI-P3 D65)");
    case DisplayPrimaries::AdobeRgb: return QStringLiteral("Adobe RGB (1998)");
    case DisplayPrimaries::Bt2020: return QStringLiteral("BT.2020");
    case DisplayPrimaries::SrgbBt709:
    default: return QStringLiteral("sRGB / BT.709");
    }
}

QString displayTransferName(DisplayTransfer transfer) {
    switch (transfer) {
    case DisplayTransfer::AdobeGamma1998: return QStringLiteral("Gamma 2.19921875");
    case DisplayTransfer::Bt2020: return QStringLiteral("BT.2020 (gamma 2.2)");
    case DisplayTransfer::Srgb:
    default: return QStringLiteral("sRGB");
    }
}

QString displayColorSpaceName(DisplayColorSpace space) {
    switch (space) {
    case DisplayColorSpace::DisplayP3: return QStringLiteral("Display P3");
    case DisplayColorSpace::AdobeRgb: return QStringLiteral("Adobe RGB (1998)");
    case DisplayColorSpace::Bt2020: return QStringLiteral("BT.2020");
    case DisplayColorSpace::Srgb:
    default: return QStringLiteral("sRGB");
    }
}

QString displayColorSpaceKey(DisplayColorSpace space) {
    switch (space) {
    case DisplayColorSpace::DisplayP3: return QStringLiteral("display-p3");
    case DisplayColorSpace::AdobeRgb: return QStringLiteral("adobe-rgb");
    case DisplayColorSpace::Bt2020: return QStringLiteral("bt2020");
    case DisplayColorSpace::Srgb:
    default: return QStringLiteral("srgb");
    }
}

DisplayColorSpace displayColorSpaceFromKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    for (const DisplayColorSpace space : availableDisplayColorSpaces()) {
        if (displayColorSpaceKey(space) == normalized) {
            return space;
        }
    }
    return DisplayColorSpace::Srgb;
}

QList<DisplayColorSpace> availableDisplayColorSpaces() {
    return {DisplayColorSpace::Srgb, DisplayColorSpace::DisplayP3, DisplayColorSpace::AdobeRgb,
            DisplayColorSpace::Bt2020};
}

bool displayColorSpacesAgree(const QColorSpace& first, const QColorSpace& second) {
    if (!first.isValid() || !second.isValid()) {
        return false;
    }
    if (first == second) {
        return true;
    }
    for (const QColor& probe : agreementProbes()) {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.setColorSpace(first);
        image.setPixelColor(0, 0, probe);
        const QImage converted = image.convertedToColorSpace(second, QImage::Format_RGBA8888);
        if (converted.isNull()) {
            return false;
        }
        const QColor value = converted.pixelColor(0, 0);
        if (std::abs(value.red() - probe.red()) > kAgreementTolerance ||
            std::abs(value.green() - probe.green()) > kAgreementTolerance ||
            std::abs(value.blue() - probe.blue()) > kAgreementTolerance) {
            return false;
        }
    }
    return true;
}

DisplayColorSpace displayColorSpaceForSurface(const QColorSpace& surface,
                                             DisplayColorSpace fallback) {
    if (!surface.isValid()) {
        return fallback;
    }
    for (const DisplayColorSpace candidate : availableDisplayColorSpaces()) {
        if (displayColorSpacesAgree(surface, displayQColorSpace(candidate))) {
            return candidate;
        }
    }
    return fallback;
}

DisplayColorSpace currentDisplayColorSpace() {
    const int value = currentSpace.load(std::memory_order_relaxed);
    const auto space = static_cast<DisplayColorSpace>(value);
    return isKnownSpace(space) ? space : DisplayColorSpace::Srgb;
}

void setCurrentDisplayColorSpace(DisplayColorSpace space) {
    currentSpace.store(static_cast<int>(isKnownSpace(space) ? space : DisplayColorSpace::Srgb),
                       std::memory_order_relaxed);
}

void applyDisplayColor(ColorDescriptor& color, bool visualization) {
    const DisplayColorSpace space = currentDisplayColorSpace();
    color.colorSpace = displayColorSpaceName(space);
    if (visualization) {
        color.colorSpace += QStringLiteral(" visualization");
    }
    color.primaries = displayPrimariesName(displayPrimaries(space));
    color.transferFunction = displayTransferName(displayTransfer(space));
}

} // namespace ispview