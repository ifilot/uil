#pragma once

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QStringList>

#include "util/PdfMediaDetector.hpp"

class AppController;
class QComboBox;
class QMenu;

class SlidePreview final : public QLabel {
    Q_OBJECT

public:
    explicit SlidePreview(QWidget* parent = nullptr);
    void setPreviewImage(const QImage& image);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
};

class PresenterWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit PresenterWindow(AppController* controller, QWidget* parent = nullptr);

private slots:
    void openPdf();
    void startPresentationMode();
    void updateMediaLabel(const PdfMediaScanResult& result);
    void updatePageLabel(int pageIndex, int pageCount);
    void updateScreenList();
    void updateAudienceScreenSelection(QScreen* screen);

private:
    void createActions();
    void createLayout();
    void createConnections();
    void selectScreenFromCombo(int index);
    bool openPdfPath(const QString& path);
    QStringList recentPdfPaths() const;
    void saveRecentPdfPaths(const QStringList& paths);
    void addRecentPdfPath(const QString& path);
    void removeRecentPdfPath(const QString& path);
    void rebuildOpenRecentMenu();

    AppController* m_controller = nullptr;
    SlidePreview* m_currentPreview = nullptr;
    SlidePreview* m_nextPreview = nullptr;
    QLabel* m_pageLabel = nullptr;
    QLabel* m_timerLabel = nullptr;
    QLabel* m_mediaLabel = nullptr;
    QComboBox* m_screenCombo = nullptr;
    QAction* m_openAction = nullptr;
    QMenu* m_openRecentMenu = nullptr;
    QAction* m_nextAction = nullptr;
    QAction* m_previousAction = nullptr;
    QAction* m_firstAction = nullptr;
    QAction* m_lastAction = nullptr;
    QAction* m_startPresentationAction = nullptr;
    QAction* m_playPauseMediaAction = nullptr;
    QAction* m_fullscreenAction = nullptr;
};
