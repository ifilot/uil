#include "AudienceWindow.hpp"

#include "ui/FontAwesome.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImageWriter>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStandardPaths>
#include <QWindow>
#include <QtMath>
#include <QtGlobal>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(logUi, "ui")

namespace {
constexpr int maxAudienceSlides = 4;
constexpr qreal pointerLogicalSize = 46.0;
const QColor menuIconColor(0xcc, 0xcc, 0xcc);

QRectF imageSourceRect(const QImage& image) {
    return QRectF(QPointF(0.0, 0.0), QSizeF(image.size()));
}

QIcon menuIcon(const QString& name, QColor color = menuIconColor) {
    return FontAwesome::icon(FontAwesome::Style::Solid, name, color, QSize(18, 18));
}

QString checkedMenuText(const QString& text, bool checked) {
    return checked ? QStringLiteral("%1\t%2").arg(text, QString::fromUtf8("\xe2\x9c\x93")) : text;
}

QAction* addIconAction(QMenu* menu, const QString& text, const QString& iconName, bool checked = false) {
    QAction* action = menu->addAction(menuIcon(iconName), checkedMenuText(text, checked));
    action->setIconVisibleInMenu(true);
    return action;
}
}

AudienceWindow::AudienceWindow()
    : QWidget(nullptr, Qt::Window) {
    setWindowTitle(QStringLiteral("uil Audience"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/uil.svg")));
    resize(960, 540);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    m_cursorHideTimer.setSingleShot(true);
    m_cursorHideTimer.setInterval(2000);
    connect(&m_cursorHideTimer, &QTimer::timeout, this, &AudienceWindow::hideCursor);
}

AudienceWindow::~AudienceWindow() = default;

void AudienceWindow::setSlideImage(const QString& textureKey, const QImage& image) {
    if (textureKey.isEmpty() || image.isNull()) {
        clearSlideImage();
        return;
    }

    m_currentTextureKey = textureKey;
    m_currentSlideImage = image;
    cacheSlideImage(textureKey, image);
    emit annotationOverlayChanged(currentAnnotationOverlayImage());
    update();
}

void AudienceWindow::clearSlideImage() {
    m_currentTextureKey.clear();
    m_currentSlideImage = {};
    m_videoFrame = {};
    m_videoRect = {};
    m_hasVideoOverlay = false;
    emit annotationOverlayChanged({});
    update();
}

void AudienceWindow::cacheSlideImage(const QString& textureKey, const QImage& image) {
    if (textureKey.isEmpty() || image.isNull()) {
        return;
    }

    auto it = std::find_if(m_slideCache.begin(), m_slideCache.end(), [&textureKey](const CachedSlide& slide) {
        return slide.key == textureKey;
    });

    if (it != m_slideCache.end()) {
        it->image = image;
        if (textureKey == m_currentTextureKey) {
            m_currentSlideImage = image;
        }
        if (it != m_slideCache.begin()) {
            CachedSlide slide = std::move(*it);
            m_slideCache.erase(it);
            m_slideCache.insert(m_slideCache.begin(), std::move(slide));
        }
        update();
        return;
    }

    m_slideCache.insert(m_slideCache.begin(), CachedSlide{textureKey, image});
    evictOldSlides();
    update();
}

void AudienceWindow::setVideoFrame(const QImage& image, QRectF slideRect) {
    if (image.isNull() || !slideRect.isValid()) {
        clearVideoOverlay();
        return;
    }

    m_videoFrame = image;
    m_videoRect = slideRect;
    m_hasVideoOverlay = true;
    update();
}

void AudienceWindow::clearVideoOverlay() {
    if (!m_hasVideoOverlay && m_videoFrame.isNull() && m_videoRect.isNull()) {
        return;
    }

    m_videoFrame = {};
    m_videoRect = {};
    m_hasVideoOverlay = false;
    update();
}

void AudienceWindow::setAudienceScreen(QScreen* screen) {
    if (!screen) {
        return;
    }

    m_screen = screen;
    applyScreenGeometry(m_isFullscreen);
    if (m_isFullscreen) {
        showFullScreen();
    }
    emit renderTargetChanged();
}

void AudienceWindow::enterFullscreen() {
    m_isFullscreen = true;
    applyScreenGeometry(true);
    showFullScreen();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    if (QWindow* handle = windowHandle()) {
        handle->requestActivate();
    }
    QTimer::singleShot(0, this, [this] {
        raise();
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        if (QWindow* handle = windowHandle()) {
            handle->requestActivate();
        }
    });
    showCursorTemporarily();
    qCInfo(logUi) << "Audience fullscreen entered";
}

void AudienceWindow::toggleFullscreen() {
    if (!m_isFullscreen) {
        enterFullscreen();
    } else {
        exitFullscreen();
    }
}

void AudienceWindow::exitFullscreen() {
    if (!m_isFullscreen) {
        return;
    }

    m_isFullscreen = false;
    m_isAnnotating = false;
    m_pointerVisible = false;
    showNormal();
    hide();
    clearBlankScreen();
    unsetCursor();
    emit presentationClosed();
    qCInfo(logUi) << "Audience fullscreen closed";
}

void AudienceWindow::toggleBlackScreen() {
    m_blankMode = (m_blankMode == BlankMode::Black) ? BlankMode::None : BlankMode::Black;
    update();
}

void AudienceWindow::toggleWhiteScreen() {
    m_blankMode = (m_blankMode == BlankMode::White) ? BlankMode::None : BlankMode::White;
    update();
}

void AudienceWindow::clearBlankScreen() {
    if (m_blankMode == BlankMode::None) {
        return;
    }
    m_blankMode = BlankMode::None;
    update();
}

void AudienceWindow::setCursorTool() {
    m_interactionTool = InteractionTool::Cursor;
    m_pointerVisible = false;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setPointerTool() {
    m_interactionTool = InteractionTool::Pointer;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setPenTool() {
    m_interactionTool = InteractionTool::Pen;
    m_pointerVisible = false;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setEraserTool() {
    m_interactionTool = InteractionTool::Eraser;
    m_pointerVisible = false;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setPointerColor(const QColor& color) {
    if (color.isValid()) {
        m_pointerColor = color;
        update();
    }
}

void AudienceWindow::setAnnotationColor(const QColor& color) {
    if (color.isValid()) {
        m_annotationColor = color;
    }
}

void AudienceWindow::setAnnotationThickness(int thickness) {
    m_annotationThickness = std::clamp(thickness, 1, 64);
}

void AudienceWindow::clearAnnotations() {
    if (!m_currentTextureKey.isEmpty() && m_annotationImages.contains(m_currentTextureKey)) {
        m_annotationImages[m_currentTextureKey].fill(Qt::transparent);
        emit annotationOverlayChanged(currentAnnotationOverlayImage());
        update();
    }
}

QImage AudienceWindow::currentAnnotatedSlideImage() const {
    if (m_currentSlideImage.isNull()) {
        return {};
    }

    QImage result = m_currentSlideImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage* annotation = currentAnnotationImage();
    if (annotation && !annotation->isNull()) {
        QPainter painter(&result);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(result.rect(), *annotation);
    }

    return result;
}

QImage AudienceWindow::currentAnnotationOverlayImage() const {
    const QImage* annotation = currentAnnotationImage();
    return annotation ? *annotation : QImage();
}

QSize AudienceWindow::renderLogicalSize() const {
    if (m_screen) {
        return m_screen->geometry().size();
    }

    return size();
}

qreal AudienceWindow::renderDevicePixelRatio() const {
    if (m_screen) {
        return m_screen->devicePixelRatio();
    }

    return devicePixelRatioF();
}

void AudienceWindow::closeEvent(QCloseEvent* event) {
    m_isFullscreen = false;
    m_isAnnotating = false;
    m_pointerVisible = false;
    clearBlankScreen();
    unsetCursor();
    emit presentationClosed();
    QWidget::closeEvent(event);
}

void AudienceWindow::contextMenuEvent(QContextMenuEvent* event) {
    showFeatureMenu(event->globalPos());
    event->accept();
}

void AudienceWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), m_blankMode == BlankMode::White ? Qt::white : Qt::black);

    if (m_blankMode != BlankMode::None || m_currentSlideImage.isNull()) {
        return;
    }

    const QRectF slideRect = slideLogicalRect(m_currentSlideImage.size());
    if (!slideRect.isValid()) {
        return;
    }

    painter.drawImage(slideRect, m_currentSlideImage, imageSourceRect(m_currentSlideImage));

    if (m_hasVideoOverlay && !m_videoFrame.isNull() && m_videoRect.isValid()) {
        const QRectF target(
            slideRect.left() + m_videoRect.left() * slideRect.width(),
            slideRect.top() + m_videoRect.top() * slideRect.height(),
            m_videoRect.width() * slideRect.width(),
            m_videoRect.height() * slideRect.height());
        painter.drawImage(target, m_videoFrame, imageSourceRect(m_videoFrame));
    }

    const QImage* annotation = currentAnnotationImage();
    if (annotation && !annotation->isNull()) {
        painter.drawImage(slideRect, *annotation, imageSourceRect(*annotation));
    }

    drawPointer(painter);
}

void AudienceWindow::keyPressEvent(QKeyEvent* event) {
    showCursorTemporarily();
    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        emit nextRequested();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        emit previousRequested();
        event->accept();
        return;
    case Qt::Key_Home:
        emit firstRequested();
        event->accept();
        return;
    case Qt::Key_End:
        emit lastRequested();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit playPauseRequested();
        event->accept();
        return;
    case Qt::Key_B:
        toggleBlackScreen();
        event->accept();
        return;
    case Qt::Key_W:
        toggleWhiteScreen();
        event->accept();
        return;
    case Qt::Key_F11:
        toggleFullscreen();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (m_isFullscreen) {
            exitFullscreen();
            event->accept();
            return;
        }
        if (m_blankMode != BlankMode::None) {
            clearBlankScreen();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void AudienceWindow::leaveEvent(QEvent* event) {
    m_pointerVisible = false;
    m_isAnnotating = false;
    update();
    QWidget::leaveEvent(event);
}

void AudienceWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_interactionTool == InteractionTool::Pointer) {
        m_pointerPosition = event->position();
        m_pointerVisible = true;
        update();
        event->accept();
        return;
    }

    if (m_isAnnotating && (event->buttons() & Qt::LeftButton)) {
        drawAnnotationSegment(m_lastAnnotationPoint, event->position());
        m_lastAnnotationPoint = event->position();
        event->accept();
        return;
    }

    showCursorTemporarily();
    QWidget::mouseMoveEvent(event);
}

void AudienceWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
        && (m_interactionTool == InteractionTool::Pen || m_interactionTool == InteractionTool::Eraser)) {
        m_isAnnotating = true;
        m_lastAnnotationPoint = event->position();
        drawAnnotationSegment(m_lastAnnotationPoint, m_lastAnnotationPoint);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_interactionTool == InteractionTool::Pointer) {
        m_pointerPosition = event->position();
        m_pointerVisible = true;
        update();
        event->accept();
        return;
    }

    showCursorTemporarily();
    QWidget::mousePressEvent(event);
}

void AudienceWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        showFeatureMenu(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_isAnnotating) {
        m_isAnnotating = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void AudienceWindow::evictOldSlides() {
    for (int i = int(m_slideCache.size()) - 1; int(m_slideCache.size()) > maxAudienceSlides && i >= 0; --i) {
        if (m_slideCache.at(size_t(i)).key == m_currentTextureKey) {
            continue;
        }
        m_slideCache.erase(m_slideCache.begin() + i);
    }
}

void AudienceWindow::applyScreenGeometry(bool fullscreen) {
    if (!m_screen) {
        return;
    }

    winId();
    if (QWindow* handle = windowHandle()) {
        handle->setScreen(m_screen);
    }

    if (fullscreen) {
        setGeometry(m_screen->geometry());
        return;
    }

    const QRect availableGeometry = m_screen->availableGeometry();
    QSize targetSize = size();
    if (!targetSize.isValid()) {
        targetSize = QSize(960, 540);
    }
    targetSize = targetSize.boundedTo(availableGeometry.size());

    const QPoint topLeft(
        availableGeometry.x() + (availableGeometry.width() - targetSize.width()) / 2,
        availableGeometry.y() + (availableGeometry.height() - targetSize.height()) / 2);
    setGeometry(QRect(topLeft, targetSize));
}

void AudienceWindow::showCursorTemporarily() {
    if (m_interactionTool != InteractionTool::Cursor) {
        updateCursorAppearance();
        return;
    }

    unsetCursor();
    if (m_isFullscreen) {
        m_cursorHideTimer.start();
    }
}

void AudienceWindow::hideCursor() {
    if (m_isFullscreen && m_interactionTool == InteractionTool::Cursor) {
        setCursor(QCursor(Qt::BlankCursor));
    }
}

void AudienceWindow::updateCursorAppearance() {
    m_cursorHideTimer.stop();
    switch (m_interactionTool) {
    case InteractionTool::Cursor:
        unsetCursor();
        if (m_isFullscreen) {
            m_cursorHideTimer.start();
        }
        break;
    case InteractionTool::Pointer:
        setCursor(QCursor(Qt::BlankCursor));
        break;
    case InteractionTool::Pen:
    case InteractionTool::Eraser:
        setCursor(QCursor(Qt::CrossCursor));
        break;
    }
}

QRectF AudienceWindow::slideLogicalRect(QSize textureSize) const {
    if (!textureSize.isValid() || height() <= 0 || width() <= 0) {
        return {};
    }

    const qreal viewportAspect = qreal(width()) / qreal(height());
    const qreal textureAspect = qreal(textureSize.width()) / qreal(textureSize.height());
    QSizeF displayedSize(width(), height());
    if (textureAspect > viewportAspect) {
        displayedSize.setHeight(displayedSize.width() / textureAspect);
    } else {
        displayedSize.setWidth(displayedSize.height() * textureAspect);
    }

    return QRectF(
        (width() - displayedSize.width()) / 2.0,
        (height() - displayedSize.height()) / 2.0,
        displayedSize.width(),
        displayedSize.height());
}

QPointF AudienceWindow::slideImagePoint(QPointF windowPoint, QSize textureSize, bool* inside) const {
    const QRectF slideRect = slideLogicalRect(textureSize);
    const bool contains = slideRect.contains(windowPoint);
    if (inside) {
        *inside = contains;
    }
    if (!contains || !slideRect.isValid()) {
        return {};
    }

    return QPointF(
        (windowPoint.x() - slideRect.left()) / slideRect.width() * textureSize.width(),
        (windowPoint.y() - slideRect.top()) / slideRect.height() * textureSize.height());
}

QImage& AudienceWindow::annotationImageForCurrentSlide(QSize size) {
    QImage& image = m_annotationImages[m_currentTextureKey];
    if (image.size() != size || image.format() != QImage::Format_ARGB32_Premultiplied) {
        QImage replacement(size, QImage::Format_ARGB32_Premultiplied);
        replacement.fill(Qt::transparent);
        if (!image.isNull()) {
            QPainter painter(&replacement);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(replacement.rect(), image);
        }
        image = replacement;
    }

    return image;
}

const QImage* AudienceWindow::currentAnnotationImage() const {
    if (m_currentTextureKey.isEmpty()) {
        return nullptr;
    }

    const auto it = m_annotationImages.constFind(m_currentTextureKey);
    return it == m_annotationImages.constEnd() ? nullptr : &it.value();
}

void AudienceWindow::drawAnnotationSegment(QPointF fromWindowPoint, QPointF toWindowPoint) {
    if (m_currentTextureKey.isEmpty() || m_currentSlideImage.isNull()) {
        return;
    }

    const QSize targetSize = m_currentSlideImage.size();
    if (!targetSize.isValid()) {
        return;
    }

    bool fromInside = false;
    bool toInside = false;
    const QPointF from = slideImagePoint(fromWindowPoint, targetSize, &fromInside);
    const QPointF to = slideImagePoint(toWindowPoint, targetSize, &toInside);
    if (!fromInside || !toInside) {
        return;
    }

    QImage& image = annotationImageForCurrentSlide(targetSize);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(m_annotationColor, m_annotationThickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (m_interactionTool == InteractionTool::Eraser) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        pen.setColor(Qt::transparent);
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    painter.setPen(pen);

    if (from == to) {
        painter.drawPoint(from);
    } else {
        painter.drawLine(from, to);
    }

    emit annotationOverlayChanged(currentAnnotationOverlayImage());
    update();
}

void AudienceWindow::drawPointer(QPainter& painter) const {
    if (!m_pointerVisible || m_interactionTool != InteractionTool::Pointer) {
        return;
    }

    QColor glow = m_pointerColor;
    glow.setAlpha(55);
    QColor fill = m_pointerColor;
    fill.setAlpha(120);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(m_pointerPosition, pointerLogicalSize * 0.68, pointerLogicalSize * 0.68);
    painter.setBrush(fill);
    painter.drawEllipse(m_pointerPosition, pointerLogicalSize * 0.35, pointerLogicalSize * 0.35);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(m_pointerColor, 4.0));
    painter.drawEllipse(m_pointerPosition, pointerLogicalSize * 0.48, pointerLogicalSize * 0.48);
    painter.restore();
}

void AudienceWindow::showFeatureMenu(const QPoint& globalPosition) {
    if (m_featureMenu && m_featureMenu->isVisible()) {
        return;
    }
    if (m_featureMenu) {
        m_featureMenu->close();
        m_featureMenu = nullptr;
    }

    auto* menu = new QMenu(this);
    m_featureMenu = menu;
    menu->setAttribute(Qt::WA_DeleteOnClose, true);
    menu->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    menu->winId();
    if (QWindow* menuWindow = menu->windowHandle()) {
        if (QWindow* audienceWindow = windowHandle()) {
            menuWindow->setScreen(audienceWindow->screen());
        }
    }

    QAction* cursorAction = addIconAction(
        menu,
        QStringLiteral("Classic pointer"),
        QStringLiteral("arrow-pointer"),
        m_interactionTool == InteractionTool::Cursor);
    QAction* pointerAction = addIconAction(
        menu,
        QStringLiteral("Laser pointer"),
        QStringLiteral("location-crosshairs"),
        m_interactionTool == InteractionTool::Pointer);
    QAction* penAction = addIconAction(
        menu,
        QStringLiteral("Draw"),
        QStringLiteral("pen"),
        m_interactionTool == InteractionTool::Pen);
    QAction* eraserAction = addIconAction(
        menu,
        QStringLiteral("Eraser"),
        QStringLiteral("eraser"),
        m_interactionTool == InteractionTool::Eraser);

    connect(cursorAction, &QAction::triggered, this, &AudienceWindow::setCursorTool);
    connect(pointerAction, &QAction::triggered, this, &AudienceWindow::setPointerTool);
    connect(penAction, &QAction::triggered, this, &AudienceWindow::setPenTool);
    connect(eraserAction, &QAction::triggered, this, &AudienceWindow::setEraserTool);

    menu->addSeparator();
    QMenu* pointerColorMenu = menu->addMenu(menuIcon(QStringLiteral("palette")), QStringLiteral("Pointer color"));
    const QList<QPair<QString, QColor>> pointerColors{
        {QStringLiteral("Bright red"), QColor(255, 36, 36)},
        {QStringLiteral("Bright green"), QColor(28, 255, 83)},
        {QStringLiteral("Bright purple"), QColor(190, 82, 255)}
    };
    for (const auto& colorChoice : pointerColors) {
        QAction* action = pointerColorMenu->addAction(
            menuIcon(QStringLiteral("circle"), colorChoice.second),
            checkedMenuText(colorChoice.first, colorChoice.second == m_pointerColor));
        action->setIconVisibleInMenu(true);
        connect(action, &QAction::triggered, this, [this, color = colorChoice.second] {
            setPointerColor(color);
        });
    }

    QMenu* inkColorMenu = menu->addMenu(menuIcon(QStringLiteral("droplet")), QStringLiteral("Ink color"));
    const QList<QPair<QString, QColor>> inkColors{
        {QStringLiteral("Bright red"), QColor(255, 36, 36)},
        {QStringLiteral("Bright green"), QColor(28, 255, 83)},
        {QStringLiteral("Bright purple"), QColor(190, 82, 255)},
        {QStringLiteral("Yellow"), QColor(255, 224, 48)},
        {QStringLiteral("White"), QColor(255, 255, 255)}
    };
    for (const auto& colorChoice : inkColors) {
        QAction* action = inkColorMenu->addAction(
            menuIcon(QStringLiteral("circle"), colorChoice.second),
            checkedMenuText(colorChoice.first, colorChoice.second == m_annotationColor));
        action->setIconVisibleInMenu(true);
        connect(action, &QAction::triggered, this, [this, color = colorChoice.second] {
            setAnnotationColor(color);
        });
    }

    QMenu* thicknessMenu = menu->addMenu(menuIcon(QStringLiteral("ruler-horizontal")), QStringLiteral("Ink thickness"));
    for (const int thickness : {3, 6, 10, 16, 24}) {
        QAction* action = thicknessMenu->addAction(
            menuIcon(QStringLiteral("minus")),
            checkedMenuText(QStringLiteral("%1 px").arg(thickness), thickness == m_annotationThickness));
        action->setIconVisibleInMenu(true);
        connect(action, &QAction::triggered, this, [this, thickness] {
            setAnnotationThickness(thickness);
        });
    }

    menu->addSeparator();
    QAction* clearAction = addIconAction(menu, QStringLiteral("Clear writings"), QStringLiteral("trash-can"));
    connect(clearAction, &QAction::triggered, this, &AudienceWindow::clearAnnotations);
    QAction* saveAction = addIconAction(menu, QStringLiteral("Save as image"), QStringLiteral("file-image"));
    connect(saveAction, &QAction::triggered, this, &AudienceWindow::saveAnnotatedSlideImage);

    connect(menu, &QMenu::aboutToHide, this, [this, menu] {
        if (m_featureMenu == menu) {
            m_featureMenu = nullptr;
        }
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        if (QWindow* handle = windowHandle()) {
            handle->requestActivate();
        }
        updateCursorAppearance();
        update();
    });

    setCursor(QCursor(Qt::ArrowCursor));
    menu->popup(globalPosition);
}

void AudienceWindow::saveAnnotatedSlideImage() {
    const QImage image = currentAnnotatedSlideImage();
    if (image.isNull()) {
        QMessageBox::information(this, QStringLiteral("Save Annotated Slide"), QStringLiteral("No slide image is available to save."));
        return;
    }

    QString defaultDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (defaultDirectory.isEmpty()) {
        defaultDirectory = QDir::homePath();
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Annotated Slide"),
        QDir(defaultDirectory).filePath(QStringLiteral("annotated-slide.png")),
        QStringLiteral("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));

    if (path.isEmpty()) {
        return;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    const QByteArray format = (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) ? QByteArrayLiteral("JPG") : QByteArrayLiteral("PNG");
    QImage output = image;
    if (format == QByteArrayLiteral("JPG")) {
        output = image.convertToFormat(QImage::Format_RGB888);
    }

    QImageWriter writer(path, format);
    writer.setQuality(format == QByteArrayLiteral("JPG") ? 95 : 100);
    if (!writer.write(output)) {
        QMessageBox::warning(this, QStringLiteral("Save Annotated Slide"), writer.errorString());
    }
}
