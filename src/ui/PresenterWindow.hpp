#pragma once

#include <QImage>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QStringList>
#include <QTimer>

#include "util/PdfMediaDetector.hpp"

class AppController;
class QCloseEvent;
class QComboBox;
class QEvent;
class QMenu;
class QMenuBar;
class QToolButton;
class QWidget;
class SlideDeckOverview;

class SlidePreview final : public QLabel {
    Q_OBJECT

public:
    explicit SlidePreview(QWidget* parent = nullptr);
    void setPreviewImage(const QImage& image);
    void setOverlayImage(const QImage& image);
    void setOverlayVisible(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
    QImage m_overlayImage;
    bool m_overlayVisible = true;
};

class PresenterWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit PresenterWindow(AppController* controller, QWidget* parent = nullptr);
    ~PresenterWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openPdf();
    void jumpToPage();
    void showSlideOverview();
    void showAbout();
    void startPresentationMode();
    void resetPresentationTimer();
    void updateTimerLabel();
    void updateMediaLabel(const PdfMediaScanResult& result);
    void updateDocumentOverview(int pageCount);
    void updatePageLabel(int pageIndex, int pageCount);
    void updateScreenList();
    void updateAudienceScreenSelection(QScreen* screen);

private:
    void createActions();
    QWidget* createTitleBar();
    void createLayout();
    void createConnections();
    bool isTitleDragAreaAt(const QPoint& globalPosition) const;
    Qt::Edges resizeEdgesAt(const QPoint& position) const;
    void updateResizeCursor(Qt::Edges edges);
    void clearResizeCursor();
    void beginManualResize(Qt::Edges edges, const QPoint& globalPosition);
    void updateManualResize(const QPoint& globalPosition);
    void finishManualResize();
    void beginManualMove(const QPoint& globalPosition);
    void updateManualMove(const QPoint& globalPosition);
    void finishManualMove();
    void toggleMaximized();
    void updateMaximizeButton();
    void selectScreenFromCombo(int index);
    bool openPdfPath(const QString& path);
    QStringList recentPdfPaths() const;
    void saveRecentPdfPaths(const QStringList& paths);
    void addRecentPdfPath(const QString& path);
    void removeRecentPdfPath(const QString& path);
    void rebuildOpenRecentMenu();
    void loadSettings();
    void saveSettings();

    AppController* m_controller = nullptr;
    SlidePreview* m_currentPreview = nullptr;
    SlideDeckOverview* m_deckOverview = nullptr;
    QLabel* m_pageLabel = nullptr;
    QLabel* m_timerLabel = nullptr;
    QLabel* m_mediaLabel = nullptr;
    QComboBox* m_screenCombo = nullptr;
    QMenuBar* m_menuBar = nullptr;
    QWidget* m_titleBar = nullptr;
    QToolButton* m_minimizeButton = nullptr;
    QToolButton* m_maximizeButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QAction* m_openAction = nullptr;
    QMenu* m_openRecentMenu = nullptr;
    QAction* m_nextAction = nullptr;
    QAction* m_previousAction = nullptr;
    QAction* m_firstAction = nullptr;
    QAction* m_lastAction = nullptr;
    QAction* m_startPresentationAction = nullptr;
    QAction* m_playPauseMediaAction = nullptr;
    QAction* m_jumpToPageAction = nullptr;
    QAction* m_slideOverviewAction = nullptr;
    QAction* m_resetTimerAction = nullptr;
    QAction* m_blackScreenAction = nullptr;
    QAction* m_whiteScreenAction = nullptr;
    QAction* m_fullscreenAction = nullptr;
    QAction* m_showAudienceOverlayAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QElapsedTimer m_elapsedTimer;
    QTimer m_timerUpdateTimer;
    Qt::Edges m_resizeEdges;
    QRect m_resizeStartGeometry;
    QPoint m_resizeStartGlobalPosition;
    QPoint m_moveOffset;
    bool m_timerRunning = false;
    bool m_manualResizeActive = false;
    bool m_manualMoveActive = false;
    bool m_resizeCursorActive = false;
};
