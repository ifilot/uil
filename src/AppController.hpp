#pragma once

#include "cache/SlideCache.hpp"
#include "pdf/PdfBackend.hpp"
#include "media/VideoFrameBuffer.hpp"
#include "media/VideoFrameExtractor.hpp"
#include "render/RenderScheduler.hpp"
#include "util/PdfMediaDetector.hpp"

#include <QObject>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

class AudienceWindow;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);

    void setAudienceWindow(AudienceWindow* audienceWindow);

    bool openPdf(const QString& path);
    void nextPage();
    void previousPage();
    void goToPage(int pageIndex);
    int currentPage() const;
    int pageCount() const;
    void requestDeckOverviewRenders(const QSize& boundingPixelSize, int focusedPageIndex = -1);

    QString currentPath() const;
    QScreen* selectedAudienceScreen() const;
    void setAudienceScreen(QScreen* screen);
    void refreshScreens();
    void enterAudienceFullscreen();
    void toggleAudienceFullscreen();
    void toggleBlackScreen();
    void toggleWhiteScreen();
    void toggleMediaPlayback();
    void closeAudienceWindow();
    bool saveUilPackage(const QString& path, const QHash<int, QImage>& overlayImages, const QSet<int>& hiddenOverlayPages, bool overlaysGloballyVisible, QString* errorMessage = nullptr);
    bool exportAnnotatedPdf(const QString& path, const QHash<int, QImage>& overlayImages, QString* errorMessage = nullptr);
    void clearAnnotationOverlayForPage(int pageIndex);
    void clearAllAnnotationOverlays();
    QString currentPackagePath() const;
    QHash<int, QImage> loadedOverlayImages() const;
    QSet<int> loadedHiddenOverlayPages() const;
    bool loadedOverlaysGloballyVisible() const;

signals:
    void documentChanged(int pageCount);
    void pageChanged(int pageIndex, int pageCount);
    void currentSlideImageChanged(const QImage& image);
    void currentAnnotationOverlayChanged(const QImage& image);
    void nextSlideImageChanged(const QImage& image);
    void deckSlideImageChanged(int pageIndex, const QSize& boundingPixelSize, const QImage& image);
    void statusMessageChanged(const QString& message);
    void audienceScreenChanged(QScreen* screen);
    void screenListChanged();
    void mediaScanChanged(const PdfMediaScanResult& result);

private:
    QSize audienceRenderPixelSize(int pageIndex) const;
    SlideCacheKey cacheKeyForPageAtSize(int pageIndex, const QSize& boundingPixelSize) const;
    SlideCacheKey cacheKeyForPage(int pageIndex) const;
    QString textureKeyForCacheKey(const SlideCacheKey& key) const;
    RenderRequest renderRequestForPageAtSize(int pageIndex, const QSize& boundingPixelSize) const;
    RenderRequest renderRequestForPage(int pageIndex) const;
    void updateVisibleSlides();
    void schedulePredictiveRenders();
    void requestPageRenderAtSize(int pageIndex, const QSize& boundingPixelSize, int priority);
    void requestPageRender(int pageIndex, int priority);
    QImage imageWithMediaFrames(int pageIndex, const QImage& image) const;
    const PdfMediaAnnotation* currentPlayableMediaAnnotation() const;
    QRectF normalizedMediaRect(const PdfMediaAnnotation& annotation) const;
    void startMediaPlayback();
    void stopMediaPlayback();
    void advanceVideoFrame();
    void handleBufferedVideoFrameAvailable();
    void handleVideoDecodeFinished();
    void handleVideoDecodeFailed(const QString& errorMessage);
    void handleAudienceRenderTargetChanged();
    void handleRenderStarted(const RenderRequest& request);
    void handleRenderFinished(const RenderRequest& request, const QImage& image, qint64 elapsedMs, const QString& errorMessage);
    bool hasDocument() const;

    std::unique_ptr<PdfBackend> m_backend;
    SlideCache m_slideCache;
    RenderScheduler m_renderScheduler;
    QTimer m_videoTimer;
    std::unique_ptr<VideoFrameBuffer> m_videoBuffer;
    std::unique_ptr<QTemporaryDir> m_packageTempDir;
    QPointer<AudienceWindow> m_audienceWindow;
    QPointer<QScreen> m_audienceScreen;
    QString m_currentPath;
    QString m_currentPackagePath;
    QString m_packageRootPath;
    QString m_entryPdfRelativePath;
    QStringList m_packageMovieAssetPaths;
    QString m_documentHash;
    PdfMediaScanResult m_mediaScanResult;
    QRectF m_activeVideoRect;
    QSize m_deckOverviewRenderSize;
    qint64 m_lastVideoPtsMs = -1;
    bool m_waitingForVideoFrame = false;
    bool m_videoPlaying = false;
    bool m_loadedOverlaysGloballyVisible = true;
    int m_currentPageIndex = 0;
    int m_renderGeneration = 0;
    QHash<int, QImage> m_loadedOverlayImages;
    QSet<int> m_loadedHiddenOverlayPages;
};
