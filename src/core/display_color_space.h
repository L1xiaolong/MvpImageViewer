#pragma once

#include "core/image_types.h"

#include <QColorSpace>
#include <QList>
#include <QString>

namespace ispview {

// Primaries of a display-referred RGB encoding. Every space the viewer targets is D65 white,
// so primaries alone define the gamut mapping.
enum class DisplayPrimaries { SrgbBt709, DisplayP3, AdobeRgb, Bt2020 };

// Encoding curve of a display-referred RGB encoding. The values are the ones Qt uses for the
// matching named QColorSpace, so a tagged image, its ICC profile, and the shader agree.
enum class DisplayTransfer { Srgb, AdobeGamma1998, Bt2020 };

// The interchange space every presented frame is encoded in. It is an application-wide setting
// because the CPU reference paths, the GPU shaders, the ICC destination, and the QImage tags all
// have to describe the same buffer. sRGB stays the default and reproduces the previous results
// exactly.
enum class DisplayColorSpace { Srgb, DisplayP3, AdobeRgb, Bt2020 };

[[nodiscard]] DisplayPrimaries displayPrimaries(DisplayColorSpace space);
[[nodiscard]] DisplayTransfer displayTransfer(DisplayColorSpace space);
[[nodiscard]] QColorSpace displayQColorSpace(DisplayColorSpace space);
[[nodiscard]] QString displayPrimariesName(DisplayPrimaries primaries);
[[nodiscard]] QString displayTransferName(DisplayTransfer transfer);
[[nodiscard]] QString displayColorSpaceName(DisplayColorSpace space);
// Stable identifier used for settings, cache identities, and diagnostics.
[[nodiscard]] QString displayColorSpaceKey(DisplayColorSpace space);
[[nodiscard]] DisplayColorSpace displayColorSpaceFromKey(const QString& key);
[[nodiscard]] QList<DisplayColorSpace> availableDisplayColorSpaces();

// True when two spaces describe the same primaries and transfer curve within 8-bit round-off.
// Platform profiles are rarely bit-identical to the nominal space, so a tolerance is required to
// recognise the space a window surface is tagged with.
[[nodiscard]] bool displayColorSpacesAgree(const QColorSpace& first, const QColorSpace& second);
// Maps a platform-reported surface colour space onto a space the viewer can encode. Falls back
// when the space is invalid or matches none of them (HDR, linear, or a custom wrapper), which is
// what an untagged or unsupported surface behaves like on every platform.
[[nodiscard]] DisplayColorSpace
displayColorSpaceForSurface(const QColorSpace& surface,
                            DisplayColorSpace fallback = DisplayColorSpace::Srgb);

// Current application-wide target space. Decoding happens on worker threads, so the accessors are
// thread safe.
[[nodiscard]] DisplayColorSpace currentDisplayColorSpace();
void setCurrentDisplayColorSpace(DisplayColorSpace space);

// Describes the pixels a decoder has already encoded into the current display space. Pass
// visualization = true for channel visualisations (CFA false colour), which are tagged with the
// display space because that is what the compositor will assume, but are not colorimetric.
void applyDisplayColor(ColorDescriptor& color, bool visualization = false);

} // namespace ispview