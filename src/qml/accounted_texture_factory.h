#pragma once

#include "core/pixel_memory_ledger.h"
#include "core/nominal_gpu_bytes.h"
#include <QQuickTextureFactory>
#include <QSGTexture>
#include <QMutexLocker>
#include <memory>

namespace mvpview {

// Preserve Qt's texture conversion/atlas behavior; account the actual CPU image
// held by its factory, which may differ from the decoder's representation.
class AccountedTextureFactory final : public QQuickTextureFactory {
public:
    static QQuickTextureFactory* create(const QImage& image, std::shared_ptr<PixelMemoryLedger> ledger) {
        std::unique_ptr<QQuickTextureFactory> factory(textureFactoryForImage(image));
        if (!factory) return nullptr;
        return new AccountedTextureFactory(std::move(factory), std::move(ledger));
    }
    QSGTexture* createTexture(QQuickWindow* window) const override {
        const QMutexLocker lock(&mutex_);
        auto* texture = factory_->createTexture(window);
        trackTexture(texture, ledger_);
        refreshAccounting(); // QSG_TRANSIENT_IMAGES can release the factory's CPU image.
        return texture;
    }
    QSize textureSize() const override { const QMutexLocker lock(&mutex_); return factory_->textureSize(); }
    int textureByteCount() const override { const QMutexLocker lock(&mutex_); return factory_->textureByteCount(); }
    QImage image() const override { const QMutexLocker lock(&mutex_); return factory_->image(); }
    static void trackTexture(QSGTexture* texture, const std::shared_ptr<PixelMemoryLedger>& ledger) {
        if (!texture || !ledger) return;
        // Qt's default image factory uploads RGBA8. Atlas subtextures are charged
        // by logical area; the shared atlas allocation and unused area are opaque.
        PixelStorageFootprint footprint;
        footprint.addResource(texture, nominalGpuBytes(texture->textureSize(), 4));
        const auto gpuLedger = ledger->gpuResources();
        auto lease = gpuLedger->retain(std::move(footprint));
        QObject::connect(texture, &QObject::destroyed, [lease = std::move(lease)]() mutable {
            lease.reset();
        });
    }
private:
    AccountedTextureFactory(std::unique_ptr<QQuickTextureFactory> factory, std::shared_ptr<PixelMemoryLedger> ledger)
        : factory_(std::move(factory)), ledger_(std::move(ledger)) { refreshAccounting(); }
    void refreshAccounting() const {
        if (!ledger_) return;
        PixelStorageFootprint footprint; footprint.add(factory_->image());
        ownership_.attach(ledger_, std::move(footprint));
    }
    std::unique_ptr<QQuickTextureFactory> factory_;
    std::shared_ptr<PixelMemoryLedger> ledger_;
    mutable QMutex mutex_;
    mutable PixelMemoryOwnership ownership_;
};

} // namespace mvpview
