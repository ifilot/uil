#include "PresenterWindow.hpp"

#include "AppController.hpp"
#include "ui/FontAwesome.hpp"
#include "util/ImageUtil.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QListWidget>
#include <QLibraryInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr int maxRecentPdfPaths = 5;
constexpr auto recentPdfPathsKey = "recentPdfPaths";
constexpr auto lastOpenDirectoryKey = "lastOpenDirectory";
constexpr auto windowGeometryKey = "presenterWindowGeometry";
constexpr auto windowStateKey = "presenterWindowState";
constexpr auto audienceScreenNameKey = "audienceScreenName";
constexpr int resizeBorderWidth = 6;

QString menuSafePathText(QString path) {
    return path.replace(QStringLiteral("&"), QStringLiteral("&&"));
}

bool isDescendantOf(const QWidget* widget, const QWidget* ancestor) {
    for (const QWidget* current = widget; current; current = current->parentWidget()) {
        if (current == ancestor) {
            return true;
        }
    }

    return false;
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

void SlidePreview::setOverlayImage(const QImage& image) {
    m_overlayImage = image;
    update();
}

void SlidePreview::setOverlayVisible(bool visible) {
    if (m_overlayVisible == visible) {
        return;
    }

    m_overlayVisible = visible;
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
    if (m_overlayVisible && !m_overlayImage.isNull()) {
        painter.drawImage(target, m_overlayImage);
    }
}

class SlideThumbnail final : public QWidget {
public:
    explicit SlideThumbnail(int pageIndex, QWidget* parent = nullptr)
        : QWidget(parent),
          m_pageIndex(pageIndex) {
        setObjectName(QStringLiteral("slideThumbnail"));
        setFixedSize(180, 128);
        setCursor(Qt::PointingHandCursor);
    }

    int pageIndex() const {
        return m_pageIndex;
    }

    void setSelected(bool selected) {
        if (m_selected == selected) {
            return;
        }
        m_selected = selected;
        update();
    }

    void setImage(const QImage& image) {
        m_image = image;
        update();
    }

    std::function<void(int)> activated;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && activated) {
            activated(m_pageIndex);
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(0x1e, 0x1e, 0x1e));

        const QRect slideFrame = rect().adjusted(10, 8, -10, -24);
        painter.fillRect(slideFrame, QColor(0x11, 0x11, 0x11));

        if (!m_image.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QRect imageRect = centeredRectForImage(m_image.size(), slideFrame.adjusted(1, 1, -1, -1));
            painter.drawImage(imageRect, m_image);
        } else {
            painter.setPen(QColor(0x85, 0x85, 0x85));
            painter.drawText(slideFrame, Qt::AlignCenter, QStringLiteral("..."));
        }

        const QColor borderColor = m_selected ? QColor(0x00, 0x8c, 0x8c) : QColor(0x3c, 0x3c, 0x3c);
        painter.setPen(QPen(borderColor, m_selected ? 2 : 1));
        painter.drawRect(slideFrame.adjusted(0, 0, -1, -1));

        painter.setPen(m_selected ? QColor(0xff, 0xff, 0xff) : QColor(0x8a, 0x8a, 0x8a));
        painter.drawText(QRect(0, height() - 21, width(), 18), Qt::AlignCenter, QString::number(m_pageIndex + 1));
    }

private:
    QImage m_image;
    int m_pageIndex = -1;
    bool m_selected = false;
};

class SlideDeckOverview final : public QScrollArea {
public:
    explicit SlideDeckOverview(QWidget* parent = nullptr)
        : QScrollArea(parent),
          m_content(new QWidget(this)),
          m_grid(new QGridLayout(m_content)) {
        setObjectName(QStringLiteral("deckOverview"));
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_grid->setContentsMargins(8, 8, 8, 8);
        m_grid->setHorizontalSpacing(8);
        m_grid->setVerticalSpacing(8);
        m_content->setObjectName(QStringLiteral("deckOverviewContent"));
        setWidget(m_content);
    }

    QSize thumbnailBoundingPixelSize() const {
        return QSize(176, 104);
    }

