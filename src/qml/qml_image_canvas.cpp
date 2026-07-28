#include "qml/qml_image_canvas.h"

#include "render/bayer_render_parameters.h"
#include "render/yuv_render_parameters.h"

#include <QFile>
#include <QCursor>
#include <QHoverEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtGui/qrgbafloat.h>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace ispview {
namespace {

bool isIndependentViewAdjustment(Qt::KeyboardModifiers modifiers) {
#if defined(Q_OS_MACOS)
    // Qt exposes the physical Control key as MetaModifier on macOS, while
    // ControlModifier represents Command. Accept both so the documented Ctrl
    // gesture and the platform-equivalent modifier behave consistently.
    return modifiers.testFlag(Qt::MetaModifier) || modifiers.testFlag(Qt::ControlModifier);
#else
    return modifiers.testFlag(Qt::ControlModifier);
#endif
}

constexpr int kMaximumImages = 4;
constexpr qreal kCellSpacing = 2.0;
constexpr std::array<float, 24> kQuadVertices{
    -1.F, -1.F, 0.F, 1.F, 1.F, -1.F, 1.F, 1.F, -1.F, 1.F, 0.F, 0.F,
    -1.F, 1.F, 0.F, 0.F, 1.F, -1.F, 1.F, 1.F, 1.F, 1.F, 1.F, 0.F,
};

QShader loadShader(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}

QRhiTextureSubresourceUploadDescription planeUpload(const PlaneBufferSet& storage, int planeIndex,
                                                    const QSize& size) {
    const PlaneBuffer& plane = storage.planes.at(planeIndex);
    const QByteArray bytes = QByteArray::fromRawData(storage.storage.constData() + plane.offset,
                                                     plane.byteSize);
    QRhiTextureSubresourceUploadDescription upload(bytes);
    upload.setDataStride(static_cast<quint32>(plane.stride));
    upload.setSourceSize(size);
    return upload;
}

QRhiTextureSubresourceUploadDescription p010PlaneUpload(const PlaneBufferSet& storage, int index,
                                                        const QSize& size, bool littleEndian) {
    if (littleEndian) return planeUpload(storage, index, size);
    const PlaneBuffer& plane = storage.planes.at(index);
    QByteArray normalized(plane.byteSize, Qt::Uninitialized);
    const char* source = storage.storage.constData() + plane.offset;
    for (qsizetype offset = 0; offset < plane.byteSize; offset += 2) {
        normalized[offset] = source[offset + 1];
        normalized[offset + 1] = source[offset];
    }
    QRhiTextureSubresourceUploadDescription upload(std::move(normalized));
    upload.setDataStride(static_cast<quint32>(plane.stride));
    upload.setSourceSize(size);
    return upload;
}

bool supportsGpuYuv(const ImageFramePtr& frame) {
    if (!frame || !frame->rawParameters || !frame->rawParameters->isYuv()) return false;
    const auto* storage = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
    if (!storage || !*storage) return false;
    const bool planar = frame->rawParameters->format == RawPixelFormat::I420;
    const int required = planar ? 3 : 2;
    if ((*storage)->planes.size() < required) return false;
    if (frame->rawParameters->format == RawPixelFormat::P010) {
        for (int index = 0; index < required; ++index) {
            const PlaneBuffer& plane = (*storage)->planes.at(index);
            if (plane.stride <= 0 || (plane.stride & 1) != 0 || (plane.byteSize & 1) != 0)
                return false;
        }
    }
    return true;
}

bool supportsGpuBayer(const ImageFramePtr& frame) {
    if (!frame || !frame->rawParameters || frame->rawParameters->isYuv()) return false;
    const auto* storage = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
    if (!storage || !*storage || (*storage)->planes.isEmpty()) return false;
    const PlaneBuffer& plane = (*storage)->planes.constFirst();
    return plane.stride > 0 && plane.stride <= std::numeric_limits<int>::max() &&
           plane.byteSize >= plane.stride * frame->rawParameters->size.height();
}

QImage displayImage(const ImageFramePtr& frame) {
    if (!frame) return {};
    const QImage* image = frame->qImage();
    return image ? *image : QImage{};
}

QSize logicalSize(const ImageFramePtr& frame) {
    if (!frame) return {};
    if (frame->rawParameters)
        return orientedImageSize(frame->rawParameters->size, frame->rawParameters->orientation);
    if (!frame->descriptor.size.isEmpty()) return frame->descriptor.size;
    return displayImage(frame).size();
}

struct EncodedTextureUpload {
    QRhiTexture::Format format = QRhiTexture::RGBA8;
    QRhiTextureSubresourceUploadDescription description;
};

EncodedTextureUpload encodedTextureUpload(const QImage& source) {
    if (source.format() == QImage::Format_RGBA16FPx4) {
        const QByteArray bytes = QByteArray::fromRawData(
            reinterpret_cast<const char*>(source.constBits()), source.sizeInBytes());
        QRhiTextureSubresourceUploadDescription upload(bytes);
        upload.setDataStride(static_cast<quint32>(source.bytesPerLine()));
        upload.setSourceSize(source.size());
        return {QRhiTexture::RGBA16F, std::move(upload)};
    }
    if (source.format() == QImage::Format_RGBA32FPx4) {
        const QByteArray bytes = QByteArray::fromRawData(
            reinterpret_cast<const char*>(source.constBits()), source.sizeInBytes());
        QRhiTextureSubresourceUploadDescription upload(bytes);
        upload.setDataStride(static_cast<quint32>(source.bytesPerLine()));
        upload.setSourceSize(source.size());
        return {QRhiTexture::RGBA32F, std::move(upload)};
    }
    if (source.format() == QImage::Format_RGBA64) {
        const qsizetype pixelStride = qsizetype(sizeof(QRgbaFloat16));
        const qsizetype rowStride = qsizetype(source.width()) * pixelStride;
        QByteArray bytes(rowStride * qsizetype(source.height()), Qt::Uninitialized);
        for (int y = 0; y < source.height(); ++y) {
            const auto* input = reinterpret_cast<const QRgba64*>(source.constScanLine(y));
            auto* output = reinterpret_cast<QRgbaFloat16*>(bytes.data() + qsizetype(y) * rowStride);
            for (int x = 0; x < source.width(); ++x) {
                output[x] = QRgbaFloat16::fromRgba64(input[x].red(), input[x].green(),
                                                     input[x].blue(), input[x].alpha());
            }
        }
        QRhiTextureSubresourceUploadDescription upload(std::move(bytes));
        upload.setDataStride(static_cast<quint32>(rowStride));
        upload.setSourceSize(source.size());
        return {QRhiTexture::RGBA16F, std::move(upload)};
    }
    QImage normalized = source;
    if (normalized.format() != QImage::Format_RGBA8888)
        normalized = normalized.convertToFormat(QImage::Format_RGBA8888);
    return {QRhiTexture::RGBA8, QRhiTextureSubresourceUploadDescription(std::move(normalized))};
}

QRectF comparisonCell(int slot, int count, int presentationMode, const QSize& itemSize) {
    if (slot < 0 || itemSize.isEmpty()) return {};
    if (presentationMode != 0) return slot == 0 ? QRectF(QPointF{}, itemSize) : QRectF{};
    const int boundedCount = std::clamp(count, 1, kMaximumImages);
    const int columns = boundedCount == 4 ? 2 : boundedCount;
    const int rows = boundedCount == 4 ? 2 : 1;
    if (slot >= boundedCount) return {};
    const qreal cellWidth = (itemSize.width() - kCellSpacing * (columns - 1)) / columns;
    const qreal cellHeight = (itemSize.height() - kCellSpacing * (rows - 1)) / rows;
    const int column = slot % columns;
    const int row = slot / columns;
    return {column * (cellWidth + kCellSpacing), row * (cellHeight + kCellSpacing), cellWidth,
            cellHeight};
}

class Renderer final : public QQuickRhiItemRenderer {
    struct SlotResources {
        std::unique_ptr<QRhiBuffer> matrix;
        std::unique_ptr<QRhiBuffer> yuvUniform;
        std::unique_ptr<QRhiBuffer> bayerUniform;
        std::unique_ptr<QRhiBuffer> sourceUniform;
        std::unique_ptr<QRhiTexture> encoded;
        std::unique_ptr<QRhiTexture> y;
        std::unique_ptr<QRhiTexture> u;
        std::unique_ptr<QRhiTexture> v;
        std::unique_ptr<QRhiTexture> raw;
        std::unique_ptr<QRhiShaderResourceBindings> sideBindings;
        bool gpuYuv = false;
        bool gpuBayer = false;

