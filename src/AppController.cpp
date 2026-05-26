#include "AppController.hpp"

#include "package/UilPackage.hpp"
#include "pdf/QtPdfBackend.hpp"
#include "ui/AudienceWindow.hpp"
#include "util/DocumentHash.hpp"
#include "util/ImageUtil.hpp"

#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QScreen>
#include <QStringList>
#include <QTemporaryDir>
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
        connect(m_audienceWindow, &AudienceWindow::annotationOverlayChanged, this, &AppController::currentAnnotationOverlayChanged);
        connect(m_audienceWindow, &AudienceWindow::presentationClosed, this, &AppController::stopMediaPlayback);
    }
}

bool AppController::openPdf(const QString& path) {
    stopMediaPlayback();
    const QFileInfo inputInfo(path);
    const bool isUilPackage = inputInfo.suffix().compare(QStringLiteral("uil"), Qt::CaseInsensitive) == 0;
    QString pdfPath = path;
    QString documentHashPath = path;
    QString packageRootPath;
    QStringList packageMovieAssetPaths;
    QString entryPdfRelativePath = QStringLiteral("build/presentation.pdf");
    QHash<int, QImage> packageOverlayImages;
    QSet<int> packageHiddenOverlayPages;
    bool packageOverlaysGloballyVisible = true;
    std::unique_ptr<QTemporaryDir> packageTempDir;

    if (isUilPackage) {
        packageTempDir = std::make_unique<QTemporaryDir>();
        packageTempDir->setAutoRemove(true);
        if (!packageTempDir->isValid()) {
            emit statusMessageChanged(QStringLiteral("Could not create temporary directory for UIL package"));
            return false;
        }

        UilPackageOpenResult packageResult;
        QString packageError;
        if (!extractUilPackage(path, *packageTempDir, &packageResult, &packageError)) {
            emit statusMessageChanged(QStringLiteral("Could not open UIL package: %1").arg(packageError));
            return false;
        }

        pdfPath = packageResult.entryPdfPath;
        packageRootPath = packageResult.packageRootPath;
        packageMovieAssetPaths = packageResult.movieAssetPaths;
        entryPdfRelativePath = packageResult.entryPdfRelativePath;
        packageHiddenOverlayPages = packageResult.hiddenOverlayPages;
        packageOverlaysGloballyVisible = packageResult.overlaysGloballyVisible;
        for (auto it = packageResult.overlayImagePaths.constBegin(); it != packageResult.overlayImagePaths.constEnd(); ++it) {
            QImage overlay(packageTempDir->filePath(it.value()));
            if (!overlay.isNull()) {
                packageOverlayImages.insert(it.key(), overlay);
            }
        }
    }

    auto backend = std::make_unique<QtPdfBackend>();
    QString errorMessage;
    if (!backend->open(pdfPath, &errorMessage)) {
        emit statusMessageChanged(isUilPackage
            ? QStringLiteral("Could not open UIL package PDF: %1").arg(errorMessage)
            : QStringLiteral("Could not open PDF: %1").arg(errorMessage));
        return false;
    }

    m_backend = std::move(backend);
    m_packageTempDir = std::move(packageTempDir);
    m_currentPath = pdfPath;
    m_currentPackagePath = isUilPackage ? QFileInfo(path).absoluteFilePath() : QString();
    m_packageRootPath = packageRootPath;
    m_entryPdfRelativePath = entryPdfRelativePath;
    m_packageMovieAssetPaths = packageMovieAssetPaths;
    m_loadedOverlayImages = packageOverlayImages;
    m_loadedHiddenOverlayPages = packageHiddenOverlayPages;
    m_loadedOverlaysGloballyVisible = packageOverlaysGloballyVisible;
    m_documentHash = documentHashForFile(documentHashPath);
    m_mediaScanResult = scanPdfMediaAnnotations(pdfPath, packageRootPath, packageMovieAssetPaths);
    m_currentPageIndex = 0;
    m_slideCache.clear();
    m_renderScheduler.clear();
    m_renderGeneration = m_renderScheduler.generation();
    m_deckOverviewRenderSize = {};
    if (m_audienceWindow) {
        m_audienceWindow->clearSlideImage();
        m_audienceWindow->clearVideoOverlay();
        QHash<QString, QImage> overlaysByTextureKey;
        for (auto it = m_loadedOverlayImages.constBegin(); it != m_loadedOverlayImages.constEnd(); ++it) {
            if (it.key() >= 0 && it.key() < pageCount() && !it.value().isNull()) {
                overlaysByTextureKey.insert(textureKeyForCacheKey(cacheKeyForPage(it.key())), it.value());
            }
        }
        m_audienceWindow->setAnnotationOverlaysByTextureKey(overlaysByTextureKey);
    }

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
    requestPageRender(m_currentPageIndex, 1000);
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

void AppController::requestDeckOverviewRenders(const QSize& boundingPixelSize, int focusedPageIndex) {
    if (!hasDocument() || !boundingPixelSize.isValid()) {
        return;
    }

    const bool renderSizeChanged = m_deckOverviewRenderSize != boundingPixelSize;
    m_deckOverviewRenderSize = boundingPixelSize;
    const int focusedPage = std::clamp(focusedPageIndex >= 0 ? focusedPageIndex : m_currentPageIndex, 0, pageCount() - 1);

    const int firstCacheCheckPage = renderSizeChanged ? 0 : focusedPage;
    const int lastCacheCheckPage = renderSizeChanged ? pageCount() - 1 : focusedPage;
    for (int page = firstCacheCheckPage; page <= lastCacheCheckPage; ++page) {
        const SlideCacheKey key = cacheKeyForPageAtSize(page, boundingPixelSize);
        if (auto image = m_slideCache.get(key)) {
            emit deckSlideImageChanged(page, boundingPixelSize, *image);
        }
    }

    requestPageRenderAtSize(focusedPage, boundingPixelSize, 95);
    for (int distance = 1; distance < pageCount(); ++distance) {
        const int afterPage = focusedPage + distance;
        const int beforePage = focusedPage - distance;
        const int priority = std::max(1, 80 - distance);
        if (afterPage < pageCount()) {
            requestPageRenderAtSize(afterPage, boundingPixelSize, priority);
        }
        if (beforePage >= 0) {
            requestPageRenderAtSize(beforePage, boundingPixelSize, priority);
        }
    }
}

QString AppController::currentPath() const {
    return m_currentPath;
}

bool AppController::exportAnnotatedPdf(const QString& path, const QHash<int, QImage>& overlayImages, QString* errorMessage) {
    if (!hasDocument()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No presentation is open");
        }
        return false;
    }
    if (path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Missing export path");
        }
        return false;
    }

    QPdfWriter writer(path);
    writer.setResolution(144);
    writer.setCreator(QStringLiteral("uil"));

    QPainter painter;
    for (int pageIndex = 0; pageIndex < pageCount(); ++pageIndex) {
        const QSizeF pageSizePoints = m_backend->pageSizePoints(pageIndex);
        if (!pageSizePoints.isValid()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not determine page size for slide %1").arg(pageIndex + 1);
            }
            return false;
        }

        const QPageSize pageSize(pageSizePoints, QPageSize::Point);
        writer.setPageSize(pageSize);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        if (pageIndex > 0 && !writer.newPage()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not create PDF page %1").arg(pageIndex + 1);
            }
            return false;
        }

        if (!painter.isActive() && !painter.begin(&writer)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not start PDF export");
            }
            return false;
        }

        const QSize targetPixelSize(
            qMax(1, int(qRound(pageSizePoints.width() * 2.0))),
            qMax(1, int(qRound(pageSizePoints.height() * 2.0))));
        QImage pageImage = imageWithMediaFrames(pageIndex, m_backend->renderPage(pageIndex, targetPixelSize));
        if (pageImage.isNull()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not render slide %1").arg(pageIndex + 1);
            }
            return false;
        }

        const QImage overlay = overlayImages.value(pageIndex);
        if (!overlay.isNull()) {
            QPainter imagePainter(&pageImage);
            imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            imagePainter.drawImage(pageImage.rect(), overlay);
        }

        painter.drawImage(QRectF(QPointF(0, 0), pageSizePoints), pageImage);
    }

    painter.end();
    return true;
}

