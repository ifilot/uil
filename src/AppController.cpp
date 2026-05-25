#include "AppController.hpp"

#include "pdf/QtPdfBackend.hpp"
#include "ui/AudienceWindow.hpp"
#include "util/DocumentHash.hpp"
#include "util/ImageUtil.hpp"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QScreen>
#include <QtMath>

#include <algorithm>
#include <optional>

Q_LOGGING_CATEGORY(logRender, "render")
Q_LOGGING_CATEGORY(logScreens, "screens")

AppController::AppController(QObject* parent)
    : QObject(parent),
      m_backend(std::make_unique<QtPdfBackend>()),
      m_slideCache(512ll * 1024ll * 1024ll),
      m_renderScheduler(this) {
    connect(&m_renderScheduler, &RenderScheduler::renderStarted, this, &AppController::handleRenderStarted);
    connect(&m_renderScheduler, &RenderScheduler::renderFinished, this, &AppController::handleRenderFinished);
    connect(&m_videoTimer, &QTimer::timeout, this, &AppController::advanceVideoFrame);
    refreshScreens();
}

void AppController::setAudienceWindow(AudienceWindow* audienceWindow) {
    if (m_audienceWindow) {
        disconnect(m_audienceWindow, nullptr, this, nullptr);
    }

    m_audienceWindow = audienceWindow;
    if (m_audienceWindow) {
        connect(m_audienceWindow, &AudienceWindow::renderTargetChanged, this, &AppController::handleAudienceRenderTargetChanged);
    }
}

bool AppController::openPdf(const QString& path) {
    stopMediaPlayback();
    auto backend = std::make_unique<QtPdfBackend>();
    QString errorMessage;
    if (!backend->open(path, &errorMessage)) {
        emit statusMessageChanged(QStringLiteral("Could not open PDF: %1").arg(errorMessage));
        return false;
    }

    m_backend = std::move(backend);
    m_currentPath = path;
    m_documentHash = documentHashForFile(path);
    m_mediaScanResult = scanPdfMediaAnnotations(path);
    m_currentPageIndex = 0;
    m_slideCache.clear();
    m_renderScheduler.clear();
    m_renderGeneration = m_renderScheduler.generation();

    emit documentChanged(pageCount());
    emit mediaScanChanged(m_mediaScanResult);
    emit pageChanged(m_currentPageIndex, pageCount());
    if (m_mediaScanResult.hasMedia()) {
        emit statusMessageChanged(m_mediaScanResult.summary());
    } else {
        emit statusMessageChanged(QStringLiteral("Opened %1 page(s)").arg(pageCount()));
    }
    updateVisibleSlides();
    schedulePredictiveRenders();
    return true;
}

void AppController::nextPage() {
    if (!hasDocument()) {
        return;
    }
    goToPage(m_currentPageIndex + 1);
}

void AppController::previousPage() {
    if (!hasDocument()) {
        return;
    }
    goToPage(m_currentPageIndex - 1);
}

void AppController::goToPage(int pageIndex) {
    if (!hasDocument()) {
        return;
    }

    const int clampedPage = std::clamp(pageIndex, 0, pageCount() - 1);
    if (clampedPage == m_currentPageIndex) {
        return;
    }

    stopMediaPlayback();
    m_currentPageIndex = clampedPage;
    emit pageChanged(m_currentPageIndex, pageCount());
    updateVisibleSlides();
    schedulePredictiveRenders();
}

int AppController::currentPage() const {
    return m_currentPageIndex;
}

int AppController::pageCount() const {
    return m_backend ? m_backend->pageCount() : 0;
}

QString AppController::currentPath() const {
    return m_currentPath;
}

QScreen* AppController::selectedAudienceScreen() const {
    return m_audienceScreen;
}

void AppController::setAudienceScreen(QScreen* screen) {
    if (!screen) {
        return;
    }

    const bool screenChanged = screen != m_audienceScreen;
    m_audienceScreen = screen;
    qCInfo(logScreens) << "Audience screen selected:" << screen->name() << screen->geometry();
    emit audienceScreenChanged(screen);

    if (m_audienceWindow) {
        m_audienceWindow->setAudienceScreen(screen);
    }

    if (hasDocument()) {
        if (screenChanged) {
            m_slideCache.clear();
            m_renderScheduler.clear();
            m_renderGeneration = m_renderScheduler.generation();
        }
        updateVisibleSlides();
        schedulePredictiveRenders();
    }
}

