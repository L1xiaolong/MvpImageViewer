#include "qml/thumbnail_image_provider.h"

#include "core/raw_image_parameters.h"
#include "io/image_decoder.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"

#include <QAbstractFileIconProvider>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMutexLocker>
#include <QPainter>
#include <QPointer>
#include <QQuickTextureFactory>
#include <QUrl>

namespace ispview {

SystemFolderIconProvider::SystemFolderIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image),
      fileSystemModel_(std::make_unique<QFileSystemModel>()) {}

SystemFolderIconProvider::~SystemFolderIconProvider() = default;

QImage SystemFolderIconProvider::requestImage(const QString& id, QSize* size,
                                               const QSize& requestedSize) {
    const QString path = QUrl::fromPercentEncoding(id.toUtf8());
    const QSize target = requestedSize.isValid() ? requestedSize : QSize(32, 32);
    QAbstractFileIconProvider* iconProvider = fileSystemModel_->iconProvider();
    QIcon icon = iconProvider->icon(QFileInfo(path));
    if (icon.isNull()) icon = iconProvider->icon(QAbstractFileIconProvider::Folder);
    const QImage image = icon.pixmap(target).toImage();
    if (size) *size = image.size();
    return image;
}

namespace {

QImage placeholder(const QString& text, const QSize& size) {
    QImage image(size.expandedTo(QSize(160, 120)), QImage::Format_RGBA8888);
    image.fill(QColor(QStringLiteral("#E8EBEB")));
    QPainter painter(&image);
    painter.setPen(QColor(QStringLiteral("#69747D")));
    painter.drawText(image.rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                     text);
    return image;
}

class ThumbnailImageResponse final : public QQuickImageResponse {
  public:
    ThumbnailImageResponse(ImageLoader* loader, QString id, QSize requestedSize)
        : loader_(loader), id_(std::move(id)), requestedSize_(requestedSize) {
        const QPointer<ThumbnailImageResponse> self(this);
        QMetaObject::invokeMethod(loader_, [self] {
            if (self) self->start();
        }, Qt::QueuedConnection);
    }

    QQuickTextureFactory* textureFactory() const override {
        const QMutexLocker lock(&mutex_);
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }

    void cancel() override {
        cancelled_.store(true, std::memory_order_relaxed);
        handle_.cancel();
    }

    [[nodiscard]] QImage image() const {
        const QMutexLocker lock(&mutex_);
        return image_;
    }

  private:
    void start() {
        if (cancelled_.load(std::memory_order_relaxed)) return;
        const QString path = QUrl::fromPercentEncoding(
            id_.section(QLatin1Char('?'), 0, 0).toUtf8());
        std::optional<RawImageParameters> parameters = loader_->rawParameters(path);
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (!parameters && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv"))) {
            parameters = RawPresetStore::loadForFile(path);
            if (!parameters) parameters = RawPresetStore::inferFromFileName(path);
            if (parameters && availableFrameCount(QFileInfo(path).size(), *parameters) > 0) {
                loader_->setRawParameters(path, *parameters);
            } else {
                parameters.reset();
            }
        }
        const QPointer<ThumbnailImageResponse> self(this);
        const bool galleryPreview = id_.contains(QStringLiteral("purpose=gallery"));
        const LoadCategory category = galleryPreview
                                          ? LoadCategory::Interactive
                                          : id_.contains(QStringLiteral("priority=near"))
                                                ? LoadCategory::NearViewport
                                                : LoadCategory::VisibleThumbnail;
        const DecodePurpose purpose = galleryPreview ? DecodePurpose::Preview
                                                     : DecodePurpose::Thumbnail;
        handle_ = loader_->request(
            ++requestCounter_, {path, purpose, requestedSize_, parameters},
            [self](quint64, const DecodeResult& result) {
                if (!self || self->cancelled_.load(std::memory_order_relaxed)) return;
                QImage image;
                if (result.frame && result.frame->qImage()) image = *result.frame->qImage();
                if (image.isNull()) image = placeholder(QStringLiteral("Preview unavailable"),
                                                        self->requestedSize_);
                {
                    const QMutexLocker lock(&self->mutex_);
                    self->image_ = std::move(image);
                }
                emit self->finished();
            },
            RequestOptions{category, 0, QStringLiteral("qml-thumbnail")});
    }

    ImageLoader* loader_ = nullptr;
    QString id_;
    QSize requestedSize_;
    LoadHandle handle_;
    mutable QMutex mutex_;
    QImage image_;
    std::atomic_bool cancelled_{false};
    inline static std::atomic<quint64> requestCounter_{0};
};

} // namespace

ThumbnailImageProvider::ThumbnailImageProvider(std::shared_ptr<const IImageDecoder> decoder,
                                               ImageLoader* loader)
    : loader_(loader) {
    Q_UNUSED(decoder);
}

QQuickImageResponse* ThumbnailImageProvider::requestImageResponse(const QString& id,
                                                                  const QSize& requestedSize) {
    const QSize target = id.contains(QStringLiteral("purpose=gallery"))
                             ? requestedSize.boundedTo(QSize(2048, 2048))
                             : bucketedSize(requestedSize);
    return new ThumbnailImageResponse(loader_, id, target);
}

QImage ThumbnailImageProvider::requestImage(const QString& id, QSize* size,
                                            const QSize& requestedSize) {
    auto* response = static_cast<ThumbnailImageResponse*>(requestImageResponse(id, requestedSize));
    QEventLoop loop;
    QObject::connect(response, &QQuickImageResponse::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QImage image = response->image();
    if (size) *size = image.size();
    delete response;
    return image;
}

QSize ThumbnailImageProvider::bucketedSize(const QSize& requestedSize) {
    const QSize fallback(256, 256);
    const int edge = qMax(requestedSize.isValid() ? qMax(requestedSize.width(), requestedSize.height())
                                                   : fallback.width(),
                          1);
    for (const int bucket : {128, 256, 384, 512}) {
        if (edge <= bucket) return {bucket, bucket};
    }
    return {512, 512};
}

} // namespace ispview
