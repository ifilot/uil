#pragma once

#include "cache/SlideCache.hpp"
#include "pdf/PdfBackend.hpp"
#include "render/RenderScheduler.hpp"
#include "util/PdfMediaDetector.hpp"

#include <QObject>
#include <QImage>
#include <QPointer>
#include <QScreen>
#include <QString>

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
    void handleAudienceRenderTargetChanged();
    void handleRenderStarted(const RenderRequest& request);
    void handleRenderFinished(const RenderRequest& request, const QImage& image, qint64 elapsedMs, const QString& errorMessage);
    bool hasDocument() const;

    std::unique_ptr<PdfBackend> m_backend;
    SlideCache m_slideCache;
    RenderScheduler m_renderScheduler;
    QPointer<AudienceWindow> m_audienceWindow;
    QPointer<QScreen> m_audienceScreen;
    QString m_currentPath;
    QString m_documentHash;
    PdfMediaScanResult m_mediaScanResult;
    int m_currentPageIndex = 0;
    int m_renderGeneration = 0;
};