        void resetTextures() {
            sideBindings.reset();
            encoded.reset();
            y.reset();
            u.reset();
            v.reset();
            raw.reset();
            gpuYuv = false;
            gpuBayer = false;
        }

        void resetAll() {
            resetTextures();
            matrix.reset();
            yuvUniform.reset();
            bayerUniform.reset();
            sourceUniform.reset();
        }
    };

    QRhi* rhi_ = nullptr;
    QVector<ImageFramePtr> frames_;
    QVector<QImage> displayImages_;
    QVector<ViewState> viewStates_;
    QSize itemSize_;
    int presentationMode_ = 0;
    qreal compareAmount_ = 0.5;
    bool resourcesDirty_ = true;
    std::unique_ptr<QRhiBuffer> vertices_;
    std::unique_ptr<QRhiBuffer> compareUniform_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::array<SlotResources, kMaximumImages> slots_;
    std::unique_ptr<QRhiShaderResourceBindings> encodedCompareBindings_;
    std::unique_ptr<QRhiShaderResourceBindings> yuvCompareBindings_;
    std::unique_ptr<QRhiShaderResourceBindings> bayerCompareBindings_;
    std::array<std::unique_ptr<QRhiGraphicsPipeline>, kMaximumImages> sidePipelines_;
    std::unique_ptr<QRhiGraphicsPipeline> encodedComparePipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> yuvComparePipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> bayerComparePipeline_;

    void resetPipelines() {
        for (auto& pipeline : sidePipelines_) pipeline.reset();
        encodedComparePipeline_.reset();
        yuvComparePipeline_.reset();
        bayerComparePipeline_.reset();
    }

    void resetAll() {
        resetPipelines();
        encodedCompareBindings_.reset();
        yuvCompareBindings_.reset();
        bayerCompareBindings_.reset();
        for (auto& slot : slots_) slot.resetAll();
        sampler_.reset();
        compareUniform_.reset();
        vertices_.reset();
    }