    void setPageCount(int pageCount) {
        while (QLayoutItem* item = m_grid->takeAt(0)) {
            delete item;
        }
        qDeleteAll(m_thumbnails);
        m_thumbnails.clear();
        m_currentPage = -1;

        for (int page = 0; page < pageCount; ++page) {
            auto* thumbnail = new SlideThumbnail(page, m_content);
            thumbnail->activated = [this](int pageIndex) {
                if (pageActivated) {
                    pageActivated(pageIndex);
                }
            };
            m_thumbnails.append(thumbnail);
        }

        relayout();
        requestRenders();
    }

    void setCurrentPage(int pageIndex) {
        m_currentPage = pageIndex;
        for (SlideThumbnail* thumbnail : std::as_const(m_thumbnails)) {
            thumbnail->setSelected(thumbnail->pageIndex() == pageIndex);
        }

        if (pageIndex >= 0 && pageIndex < m_thumbnails.size()) {
            ensureWidgetVisible(m_thumbnails.at(pageIndex), 24, 24);
        }
    }

    void setSlideImage(int pageIndex, const QSize& boundingPixelSize, const QImage& image) {
        if (boundingPixelSize != thumbnailBoundingPixelSize()
            || pageIndex < 0
            || pageIndex >= m_thumbnails.size()) {
            return;
        }

        m_thumbnails.at(pageIndex)->setImage(image);
    }

    std::function<void(int)> pageActivated;
    std::function<void(const QSize&, int)> renderBatchRequested;

protected:
    void resizeEvent(QResizeEvent* event) override {
        QScrollArea::resizeEvent(event);
        relayout();
        requestRenders();
    }

private:
    void relayout() {
        while (QLayoutItem* item = m_grid->takeAt(0)) {
            delete item;
        }

        const int tileWidth = 180;
        const int availableWidth = qMax(tileWidth, viewport()->width() - 16);
        const int columns = qMax(1, availableWidth / (tileWidth + m_grid->horizontalSpacing()));
        for (int i = 0; i < m_thumbnails.size(); ++i) {
            m_grid->addWidget(m_thumbnails.at(i), i / columns, i % columns);
        }
        m_grid->setRowStretch((m_thumbnails.size() + columns - 1) / columns, 1);
        m_grid->setColumnStretch(columns, 1);
    }

    void requestRenders() {
        if (renderBatchRequested && !m_thumbnails.isEmpty()) {
            renderBatchRequested(thumbnailBoundingPixelSize(), m_currentPage);
        }
    }

    QWidget* m_content = nullptr;
    QGridLayout* m_grid = nullptr;
    QList<SlideThumbnail*> m_thumbnails;
    int m_currentPage = -1;
};

class IconToolButton final : public QToolButton {
public:
    explicit IconToolButton(QWidget* parent = nullptr)
        : QToolButton(parent) {
    }

    void setStateIcons(const QIcon& normalIcon, const QIcon& hoverIcon) {
        m_normalIcon = normalIcon;
        m_hoverIcon = hoverIcon;
        setIcon(m_isHovered ? m_hoverIcon : m_normalIcon);
    }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Enter) {
            m_isHovered = true;
            setIcon(m_hoverIcon);
        } else if (event->type() == QEvent::Leave) {
            m_isHovered = false;
            setIcon(m_normalIcon);
        }

        return QToolButton::event(event);
    }

private:
    QIcon m_normalIcon;
    QIcon m_hoverIcon;
    bool m_isHovered = false;
};

