#include "AudienceWindow.hpp"

#include "ui/FontAwesome.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageWriter>
#include <QKeyEvent>
#include <QLabel>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <QtMath>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <utility>

Q_LOGGING_CATEGORY(logUi, "ui")

namespace {
constexpr int maxAudienceSlides = 4;
constexpr int deckOverviewColumns = 6;
constexpr int deckOverviewFooterHeight = 28;
constexpr int deckOverviewScrollTrackWidth = 5;
constexpr int deckOverviewScrollbarGutter = 22;
constexpr qreal pointerLogicalSize = 46.0;
const QColor menuIconColor(0xcc, 0xcc, 0xcc);

QRectF imageSourceRect(const QImage& image) {
    return QRectF(QPointF(0.0, 0.0), QSizeF(image.size()));
}

QRect centeredRectForImage(const QSize& imageSize, const QRect& bounds) {
    if (!imageSize.isValid() || !bounds.isValid()) {
        return {};
    }

    const QSize scaledSize = imageSize.scaled(bounds.size(), Qt::KeepAspectRatio);
    return QRect(
        bounds.x() + (bounds.width() - scaledSize.width()) / 2,
        bounds.y() + (bounds.height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height());
}

QIcon menuIcon(const QString& name, QColor color = menuIconColor, QSize size = QSize(18, 18)) {
    return FontAwesome::icon(FontAwesome::Style::Solid, name, color, size);
}

QIcon colorSwatchIcon(const QColor& color) {
    QPixmap pixmap(26, 26);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor borderColor = color.lightness() < 45 ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x44, 0x44, 0x44);
    painter.setPen(QPen(borderColor, 1.0));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(3.0, 3.0, 20.0, 20.0));
    return QIcon(pixmap);
}

void clearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout);
        }
        delete item;
    }
}

class EraserSizePreview final : public QWidget {
public:
    explicit EraserSizePreview(QWidget* parent = nullptr)
        : QWidget(parent) {
        setObjectName(QStringLiteral("featureEraserPreview"));
        setFixedSize(78, 78);
    }

    void setDiameter(int diameter) {
        m_diameter = diameter;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), Qt::transparent);

        const qreal diameter = qreal(std::clamp(m_diameter, 4, 64));
        const QPointF center(width() / 2.0, height() / 2.0);
        const QRectF circle(
            center.x() - diameter / 2.0,
            center.y() - diameter / 2.0,
            diameter,
            diameter);

        painter.setPen(QPen(QColor(0xff, 0xff, 0xff), 1.5));
        painter.setBrush(QColor(0x00, 0x8c, 0x8c, 80));
        painter.drawEllipse(circle);
    }

private:
    int m_diameter = 24;
};

}

class AudienceWindow::FeatureMenuPanel final : public QWidget {
public:
    explicit FeatureMenuPanel(AudienceWindow* audience)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint),
          m_audience(audience) {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setAttribute(Qt::WA_DeleteOnClose, true);
        qApp->installEventFilter(this);
        buildUi();
    }

    ~FeatureMenuPanel() override {
        qApp->removeEventFilter(this);
    }

    void popupAt(const QPoint& globalPosition) {
        m_anchorGlobalPosition = globalPosition;
        resizeToContents();
        move(boundedPopupPosition(globalPosition));
        show();
        raise();
        activateWindow();
    }