    bool uploadYuv(const ImageFramePtr& frame, SlotResources& slot,
                   QRhiResourceUpdateBatch* updates) {
        if (!supportsGpuYuv(frame)) return false;
        const auto* source = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
        const PlaneBufferSet& storage = **source;
        const RawImageParameters& parameters = *frame->rawParameters;
        const bool highBitDepth = parameters.format == RawPixelFormat::P010;
        const bool planar = parameters.format == RawPixelFormat::I420;
        const QSize chroma((parameters.size.width() + 1) / 2,
                           (parameters.size.height() + 1) / 2);
        slot.y.reset(rhi_->newTexture(highBitDepth ? QRhiTexture::R16 : QRhiTexture::R8,
                                      parameters.size, 1));
        slot.u.reset(rhi_->newTexture(
            planar ? QRhiTexture::R8 : (highBitDepth ? QRhiTexture::RG16 : QRhiTexture::RG8),
            chroma, 1));
        slot.v.reset(rhi_->newTexture(QRhiTexture::R8, planar ? chroma : QSize(1, 1), 1));
        if (!slot.y->create() || !slot.u->create() || !slot.v->create()) {
            slot.y.reset();
            slot.u.reset();
            slot.v.reset();
            return false;
        }
        const auto plane = [&storage, &parameters, highBitDepth](int index, const QSize& size) {
            return highBitDepth ? p010PlaneUpload(storage, index, size, parameters.littleEndian)
                                : planeUpload(storage, index, size);
        };
        updates->uploadTexture(slot.y.get(), QRhiTextureUploadDescription(
            QRhiTextureUploadEntry(0, 0, plane(0, parameters.size))));
        updates->uploadTexture(slot.u.get(), QRhiTextureUploadDescription(
            QRhiTextureUploadEntry(0, 0, plane(1, chroma))));
        if (planar) {
            updates->uploadTexture(slot.v.get(), QRhiTextureUploadDescription(
                QRhiTextureUploadEntry(0, 0, planeUpload(storage, 2, chroma))));
        }
        return true;
    }

    bool uploadBayer(const ImageFramePtr& frame, SlotResources& slot,
                     QRhiResourceUpdateBatch* updates) {
        if (!supportsGpuBayer(frame)) return false;
        const auto* source = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame->storage);
        const PlaneBufferSet& storage = **source;
        const int stride = static_cast<int>(storage.planes.constFirst().stride);
        const QSize storageSize(stride, frame->rawParameters->size.height());
        slot.raw.reset(rhi_->newTexture(QRhiTexture::R8, storageSize, 1));
        if (!slot.raw->create()) {
            slot.raw.reset();
            return false;
        }
        updates->uploadTexture(slot.raw.get(), QRhiTextureUploadDescription(
            QRhiTextureUploadEntry(0, 0, planeUpload(storage, 0, storageSize))));
        return true;
    }

    void ensurePlaceholder(std::unique_ptr<QRhiTexture>& texture, QRhiTexture::Format format) {
        if (texture) return;
        texture.reset(rhi_->newTexture(format, QSize(1, 1), 1));
        texture->create();
    }