PresenterWindow::PresenterWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent),
      m_controller(controller) {
    setWindowTitle(QStringLiteral("uil Presenter"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/uil.svg")));
    setWindowFlag(Qt::FramelessWindowHint, true);
    createActions();
    createLayout();
    createConnections();
    qApp->installEventFilter(this);
    updateScreenList();
    loadSettings();
    if (size().isEmpty()) {
        resize(1100, 650);
    }
}

PresenterWindow::~PresenterWindow() {
    qApp->removeEventFilter(this);
    clearResizeCursor();
}

bool PresenterWindow::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool isPresenterWidget = widget && (widget == this || isAncestorOf(widget)) && widget->window() == this;

    if (!isPresenterWidget) {
        if (m_resizeCursorActive && event->type() == QEvent::MouseMove) {
            clearResizeCursor();
        }

        return QMainWindow::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonDblClick: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && isTitleDragAreaAt(mouseEvent->globalPosition().toPoint())) {
            toggleMaximized();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }

        const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
        const Qt::Edges edges = resizeEdgesAt(mapFromGlobal(globalPosition));
        if (edges) {
            clearResizeCursor();
            if (windowHandle() && windowHandle()->startSystemResize(edges)) {
                return true;
            }

            beginManualResize(edges, globalPosition);
            return true;
        }

        if (isTitleDragAreaAt(globalPosition)) {
            clearResizeCursor();
            if (windowHandle() && windowHandle()->startSystemMove()) {
                return true;
            }

            beginManualMove(globalPosition);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
        if (m_manualResizeActive) {
            updateManualResize(globalPosition);
            return true;
        }

        if (m_manualMoveActive) {
            updateManualMove(globalPosition);
            return true;
        }

        updateResizeCursor(resizeEdgesAt(mapFromGlobal(globalPosition)));
        break;
    }
    case QEvent::MouseButtonRelease:
        finishManualResize();
        finishManualMove();
        break;
    case QEvent::Leave:
        if (!m_manualResizeActive) {
            clearResizeCursor();
        }
        break;
    default:
        break;
    }

    return QMainWindow::eventFilter(watched, event);
}

void PresenterWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        clearResizeCursor();
        updateMaximizeButton();
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
        QStringLiteral("Open Presentation"),
        settings.value(QString::fromLatin1(lastOpenDirectoryKey)).toString(),
        QStringLiteral("Presentations (*.pdf *.uil);;PDF files (*.pdf);;UIL packages (*.uil);;All files (*.*)"));

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
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    aboutBox.setText(QStringLiteral(
        "<h2>uil %1</h2>"
        "<p>%2<br>"
        "Author: %3 &lt;<a href=\"mailto:%4\">%4</a>&gt;<br>"
        "Repository: <a href=\"%5\">%5</a><br>"
        "License: %6.</p>")
        .arg(
            QStringLiteral(UIL_VERSION_DISPLAY),
            QStringLiteral(UIL_APP_COPYRIGHT),
            QStringLiteral(UIL_APP_AUTHOR),
            QStringLiteral(UIL_APP_EMAIL),
            QStringLiteral(UIL_REPOSITORY_URL),
            QStringLiteral(UIL_APP_LICENSE)));

    const QString buildConfig = QStringLiteral(UIL_BUILD_CONFIG).isEmpty()
        ? QStringLiteral("unspecified")
        : QStringLiteral(UIL_BUILD_CONFIG);
    const QString compiler = QStringLiteral(UIL_COMPILER_ID " " UIL_COMPILER_VERSION).trimmed();
    const QString compilerTimestamp = QStringLiteral(__DATE__ " " __TIME__);
    const QString cppStandard = QString::number(__cplusplus);

    QString ffmpegLine;
#ifdef UIL_HAVE_FFMPEG
    ffmpegLine = QStringLiteral("<li><b>FFmpeg libraries</b>: libavformat, libavcodec, libavutil, libswscale for MP4/media frame extraction. FFmpeg is licensed under LGPL/GPL depending on the linked build configuration.</li>");
#else
    ffmpegLine = QStringLiteral("<li><b>FFmpeg libraries</b>: optional media support dependency; not linked in this build.</li>");
#endif
    aboutBox.setInformativeText(QStringLiteral(
        "<p>A Windows-focused Qt PDF presentation app for Beamer-style slide decks.</p>"
        "<p><b>Compilation details:</b></p>"
        "<ul>"
        "<li><b>Compiled</b>: %1.</li>"
        "<li><b>Compiler</b>: %2; C++ value %3.</li>"
        "<li><b>Build configuration</b>: %4; CMake %5.</li>"
        "<li><b>Qt runtime</b>: %6; Qt build: %7.</li>"
        "</ul>"
        "<p><b>External packages and assets used by this build:</b></p>"
        "<ul>"
        "<li><b>Qt %6</b>: Core, Gui, Widgets, Pdf, and Svg modules for the application framework, PDF rendering, audience output, and SVG rendering. The Windows deployment also includes Qt plugins such as the platform and SVG icon plugins. Qt is available under LGPL/GPL/commercial licensing depending on distribution; Qt Pdf includes PDFium and its third-party components.</li>"
        "<li><b>zlib</b>: compression library used through ZLIB::ZLIB for PDF media stream handling and .uil package extraction; zlib License.</li>"
        "%8"
        "<li><b>Font Awesome Free 7.2.0</b>: vendored SVG icon assets under resources/fontawesome. The SVG icons are licensed under CC BY 4.0. The upstream package also includes MIT-licensed code and SIL OFL 1.1 fonts; this app uses the SVG assets. Copyright Fonticons, Inc.</li>"
        "<li><b>MSYS2/GCC runtime libraries</b>: deployed on Windows as needed by the toolchain and audited by the installer staging script.</li>"
        "</ul>"
        "<p>Application rendering, scheduling, caching, and presentation control code is local to uil.</p>"
        "<p>Windows installers include THIRD_PARTY_NOTICES.txt and third-party/package-inventory.tsv with the complete staged dependency inventory and copied license files.</p>")
        .arg(
            compilerTimestamp,
            compiler,
            cppStandard,
            buildConfig,
            QStringLiteral(UIL_CMAKE_VERSION),
            QString::fromLatin1(qVersion()),
            QLibraryInfo::build(),
            ffmpegLine));
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

