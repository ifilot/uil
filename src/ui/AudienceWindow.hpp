#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <vector>

class QCloseEvent;
class QContextMenuEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

class AudienceWindow : public QWidget {
    Q_OBJECT

public:
    explicit AudienceWindow();
    ~AudienceWindow() override;

    void setSlideImage(const QString& textureKey, const QImage& image);
    void clearSlideImage();
    void cacheSlideImage(const QString& textureKey, const QImage& image);
    void setDocumentOverview(int pageCount, int currentPage);
    void setDeckOverviewSlideImage(int pageIndex, const QSize& boundingPixelSize, const QImage& image);
    void setVideoFrame(const QImage& image, QRectF slideRect);
    void clearVideoOverlay();
    void setAudienceScreen(QScreen* screen);
    void enterFullscreen();
    void toggleFullscreen();
    void exitFullscreen();
    void toggleBlackScreen();
    void toggleWhiteScreen();
    void clearBlankScreen();
    void setCursorTool();
    void setPointerTool();
    void setPenTool();
    void setEraserTool();
    void setPointerColor(const QColor& color);
    void setAnnotationColor(const QColor& color);
    void setAnnotationThickness(int thickness);
    void setEraserThickness(int thickness);
    void clearAnnotations();
    void clearAnnotationOverlayForPage(int pageIndex);
    void clearAllAnnotationOverlays();
    void setAnnotationOverlaysByTextureKey(const QHash<QString, QImage>& overlays);
    QHash<int, QImage> annotationOverlaysByPage() const;
    QImage currentAnnotatedSlideImage() const;
    QImage currentAnnotationOverlayImage() const;
    QSize renderLogicalSize() const;
    qreal renderDevicePixelRatio() const;

signals:
    void nextRequested();
    void previousRequested();
    void firstRequested();
    void lastRequested();
    void pageRequested(int pageIndex);
    void deckOverviewRendersRequested(const QSize& boundingPixelSize, int focusedPageIndex);
    void playPauseRequested();
    void renderTargetChanged();
    void annotationOverlayChanged(const QImage& image);
    void presentationClosed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    class FeatureMenuPanel;

    enum class InteractionTool {
        Cursor,
        Pointer,
        Pen,
        Eraser
    };

    enum class BlankMode {
        None,
        Black,
        White
    };

    struct CachedSlide {
        QString key;
        QImage image;
    };

    void evictOldSlides();
    void applyScreenGeometry(bool fullscreen);
    void showCursorTemporarily();
    void hideCursor();
    void updateCursorAppearance();
    QRectF slideLogicalRect(QSize textureSize) const;
    QPointF slideImagePoint(QPointF windowPoint, QSize textureSize, bool* inside) const;
    QImage& annotationImageForCurrentSlide(QSize size);
    const QImage* currentAnnotationImage() const;
    void drawAnnotationSegment(QPointF fromWindowPoint, QPointF toWindowPoint);
    void drawPointer(QPainter& painter) const;
    void drawEraserCursor(QPainter& painter) const;
    qreal eraserLogicalDiameter() const;
    void showFeatureMenu(const QPoint& globalPosition);
    void enterDeckOverview();
    void exitDeckOverview();
    void drawDeckOverview(QPainter& painter);
    QSize deckOverviewThumbnailBoundingPixelSize() const;
    QRect deckOverviewViewportRect() const;
    int deckOverviewContentHeight() const;
    int deckOverviewMaxScrollY() const;
    int deckOverviewPageAt(const QPoint& position) const;
    void scrollDeckOverviewBy(int deltaY);
    void saveAnnotatedSlideImage();

    QString m_currentTextureKey;
    QImage m_currentSlideImage;
    std::vector<CachedSlide> m_slideCache;
    QImage m_videoFrame;
    QRectF m_videoRect;
    QHash<int, QImage> m_deckOverviewImages;
    QSize m_deckOverviewImageSize;
    QHash<QString, QImage> m_annotationImages;
    QColor m_pointerColor = QColor(255, 36, 36);
    QColor m_annotationColor = QColor(0xe3, 0x1a, 0x1c);
    QPointF m_lastAnnotationPoint;
    QPointF m_pointerPosition;
    QPointF m_eraserCursorPosition;
    InteractionTool m_interactionTool = InteractionTool::Cursor;
    bool m_isAnnotating = false;
    bool m_pointerVisible = false;
    bool m_eraserCursorVisible = false;
    bool m_hasVideoOverlay = false;
    bool m_deckOverviewVisible = false;
    QPointer<QScreen> m_screen;
    QTimer m_cursorHideTimer;
    BlankMode m_blankMode = BlankMode::None;
    bool m_isFullscreen = false;
    int m_annotationThickness = 6;
    int m_eraserThickness = 24;
    int m_pageCount = 0;
    int m_currentPageIndex = -1;
    int m_deckOverviewScrollY = 0;
    QPointer<QWidget> m_featureMenu;
};
