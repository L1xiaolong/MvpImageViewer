#pragma once

#include "io/image_decoder.h"

#include <memory>
#include <vector>

namespace ispview {

class ImageDecoderRegistry final : public IImageDecoder {
  public:
    void add(std::shared_ptr<const IImageDecoder> decoder);

    [[nodiscard]] QString cacheIdentity() const override;
    [[nodiscard]] DecodeExecutionMode executionMode(const QString& path) const override;
    [[nodiscard]] bool canDecode(const QString& path) const override;
    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override;

  private:
    std::vector<std::shared_ptr<const IImageDecoder>> decoders_;
};

} // namespace ispview