void PresenterWindow::updateDocumentOverview(int pageCount) {
    if (!m_deckOverview) {
        return;
    }

    m_deckOverview->setPageCount(pageCount);
    m_deckOverview->setCurrentPage(m_controller->currentPage());
}

void PresenterWindow::updatePageLabel(int pageIndex, int pageCount) {
    if (pageCount <= 0) {
        m_pageLabel->setText(QStringLiteral("Page: - / -"));
        if (m_deckOverview) {
            m_deckOverview->setCurrentPage(-1);
        }
        return;
    }

    m_pageLabel->setText(QStringLiteral("Page: %1 / %2").arg(pageIndex + 1).arg(pageCount));
    if (m_deckOverview) {
        m_deckOverview->setCurrentPage(pageIndex);
        m_controller->requestDeckOverviewRenders(m_deckOverview->thumbnailBoundingPixelSize(), pageIndex);
    }
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
    m_menuBar = new QMenuBar(this);
    m_menuBar->setObjectName(QStringLiteral("titleMenuBar"));
    m_menuBar->setNativeMenuBar(false);
    m_menuBar->setFixedHeight(31);

    m_openAction = new QAction(QStringLiteral("&Open Presentation"), this);
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

    m_blackScreenAction = new QAction(QStringLiteral("Black Screen"), this);
    m_blackScreenAction->setShortcut(Qt::Key_B);

    m_whiteScreenAction = new QAction(QStringLiteral("White Screen"), this);
    m_whiteScreenAction->setShortcut(Qt::Key_W);

    m_fullscreenAction = new QAction(QStringLiteral("Toggle Audience Fullscreen"), this);
    m_fullscreenAction->setShortcut(Qt::Key_F11);

    m_showAudienceOverlayAction = new QAction(QStringLiteral("Show Audience Overlay"), this);
    m_showAudienceOverlayAction->setCheckable(true);
    m_showAudienceOverlayAction->setChecked(true);

    m_quitAction = new QAction(QStringLiteral("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);

    m_aboutAction = new QAction(QStringLiteral("About uil"), this);

    QMenu* fileMenu = m_menuBar->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_openAction);
    m_openRecentMenu = fileMenu->addMenu(QStringLiteral("Open &Recent"));
    rebuildOpenRecentMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* presentationMenu = m_menuBar->addMenu(QStringLiteral("&Presentation"));
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
    presentationMenu->addAction(m_showAudienceOverlayAction);
    presentationMenu->addSeparator();
    presentationMenu->addAction(m_fullscreenAction);

    QMenu* helpMenu = m_menuBar->addMenu(QStringLiteral("&Help"));
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
    addAction(m_blackScreenAction);
    addAction(m_whiteScreenAction);
    addAction(m_fullscreenAction);
    addAction(m_showAudienceOverlayAction);
    addAction(m_quitAction);
}

QWidget* PresenterWindow::createTitleBar() {
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(32);

    auto* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);

    auto* iconLabel = new QLabel(m_titleBar);
    iconLabel->setObjectName(QStringLiteral("titleIcon"));
    iconLabel->setFixedSize(36, 32);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(QIcon(QStringLiteral(":/icons/uil-white.svg")).pixmap(QSize(16, 16)));

    titleLayout->addWidget(iconLabel);
    titleLayout->addWidget(m_menuBar, 0, Qt::AlignTop);

    auto* dragArea = new QWidget(m_titleBar);
    dragArea->setObjectName(QStringLiteral("titleDragArea"));
    dragArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLayout->addWidget(dragArea, 1);

    auto createControlButton = [this](const QString& toolTip, const QString& iconName) {
        auto* button = new IconToolButton(m_titleBar);
        button->setObjectName(QStringLiteral("windowControlButton"));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(46, 32);
        button->setToolTip(toolTip);
        button->setIconSize(QSize(12, 12));
        button->setStateIcons(
            FontAwesome::icon(FontAwesome::Style::Solid, iconName, QColor(0xcc, 0xcc, 0xcc), QSize(16, 16)),
            FontAwesome::icon(FontAwesome::Style::Solid, iconName, QColor(0xff, 0xff, 0xff), QSize(16, 16)));
        return button;
    };

    m_minimizeButton = createControlButton(QStringLiteral("Minimize"), QStringLiteral("window-minimize"));
    m_maximizeButton = createControlButton(QStringLiteral("Maximize"), QStringLiteral("window-maximize"));
    m_closeButton = createControlButton(QStringLiteral("Close"), QStringLiteral("xmark"));
    m_closeButton->setObjectName(QStringLiteral("windowCloseButton"));

    connect(m_minimizeButton, &QToolButton::clicked, this, &PresenterWindow::showMinimized);
    connect(m_maximizeButton, &QToolButton::clicked, this, &PresenterWindow::toggleMaximized);
    connect(m_closeButton, &QToolButton::clicked, this, &PresenterWindow::close);

    titleLayout->addWidget(m_minimizeButton);
    titleLayout->addWidget(m_maximizeButton);
    titleLayout->addWidget(m_closeButton);
    updateMaximizeButton();

    return m_titleBar;
}