void AppController::refreshScreens() {
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    QScreen* preferredAudience = primary;

    for (QScreen* screen : screens) {
        qCInfo(logScreens) << "Screen:" << screen->name()
                           << "primary:" << (screen == primary)
                           << "geometry:" << screen->geometry()
                           << "dpr:" << screen->devicePixelRatio();
        if (screen != primary && preferredAudience == primary) {
            preferredAudience = screen;
        }
    }

    const bool selectedScreenGone = !screens.contains(m_audienceScreen);
    if (selectedScreenGone) {
        m_audienceScreen = preferredAudience;
        m_slideCache.clear();
        m_renderScheduler.clear();
        m_renderGeneration = m_renderScheduler.generation();
        emit audienceScreenChanged(m_audienceScreen);
        if (m_audienceWindow && m_audienceScreen) {
            m_audienceWindow->setAudienceScreen(m_audienceScreen);
        }
    }

    emit screenListChanged();

    if (selectedScreenGone && hasDocument()) {
        updateVisibleSlides();
        schedulePredictiveRenders();
    }
}

void AppController::toggleAudienceFullscreen() {
    if (m_audienceWindow) {
        m_audienceWindow->toggleFullscreen();
    }
}

void AppController::toggleMediaPlayback() {
    if (m_videoPlaying) {
        stopMediaPlayback();
        emit statusMessageChanged(QStringLiteral("Media stopped"));
        return;
    }

    startMediaPlayback();
}

void AppController::enterAudienceFullscreen() {
    if (m_audienceWindow) {
        m_audienceWindow->enterFullscreen();
    }
}

QSize AppController::audienceRenderPixelSize(int pageIndex) const {
    if (!hasDocument()) {
        return {};
    }

    QSize logicalSize;
    qreal devicePixelRatio = 1.0;
    if (m_audienceScreen) {
        logicalSize = m_audienceScreen->geometry().size();
        devicePixelRatio = m_audienceScreen->devicePixelRatio();
    } else if (m_audienceWindow) {
        logicalSize = m_audienceWindow->renderLogicalSize();
        devicePixelRatio = m_audienceWindow->renderDevicePixelRatio();
    }

    if (!logicalSize.isValid()) {
        logicalSize = QSize(1280, 720);
    }

    const QSize boundingPixels(qMax(1, int(qRound(logicalSize.width() * devicePixelRatio))),
                               qMax(1, int(qRound(logicalSize.height() * devicePixelRatio))));

    return containedSizeForAspect(m_backend->pageSizePoints(pageIndex), boundingPixels);
}

SlideCacheKey AppController::cacheKeyForPage(int pageIndex) const {
    return SlideCacheKey{
        m_documentHash,
        pageIndex,
        audienceRenderPixelSize(pageIndex),
        0
    };
}

QString AppController::textureKeyForCacheKey(const SlideCacheKey& key) const {
    return QStringLiteral("%1:%2:%3x%4:%5")
        .arg(key.documentHash)
        .arg(key.pageIndex)
        .arg(key.pixelSize.width())
        .arg(key.pixelSize.height())
        .arg(key.rotation);
}

bool AppController::hasDocument() const {
    return m_backend && m_backend->pageCount() > 0;
}

RenderRequest AppController::renderRequestForPage(int pageIndex) const {
    const SlideCacheKey key = cacheKeyForPage(pageIndex);
    return RenderRequest{
        m_currentPath,
        key.documentHash,
        key.pageIndex,
        key.pixelSize,
        key.rotation,
        m_renderGeneration
    };
}