bool AppController::saveUilPackage(
    const QString& path,
    const QHash<int, QImage>& overlayImages,
    const QSet<int>& hiddenOverlayPages,
    bool overlaysGloballyVisible,
    QString* errorMessage) {
    if (!hasDocument()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No presentation is open");
        }
        return false;
    }

    const QString entryPdf = m_entryPdfRelativePath.isEmpty()
        ? QStringLiteral("build/presentation.pdf")
        : m_entryPdfRelativePath;
    QHash<int, QImage> packageOverlayImages = overlayImages;
    if (m_audienceWindow) {
        const QHash<int, QImage> audienceOverlays = m_audienceWindow->annotationOverlaysByPage();
        for (auto it = audienceOverlays.constBegin(); it != audienceOverlays.constEnd(); ++it) {
            if (!it.value().isNull()) {
                packageOverlayImages.insert(it.key(), it.value());
            }
        }
    }

    if (!writeUilPackage(
            path,
            m_currentPath,
            entryPdf,
            m_packageRootPath,
            m_packageMovieAssetPaths,
            packageOverlayImages,
            hiddenOverlayPages,
            overlaysGloballyVisible,
            errorMessage)) {
        return false;
    }

    m_currentPackagePath = QFileInfo(path).absoluteFilePath();
    m_loadedOverlayImages = packageOverlayImages;
    m_loadedHiddenOverlayPages = hiddenOverlayPages;
    m_loadedOverlaysGloballyVisible = overlaysGloballyVisible;
    return true;
}

