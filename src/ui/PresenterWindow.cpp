#include "PresenterWindow.hpp"

#include "AppController.hpp"
#include "util/ImageUtil.hpp"

#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGlobal>

namespace {
constexpr int maxRecentPdfPaths = 5;
constexpr auto recentPdfPathsKey = "recentPdfPaths";
constexpr auto lastOpenDirectoryKey = "lastOpenDirectory";
constexpr auto windowGeometryKey = "presenterWindowGeometry";
constexpr auto windowStateKey = "presenterWindowState";
constexpr auto audienceScreenNameKey = "audienceScreenName";

QString menuSafePathText(QString path) {
    return path.replace(QStringLiteral("&"), QStringLiteral("&&"));
}
}

SlidePreview::SlidePreview(QWidget* parent)
    : QLabel(parent) {
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
}

void SlidePreview::setPreviewImage(const QImage& image) {
    m_image = image;
    update();
}

void SlidePreview::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x11, 0x11, 0x11));

    if (m_image.isNull()) {
        painter.setPen(QColor(0x85, 0x85, 0x85));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No slide"));
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect target = centeredRectForImage(m_image.size(), rect().adjusted(8, 8, -8, -8));
    painter.drawImage(target, m_image);
}

PresenterWindow::PresenterWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent),
      m_controller(controller) {
    setWindowTitle(QStringLiteral("uil Presenter"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/uil.svg")));
    createActions();
    createLayout();
    createConnections();
    updateScreenList();
    m_timerUpdateTimer.setInterval(1000);
    connect(&m_timerUpdateTimer, &QTimer::timeout, this, &PresenterWindow::updateTimerLabel);
    loadSettings();
    if (size().isEmpty()) {
        resize(1100, 650);
    }
}

void PresenterWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    m_controller->closeAudienceWindow();
    QMainWindow::closeEvent(event);
}

void PresenterWindow::openPdf() {
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open PDF"),
        settings.value(QString::fromLatin1(lastOpenDirectoryKey)).toString(),
        QStringLiteral("PDF files (*.pdf);;All files (*.*)"));

    if (path.isEmpty()) {
        return;
    }

    openPdfPath(path);
}

void PresenterWindow::jumpToPage() {
    const int pageCount = m_controller->pageCount();
    if (pageCount <= 0) {
        return;
    }

    bool ok = false;
    const int page = QInputDialog::getInt(
        this,
        QStringLiteral("Jump to Page"),
        QStringLiteral("Page:"),
        m_controller->currentPage() + 1,
        1,
        pageCount,
        1,
        &ok);
    if (ok) {
        m_controller->goToPage(page - 1);
    }
}

void PresenterWindow::showSlideOverview() {
    const int pageCount = m_controller->pageCount();
    if (pageCount <= 0) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Slide Overview"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    for (int i = 0; i < pageCount; ++i) {
        auto* item = new QListWidgetItem(QStringLiteral("Page %1").arg(i + 1), list);
        item->setData(Qt::UserRole, i);
    }
    list->setCurrentRow(m_controller->currentPage());
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
        m_controller->goToPage(list->currentItem()->data(Qt::UserRole).toInt());
    }
}

void PresenterWindow::showAbout() {
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(QStringLiteral("About uil"));
    aboutBox.setWindowIcon(QIcon(QStringLiteral(":/icons/uil.svg")));
    aboutBox.setIconPixmap(QIcon(QStringLiteral(":/icons/uil.svg")).pixmap(QSize(96, 96)));
    aboutBox.setText(QStringLiteral("<h2>uil %1</h2>").arg(QStringLiteral(UIL_VERSION_DISPLAY)));
    aboutBox.setInformativeText(QStringLiteral(
        "A Windows-focused Qt PDF presentation app for Beamer-style slide decks.\n\n"
        "Built with Qt %1.").arg(QString::fromLatin1(qVersion())));
    aboutBox.setStandardButtons(QMessageBox::Ok);
    aboutBox.exec();
}

void PresenterWindow::startPresentationMode() {
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (primaryScreen) {
        if (windowHandle()) {
            windowHandle()->setScreen(primaryScreen);
        }

        const QRect availableGeometry = primaryScreen->availableGeometry();
        const QSize targetSize = size().boundedTo(availableGeometry.size());
        const QPoint centeredTopLeft(
            availableGeometry.x() + (availableGeometry.width() - targetSize.width()) / 2,
            availableGeometry.y() + (availableGeometry.height() - targetSize.height()) / 2);

        showNormal();
        setGeometry(QRect(centeredTopLeft, targetSize));
    }

    show();
    raise();
    activateWindow();
    m_controller->enterAudienceFullscreen();
}