void AppController::updateVisibleSlides() {
    if (!hasDocument()) {
        return;
    }

    const SlideCacheKey currentKey = cacheKeyForPage(m_currentPageIndex);
    if (auto currentImage = m_slideCache.get(currentKey)) {
        emit currentSlideImageChanged(*currentImage);
        if (m_audienceWindow) {
            m_audienceWindow->setSlideImage(textureKeyForCacheKey(currentKey), *currentImage);
            if (!m_videoPlaying) {
                m_audienceWindow->clearVideoOverlay();
            }
        }
        emit statusMessageChanged(QStringLiteral("Cache hit: page %1").arg(m_currentPageIndex + 1));
    } else {
        emit currentSlideImageChanged(QImage());
        if (m_audienceWindow) {
            m_audienceWindow->clearSlideImage();
            m_audienceWindow->clearVideoOverlay();
        }
        emit statusMessageChanged(QStringLiteral("Rendering page %1").arg(m_currentPageIndex + 1));
    }

    const int nextPage = m_currentPageIndex + 1;
    if (nextPage < pageCount()) {
        const SlideCacheKey nextKey = cacheKeyForPage(nextPage);
        if (auto nextImage = m_slideCache.get(nextKey)) {
            emit nextSlideImageChanged(*nextImage);
        } else {
            emit nextSlideImageChanged(QImage());
        }
    } else {
        emit nextSlideImageChanged(QImage());
    }

    emit pageChanged(m_currentPageIndex, pageCount());
}

void AppController::schedulePredictiveRenders() {
    if (!hasDocument()) {
        return;
    }

    requestPageRender(m_currentPageIndex, 100);
    requestPageRender(m_currentPageIndex + 1, 75);
    requestPageRender(m_currentPageIndex - 1, 50);
    requestPageRender(m_currentPageIndex + 2, 25);
}

void AppController::requestPageRender(int pageIndex, int priority) {
    if (!hasDocument() || pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }

    const SlideCacheKey key = cacheKeyForPage(pageIndex);
    if (m_slideCache.contains(key)) {
        return;
    }

    m_renderScheduler.requestRender(renderRequestForPage(pageIndex), priority);
}

QImage AppController::imageWithMediaFrames(int pageIndex, const QImage& image) const {
    if (image.isNull() || !m_backend) {
        return image;
    }

    QImage result = image;
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QSizeF pageSize = m_backend->pageSizePoints(pageIndex);
    if (!pageSize.isValid()) {
        return result;
    }

    const double scaleX = double(result.width()) / pageSize.width();
    const double scaleY = double(result.height()) / pageSize.height();

    for (const PdfMediaAnnotation& annotation : m_mediaScanResult.annotations) {
        if (annotation.pageIndex != pageIndex || !annotation.hasFirstFrame() || !annotation.rect.isValid()) {
            continue;
        }

        const QRectF target(
            annotation.rect.left() * scaleX,
            (pageSize.height() - annotation.rect.bottom()) * scaleY,
            annotation.rect.width() * scaleX,
            annotation.rect.height() * scaleY);
        painter.drawImage(target.toRect(), annotation.firstFrame);
    }

    return result;
}

const PdfMediaAnnotation* AppController::currentPlayableMediaAnnotation() const {
    for (const PdfMediaAnnotation& annotation : m_mediaScanResult.annotations) {
        if (annotation.pageIndex == m_currentPageIndex
            && annotation.isMp4()
            && !annotation.resolvedFilePath.isEmpty()
            && annotation.rect.isValid()) {
            return &annotation;
        }
    }
    return nullptr;
}

QRectF AppController::normalizedMediaRect(const PdfMediaAnnotation& annotation) const {
    if (!m_backend || !annotation.rect.isValid()) {
        return {};
    }

    const QSizeF pageSize = m_backend->pageSizePoints(annotation.pageIndex);
    if (!pageSize.isValid()) {
        return {};
    }

    return QRectF(
        annotation.rect.left() / pageSize.width(),
        (pageSize.height() - annotation.rect.bottom()) / pageSize.height(),
        annotation.rect.width() / pageSize.width(),
        annotation.rect.height() / pageSize.height());
}

void AppController::startMediaPlayback() {
    const PdfMediaAnnotation* annotation = currentPlayableMediaAnnotation();
    if (!annotation) {
        emit statusMessageChanged(QStringLiteral("No playable MP4 media on this slide"));
        return;
    }

    auto reader = std::make_unique<VideoFrameReader>();
    QString errorMessage;
    if (!reader->open(annotation->resolvedFilePath, &errorMessage)) {
        emit statusMessageChanged(QStringLiteral("Could not play media: %1").arg(errorMessage));
        return;
    }

    m_videoReader = std::move(reader);
    m_activeVideoRect = normalizedMediaRect(*annotation);
    m_lastVideoPtsMs = -1;
    m_videoPlaying = true;
    emit statusMessageChanged(QStringLiteral("Playing media: %1").arg(annotation->fileName));
    advanceVideoFrame();
}