    void rebuildBindings() {
        for (auto& slot : slots_) {
            slot.sideBindings.reset(rhi_->newShaderResourceBindings());
            slot.sideBindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage, slot.matrix.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage, slot.encoded.get(), sampler_.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    2, QRhiShaderResourceBinding::FragmentStage, slot.y.get(), sampler_.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    3, QRhiShaderResourceBinding::FragmentStage, slot.u.get(), sampler_.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    4, QRhiShaderResourceBinding::FragmentStage, slot.v.get(), sampler_.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    5, QRhiShaderResourceBinding::FragmentStage, slot.raw.get(), sampler_.get()),
                QRhiShaderResourceBinding::uniformBuffer(
                    6, QRhiShaderResourceBinding::FragmentStage, slot.yuvUniform.get()),
                QRhiShaderResourceBinding::uniformBuffer(
                    7, QRhiShaderResourceBinding::FragmentStage, slot.bayerUniform.get()),
                QRhiShaderResourceBinding::uniformBuffer(
                    8, QRhiShaderResourceBinding::FragmentStage, slot.sourceUniform.get()),
            });
            slot.sideBindings->create();
        }

        encodedCompareBindings_.reset(rhi_->newShaderResourceBindings());
        encodedCompareBindings_->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, slots_[0].matrix.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, slots_[0].encoded.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, slots_[1].encoded.get(), sampler_.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                3, QRhiShaderResourceBinding::FragmentStage, compareUniform_.get()),
        });
        encodedCompareBindings_->create();

        yuvCompareBindings_.reset(rhi_->newShaderResourceBindings());
        yuvCompareBindings_->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, slots_[0].matrix.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, slots_[0].y.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, slots_[0].u.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                3, QRhiShaderResourceBinding::FragmentStage, slots_[0].v.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                4, QRhiShaderResourceBinding::FragmentStage, slots_[1].y.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                5, QRhiShaderResourceBinding::FragmentStage, slots_[1].u.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                6, QRhiShaderResourceBinding::FragmentStage, slots_[1].v.get(), sampler_.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                7, QRhiShaderResourceBinding::FragmentStage, slots_[0].yuvUniform.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                8, QRhiShaderResourceBinding::FragmentStage, slots_[1].yuvUniform.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                9, QRhiShaderResourceBinding::FragmentStage, compareUniform_.get()),
        });
        yuvCompareBindings_->create();

        bayerCompareBindings_.reset(rhi_->newShaderResourceBindings());
        bayerCompareBindings_->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, slots_[0].matrix.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, slots_[0].raw.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, slots_[1].raw.get(), sampler_.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                3, QRhiShaderResourceBinding::FragmentStage, slots_[0].bayerUniform.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                4, QRhiShaderResourceBinding::FragmentStage, slots_[1].bayerUniform.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                5, QRhiShaderResourceBinding::FragmentStage, compareUniform_.get()),
        });
        bayerCompareBindings_->create();
    }

    void rebuildResources(QRhiCommandBuffer* commandBuffer) {
        for (auto& slot : slots_) slot.resetTextures();
        auto* updates = rhi_->nextResourceUpdateBatch();
        const bool compositeRequested = presentationMode_ != 0 && frames_.size() == 2;
        for (int index = 0; index < kMaximumImages; ++index) {
            SlotResources& slot = slots_[static_cast<std::size_t>(index)];
            const ImageFramePtr frame = frames_.value(index);
            slot.gpuYuv = uploadYuv(frame, slot, updates);
            slot.gpuBayer = uploadBayer(frame, slot, updates);

            QImage encoded = displayImages_.value(index);
            if (encoded.isNull() || ((slot.gpuYuv || slot.gpuBayer) && !compositeRequested)) {
                encoded = QImage(1, 1, QImage::Format_RGBA8888);
                encoded.fill(Qt::transparent);
            }
            const EncodedTextureUpload upload = encodedTextureUpload(encoded);
            slot.encoded.reset(rhi_->newTexture(upload.format, encoded.size(), 1));
            slot.encoded->create();
            updates->uploadTexture(slot.encoded.get(), QRhiTextureUploadDescription(
                QRhiTextureUploadEntry(0, 0, upload.description)));

            ensurePlaceholder(slot.y, QRhiTexture::R8);
            ensurePlaceholder(slot.u, QRhiTexture::RG8);
            ensurePlaceholder(slot.v, QRhiTexture::R8);
            ensurePlaceholder(slot.raw, QRhiTexture::R8);
        }
        commandBuffer->resourceUpdate(updates);
        rebuildBindings();
        resetPipelines();
        resourcesDirty_ = false;
    }

    void createPipelines() {
        if (sidePipelines_[0]) return;
        QRhiVertexInputLayout layout;
        layout.setBindings({{4 * sizeof(float)}});
        layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float2, 0},
            {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
        });
        const auto create = [this, &layout](std::unique_ptr<QRhiGraphicsPipeline>& target,
                                             const QString& fragment,
                                             QRhiShaderResourceBindings* bindings) {
            target.reset(rhi_->newGraphicsPipeline());
            target->setShaderStages({
                {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/image.vert.qsb"))},
                {QRhiShaderStage::Fragment, loadShader(fragment)},
            });
            target->setVertexInputLayout(layout);
            target->setShaderResourceBindings(bindings);
            target->setFlags(QRhiGraphicsPipeline::UsesScissor);
            target->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            target->create();
        };
        for (int slot = 0; slot < kMaximumImages; ++slot) {
            const auto index = static_cast<std::size_t>(slot);
            create(sidePipelines_[index], QStringLiteral(":/shaders/qml_side.frag.qsb"),
                   slots_[index].sideBindings.get());
        }
        create(encodedComparePipeline_, QStringLiteral(":/shaders/image.frag.qsb"),
               encodedCompareBindings_.get());
        create(yuvComparePipeline_, QStringLiteral(":/shaders/yuv_compare.frag.qsb"),
               yuvCompareBindings_.get());
        create(bayerComparePipeline_, QStringLiteral(":/shaders/bayer_compare.frag.qsb"),
               bayerCompareBindings_.get());
    }

    QMatrix4x4 matrixFor(int slot) const {
        QMatrix4x4 matrix = rhi_->clipSpaceCorrMatrix();
        const QSize imageSize = logicalSize(frames_.value(slot));
        const QRectF cell = comparisonCell(slot, static_cast<int>(frames_.size()),
                                           presentationMode_, itemSize_);
        if (imageSize.isEmpty() || cell.isEmpty() || itemSize_.isEmpty()) {
            matrix.scale(0.F);
            return matrix;
        }
        const ViewState state = viewStates_.value(slot);
        const double imageWidth = imageSize.width() * state.pixelsPerImagePixel;
        const double imageHeight = imageSize.height() * state.pixelsPerImagePixel;
        const double centerX = cell.center().x() + (0.5 - state.normalizedCenter.x()) * imageWidth;
        const double centerY = cell.center().y() + (0.5 - state.normalizedCenter.y()) * imageHeight;
        const float ndcX = static_cast<float>(2.0 * centerX / itemSize_.width() - 1.0);
        const float ndcY = static_cast<float>(1.0 - 2.0 * centerY / itemSize_.height());
        QMatrix4x4 model;
        model.translate(ndcX, ndcY);
        model.scale(static_cast<float>(imageWidth / itemSize_.width()),
                    static_cast<float>(imageHeight / itemSize_.height()));
        matrix *= model;
        return matrix;
    }

    QRhiScissor scissorFor(int slot, const QSize& outputSize) const {
        const QRectF cell = comparisonCell(slot, static_cast<int>(frames_.size()),
                                           presentationMode_, itemSize_);
        const qreal scaleX = itemSize_.width() > 0
            ? static_cast<qreal>(outputSize.width()) / itemSize_.width() : 1.0;
        const qreal scaleY = itemSize_.height() > 0
            ? static_cast<qreal>(outputSize.height()) / itemSize_.height() : 1.0;
        const int x = qRound(cell.x() * scaleX);
        const int width = qRound(cell.width() * scaleX);
        const int height = qRound(cell.height() * scaleY);
        // QQuickRhiItem renders into an offscreen texture that is sampled by
        // the scene graph. Its render-target Y axis is opposite to QQuickItem's
        // top-left geometry regardless of the final window backend.
        const int top = qRound(cell.y() * scaleY);
        const int y = outputSize.height() - top - height;
        return {x, y, width, height};
    }

  protected:
    void initialize(QRhiCommandBuffer* commandBuffer) override {
        if (rhi_ != rhi()) {
            resetAll();
            rhi_ = rhi();
            resourcesDirty_ = true;
        }
        if (!vertices_) {
            vertices_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                            sizeof(kQuadVertices)));
            vertices_->create();
            compareUniform_.reset(
                rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
            compareUniform_->create();
            sampler_.reset(rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                            QRhiSampler::None, QRhiSampler::ClampToEdge,
                                            QRhiSampler::ClampToEdge));
            sampler_->create();
            for (auto& slot : slots_) {
                slot.matrix.reset(
                    rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
                slot.matrix->create();
                slot.yuvUniform.reset(
                    rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 48));
                slot.yuvUniform->create();
                slot.bayerUniform.reset(
                    rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 112));
                slot.bayerUniform->create();
                slot.sourceUniform.reset(
                    rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
                slot.sourceUniform->create();
            }
            auto* updates = rhi_->nextResourceUpdateBatch();
            updates->uploadStaticBuffer(vertices_.get(), kQuadVertices.data());
            commandBuffer->resourceUpdate(updates);
        }
        if (resourcesDirty_ || !slots_[0].encoded) rebuildResources(commandBuffer);
        createPipelines();
    }

    void synchronize(QQuickRhiItem* item) override {
        auto* canvas = static_cast<QmlImageCanvas*>(item);
        const QVector<ImageFramePtr> incoming = canvas->frames();
        const int incomingMode = canvas->presentationMode();
        if (frames_ != incoming || ((presentationMode_ == 0) != (incomingMode == 0))) {
            frames_ = incoming;
            displayImages_.clear();
            displayImages_.reserve(frames_.size());
            for (const auto& frame : frames_) displayImages_.append(displayImage(frame).copy());
            resourcesDirty_ = true;
        }
        presentationMode_ = incomingMode;
        compareAmount_ = canvas->compareAmount();
        viewStates_.clear();
        viewStates_.reserve(frames_.size());
        for (int slot = 0; slot < frames_.size(); ++slot)
            viewStates_.append(canvas->effectiveViewState(slot));
        itemSize_ = QSize(qRound(canvas->width()), qRound(canvas->height()));
    }

    void render(QRhiCommandBuffer* commandBuffer) override {
        if (resourcesDirty_) rebuildResources(commandBuffer);
        createPipelines();

        auto* updates = rhi_->nextResourceUpdateBatch();
        for (int slot = 0; slot < kMaximumImages; ++slot) {
            const QMatrix4x4 matrix = matrixFor(slot);
            updates->updateDynamicBuffer(slots_[static_cast<std::size_t>(slot)].matrix.get(), 0,
                                         64, matrix.constData());
            const ImageFramePtr frame = frames_.value(slot);
            if (frame && frame->rawParameters) {
                if (frame->rawParameters->isYuv()) {
                    const auto values = makeYuvRenderUniformData(*frame->rawParameters);
                    updates->updateDynamicBuffer(
                        slots_[static_cast<std::size_t>(slot)].yuvUniform.get(), 0,
                        static_cast<quint32>(sizeof(values)), values.data());
                } else {
                    const auto values = makeBayerRenderUniformData(*frame->rawParameters);
                    updates->updateDynamicBuffer(
                        slots_[static_cast<std::size_t>(slot)].bayerUniform.get(), 0,
                        static_cast<quint32>(sizeof(values)), values.data());
                }
            }
            const std::array<float, 4> sourceValues{
                slots_[static_cast<std::size_t>(slot)].gpuYuv
                    ? 1.F
                    : slots_[static_cast<std::size_t>(slot)].gpuBayer ? 2.F : 0.F,
                0.F, 0.F, 0.F};
            updates->updateDynamicBuffer(
                slots_[static_cast<std::size_t>(slot)].sourceUniform.get(), 0,
                static_cast<quint32>(sizeof(sourceValues)), sourceValues.data());
        }
        const std::array<float, 8> compareValues{
            static_cast<float>(presentationMode_), static_cast<float>(compareAmount_),
            0.F, 0.F, 0.F, 0.F, 0.F, 0.F};
        updates->updateDynamicBuffer(compareUniform_.get(), 0, 32, compareValues.data());

        const QSize outputSize = renderTarget()->pixelSize();
        commandBuffer->beginPass(renderTarget(), QColor(160, 160, 160, 255), {1.F, 0}, updates);
        commandBuffer->setViewport(
            QRhiViewport(0, 0, static_cast<float>(outputSize.width()),
                         static_cast<float>(outputSize.height())));
        const QRhiCommandBuffer::VertexInput input(vertices_.get(), 0);

        if (presentationMode_ != 0 && frames_.size() == 2) {
            QRhiGraphicsPipeline* pipeline = encodedComparePipeline_.get();
            QRhiShaderResourceBindings* bindings = encodedCompareBindings_.get();
            if (slots_[0].gpuYuv && slots_[1].gpuYuv) {
                pipeline = yuvComparePipeline_.get();
                bindings = yuvCompareBindings_.get();
            } else if (slots_[0].gpuBayer && slots_[1].gpuBayer) {
                pipeline = bayerComparePipeline_.get();
                bindings = bayerCompareBindings_.get();
            }
            commandBuffer->setGraphicsPipeline(pipeline);
            commandBuffer->setViewport(
                QRhiViewport(0, 0, static_cast<float>(outputSize.width()),
                             static_cast<float>(outputSize.height())));
            commandBuffer->setVertexInput(0, 1, &input);
            commandBuffer->setShaderResources(bindings);
            commandBuffer->setScissor(scissorFor(0, outputSize));
            commandBuffer->draw(6);
        } else {
            for (int slot = 0; slot < frames_.size() && slot < kMaximumImages; ++slot) {
                if (!frames_.at(slot)) continue;
                const auto index = static_cast<std::size_t>(slot);
                SlotResources& resources = slots_[index];
                QRhiGraphicsPipeline* pipeline = sidePipelines_[index].get();
                QRhiShaderResourceBindings* bindings = resources.sideBindings.get();
                commandBuffer->setGraphicsPipeline(pipeline);
                // Viewport is dynamic RHI state. Re-issue it after every
                // pipeline change; otherwise Metal can keep the first draw's
                // state while a different encoded/YUV/Bayer pipeline is bound.
                commandBuffer->setViewport(
                    QRhiViewport(0, 0, static_cast<float>(outputSize.width()),
                                 static_cast<float>(outputSize.height())));
                commandBuffer->setVertexInput(0, 1, &input);
                commandBuffer->setShaderResources(bindings);
                commandBuffer->setScissor(scissorFor(slot, outputSize));
                commandBuffer->draw(6);
            }
        }
        commandBuffer->endPass();
    }
};

} // namespace