private:
    void buildUi() {
        setObjectName(QStringLiteral("featureMenuPanel"));
        setMinimumWidth(560);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(14, 14, 14, 14);
        rootLayout->setSpacing(10);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        auto* toolRow = new QHBoxLayout;
        toolRow->setContentsMargins(0, 0, 0, 0);
        toolRow->setSpacing(14);
        m_cursorButton = createToolButton(QStringLiteral("Classic\npointer"), QStringLiteral("arrow-pointer"), InteractionTool::Cursor);
        m_pointerButton = createToolButton(QStringLiteral("Laser\npointer"), QStringLiteral("location-crosshairs"), InteractionTool::Pointer);
        m_penButton = createToolButton(QStringLiteral("Pencil"), QStringLiteral("pencil"), InteractionTool::Pen);
        m_eraserButton = createToolButton(QStringLiteral("Eraser"), QStringLiteral("eraser"), InteractionTool::Eraser);
        toolRow->addStretch(1);
        toolRow->addWidget(m_cursorButton, 0, Qt::AlignCenter);
        toolRow->addWidget(m_pointerButton, 0, Qt::AlignCenter);
        toolRow->addWidget(m_penButton, 0, Qt::AlignCenter);
        toolRow->addWidget(m_eraserButton, 0, Qt::AlignCenter);
        toolRow->addStretch(1);
        rootLayout->addLayout(toolRow);

        m_settingsFrame = new QFrame(this);
        m_settingsFrame->setObjectName(QStringLiteral("featureSettingsRow"));
        m_settingsFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        m_settingsLayout = new QVBoxLayout(m_settingsFrame);
        m_settingsLayout->setContentsMargins(12, 10, 12, 10);
        m_settingsLayout->setSpacing(8);
        rootLayout->addWidget(m_settingsFrame);

        auto* bottomRow = new QHBoxLayout;
        bottomRow->setContentsMargins(0, 0, 0, 0);
        bottomRow->setSpacing(10);

        QToolButton* clearButton = createBottomButton(QStringLiteral("Clear"), QStringLiteral("trash-can"), QStringLiteral("featureDangerButton"), 92);
        QToolButton* closeButton = createBottomButton(QStringLiteral("Close slideshow"), QStringLiteral("door-open"), QStringLiteral("featureCloseButton"), 148);
        QToolButton* firstButton = createIconBottomButton(QStringLiteral("First slide"), QStringLiteral("angles-left"), QStringLiteral("featureNavButton"));
        QToolButton* previousButton = createIconBottomButton(QStringLiteral("Previous slide"), QStringLiteral("chevron-left"), QStringLiteral("featureNavButton"));
        m_gridButton = createIconBottomButton(QStringLiteral("Show slide grid"), QStringLiteral("table-cells-large"), QStringLiteral("featureNavButton"));
        m_gridButton->installEventFilter(this);
        QToolButton* nextButton = createIconBottomButton(QStringLiteral("Next slide"), QStringLiteral("chevron-right"), QStringLiteral("featureNavButton"));
        QToolButton* lastButton = createIconBottomButton(QStringLiteral("Last slide"), QStringLiteral("angles-right"), QStringLiteral("featureNavButton"));

        connect(clearButton, &QToolButton::clicked, this, [this] {
            confirmClearAnnotations();
        });
        connect(closeButton, &QToolButton::clicked, this, [this] {
            AudienceWindow* audience = m_audience;
            closeMenu();
            audience->exitFullscreen();
        });
        connect(firstButton, &QToolButton::clicked, this, [this] {
            emit m_audience->firstRequested();
        });
        connect(previousButton, &QToolButton::clicked, this, [this] {
            emit m_audience->previousRequested();
        });
        connect(nextButton, &QToolButton::clicked, this, [this] {
            emit m_audience->nextRequested();
        });
        connect(lastButton, &QToolButton::clicked, this, [this] {
            emit m_audience->lastRequested();
        });

        bottomRow->addWidget(clearButton);
        bottomRow->addWidget(closeButton);
        bottomRow->addStretch(1);
        bottomRow->addWidget(firstButton);
        bottomRow->addWidget(previousButton);
        bottomRow->addWidget(m_gridButton);
        bottomRow->addWidget(nextButton);
        bottomRow->addWidget(lastButton);
        rootLayout->addLayout(bottomRow);

        refreshToolButtons();
        rebuildSettings();
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress && m_gridButton) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton
                && gridButtonGlobalRect().contains(mouseEvent->globalPosition().toPoint())) {
                openDeckOverview();
                event->accept();
                return true;
            }
        }

        if (watched == m_gridButton && event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return
                || keyEvent->key() == Qt::Key_Enter
                || keyEvent->key() == Qt::Key_Space) {
                openDeckOverview();
                event->accept();
                return true;
            }
        }

        return QWidget::eventFilter(watched, event);
    }

    QToolButton* createToolButton(const QString& text, const QString& iconName, InteractionTool tool) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("featureToolButton"));
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(menuIcon(iconName, menuIconColor, QSize(32, 32)));
        button->setIconSize(QSize(32, 32));
        button->setText(text);
        button->setFixedSize(118, 90);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(button, &QToolButton::clicked, this, [this, tool] {
            selectTool(tool);
        });
        return button;
    }

    QToolButton* createBottomButton(const QString& text, const QString& iconName, const QString& objectName, int minimumWidth) {
        auto* button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(menuIcon(iconName, QColor(0xff, 0xff, 0xff), QSize(18, 18)));
        button->setIconSize(QSize(18, 18));
        button->setText(text);
        button->setMinimumSize(minimumWidth, 38);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    }

    QToolButton* createIconBottomButton(const QString& toolTip, const QString& iconName, const QString& objectName) {
        auto* button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(menuIcon(iconName, QColor(0xff, 0xff, 0xff), QSize(20, 20)));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(toolTip);
        button->setFixedSize(42, 38);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    }

    QToolButton* createColorButton(const QColor& color, bool checked, const QString& label) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("featureSwatchButton"));
        button->setCheckable(true);
        button->setChecked(checked);
        button->setIcon(colorSwatchIcon(color));
        button->setIconSize(QSize(26, 26));
        button->setToolTip(label);
        button->setFixedSize(40, 38);
        return button;
    }

    QToolButton* createSizeButton(int size, bool checked) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("featureSizeButton"));
        button->setCheckable(true);
        button->setChecked(checked);
        button->setText(QString::number(size));
        button->setFixedSize(42, 36);
        return button;
    }

    QLabel* createSettingsLabel(const QString& text) {
        auto* label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("featureSettingsLabel"));
        label->setAlignment(Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return label;
    }

    QHBoxLayout* createSettingsRow() {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        return row;
    }

    void selectTool(InteractionTool tool) {
        switch (tool) {
        case InteractionTool::Cursor:
            m_audience->setCursorTool();
            break;
        case InteractionTool::Pointer:
            m_audience->setPointerTool();
            break;
        case InteractionTool::Pen:
            m_audience->setPenTool();
            break;
        case InteractionTool::Eraser:
            m_audience->setEraserTool();
            break;
        }

        refreshToolButtons();
        rebuildSettings();
    }

    void refreshToolButtons() {
        m_cursorButton->setChecked(m_audience->m_interactionTool == InteractionTool::Cursor);
        m_pointerButton->setChecked(m_audience->m_interactionTool == InteractionTool::Pointer);
        m_penButton->setChecked(m_audience->m_interactionTool == InteractionTool::Pen);
        m_eraserButton->setChecked(m_audience->m_interactionTool == InteractionTool::Eraser);
    }

    void rebuildSettings() {
        clearLayout(m_settingsLayout);

        switch (m_audience->m_interactionTool) {
        case InteractionTool::Cursor:
            if (auto* row = createSettingsRow()) {
                row->addWidget(createSettingsLabel(QStringLiteral("No settings for classic pointer")), 0, Qt::AlignVCenter);
                row->addStretch(1);
                m_settingsLayout->addLayout(row);
            }
            break;
        case InteractionTool::Pointer:
            addPointerSettings();
            break;
        case InteractionTool::Pen:
            addPenSettings();
            break;
        case InteractionTool::Eraser:
            addEraserSettings();
            break;
        }

        resizeMenuToContents();
    }

    void addPointerSettings() {
        auto* row = createSettingsRow();
        row->addWidget(createSettingsLabel(QStringLiteral("Pointer color")), 0, Qt::AlignVCenter);
        const std::array<QPair<QString, QColor>, 3> colors{{
            {QStringLiteral("Bright red"), QColor(255, 36, 36)},
            {QStringLiteral("Bright green"), QColor(28, 255, 83)},
            {QStringLiteral("Bright purple"), QColor(190, 82, 255)}
        }};
        for (const auto& colorChoice : colors) {
            QToolButton* button = createColorButton(colorChoice.second, colorChoice.second == m_audience->m_pointerColor, colorChoice.first);
            connect(button, &QToolButton::clicked, this, [this, color = colorChoice.second] {
                m_audience->setPointerColor(color);
                rebuildSettings();
            });
            row->addWidget(button, 0, Qt::AlignVCenter);
        }
        row->addStretch(1);
        m_settingsLayout->addLayout(row);
    }

    void addPenSettings() {
        auto* colorRow = createSettingsRow();
        colorRow->addWidget(createSettingsLabel(QStringLiteral("Ink")), 0, Qt::AlignTop);

        auto* colorGrid = new QGridLayout;
        colorGrid->setContentsMargins(0, 0, 0, 0);
        colorGrid->setHorizontalSpacing(8);
        colorGrid->setVerticalSpacing(6);

        const std::array<QPair<QString, QColor>, 14> colors{{
            {QStringLiteral("#a6cee3"), QColor(0xa6, 0xce, 0xe3)},
            {QStringLiteral("#1f78b4"), QColor(0x1f, 0x78, 0xb4)},
            {QStringLiteral("#b2df8a"), QColor(0xb2, 0xdf, 0x8a)},
            {QStringLiteral("#33a02c"), QColor(0x33, 0xa0, 0x2c)},
            {QStringLiteral("#fb9a99"), QColor(0xfb, 0x9a, 0x99)},
            {QStringLiteral("#e31a1c"), QColor(0xe3, 0x1a, 0x1c)},
            {QStringLiteral("#fdbf6f"), QColor(0xfd, 0xbf, 0x6f)},
            {QStringLiteral("#ff7f00"), QColor(0xff, 0x7f, 0x00)},
            {QStringLiteral("#cab2d6"), QColor(0xca, 0xb2, 0xd6)},
            {QStringLiteral("#6a3d9a"), QColor(0x6a, 0x3d, 0x9a)},
            {QStringLiteral("#ffff99"), QColor(0xff, 0xff, 0x99)},
            {QStringLiteral("#b15928"), QColor(0xb1, 0x59, 0x28)},
            {QStringLiteral("#000000"), QColor(0x00, 0x00, 0x00)},
            {QStringLiteral("#ffffff"), QColor(0xff, 0xff, 0xff)}
        }};
        for (qsizetype index = 0; index < qsizetype(colors.size()); ++index) {
            const auto& colorChoice = colors[std::size_t(index)];
            QToolButton* button = createColorButton(colorChoice.second, colorChoice.second == m_audience->m_annotationColor, colorChoice.first);
            connect(button, &QToolButton::clicked, this, [this, color = colorChoice.second] {
                m_audience->setAnnotationColor(color);
                rebuildSettings();
            });
            colorGrid->addWidget(button, int(index / 7), int(index % 7), Qt::AlignCenter);
        }
        colorRow->addLayout(colorGrid);
        colorRow->addStretch(1);
        m_settingsLayout->addLayout(colorRow);

        auto* row = createSettingsRow();
        row->addWidget(createSettingsLabel(QStringLiteral("Size")), 0, Qt::AlignVCenter);
        for (const int thickness : {3, 6, 10, 16, 24}) {
            QToolButton* button = createSizeButton(thickness, thickness == m_audience->m_annotationThickness);
            connect(button, &QToolButton::clicked, this, [this, thickness] {
                m_audience->setAnnotationThickness(thickness);
                rebuildSettings();
            });
            row->addWidget(button, 0, Qt::AlignVCenter);
        }
        row->addStretch(1);
        m_settingsLayout->addLayout(row);
    }

    void addEraserSettings() {
        auto* sliderRow = createSettingsRow();
        auto* preview = new EraserSizePreview(this);
        preview->setDiameter(m_audience->m_eraserThickness);
        sliderRow->addWidget(preview, 0, Qt::AlignVCenter);

        auto* sizeColumn = new QVBoxLayout;
        sizeColumn->setContentsMargins(0, 0, 0, 0);
        sizeColumn->setSpacing(4);
        auto* label = createSettingsLabel(QStringLiteral("Eraser size: %1 px").arg(m_audience->m_eraserThickness));
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("featureSizeSlider"));
        slider->setMinimumWidth(340);
        slider->setRange(4, 64);
        slider->setSingleStep(2);
        slider->setPageStep(8);
        slider->setValue(m_audience->m_eraserThickness);
        connect(slider, &QSlider::valueChanged, this, [this, preview, label](int value) {
            m_audience->setEraserThickness(value);
            preview->setDiameter(value);
            label->setText(QStringLiteral("Eraser size: %1 px").arg(value));
        });
        sizeColumn->addWidget(label);
        sizeColumn->addWidget(slider);
        sliderRow->addLayout(sizeColumn, 1);
        m_settingsLayout->addLayout(sliderRow);

        auto* presetRow = createSettingsRow();
        presetRow->addWidget(createSettingsLabel(QStringLiteral("Presets")), 0, Qt::AlignVCenter);
        for (const int thickness : {8, 16, 24, 36, 48, 64}) {
            QToolButton* button = createSizeButton(thickness, thickness == m_audience->m_eraserThickness);
            connect(button, &QToolButton::clicked, this, [this, thickness] {
                m_audience->setEraserThickness(thickness);
                rebuildSettings();
            });
            presetRow->addWidget(button, 0, Qt::AlignVCenter);
        }
        presetRow->addStretch(1);
        m_settingsLayout->addLayout(presetRow);
    }

    void resizeMenuToContents() {
        invalidateMenuLayout();
        resizeToContents();
        if (isVisible()) {
            move(boundedPopupPosition(m_anchorGlobalPosition));
            QTimer::singleShot(0, this, [this] {
                if (!isVisible()) {
                    return;
                }
                invalidateMenuLayout();
                resizeToContents();
                move(boundedPopupPosition(m_anchorGlobalPosition));
            });
        }
    }

    void confirmClearAnnotations() {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Clear Writings"));
        box.setText(QStringLiteral("Clear all writings on this slide?"));
        box.setIcon(QMessageBox::Warning);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() == QMessageBox::Yes) {
            m_audience->clearAnnotations();
        }
    }

    void closeMenu() {
        close();
    }

    void openDeckOverview() {
        if (m_openingDeckOverview) {
            return;
        }

        m_openingDeckOverview = true;
        QPointer<AudienceWindow> audience = m_audience;
        qApp->removeEventFilter(this);
        hide();
        QTimer::singleShot(0, this, [this] {
            closeMenu();
        });
        QTimer::singleShot(0, audience, [audience] {
            if (audience) {
                audience->enterDeckOverview();
            }
        });
    }

    QRect gridButtonGlobalRect() const {
        if (!m_gridButton) {
            return {};
        }

        return QRect(m_gridButton->mapToGlobal(QPoint(0, 0)), m_gridButton->size());
    }

    void resizeToContents() {
        if (QLayout* rootLayout = layout()) {
            rootLayout->activate();
            const QSize targetSize = rootLayout->totalSizeHint()
                .expandedTo(rootLayout->totalMinimumSize())
                .expandedTo(minimumSize());
            resize(targetSize);
            updateGeometry();
            return;
        }
        resize(sizeHint().expandedTo(minimumSizeHint()).expandedTo(minimumSize()));
    }

    void invalidateMenuLayout() {
        if (m_settingsLayout) {
            m_settingsLayout->invalidate();
        }
        if (m_settingsFrame) {
            m_settingsFrame->updateGeometry();
            if (QLayout* settingsFrameLayout = m_settingsFrame->layout()) {
                settingsFrameLayout->invalidate();
                settingsFrameLayout->activate();
            }
        }
        updateGeometry();
        if (QLayout* rootLayout = layout()) {
            rootLayout->invalidate();
            rootLayout->activate();
        }
    }

    QPoint boundedPopupPosition(const QPoint& requestedPosition) const {
        QRect availableGeometry;
        if (QScreen* screenAtPosition = QGuiApplication::screenAt(requestedPosition)) {
            availableGeometry = screenAtPosition->availableGeometry();
        } else if (m_audience && m_audience->windowHandle() && m_audience->windowHandle()->screen()) {
            availableGeometry = m_audience->windowHandle()->screen()->availableGeometry();
        }

        if (!availableGeometry.isValid()) {
            return requestedPosition;
        }

        const QSize panelSize = size().expandedTo(sizeHint()).expandedTo(minimumSizeHint());
        QPoint position = requestedPosition;
        if (position.x() + panelSize.width() > availableGeometry.right()) {
            position.setX(availableGeometry.right() - panelSize.width());
        }
        if (position.y() + panelSize.height() > availableGeometry.bottom()) {
            position.setY(availableGeometry.bottom() - panelSize.height());
        }
        position.setX(std::max(position.x(), availableGeometry.left()));
        position.setY(std::max(position.y(), availableGeometry.top()));
        return position;
    }

    AudienceWindow* m_audience = nullptr;
    QPoint m_anchorGlobalPosition;
    QToolButton* m_cursorButton = nullptr;
    QToolButton* m_pointerButton = nullptr;
    QToolButton* m_penButton = nullptr;
    QToolButton* m_eraserButton = nullptr;
    QToolButton* m_gridButton = nullptr;
    QFrame* m_settingsFrame = nullptr;
    QVBoxLayout* m_settingsLayout = nullptr;
    bool m_openingDeckOverview = false;
};

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