void PresenterWindow::createLayout() {
    setMenuWidget(createTitleBar());

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("presenterRoot"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 16, 16, 10);
    rootLayout->setSpacing(12);

    auto createPane = [](const QString& title, QWidget* content) {
        auto* pane = new QWidget;
        pane->setObjectName(QStringLiteral("previewPane"));
        auto* paneLayout = new QVBoxLayout(pane);
        paneLayout->setContentsMargins(0, 0, 0, 0);
        paneLayout->setSpacing(0);

        auto* heading = new QLabel(title, pane);
        heading->setObjectName(QStringLiteral("previewHeading"));
        paneLayout->addWidget(heading);
        paneLayout->addWidget(content, 1);
        return pane;
    };

    auto* mainLayout = new QHBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    auto* currentSlidePanel = new QWidget(central);
    currentSlidePanel->setObjectName(QStringLiteral("currentSlidePanel"));
    auto* currentSlideLayout = new QVBoxLayout(currentSlidePanel);
    currentSlideLayout->setContentsMargins(0, 0, 0, 0);
    currentSlideLayout->setSpacing(8);
    m_currentPreview = new SlidePreview(central);
    currentSlideLayout->addWidget(m_currentPreview, 1);

    auto* navigationLayout = new QHBoxLayout;
    navigationLayout->setContentsMargins(10, 0, 10, 10);
    navigationLayout->setSpacing(8);

    auto* previousButton = new QToolButton(currentSlidePanel);
    previousButton->setObjectName(QStringLiteral("slideNavButton"));
    previousButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    previousButton->setText(QStringLiteral("Previous"));
    previousButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    previousButton->setToolTip(QStringLiteral("Previous slide"));
    auto* nextButton = new QToolButton(currentSlidePanel);
    nextButton->setObjectName(QStringLiteral("slideNavButton"));
    nextButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nextButton->setText(QStringLiteral("Next"));
    nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    nextButton->setToolTip(QStringLiteral("Next slide"));

    connect(previousButton, &QToolButton::clicked, m_previousAction, &QAction::trigger);
    connect(nextButton, &QToolButton::clicked, m_nextAction, &QAction::trigger);

    navigationLayout->addStretch(1);
    navigationLayout->addWidget(previousButton);
    navigationLayout->addWidget(nextButton);
    navigationLayout->addStretch(1);
    currentSlideLayout->addLayout(navigationLayout);

    m_deckOverview = new SlideDeckOverview(central);
    m_deckOverview->pageActivated = [this](int pageIndex) {
        m_controller->goToPage(pageIndex);
    };
    m_deckOverview->renderBatchRequested = [this](const QSize& boundingSize, int focusedPageIndex) {
        m_controller->requestDeckOverviewRenders(boundingSize, focusedPageIndex);
    };

    mainLayout->addWidget(createPane(QStringLiteral("Current slide"), currentSlidePanel), 3);
    mainLayout->addWidget(createPane(QStringLiteral("Slide deck"), m_deckOverview), 2);

    auto* statusLayout = new QHBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);
    m_pageLabel = new QLabel(QStringLiteral("Page: - / -"), central);
    m_mediaLabel = new QLabel(QStringLiteral("Media: none"), central);
    auto* screenLabel = new QLabel(QStringLiteral("Audience:"), central);
    m_screenCombo = new QComboBox(central);
    m_screenCombo->setMinimumWidth(280);
    for (QLabel* label : {m_pageLabel, m_mediaLabel}) {
        label->setObjectName(QStringLiteral("statusPill"));
    }
    screenLabel->setObjectName(QStringLiteral("fieldLabel"));
    statusLayout->addWidget(m_pageLabel);
    statusLayout->addWidget(m_mediaLabel);
    statusLayout->addStretch(1);
    statusLayout->addWidget(screenLabel);
    statusLayout->addWidget(m_screenCombo);

    rootLayout->addLayout(mainLayout, 1);
    rootLayout->addLayout(statusLayout);
    setCentralWidget(central);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->showMessage(QStringLiteral("Ready"));
}

