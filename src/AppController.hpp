#pragma once

#include "cache/SlideCache.hpp"
#include "pdf/PdfBackend.hpp"
#include "media/VideoFrameBuffer.hpp"
#include "media/VideoFrameExtractor.hpp"
#include "render/RenderScheduler.hpp"
#include "util/PdfMediaDetector.hpp"

#include <QObject>
#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QString>
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

signals:
    void documentChanged(int pageCount);
    void pageChanged(int pageIndex, int pageCount);
    void currentSlideImageChanged(const QImage& image);
    void nextSlideImageChanged(const QImage& image);
    void statusMessageChanged(const QString& message);
    void audienceScreenChanged(QScreen* screen);
    void screenListChanged();
    void mediaScanChanged(const PdfMediaScanResult& result);

private:
    QSize audienceRenderPixelSize(int pageIndex) const;
    SlideCacheKey cacheKeyForPage(int pageIndex) const;
    QString textureKeyForCacheKey(const SlideCacheKey& key) const;
    RenderRequest renderRequestForPage(int pageIndex) const;
    void updateVisibleSlides();
    void schedulePredictiveRenders();
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
    QPointer<AudienceWindow> m_audienceWindow;
    QPointer<QScreen> m_audienceScreen;
    QString m_currentPath;
    QString m_documentHash;
    PdfMediaScanResult m_mediaScanResult;
    QRectF m_activeVideoRect;
    qint64 m_lastVideoPtsMs = -1;
    bool m_waitingForVideoFrame = false;
    bool m_videoPlaying = false;
    int m_currentPageIndex = 0;
    int m_renderGeneration = 0;
};