void AudienceWindow::setDocumentOverview(int pageCount, int currentPage) {
    const bool pageCountChanged = m_pageCount != std::max(0, pageCount);
    m_pageCount = std::max(0, pageCount);
    m_currentPageIndex = currentPage >= 0 && currentPage < m_pageCount ? currentPage : -1;
    if (m_pageCount == 0) {
        m_deckOverviewImages.clear();
        m_deckOverviewImageSize = {};
        m_deckOverviewVisible = false;
        m_deckOverviewScrollY = 0;
    }

    if (m_deckOverviewVisible) {
        m_deckOverviewScrollY = std::clamp(m_deckOverviewScrollY, 0, deckOverviewMaxScrollY());
        if (pageCountChanged) {
            emit deckOverviewRendersRequested(deckOverviewThumbnailBoundingPixelSize(), m_currentPageIndex);
        }
        update();
    }
}

void AudienceWindow::setDeckOverviewSlideImage(int pageIndex, const QSize& boundingPixelSize, const QImage& image) {
    if (pageIndex < 0 || pageIndex >= m_pageCount || !boundingPixelSize.isValid()) {
        return;
    }

    if (m_deckOverviewImageSize.isValid() && m_deckOverviewImageSize != boundingPixelSize) {
        m_deckOverviewImages.clear();
    }
    m_deckOverviewImageSize = boundingPixelSize;

    if (image.isNull()) {
        m_deckOverviewImages.remove(pageIndex);
    } else {
        m_deckOverviewImages.insert(pageIndex, image);
    }

    if (m_deckOverviewVisible) {
        update();
    }
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
    m_deckOverviewVisible = false;
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
    m_eraserCursorVisible = false;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setPointerTool() {
    m_interactionTool = InteractionTool::Pointer;
    m_eraserCursorVisible = false;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setPenTool() {
    m_interactionTool = InteractionTool::Pen;
    m_pointerVisible = false;
    m_eraserCursorVisible = false;
    m_isAnnotating = false;
    updateCursorAppearance();
    update();
}

void AudienceWindow::setEraserTool() {
    m_interactionTool = InteractionTool::Eraser;
    m_pointerVisible = false;
    m_eraserCursorVisible = false;
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

void AudienceWindow::setEraserThickness(int thickness) {
    m_eraserThickness = std::clamp(thickness, 4, 64);
    update();
}

void AudienceWindow::clearAnnotations() {
    if (!m_currentTextureKey.isEmpty() && m_annotationImages.contains(m_currentTextureKey)) {
        m_annotationImages[m_currentTextureKey].fill(Qt::transparent);
        emit annotationOverlayChanged(currentAnnotationOverlayImage());
        update();
    }
}

void AudienceWindow::clearAnnotationOverlayForPage(int pageIndex) {
    if (pageIndex < 0) {
        return;
    }

    QStringList keysToRemove;
    for (auto it = m_annotationImages.constBegin(); it != m_annotationImages.constEnd(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }

        bool ok = false;
        const int keyPageIndex = parts.at(1).toInt(&ok);
        if (ok && keyPageIndex == pageIndex) {
            keysToRemove.push_back(it.key());
        }
    }

    if (keysToRemove.isEmpty()) {
        return;
    }

    for (const QString& key : keysToRemove) {
        m_annotationImages.remove(key);
    }

    emit annotationOverlayChanged(currentAnnotationOverlayImage());
    update();
}

void AudienceWindow::clearAllAnnotationOverlays() {
    if (m_annotationImages.isEmpty()) {
        return;
    }

    m_annotationImages.clear();
    emit annotationOverlayChanged(currentAnnotationOverlayImage());
    update();
}

void AudienceWindow::setAnnotationOverlaysByTextureKey(const QHash<QString, QImage>& overlays) {
    m_annotationImages = overlays;
    emit annotationOverlayChanged(currentAnnotationOverlayImage());
    update();
}

QHash<int, QImage> AudienceWindow::annotationOverlaysByPage() const {
    QHash<int, QImage> overlays;
    for (auto it = m_annotationImages.constBegin(); it != m_annotationImages.constEnd(); ++it) {
        if (it.value().isNull()) {
            continue;
        }

        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }

        bool ok = false;
        const int pageIndex = parts.at(1).toInt(&ok);
        if (ok && pageIndex >= 0) {
            overlays.insert(pageIndex, it.value());
        }
    }

    return overlays;
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
    m_deckOverviewVisible = false;
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

    if (m_blankMode != BlankMode::None) {
        return;
    }

    if (m_deckOverviewVisible) {
        drawDeckOverview(painter);
        return;
    }

    if (m_currentSlideImage.isNull()) {
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
    drawEraserCursor(painter);
}

void AudienceWindow::keyPressEvent(QKeyEvent* event) {
    if (m_deckOverviewVisible) {
        switch (event->key()) {
        case Qt::Key_Escape:
            exitDeckOverview();
            event->accept();
            return;
        case Qt::Key_Up:
            scrollDeckOverviewBy(-80);
            event->accept();
            return;
        case Qt::Key_Down:
            scrollDeckOverviewBy(80);
            event->accept();
            return;
        case Qt::Key_PageUp:
            scrollDeckOverviewBy(-deckOverviewViewportRect().height());
            event->accept();
            return;
        case Qt::Key_PageDown:
            scrollDeckOverviewBy(deckOverviewViewportRect().height());
            event->accept();
            return;
        case Qt::Key_Home:
            m_deckOverviewScrollY = 0;
            update();
            event->accept();
            return;
        case Qt::Key_End:
            m_deckOverviewScrollY = deckOverviewMaxScrollY();
            update();
            event->accept();
            return;
        default:
            break;
        }
    }

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
    m_eraserCursorVisible = false;
    m_isAnnotating = false;
    update();
    QWidget::leaveEvent(event);
}

void AudienceWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_deckOverviewVisible) {
        m_cursorHideTimer.stop();
        setCursor(QCursor(Qt::ArrowCursor));
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_interactionTool == InteractionTool::Pointer) {
        m_pointerPosition = event->position();
        m_pointerVisible = true;
        update();
        event->accept();
        return;
    }

    if (m_interactionTool == InteractionTool::Eraser) {
        m_eraserCursorPosition = event->position();
        m_eraserCursorVisible = true;
        if (m_isAnnotating && (event->buttons() & Qt::LeftButton)) {
            drawAnnotationSegment(m_lastAnnotationPoint, event->position());
            m_lastAnnotationPoint = event->position();
        } else {
            update();
        }
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
    if (m_deckOverviewVisible && event->button() == Qt::LeftButton) {
        const int pageIndex = deckOverviewPageAt(event->position().toPoint());
        if (pageIndex >= 0) {
            exitDeckOverview();
            emit pageRequested(pageIndex);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
        && (m_interactionTool == InteractionTool::Pen || m_interactionTool == InteractionTool::Eraser)) {
        m_isAnnotating = true;
        m_lastAnnotationPoint = event->position();
        if (m_interactionTool == InteractionTool::Eraser) {
            m_eraserCursorPosition = event->position();
            m_eraserCursorVisible = true;
        }
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
        update();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void AudienceWindow::wheelEvent(QWheelEvent* event) {
    if (m_deckOverviewVisible) {
        const QPoint angleDelta = event->angleDelta();
        const QPoint pixelDelta = event->pixelDelta();
        int deltaY = pixelDelta.y();
        if (deltaY == 0) {
            deltaY = angleDelta.y() / 2;
        }
        scrollDeckOverviewBy(-deltaY);
        event->accept();
        return;
    }

    const int angleDeltaY = event->angleDelta().y();
    const int pixelDeltaY = event->pixelDelta().y();
    const int deltaY = angleDeltaY != 0 ? angleDeltaY : pixelDeltaY;
    const int threshold = angleDeltaY != 0 ? 120 : 80;
    if (deltaY != 0 && m_pageCount > 0 && m_blankMode == BlankMode::None) {
        if ((deltaY > 0 && m_slideWheelRemainderY < 0)
            || (deltaY < 0 && m_slideWheelRemainderY > 0)) {
            m_slideWheelRemainderY = 0;
        }

        m_slideWheelRemainderY += deltaY;
        if (std::abs(m_slideWheelRemainderY) >= threshold) {
            if (m_slideWheelRemainderY > 0) {
                if (m_currentPageIndex > 0) {
                    emit previousRequested();
                }
            } else {
                if (m_currentPageIndex < m_pageCount - 1) {
                    emit nextRequested();
                }
            }
            m_slideWheelRemainderY = 0;
        }

        showCursorTemporarily();
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
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
        setCursor(QCursor(Qt::CrossCursor));
        break;
    case InteractionTool::Eraser:
        setCursor(QCursor(Qt::BlankCursor));
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
    QPen pen(
        m_annotationColor,
        m_interactionTool == InteractionTool::Eraser ? m_eraserThickness : m_annotationThickness,
        Qt::SolidLine,
        Qt::RoundCap,
        Qt::RoundJoin);
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

void AudienceWindow::drawEraserCursor(QPainter& painter) const {
    if (!m_eraserCursorVisible || m_interactionTool != InteractionTool::Eraser || m_currentSlideImage.isNull()) {
        return;
    }

    const qreal diameter = eraserLogicalDiameter();
    const qreal radius = diameter / 2.0;
    const QRectF circle(
        m_eraserCursorPosition.x() - radius,
        m_eraserCursorPosition.y() - radius,
        diameter,
        diameter);

    QColor fill(0x00, 0x8c, 0x8c, 36);
    QColor outline(Qt::white);
    outline.setAlpha(230);
    QColor shadow(Qt::black);
    shadow.setAlpha(150);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(fill);
    painter.setPen(QPen(shadow, 3.0));
    painter.drawEllipse(circle);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(outline, 1.5));
    painter.drawEllipse(circle);
    painter.restore();
}

qreal AudienceWindow::eraserLogicalDiameter() const {
    if (m_currentSlideImage.isNull() || !m_currentSlideImage.size().isValid()) {
        return qreal(m_eraserThickness);
    }

    const QRectF slideRect = slideLogicalRect(m_currentSlideImage.size());
    if (!slideRect.isValid() || slideRect.width() <= 0.0 || m_currentSlideImage.width() <= 0) {
        return qreal(m_eraserThickness);
    }

    const qreal slidePixelScale = slideRect.width() / qreal(m_currentSlideImage.width());
    return std::max<qreal>(1.0, qreal(m_eraserThickness) * slidePixelScale);
}

void AudienceWindow::enterDeckOverview() {
    if (m_pageCount <= 0) {
        return;
    }

    m_deckOverviewVisible = true;
    m_blankMode = BlankMode::None;
    m_isAnnotating = false;
    m_pointerVisible = false;
    m_eraserCursorVisible = false;
    m_cursorHideTimer.stop();
    setCursor(QCursor(Qt::ArrowCursor));

    const QRect viewport = deckOverviewViewportRect();
    const QSize slideBounds = deckOverviewThumbnailBoundingPixelSize();
    const int tileHeight = slideBounds.height() + deckOverviewFooterHeight;
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int row = std::max(0, m_currentPageIndex) / deckOverviewColumns;
    const int focusedCenterY = row * (tileHeight + verticalSpacing) + tileHeight / 2;
    m_deckOverviewScrollY = std::clamp(focusedCenterY - viewport.height() / 2, 0, deckOverviewMaxScrollY());

    emit deckOverviewRendersRequested(slideBounds, m_currentPageIndex);
    update();
}

void AudienceWindow::exitDeckOverview() {
    if (!m_deckOverviewVisible) {
        return;
    }

    m_deckOverviewVisible = false;
    m_deckOverviewScrollY = 0;
    updateCursorAppearance();
    update();
}

void AudienceWindow::drawDeckOverview(QPainter& painter) {
    const QRect viewport = deckOverviewViewportRect();
    const QRect gridViewport = viewport.adjusted(0, 0, -deckOverviewScrollbarGutter, 0);
    const QSize slideBounds = deckOverviewThumbnailBoundingPixelSize();
    const int horizontalSpacing = std::max(14, gridViewport.width() / 70);
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int tileWidth = slideBounds.width();
    const int tileHeight = slideBounds.height() + deckOverviewFooterHeight;
    const int gridWidth = deckOverviewColumns * tileWidth + (deckOverviewColumns - 1) * horizontalSpacing;
    const int left = gridViewport.left() + std::max(0, (gridViewport.width() - gridWidth) / 2);
    m_deckOverviewScrollY = std::clamp(m_deckOverviewScrollY, 0, deckOverviewMaxScrollY());
    const int top = viewport.top() - m_deckOverviewScrollY;

    painter.save();
    painter.fillRect(rect(), QColor(0x12, 0x12, 0x12));
    painter.setClipRect(viewport);

    for (int page = 0; page < m_pageCount; ++page) {
        const int row = page / deckOverviewColumns;
        const int column = page % deckOverviewColumns;
        const QRect tileRect(
            left + column * (tileWidth + horizontalSpacing),
            top + row * (tileHeight + verticalSpacing),
            tileWidth,
            tileHeight);
        if (!tileRect.intersects(viewport)) {
            continue;
        }

        const QRect slideFrame(tileRect.left(), tileRect.top(), tileRect.width(), slideBounds.height());
        painter.fillRect(slideFrame, QColor(0x08, 0x08, 0x08));

        const QImage image = m_deckOverviewImages.value(page);
        if (!image.isNull()) {
            const QRect imageRect = centeredRectForImage(image.size(), slideFrame.adjusted(1, 1, -1, -1));
            painter.drawImage(imageRect, image);
        } else {
            painter.setPen(QColor(0x8a, 0x8a, 0x8a));
            painter.drawText(slideFrame, Qt::AlignCenter, QStringLiteral("..."));
        }

        const bool isSelected = page == m_currentPageIndex;
        painter.setPen(QPen(isSelected ? QColor(0x00, 0xb3, 0xb3) : QColor(0x3a, 0x3a, 0x3a), isSelected ? 4 : 1));
        painter.drawRect(slideFrame.adjusted(0, 0, -1, -1));

        QRect numberRect(tileRect.left(), slideFrame.bottom() + 5, tileRect.width() - 2, deckOverviewFooterHeight - 5);
        painter.setPen(isSelected ? QColor(0xff, 0xff, 0xff) : QColor(0xb0, 0xb0, 0xb0));
        painter.drawText(numberRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(page + 1));
    }

    if (deckOverviewMaxScrollY() > 0) {
        const QRect track(viewport.right() - deckOverviewScrollTrackWidth, viewport.top(), deckOverviewScrollTrackWidth, viewport.height());
        const qreal visibleFraction = qreal(viewport.height()) / qreal(deckOverviewContentHeight());
        const int handleHeight = std::max(36, int(track.height() * visibleFraction));
        const int handleY = track.top() + int(qreal(track.height() - handleHeight) * qreal(m_deckOverviewScrollY) / qreal(deckOverviewMaxScrollY()));
        painter.fillRect(track, QColor(0x2c, 0x2c, 0x2c));
        painter.fillRect(QRect(track.left(), handleY, deckOverviewScrollTrackWidth, handleHeight), QColor(0x00, 0xb3, 0xb3));
    }

    painter.restore();
}

QSize AudienceWindow::deckOverviewThumbnailBoundingPixelSize() const {
    const QRect viewport = deckOverviewViewportRect().adjusted(0, 0, -deckOverviewScrollbarGutter, 0);
    const int horizontalSpacing = std::max(14, viewport.width() / 70);
    const int availableWidth = std::max(0, viewport.width() - (deckOverviewColumns - 1) * horizontalSpacing);
    const int slideWidth = std::max(96, availableWidth / deckOverviewColumns);
    const int slideHeight = std::max(54, int(qreal(slideWidth) * 9.0 / 16.0));
    return QSize(slideWidth, slideHeight);
}

QRect AudienceWindow::deckOverviewViewportRect() const {
    const int horizontalMargin = std::clamp(width() / 28, 28, 72);
    const int verticalMargin = std::clamp(height() / 24, 24, 64);
    return rect().adjusted(horizontalMargin, verticalMargin, -horizontalMargin, -verticalMargin);
}

int AudienceWindow::deckOverviewContentHeight() const {
    if (m_pageCount <= 0) {
        return 0;
    }

    const QRect viewport = deckOverviewViewportRect();
    const QSize slideBounds = deckOverviewThumbnailBoundingPixelSize();
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int rows = (m_pageCount + deckOverviewColumns - 1) / deckOverviewColumns;
    return rows * (slideBounds.height() + deckOverviewFooterHeight) + std::max(0, rows - 1) * verticalSpacing;
}

int AudienceWindow::deckOverviewMaxScrollY() const {
    return std::max(0, deckOverviewContentHeight() - deckOverviewViewportRect().height());
}

int AudienceWindow::deckOverviewPageAt(const QPoint& position) const {
    const QRect viewport = deckOverviewViewportRect();
    if (!viewport.contains(position)) {
        return -1;
    }

    const QRect gridViewport = viewport.adjusted(0, 0, -deckOverviewScrollbarGutter, 0);
    if (!gridViewport.contains(position)) {
        return -1;
    }

    const QSize slideBounds = deckOverviewThumbnailBoundingPixelSize();
    const int horizontalSpacing = std::max(14, gridViewport.width() / 70);
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int tileWidth = slideBounds.width();
    const int tileHeight = slideBounds.height() + deckOverviewFooterHeight;
    const int gridWidth = deckOverviewColumns * tileWidth + (deckOverviewColumns - 1) * horizontalSpacing;
    const int left = gridViewport.left() + std::max(0, (gridViewport.width() - gridWidth) / 2);
    const int contentX = position.x() - left;
    const int contentY = position.y() - viewport.top() + m_deckOverviewScrollY;
    if (contentX < 0 || contentY < 0) {
        return -1;
    }

    const int columnStride = tileWidth + horizontalSpacing;
    const int rowStride = tileHeight + verticalSpacing;
    const int column = contentX / columnStride;
    const int row = contentY / rowStride;
    if (column < 0 || column >= deckOverviewColumns) {
        return -1;
    }
    if (contentX % columnStride >= tileWidth || contentY % rowStride >= tileHeight) {
        return -1;
    }

    const int pageIndex = row * deckOverviewColumns + column;
    return pageIndex >= 0 && pageIndex < m_pageCount ? pageIndex : -1;
}

void AudienceWindow::scrollDeckOverviewBy(int deltaY) {
    const int nextScrollY = std::clamp(m_deckOverviewScrollY + deltaY, 0, deckOverviewMaxScrollY());
    if (nextScrollY == m_deckOverviewScrollY) {
        return;
    }

    m_deckOverviewScrollY = nextScrollY;
    update();
}

void AudienceWindow::showFeatureMenu(const QPoint& globalPosition) {
    if (m_featureMenu && m_featureMenu->isVisible()) {
        return;
    }
    if (m_featureMenu) {
        m_featureMenu->close();
        m_featureMenu = nullptr;
    }

    auto* menu = new FeatureMenuPanel(this);
    m_featureMenu = menu;
    menu->winId();
    if (QWindow* menuWindow = menu->windowHandle()) {
        if (QWindow* audienceWindow = windowHandle()) {
            menuWindow->setScreen(audienceWindow->screen());
        }
    }

    connect(menu, &QObject::destroyed, this, [this, menu] {
        if (m_featureMenu == menu) {
            m_featureMenu = nullptr;
        }
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        if (QWindow* handle = windowHandle()) {
            handle->requestActivate();
        }
        if (m_deckOverviewVisible) {
            m_cursorHideTimer.stop();
            setCursor(QCursor(Qt::ArrowCursor));
        } else {
            updateCursorAppearance();
        }
        update();
    });

    m_eraserCursorVisible = false;
    update();
    setCursor(QCursor(Qt::ArrowCursor));
    menu->popupAt(globalPosition);
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