QmlImageCanvas::QmlImageCanvas(QQuickItem* parent) : QQuickRhiItem(parent) {
    setMirrorVertically(false);
    setAlphaBlending(false);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    connect(this, &QQuickItem::widthChanged, this, [this] {
        emit dividerPositionChanged();
        notifyNavigationChanged();
    });
    connect(this, &QQuickItem::heightChanged, this, [this] {
        emit dividerPositionChanged();
        notifyNavigationChanged();
    });
}

void QmlImageCanvas::setPresentationMode(int mode) {
    const int bounded = std::clamp(mode, 0, 1);
    if (presentationMode_ == bounded) return;
    presentationMode_ = bounded;
    dividerDragging_ = false;
    unsetCursor();
    emit presentationModeChanged();
    emit dividerPositionChanged();
    notifyNavigationChanged();
    update();
}

void QmlImageCanvas::setCompareAmount(qreal amount) {
    const qreal bounded = std::clamp(amount, qreal(0), qreal(1));
    if (qFuzzyCompare(compareAmount_, bounded)) return;
    compareAmount_ = bounded;
    emit compareAmountChanged();
    emit dividerPositionChanged();
    update();
}

void QmlImageCanvas::setSynchronized(bool enabled) {
    if (synchronized_ == enabled) return;
    synchronized_ = enabled;
    emit synchronizedChanged();
}