bool PresenterWindow::isTitleDragAreaAt(const QPoint& globalPosition) const {
    if (!m_titleBar) {
        return false;
    }

    const QPoint titlePosition = m_titleBar->mapFromGlobal(globalPosition);
    if (!m_titleBar->rect().contains(titlePosition)) {
        return false;
    }

    QWidget* child = m_titleBar->childAt(titlePosition);
    return !isDescendantOf(child, m_menuBar)
        && !isDescendantOf(child, m_minimizeButton)
        && !isDescendantOf(child, m_maximizeButton)
        && !isDescendantOf(child, m_closeButton);
}

Qt::Edges PresenterWindow::resizeEdgesAt(const QPoint& position) const {
    if (isMaximized() || isFullScreen()) {
        return {};
    }

    Qt::Edges edges;
    const QRect windowRect = rect();
    if (position.x() >= windowRect.left() && position.x() <= windowRect.left() + resizeBorderWidth) {
        edges |= Qt::LeftEdge;
    } else if (position.x() >= windowRect.right() - resizeBorderWidth && position.x() <= windowRect.right()) {
        edges |= Qt::RightEdge;
    }

    if (position.y() >= windowRect.top() && position.y() <= windowRect.top() + resizeBorderWidth) {
        edges |= Qt::TopEdge;
    } else if (position.y() >= windowRect.bottom() - resizeBorderWidth && position.y() <= windowRect.bottom()) {
        edges |= Qt::BottomEdge;
    }

    return edges;
}

void PresenterWindow::updateResizeCursor(Qt::Edges edges) {
    if (!edges) {
        clearResizeCursor();
        return;
    }

    Qt::CursorShape cursorShape = Qt::ArrowCursor;
    const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
    const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
    if (horizontal && vertical) {
        const bool forwardDiagonal = (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge))
            || (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge));
        cursorShape = forwardDiagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    } else if (horizontal) {
        cursorShape = Qt::SizeHorCursor;
    } else if (vertical) {
        cursorShape = Qt::SizeVerCursor;
    }

    if (m_resizeCursorActive) {
        QGuiApplication::changeOverrideCursor(QCursor(cursorShape));
    } else {
        QGuiApplication::setOverrideCursor(QCursor(cursorShape));
        m_resizeCursorActive = true;
    }
}

void PresenterWindow::clearResizeCursor() {
    if (!m_resizeCursorActive) {
        return;
    }

    QGuiApplication::restoreOverrideCursor();
    m_resizeCursorActive = false;
}

void PresenterWindow::beginManualResize(Qt::Edges edges, const QPoint& globalPosition) {
    m_manualResizeActive = true;
    m_resizeEdges = edges;
    m_resizeStartGeometry = geometry();
    m_resizeStartGlobalPosition = globalPosition;
}

