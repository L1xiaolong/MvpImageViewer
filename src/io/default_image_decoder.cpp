#include "io/default_image_decoder.h"

#include "io/camera_raw_decoder.h"
#include "io/image_decoder_registry.h"
#include "io/qt_image_decoder.h"
#include "io/raw_image_decoder.h"

namespace mvpview {

std::shared_ptr<const IImageDecoder> createDefaultImageDecoder() {
    auto registry = std::make_shared<ImageDecoderRegistry>();
    registry->add(std::make_shared<QtImageDecoder>());
    registry->add(std::make_shared<CameraRawDecoder>());
    registry->add(std::make_shared<RawImageDecoder>());
    return registry;
}

} // namespace mvpview