void QmlImageCanvas::setFrames(const QVector<ImageFramePtr>& frames, int changedSlot,
                               bool resetChangedView) {
    const int oldCount = static_cast<int>(frames_.size());
    frames_ = frames.mid(0, kMaximumImages);
    viewStates_.resize(frames_.size());
    if (resetChangedView && changedSlot >= 0 && changedSlot < viewStates_.size()) {
        viewStates_[changedSlot] = {};
        viewStates_[changedSlot].fitMode = FitMode::Fit;
        viewStates_[changedSlot].normalizedCenter = {0.5, 0.5};
    }
    if (oldCount != frames_.size()) emit imageCountChanged();
    emit dividerPositionChanged();
    notifyNavigationChanged();
    update();
}

void QmlImageCanvas::setFrameAt(int slot, ImageFramePtr frame, bool resetView) {
    if (slot < 0 || slot >= frames_.size()) return;
    frames_[slot] = std::move(frame);
    if (resetView) {
        viewStates_[slot] = {};
        viewStates_[slot].fitMode = FitMode::Fit;
        viewStates_[slot].normalizedCenter = {0.5, 0.5};
    }
    emit dividerPositionChanged();
    notifyNavigationChanged();
    update();
}

ImageFramePtr QmlImageCanvas::frameAt(int slot) const {
    return slot >= 0 && slot < frames_.size() ? frames_.at(slot) : ImageFramePtr{};
}

QSize QmlImageCanvas::logicalImageSize(int slot) const { return logicalSize(frameAt(slot)); }

QRectF QmlImageCanvas::cellRect(int slot) const {
    return comparisonCell(slot, static_cast<int>(frames_.size()), presentationMode_,
                          QSize(qRound(width()), qRound(height())));
}

