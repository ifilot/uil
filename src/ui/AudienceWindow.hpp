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
class QMenu;
class QMouseEvent;
class QPaintEvent;

class AudienceWindow : public QWidget {
    Q_OBJECT

public:
    explicit AudienceWindow();
    ~AudienceWindow() override;

    void setSlideImage(const QString& textureKey, const QImage& image);
    void clearSlideImage();
    void cacheSlideImage(const QString& textureKey, const QImage& image);
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
    void clearAnnotations();
    QImage currentAnnotatedSlideImage() const;
    QImage currentAnnotationOverlayImage() const;
    QSize renderLogicalSize() const;
    qreal renderDevicePixelRatio() const;

signals:
    void nextRequested();
    void previousRequested();
    void firstRequested();
    void lastRequested();
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

private:
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
    void showFeatureMenu(const QPoint& globalPosition);
    void saveAnnotatedSlideImage();

    QString m_currentTextureKey;
    QImage m_currentSlideImage;
    std::vector<CachedSlide> m_slideCache;
    QImage m_videoFrame;
    QRectF m_videoRect;
    QHash<QString, QImage> m_annotationImages;
    QColor m_pointerColor = QColor(255, 36, 36);
    QColor m_annotationColor = QColor(255, 36, 36);
    QPointF m_lastAnnotationPoint;
    QPointF m_pointerPosition;
    InteractionTool m_interactionTool = InteractionTool::Cursor;
    bool m_isAnnotating = false;
    bool m_pointerVisible = false;
    bool m_hasVideoOverlay = false;
    QPointer<QScreen> m_screen;
    QTimer m_cursorHideTimer;
    BlankMode m_blankMode = BlankMode::None;
    bool m_isFullscreen = false;
    int m_annotationThickness = 6;
    QPointer<QMenu> m_featureMenu;
};