void PresenterWindow::resetPresentationTimer() {
    m_elapsedTimer.restart();
    m_timerRunning = true;
    m_timerUpdateTimer.start();
    updateTimerLabel();
}

void PresenterWindow::updateTimerLabel() {
    if (!m_timerRunning) {
        m_timerLabel->setText(QStringLiteral("Timer: 00:00:00"));
        return;
    }

    const qint64 totalSeconds = m_elapsedTimer.elapsed() / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    m_timerLabel->setText(QStringLiteral("Timer: %1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0')));
}

void PresenterWindow::updateMediaLabel(const PdfMediaScanResult& result) {
    if (!result.hasMedia()) {
        m_mediaLabel->setText(QStringLiteral("Media: none"));
        m_mediaLabel->setToolTip(QString());
        return;
    }

    m_mediaLabel->setText(QStringLiteral("Media: %1 item(s)").arg(result.annotations.size()));
    m_mediaLabel->setToolTip(result.summary());
    statusBar()->showMessage(result.summary());
}

void PresenterWindow::updatePageLabel(int pageIndex, int pageCount) {
    if (pageCount <= 0) {
        m_pageLabel->setText(QStringLiteral("Page: - / -"));
        return;
    }

    m_pageLabel->setText(QStringLiteral("Page: %1 / %2").arg(pageIndex + 1).arg(pageCount));
}

void PresenterWindow::updateScreenList() {
    QSignalBlocker blocker(m_screenCombo);
    m_screenCombo->clear();

    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        const QString label = QStringLiteral("%1 (%2x%3)")
            .arg(screen->name())
            .arg(screen->geometry().width())
            .arg(screen->geometry().height());
        m_screenCombo->addItem(label, QVariant::fromValue(screen));
    }

    updateAudienceScreenSelection(m_controller->selectedAudienceScreen());
}

void PresenterWindow::updateAudienceScreenSelection(QScreen* screen) {
    if (!screen) {
        return;
    }

    for (int i = 0; i < m_screenCombo->count(); ++i) {
        if (m_screenCombo->itemData(i).value<QScreen*>() == screen) {
            QSignalBlocker blocker(m_screenCombo);
            m_screenCombo->setCurrentIndex(i);
            return;
        }
    }
}

void PresenterWindow::createActions() {
    m_openAction = new QAction(QStringLiteral("&Open PDF"), this);
    m_openAction->setShortcut(QKeySequence::Open);

    m_nextAction = new QAction(QStringLiteral("Next"), this);
    m_nextAction->setShortcuts({
        QKeySequence(Qt::Key_Right),
        QKeySequence(Qt::Key_PageDown),
        QKeySequence(Qt::Key_Space)
    });

    m_previousAction = new QAction(QStringLiteral("Previous"), this);
    m_previousAction->setShortcuts({
        QKeySequence(Qt::Key_Left),
        QKeySequence(Qt::Key_PageUp),
        QKeySequence(Qt::Key_Backspace)
    });

    m_firstAction = new QAction(QStringLiteral("First"), this);
    m_firstAction->setShortcut(Qt::Key_Home);

    m_lastAction = new QAction(QStringLiteral("Last"), this);
    m_lastAction->setShortcut(Qt::Key_End);

    m_startPresentationAction = new QAction(QStringLiteral("Start Presentation"), this);
    m_startPresentationAction->setShortcut(Qt::Key_F5);

    m_playPauseMediaAction = new QAction(QStringLiteral("Play/Pause Media"), this);
    m_playPauseMediaAction->setShortcut(Qt::Key_Return);

    m_jumpToPageAction = new QAction(QStringLiteral("Jump to Page"), this);
    m_jumpToPageAction->setShortcut(Qt::Key_J);

    m_slideOverviewAction = new QAction(QStringLiteral("Slide Overview"), this);
    m_slideOverviewAction->setShortcut(Qt::Key_O);

    m_resetTimerAction = new QAction(QStringLiteral("Reset Timer"), this);
    m_resetTimerAction->setShortcut(Qt::Key_T);

    m_blackScreenAction = new QAction(QStringLiteral("Black Screen"), this);
    m_blackScreenAction->setShortcut(Qt::Key_B);

    m_whiteScreenAction = new QAction(QStringLiteral("White Screen"), this);
    m_whiteScreenAction->setShortcut(Qt::Key_W);

    m_fullscreenAction = new QAction(QStringLiteral("Toggle Audience Fullscreen"), this);
    m_fullscreenAction->setShortcut(Qt::Key_F11);

    m_aboutAction = new QAction(QIcon(QStringLiteral(":/icons/uil.svg")), QStringLiteral("About uil"), this);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_openAction);
    m_openRecentMenu = fileMenu->addMenu(QStringLiteral("Open &Recent"));
    rebuildOpenRecentMenu();

    QMenu* presentationMenu = menuBar()->addMenu(QStringLiteral("&Presentation"));
    presentationMenu->addAction(m_startPresentationAction);
    presentationMenu->addSeparator();
    presentationMenu->addAction(m_nextAction);
    presentationMenu->addAction(m_previousAction);
    presentationMenu->addSeparator();
    presentationMenu->addAction(m_firstAction);
    presentationMenu->addAction(m_lastAction);
    presentationMenu->addAction(m_jumpToPageAction);
    presentationMenu->addAction(m_slideOverviewAction);
    presentationMenu->addSeparator();
    presentationMenu->addAction(m_playPauseMediaAction);
    presentationMenu->addAction(m_blackScreenAction);
    presentationMenu->addAction(m_whiteScreenAction);
    presentationMenu->addAction(m_resetTimerAction);
    presentationMenu->addSeparator();
    presentationMenu->addAction(m_fullscreenAction);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(m_aboutAction);

    addAction(m_openAction);
    addAction(m_nextAction);
    addAction(m_previousAction);
    addAction(m_firstAction);
    addAction(m_lastAction);
    addAction(m_startPresentationAction);
    addAction(m_playPauseMediaAction);
    addAction(m_jumpToPageAction);
    addAction(m_slideOverviewAction);
    addAction(m_resetTimerAction);
    addAction(m_blackScreenAction);
    addAction(m_whiteScreenAction);
    addAction(m_fullscreenAction);
}