ViewState QmlImageCanvas::effectiveViewState(int slot) const {
    ViewState result = slot >= 0 && slot < viewStates_.size() ? viewStates_.at(slot) : ViewState{};
    const QSize imageSize = logicalImageSize(slot);
    const QRectF cell = cellRect(slot);
    if (result.fitMode == FitMode::Fit && !imageSize.isEmpty() && !cell.isEmpty()) {
        result.pixelsPerImagePixel = ViewTransform::fitScale(
            imageSize, QSize(qRound(cell.width()), qRound(cell.height())));
    }
    return result;
}

qreal QmlImageCanvas::dividerPosition() const {
    if (presentationMode_ == 0 || frames_.isEmpty()) return 0.0;
    const QSize imageSize = logicalImageSize(0);
    const QRectF cell = cellRect(0);
    if (imageSize.isEmpty() || cell.isEmpty()) return 0.0;
    const QPointF imagePoint = presentationMode_ == 1
        ? QPointF(compareAmount_ * imageSize.width(), imageSize.height() * 0.5)
        : QPointF(imageSize.width() * 0.5, compareAmount_ * imageSize.height());
    const QPointF local = ViewTransform::imageToWidget(
        imagePoint, QSize(qRound(cell.width()), qRound(cell.height())), imageSize,
        effectiveViewState(0));
    return presentationMode_ == 1 ? cell.x() + local.x() : cell.y() + local.y();
}

QVariantMap QmlImageCanvas::navigationState(int slot) const {
    const ImageFramePtr frame = frameAt(slot);
    const QSize imageSize = logicalImageSize(slot);
    const QRectF cell = cellRect(slot);
    const ViewState state = effectiveViewState(slot);
    if (!frame || imageSize.isEmpty() || cell.isEmpty() || state.fitMode == FitMode::Fit)
        return {{QStringLiteral("visible"), false}};
    const int availableWidth = std::max(24, std::min(90, qRound(cell.width()) / 6));
    const int availableHeight = std::max(18, std::min(65, qRound(cell.height()) / 6));
    const QSize contentSize = imageSize.scaled(availableWidth, availableHeight,
                                               Qt::KeepAspectRatio);
    const double percent = state.pixelsPerImagePixel * 100.0;
    const QString zoom = std::abs(percent - std::round(percent)) < 0.05
        ? QStringLiteral("%1%").arg(qRound(percent))
        : QStringLiteral("%1%").arg(QString::number(percent, 'f', 1));
    return {
        {QStringLiteral("visible"), true},
        {QStringLiteral("width"), contentSize.width() + 6},
        {QStringLiteral("height"), contentSize.height() + 6},
        {QStringLiteral("viewport"), ViewTransform::visibleNormalizedRect(
             QSize(qRound(cell.width()), qRound(cell.height())), imageSize, state)},
        {QStringLiteral("zoom"), zoom},
    };
}

void QmlImageCanvas::notifyNavigationChanged() {
    ++navigationRevision_;
    emit navigationRevisionChanged();
}

void QmlImageCanvas::setViewState(int slot, const ViewState& state, bool notify,
                                  bool synchronizeViews) {
    if (slot < 0 || slot >= viewStates_.size()) return;
    const ViewState previousSource = effectiveViewState(slot);
    viewStates_[slot] = state;
    if (synchronized_ && synchronizeViews) {
        for (int target = 0; target < viewStates_.size(); ++target) {
            if (target == slot || !frames_.value(target)) continue;
            viewStates_[target] = syncGroup_.relativelySynchronizedState(
                previousSource, state, effectiveViewState(target));
        }
    }
    if (notify) emit viewStateChanged(slot, effectiveViewState(slot));
    emit dividerPositionChanged();
    notifyNavigationChanged();
    update();
}

void QmlImageCanvas::fitAll() {
    for (int slot = 0; slot < viewStates_.size(); ++slot) {
        viewStates_[slot].fitMode = FitMode::Fit;
        viewStates_[slot].normalizedCenter = {0.5, 0.5};
        emit viewStateChanged(slot, effectiveViewState(slot));
    }
    emit dividerPositionChanged();
    notifyNavigationChanged();
    update();
}

void QmlImageCanvas::actualPixelsAll() {
    for (int slot = 0; slot < viewStates_.size(); ++slot) {
        viewStates_[slot].fitMode = FitMode::Manual;
        viewStates_[slot].pixelsPerImagePixel = 1.0;
        viewStates_[slot].normalizedCenter = {0.5, 0.5};
        emit viewStateChanged(slot, effectiveViewState(slot));
    }
    emit dividerPositionChanged();
    notifyNavigationChanged();
    update();
}

int QmlImageCanvas::slotAt(const QPointF& position) const {
    if (presentationMode_ != 0) return frames_.isEmpty() ? -1 : 0;
    for (int slot = 0; slot < frames_.size(); ++slot)
        if (cellRect(slot).contains(position)) return slot;
    return -1;
}

QPointF QmlImageCanvas::normalizedPoint(int slot, const QPointF& position) const {
    const QSize imageSize = logicalImageSize(slot);
    const QRectF cell = cellRect(slot);
    if (imageSize.isEmpty() || cell.isEmpty()) return {};
    const QPointF local = position - cell.topLeft();
    const QPointF image = ViewTransform::widgetToImage(
        local, QSize(qRound(cell.width()), qRound(cell.height())), imageSize,
        effectiveViewState(slot));
    return {std::clamp(image.x() / imageSize.width(), 0.0, 1.0),
            std::clamp(image.y() / imageSize.height(), 0.0, 1.0)};
}