void AppController::clearAnnotationOverlayForPage(int pageIndex) {
    if (m_audienceWindow) {
        m_audienceWindow->clearAnnotationOverlayForPage(pageIndex);
    }
    m_loadedOverlayImages.remove(pageIndex);
}

void AppController::clearAllAnnotationOverlays() {
    if (m_audienceWindow) {
        m_audienceWindow->clearAllAnnotationOverlays();
    }
    m_loadedOverlayImages.clear();
}

QString AppController::currentPackagePath() const {
    return m_currentPackagePath;
}

QHash<int, QImage> AppController::loadedOverlayImages() const {
    return m_loadedOverlayImages;
}

QSet<int> AppController::loadedHiddenOverlayPages() const {
    return m_loadedHiddenOverlayPages;
}

bool AppController::loadedOverlaysGloballyVisible() const {
    return m_loadedOverlaysGloballyVisible;
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
        if (m_deckOverviewRenderSize.isValid()) {
            requestDeckOverviewRenders(m_deckOverviewRenderSize, m_currentPageIndex);
        }
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
        if (m_deckOverviewRenderSize.isValid()) {
            requestDeckOverviewRenders(m_deckOverviewRenderSize, m_currentPageIndex);
        }
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

void AppController::toggleBlackScreen() {
    if (m_audienceWindow) {
        m_audienceWindow->toggleBlackScreen();
    }
}

void AppController::toggleWhiteScreen() {
    if (m_audienceWindow) {
        m_audienceWindow->toggleWhiteScreen();
    }
}

void AppController::closeAudienceWindow() {
    stopMediaPlayback();
    if (m_audienceWindow) {
        m_audienceWindow->close();
    }
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
    return cacheKeyForPageAtSize(pageIndex, audienceRenderPixelSize(pageIndex));
}

SlideCacheKey AppController::cacheKeyForPageAtSize(int pageIndex, const QSize& boundingPixelSize) const {
    if (!hasDocument() || !boundingPixelSize.isValid()) {
        return {};
    }

    return SlideCacheKey{
        m_documentHash,
        pageIndex,
        containedSizeForAspect(m_backend->pageSizePoints(pageIndex), boundingPixelSize),
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
    return renderRequestForPageAtSize(pageIndex, audienceRenderPixelSize(pageIndex));
}

RenderRequest AppController::renderRequestForPageAtSize(int pageIndex, const QSize& boundingPixelSize) const {
    const SlideCacheKey sizedKey = cacheKeyForPageAtSize(pageIndex, boundingPixelSize);
    return RenderRequest{
        m_currentPath,
        sizedKey.documentHash,
        sizedKey.pageIndex,
        sizedKey.pixelSize,
        sizedKey.rotation,
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
        emit currentAnnotationOverlayChanged({});
        if (m_audienceWindow) {
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
    requestPageRenderAtSize(pageIndex, audienceRenderPixelSize(pageIndex), priority);
}

void AppController::requestPageRenderAtSize(int pageIndex, const QSize& boundingPixelSize, int priority) {
    if (!hasDocument() || pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }

    const SlideCacheKey key = cacheKeyForPageAtSize(pageIndex, boundingPixelSize);
    if (m_slideCache.contains(key)) {
        return;
    }

    m_renderScheduler.requestRender(renderRequestForPageAtSize(pageIndex, boundingPixelSize), priority);
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

    auto buffer = std::make_unique<VideoFrameBuffer>();
    connect(buffer.get(), &VideoFrameBuffer::frameAvailable, this, &AppController::handleBufferedVideoFrameAvailable);
    connect(buffer.get(), &VideoFrameBuffer::finished, this, &AppController::handleVideoDecodeFinished);
    connect(buffer.get(), &VideoFrameBuffer::failed, this, &AppController::handleVideoDecodeFailed);
    m_videoBuffer = std::move(buffer);
    m_activeVideoRect = normalizedMediaRect(*annotation);
    m_lastVideoPtsMs = -1;
    m_waitingForVideoFrame = true;
    m_videoPlaying = true;
    emit statusMessageChanged(QStringLiteral("Playing media: %1").arg(annotation->fileName));
    m_videoBuffer->start(annotation->resolvedFilePath, 3000);
    advanceVideoFrame();
}

void AppController::stopMediaPlayback() {
    m_videoTimer.stop();
    if (m_videoBuffer) {
        m_videoBuffer->stop();
        m_videoBuffer.reset();
    }
    m_videoPlaying = false;
    m_waitingForVideoFrame = false;
    m_activeVideoRect = {};
    m_lastVideoPtsMs = -1;
    if (m_audienceWindow) {
        m_audienceWindow->clearVideoOverlay();
    }
}

void AppController::advanceVideoFrame() {
    if (!m_videoBuffer || !m_videoPlaying) {
        stopMediaPlayback();
        return;
    }

    std::optional<DecodedVideoFrame> frame = m_videoBuffer->takeFrame();
    if (!frame) {
        if (m_videoBuffer->isFinished()) {
            const QString errorMessage = m_videoBuffer->errorMessage();
            stopMediaPlayback();
            if (!errorMessage.isEmpty()) {
                emit statusMessageChanged(QStringLiteral("Media stopped: %1").arg(errorMessage));
            } else {
                emit statusMessageChanged(QStringLiteral("Media finished"));
            }
            return;
        }
        if (!m_waitingForVideoFrame) {
            emit statusMessageChanged(QStringLiteral("Buffering media..."));
        }
        m_waitingForVideoFrame = true;
        m_videoTimer.start(30);
        return;
    }

    m_waitingForVideoFrame = false;
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

void AppController::handleBufferedVideoFrameAvailable() {
    if (m_videoPlaying && m_waitingForVideoFrame && !m_videoTimer.isActive()) {
        advanceVideoFrame();
    }
}

void AppController::handleVideoDecodeFinished() {
    if (m_videoPlaying && m_waitingForVideoFrame && (!m_videoBuffer || !m_videoBuffer->hasFrames())) {
        stopMediaPlayback();
        emit statusMessageChanged(QStringLiteral("Media finished"));
    }
}

void AppController::handleVideoDecodeFailed(const QString& errorMessage) {
    if (!m_videoPlaying) {
        return;
    }

    if (m_videoBuffer && m_videoBuffer->hasFrames()) {
        return;
    }

    stopMediaPlayback();
    emit statusMessageChanged(QStringLiteral("Media stopped: %1").arg(errorMessage));
}

void AppController::handleAudienceRenderTargetChanged() {
    if (!hasDocument()) {
        return;
    }

    if (m_audienceWindow) {
        const QHash<int, QImage> overlaysByPage = m_audienceWindow->annotationOverlaysByPage();
        QHash<QString, QImage> overlaysByTextureKey;
        for (auto it = overlaysByPage.constBegin(); it != overlaysByPage.constEnd(); ++it) {
            if (it.key() >= 0 && it.key() < pageCount() && !it.value().isNull()) {
                overlaysByTextureKey.insert(textureKeyForCacheKey(cacheKeyForPage(it.key())), it.value());
            }
        }
        m_audienceWindow->setAnnotationOverlaysByTextureKey(overlaysByTextureKey);
    }

    updateVisibleSlides();
    schedulePredictiveRenders();
    if (m_deckOverviewRenderSize.isValid()) {
        requestDeckOverviewRenders(m_deckOverviewRenderSize, m_currentPageIndex);
    }
}

void AppController::handleRenderStarted(const RenderRequest& request) {
    Q_UNUSED(request);
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
    const bool isAudienceRender = key.pixelSize == cacheKeyForPage(request.pageIndex).pixelSize;
    if (m_audienceWindow && isAudienceRender) {
        m_audienceWindow->cacheSlideImage(textureKey, displayImage);
    }

    Q_UNUSED(elapsedMs);

    const SlideCacheKey currentKey = cacheKeyForPage(m_currentPageIndex);
    if (request.pageIndex == m_currentPageIndex && isAudienceRender) {
        emit currentSlideImageChanged(displayImage);
        if (m_audienceWindow) {
            m_audienceWindow->setSlideImage(textureKey, displayImage);
        }
        emit statusMessageChanged(QStringLiteral("Rendered page %1 in %2 ms").arg(request.pageIndex + 1).arg(elapsedMs));
    }

    if (request.pageIndex == m_currentPageIndex + 1 && isAudienceRender) {
        emit nextSlideImageChanged(displayImage);
    }

    if (m_deckOverviewRenderSize.isValid()
        && key.pixelSize == cacheKeyForPageAtSize(request.pageIndex, m_deckOverviewRenderSize).pixelSize) {
        emit deckSlideImageChanged(request.pageIndex, m_deckOverviewRenderSize, displayImage);
    }
}