void PresenterWindow::createLayout() {
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("presenterRoot"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 16, 16, 10);
    rootLayout->setSpacing(12);

    auto createPreviewPane = [](const QString& title, SlidePreview* preview) {
        auto* pane = new QWidget;
        pane->setObjectName(QStringLiteral("previewPane"));
        auto* paneLayout = new QVBoxLayout(pane);
        paneLayout->setContentsMargins(0, 0, 0, 0);
        paneLayout->setSpacing(0);

        auto* heading = new QLabel(title, pane);
        heading->setObjectName(QStringLiteral("previewHeading"));
        paneLayout->addWidget(heading);
        paneLayout->addWidget(preview, 1);
        return pane;
    };

    auto* previewLayout = new QHBoxLayout;
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(12);
    m_currentPreview = new SlidePreview(central);
    m_nextPreview = new SlidePreview(central);
    previewLayout->addWidget(createPreviewPane(QStringLiteral("Current"), m_currentPreview), 1);
    previewLayout->addWidget(createPreviewPane(QStringLiteral("Next"), m_nextPreview), 1);

    auto* statusLayout = new QHBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);
    m_pageLabel = new QLabel(QStringLiteral("Page: - / -"), central);
    m_timerLabel = new QLabel(QStringLiteral("Timer: 00:00:00"), central);
    m_mediaLabel = new QLabel(QStringLiteral("Media: none"), central);
    auto* screenLabel = new QLabel(QStringLiteral("Audience:"), central);
    m_screenCombo = new QComboBox(central);
    m_screenCombo->setMinimumWidth(280);
    for (QLabel* label : {m_pageLabel, m_timerLabel, m_mediaLabel}) {
        label->setObjectName(QStringLiteral("statusPill"));
    }
    screenLabel->setObjectName(QStringLiteral("fieldLabel"));
    statusLayout->addWidget(m_pageLabel);
    statusLayout->addWidget(m_timerLabel);
    statusLayout->addWidget(m_mediaLabel);
    statusLayout->addStretch(1);
    statusLayout->addWidget(screenLabel);
    statusLayout->addWidget(m_screenCombo);

    rootLayout->addLayout(previewLayout, 1);
    rootLayout->addLayout(statusLayout);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Ready"));
}