void AppController::stopMediaPlayback() {
    m_videoTimer.stop();
    if (m_videoReader) {
        m_videoReader->close();
        m_videoReader.reset();
    }
    m_videoPlaying = false;
    m_activeVideoRect = {};
    m_lastVideoPtsMs = -1;
    if (m_audienceWindow) {
        m_audienceWindow->clearVideoOverlay();
    }
}

void AppController::advanceVideoFrame() {
    if (!m_videoReader || !m_videoPlaying) {
        stopMediaPlayback();
        return;
    }

    QString errorMessage;
    std::optional<DecodedVideoFrame> frame = m_videoReader->readNextFrame(&errorMessage);
    if (!frame) {
        stopMediaPlayback();
        if (!errorMessage.isEmpty()) {
            emit statusMessageChanged(QStringLiteral("Media stopped: %1").arg(errorMessage));
        } else {
            emit statusMessageChanged(QStringLiteral("Media finished"));
        }
        return;
    }

    if (m_audienceWindow) {
        m_audienceWindow->setVideoFrame(frame->image, m_activeVideoRect);
    }

    int nextDelayMs = 33;
    if (m_lastVideoPtsMs >= 0 && frame->ptsMs > m_lastVideoPtsMs) {
        nextDelayMs = int(std::clamp<qint64>(frame->ptsMs - m_lastVideoPtsMs, 1, 100));
    }
    m_lastVideoPtsMs = frame->ptsMs;
    m_videoTimer.start(nextDelayMs);
}

void AppController::handleAudienceRenderTargetChanged() {
    if (!hasDocument()) {
        return;
    }

    updateVisibleSlides();
    schedulePredictiveRenders();
}

void AppController::handleRenderStarted(const RenderRequest& request) {
    qCInfo(logRender) << "Render request started page" << request.pageIndex + 1
                      << "at" << request.pixelSize
                      << "generation" << request.generation;
}

void AppController::handleRenderFinished(const RenderRequest& request, const QImage& image, qint64 elapsedMs, const QString& errorMessage) {
    if (request.generation != m_renderGeneration || request.documentHash != m_documentHash) {
        qCInfo(logRender) << "Ignoring stale render result page" << request.pageIndex + 1;
        return;
    }

    if (!errorMessage.isEmpty() || image.isNull()) {
        qCWarning(logRender) << "Render failed page" << request.pageIndex + 1 << errorMessage;
        if (request.pageIndex == m_currentPageIndex) {
            emit statusMessageChanged(QStringLiteral("Failed to render page %1").arg(request.pageIndex + 1));
        }
        return;
    }

    const SlideCacheKey key{
        request.documentHash,
        request.pageIndex,
        request.pixelSize,
        request.rotation
    };
    const QImage displayImage = imageWithMediaFrames(request.pageIndex, image);
    m_slideCache.put(key, displayImage);
    const QString textureKey = textureKeyForCacheKey(key);
    if (m_audienceWindow) {
        m_audienceWindow->cacheSlideImage(textureKey, displayImage);
    }

    qCInfo(logRender) << "Rendered page" << request.pageIndex + 1
                      << "at" << request.pixelSize
                      << "in" << elapsedMs << "ms"
                      << "cache bytes" << m_slideCache.estimatedMemoryBytes()
                      << "hits" << m_slideCache.hits()
                      << "misses" << m_slideCache.misses()
                      << "evictions" << m_slideCache.evictions();

    if (request.pageIndex == m_currentPageIndex) {
        emit currentSlideImageChanged(displayImage);
        if (m_audienceWindow) {
            m_audienceWindow->setSlideImage(textureKey, displayImage);
        }
        emit statusMessageChanged(QStringLiteral("Rendered page %1 in %2 ms").arg(request.pageIndex + 1).arg(elapsedMs));
    }

    if (request.pageIndex == m_currentPageIndex + 1) {
        emit nextSlideImageChanged(displayImage);
    }
}