void QmlImageCanvas::emitPixelAt(const QPointF& position) {
    const int slot = slotAt(position);
    const QSize imageSize = logicalImageSize(slot);
    const ImageFramePtr frame = frameAt(slot);
    if (slot < 0 || !frame || imageSize.isEmpty()) {
        emit pixelHovered(slot, {}, {}, false);
        return;
    }
    const QRectF cell = cellRect(slot);
    const QPointF imagePosition = ViewTransform::widgetToImage(
        position - cell.topLeft(), QSize(qRound(cell.width()), qRound(cell.height())), imageSize,
        effectiveViewState(slot));
    const QPoint pixel(qFloor(imagePosition.x()), qFloor(imagePosition.y()));
    if (!QRect(QPoint{}, imageSize).contains(pixel)) {
        emit pixelHovered(slot, pixel, {}, false);
        return;
    }
    QColor color;
    if (const QImage* image = frame->qImage(); image && !image->isNull()) {
        const int x = std::clamp(qFloor((pixel.x() + 0.5) * image->width() / imageSize.width()),
                                 0, image->width() - 1);
        const int y = std::clamp(qFloor((pixel.y() + 0.5) * image->height() / imageSize.height()),
                                 0, image->height() - 1);
        color = image->pixelColor(x, y);
    }
    emit pixelHovered(slot, pixel, color, true);
}

void QmlImageCanvas::wheelEvent(QWheelEvent* event) {
    const int slot = slotAt(event->position());
    const QSize imageSize = logicalImageSize(slot);
    const QRectF cell = cellRect(slot);
    if (slot < 0 || imageSize.isEmpty() || cell.isEmpty()) {
        event->ignore();
        return;
    }
    const ViewState state = effectiveViewState(slot);
    const double scale = state.pixelsPerImagePixel *
                         std::pow(1.2, event->angleDelta().y() / 120.0);
    const QPointF local = event->position() - cell.topLeft();
    const bool controlHeld = isIndependentViewAdjustment(event->modifiers());
    setViewState(slot,
                 ViewTransform::zoomAt(state, scale, local,
                                       QSize(qRound(cell.width()), qRound(cell.height())),
                                       imageSize),
                 true, !controlHeld);
    event->accept();
}

void QmlImageCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        emit contextMenuRequested(event->position());
        event->accept();
        return;
    }
    activeSlot_ = slotAt(event->position());
    if (activeSlot_ >= 0) emit slotActivated(activeSlot_);
    if (event->button() != Qt::LeftButton || activeSlot_ < 0 ||
        logicalImageSize(activeSlot_).isEmpty()) {
        event->ignore();
        return;
    }
    if (presentationMode_ != 0) {
        const qreal pointer = presentationMode_ == 1 ? event->position().x()
                                                     : event->position().y();
        if (std::abs(pointer - dividerPosition()) <= 7.0) dividerDragging_ = true;
    }
    if (!dividerDragging_) {
        dragging_ = true;
        lastMousePosition_ = event->position();
    }
    event->accept();
}

void QmlImageCanvas::mouseMoveEvent(QMouseEvent* event) {
    emitPixelAt(event->position());
    if (dividerDragging_) {
        const QPointF normalized = normalizedPoint(0, event->position());
        setCompareAmount(presentationMode_ == 1 ? normalized.x() : normalized.y());
        event->accept();
        return;
    }
    if (dragging_ && activeSlot_ >= 0) {
        const ViewState state = effectiveViewState(activeSlot_);
        const bool controlHeld = isIndependentViewAdjustment(event->modifiers());
        setViewState(activeSlot_,
                     ViewTransform::panBy(state, event->position() - lastMousePosition_,
                                          logicalImageSize(activeSlot_)),
                     true, !controlHeld);
        lastMousePosition_ = event->position();
        event->accept();
        return;
    }
    if (presentationMode_ != 0) {
        const qreal pointer = presentationMode_ == 1 ? event->position().x()
                                                     : event->position().y();
        if (std::abs(pointer - dividerPosition()) <= 7.0)
            setCursor(presentationMode_ == 1 ? Qt::SplitHCursor : Qt::SplitVCursor);
        else
            unsetCursor();
    }
    QQuickRhiItem::mouseMoveEvent(event);
}

void QmlImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    dragging_ = false;
    dividerDragging_ = false;
    activeSlot_ = -1;
    unsetCursor();
    QQuickRhiItem::mouseReleaseEvent(event);
}

void QmlImageCanvas::hoverMoveEvent(QHoverEvent* event) {
    emitPixelAt(event->position());
    if (presentationMode_ != 0 && !dragging_ && !dividerDragging_) {
        const qreal pointer = presentationMode_ == 1 ? event->position().x()
                                                     : event->position().y();
        if (std::abs(pointer - dividerPosition()) <= 7.0)
            setCursor(presentationMode_ == 1 ? Qt::SplitHCursor : Qt::SplitVCursor);
        else
            unsetCursor();
    }
    QQuickRhiItem::hoverMoveEvent(event);
}

void QmlImageCanvas::hoverLeaveEvent(QHoverEvent* event) {
    unsetCursor();
    emit pixelHovered(-1, {}, {}, false);
    QQuickRhiItem::hoverLeaveEvent(event);
}

QQuickRhiItemRenderer* QmlImageCanvas::createRenderer() { return new Renderer; }

} // namespace ispview