void PresenterWindow::createConnections() {
    connect(m_openAction, &QAction::triggered, this, &PresenterWindow::openPdf);
    connect(m_nextAction, &QAction::triggered, m_controller, &AppController::nextPage);
    connect(m_previousAction, &QAction::triggered, m_controller, &AppController::previousPage);
    connect(m_firstAction, &QAction::triggered, this, [this] {
        m_controller->goToPage(0);
    });
    connect(m_lastAction, &QAction::triggered, this, [this] {
        m_controller->goToPage(m_controller->pageCount() - 1);
    });
    connect(m_startPresentationAction, &QAction::triggered, this, &PresenterWindow::startPresentationMode);
    connect(m_playPauseMediaAction, &QAction::triggered, m_controller, &AppController::toggleMediaPlayback);
    connect(m_jumpToPageAction, &QAction::triggered, this, &PresenterWindow::jumpToPage);
    connect(m_slideOverviewAction, &QAction::triggered, this, &PresenterWindow::showSlideOverview);
    connect(m_aboutAction, &QAction::triggered, this, &PresenterWindow::showAbout);
    connect(m_resetTimerAction, &QAction::triggered, this, &PresenterWindow::resetPresentationTimer);
    connect(m_blackScreenAction, &QAction::triggered, m_controller, &AppController::toggleBlackScreen);
    connect(m_whiteScreenAction, &QAction::triggered, m_controller, &AppController::toggleWhiteScreen);
    connect(m_fullscreenAction, &QAction::triggered, m_controller, &AppController::toggleAudienceFullscreen);
    connect(m_screenCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PresenterWindow::selectScreenFromCombo);

    connect(m_controller, &AppController::pageChanged, this, &PresenterWindow::updatePageLabel);
    connect(m_controller, &AppController::documentChanged, this, &PresenterWindow::resetPresentationTimer);
    connect(m_controller, &AppController::mediaScanChanged, this, &PresenterWindow::updateMediaLabel);
    connect(m_controller, &AppController::currentSlideImageChanged, m_currentPreview, &SlidePreview::setPreviewImage);
    connect(m_controller, &AppController::nextSlideImageChanged, m_nextPreview, &SlidePreview::setPreviewImage);
    connect(m_controller, &AppController::statusMessageChanged, this, [this](const QString& message) {
        statusBar()->showMessage(message);
    });
    connect(m_controller, &AppController::screenListChanged, this, &PresenterWindow::updateScreenList);
    connect(m_controller, &AppController::audienceScreenChanged, this, &PresenterWindow::updateAudienceScreenSelection);
}

void PresenterWindow::selectScreenFromCombo(int index) {
    if (index < 0) {
        return;
    }

    QScreen* screen = m_screenCombo->itemData(index).value<QScreen*>();
    m_controller->setAudienceScreen(screen);
}

bool PresenterWindow::openPdfPath(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (m_controller->openPdf(absolutePath)) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(lastOpenDirectoryKey), QFileInfo(absolutePath).absolutePath());
        addRecentPdfPath(absolutePath);
        return true;
    }

    removeRecentPdfPath(absolutePath);
    return false;
}

QStringList PresenterWindow::recentPdfPaths() const {
    QSettings settings;
    return settings.value(QString::fromLatin1(recentPdfPathsKey)).toStringList();
}

void PresenterWindow::saveRecentPdfPaths(const QStringList& paths) {
    QSettings settings;
    settings.setValue(QString::fromLatin1(recentPdfPathsKey), paths);
}

void PresenterWindow::addRecentPdfPath(const QString& path) {
    QStringList paths = recentPdfPaths();
    paths.removeAll(path);
    paths.prepend(path);

    while (paths.size() > maxRecentPdfPaths) {
        paths.removeLast();
    }

    saveRecentPdfPaths(paths);
    rebuildOpenRecentMenu();
}

void PresenterWindow::removeRecentPdfPath(const QString& path) {
    QStringList paths = recentPdfPaths();
    if (!paths.removeAll(path)) {
        return;
    }

    saveRecentPdfPaths(paths);
    rebuildOpenRecentMenu();
}

void PresenterWindow::rebuildOpenRecentMenu() {
    if (!m_openRecentMenu) {
        return;
    }

    m_openRecentMenu->clear();

    const QStringList paths = recentPdfPaths();
    m_openRecentMenu->setEnabled(!paths.isEmpty());
    for (const QString& path : paths) {
        QAction* action = m_openRecentMenu->addAction(menuSafePathText(path));
        action->setData(path);
        connect(action, &QAction::triggered, this, [this, action] {
            openPdfPath(action->data().toString());
        });
    }
}

void PresenterWindow::loadSettings() {
    QSettings settings;
    const QByteArray geometry = settings.value(QString::fromLatin1(windowGeometryKey)).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(1100, 650);
    }

    const QByteArray state = settings.value(QString::fromLatin1(windowStateKey)).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    const QString savedScreenName = settings.value(QString::fromLatin1(audienceScreenNameKey)).toString();
    if (!savedScreenName.isEmpty()) {
        for (int i = 0; i < m_screenCombo->count(); ++i) {
            QScreen* screen = m_screenCombo->itemData(i).value<QScreen*>();
            if (screen && screen->name() == savedScreenName) {
                m_screenCombo->setCurrentIndex(i);
                m_controller->setAudienceScreen(screen);
                break;
            }
        }
    }
}

void PresenterWindow::saveSettings() {
    QSettings settings;
    settings.setValue(QString::fromLatin1(windowGeometryKey), saveGeometry());
    settings.setValue(QString::fromLatin1(windowStateKey), saveState());
    if (QScreen* screen = m_controller->selectedAudienceScreen()) {
        settings.setValue(QString::fromLatin1(audienceScreenNameKey), screen->name());
    }
}