void PresenterWindow::updateManualResize(const QPoint& globalPosition) {
    QRect resizedGeometry = m_resizeStartGeometry;
    const QPoint delta = globalPosition - m_resizeStartGlobalPosition;
    const QSize minimum = minimumSize();

    if (m_resizeEdges.testFlag(Qt::LeftEdge)) {
        resizedGeometry.setLeft(std::min(m_resizeStartGeometry.left() + delta.x(),
            m_resizeStartGeometry.right() - minimum.width() + 1));
    } else if (m_resizeEdges.testFlag(Qt::RightEdge)) {
        resizedGeometry.setRight(std::max(m_resizeStartGeometry.right() + delta.x(),
            m_resizeStartGeometry.left() + minimum.width() - 1));
    }

    if (m_resizeEdges.testFlag(Qt::TopEdge)) {
        resizedGeometry.setTop(std::min(m_resizeStartGeometry.top() + delta.y(),
            m_resizeStartGeometry.bottom() - minimum.height() + 1));
    } else if (m_resizeEdges.testFlag(Qt::BottomEdge)) {
        resizedGeometry.setBottom(std::max(m_resizeStartGeometry.bottom() + delta.y(),
            m_resizeStartGeometry.top() + minimum.height() - 1));
    }

    setGeometry(resizedGeometry);
}

void PresenterWindow::finishManualResize() {
    m_manualResizeActive = false;
    m_resizeEdges = {};
}

void PresenterWindow::beginManualMove(const QPoint& globalPosition) {
    m_manualMoveActive = true;
    m_moveOffset = globalPosition - frameGeometry().topLeft();
}

void PresenterWindow::updateManualMove(const QPoint& globalPosition) {
    move(globalPosition - m_moveOffset);
}

void PresenterWindow::finishManualMove() {
    m_manualMoveActive = false;
}

void PresenterWindow::toggleMaximized() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    updateMaximizeButton();
}

void PresenterWindow::updateMaximizeButton() {
    if (!m_maximizeButton) {
        return;
    }

    m_maximizeButton->setToolTip(isMaximized() ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
    const QString iconName = isMaximized() ? QStringLiteral("window-restore") : QStringLiteral("window-maximize");
    if (auto* iconButton = dynamic_cast<IconToolButton*>(m_maximizeButton)) {
        iconButton->setStateIcons(
            FontAwesome::icon(FontAwesome::Style::Solid, iconName, QColor(0xcc, 0xcc, 0xcc), QSize(16, 16)),
            FontAwesome::icon(FontAwesome::Style::Solid, iconName, QColor(0xff, 0xff, 0xff), QSize(16, 16)));
    }
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
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_aboutAction, &QAction::triggered, this, &PresenterWindow::showAbout);
    connect(m_blackScreenAction, &QAction::triggered, m_controller, &AppController::toggleBlackScreen);
    connect(m_whiteScreenAction, &QAction::triggered, m_controller, &AppController::toggleWhiteScreen);
    connect(m_fullscreenAction, &QAction::triggered, m_controller, &AppController::toggleAudienceFullscreen);
    connect(m_showAudienceOverlayAction, &QAction::toggled, m_currentPreview, &SlidePreview::setOverlayVisible);
    connect(m_screenCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PresenterWindow::selectScreenFromCombo);

    connect(m_controller, &AppController::pageChanged, this, &PresenterWindow::updatePageLabel);
    connect(m_controller, &AppController::documentChanged, this, &PresenterWindow::updateDocumentOverview);
    connect(m_controller, &AppController::mediaScanChanged, this, &PresenterWindow::updateMediaLabel);
    connect(m_controller, &AppController::currentSlideImageChanged, m_currentPreview, &SlidePreview::setPreviewImage);
    connect(m_controller, &AppController::currentAnnotationOverlayChanged, m_currentPreview, &SlidePreview::setOverlayImage);
    connect(m_controller, &AppController::deckSlideImageChanged, this,
        [this](int pageIndex, const QSize& boundingPixelSize, const QImage& image) {
            if (m_deckOverview) {
                m_deckOverview->setSlideImage(pageIndex, boundingPixelSize, image);
            }
        });
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
