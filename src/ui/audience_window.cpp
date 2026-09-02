#include "audience_window.hpp"

#include "ui/bluecurve.hpp"
#include "ui/font_awesome.hpp"
#include "ui/molecule_widget.hpp"

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
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSettings>
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
constexpr int kMaxAudienceSlides = 4;
constexpr int kDeckOverviewColumns = 6;
constexpr int kDeckOverviewFooterHeight = 28;
constexpr int kDeckOverviewScrollTrackWidth = 5;
constexpr int kDeckOverviewScrollbarGutter = 22;
constexpr int kDefaultPointerSize = 25;
constexpr int kMinimumPointerSize = 12;
constexpr int kMaximumPointerSize = 120;
constexpr auto kPointerSizeSettingsKey = "audience/pointerSize";
const QColor kMenuIconColor(0xcc, 0xcc, 0xcc);

/** @brief Returns the usable source rectangle of an image. */
QRectF image_source_rect(const QImage& image) {
    return QRectF(QPointF(0.0, 0.0), QSizeF(image.size()));
}

/** @brief Centers an image size within a bounding rectangle. */
QRect centered_rect_for_image(const QSize& imageSize, const QRect& bounds) {
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

/** @brief Creates an icon for the audience feature menu. */
QIcon menu_icon(const QString& name, QColor color = kMenuIconColor, QSize size = QSize(18, 18)) {
    return font_awesome::icon(font_awesome::Style::Solid, name, color, size);
}

/** @brief Creates a circular color-swatch icon. */
QIcon color_swatch_icon(const QColor& color) {
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

/** @brief Deletes every item owned by a Qt layout. */
void clear_layout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            clear_layout(childLayout);
        }
        delete item;
    }
}

class EraserSizePreview final : public QWidget {
public:
    /** @brief Constructs an eraser-size preview widget. */
    explicit EraserSizePreview(QWidget* parent = nullptr)
        : QWidget(parent) {
        setObjectName(QStringLiteral("featureEraserPreview"));
        setFixedSize(78, 78);
    }

    /** @brief Sets the preview circle diameter. */
    void set_diameter(int diameter) {
        diameter_ = diameter;
        update();
    }

protected:
    /** @brief Paints the annotation-size preview circle. */
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), Qt::transparent);

        const qreal diameter = qreal(std::clamp(diameter_, 4, 64));
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
    int diameter_ = 24;
};

}

class AudienceWindow::FeatureMenuPanel final : public QWidget {
public:
    /** @brief Constructs the audience interaction feature menu. */
    explicit FeatureMenuPanel(AudienceWindow* audience)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint),
          audience_(audience) {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setAttribute(Qt::WA_DeleteOnClose, true);
        qApp->installEventFilter(this);
        build_ui();
    }

    /** @brief Removes the application event filter before destruction. */
    ~FeatureMenuPanel() override {
        qApp->removeEventFilter(this);
    }

    /** @brief Opens the feature menu at a global screen position. */
    void popup_at(const QPoint& global_position) {
        anchor_global_position_ = global_position;
        resize_to_contents();
        move(bounded_popup_position(global_position));
        show();
        raise();
        activateWindow();
    }

private:
    /** @brief Constructs the feature menu controls and layouts. */
    void build_ui() {
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
        cursor_button_ = create_tool_button(QStringLiteral("Classic\npointer"), QStringLiteral("arrow-pointer"), InteractionTool::Cursor);
        pointer_button_ = create_tool_button(QStringLiteral("Laser\npointer"), QStringLiteral("location-crosshairs"), InteractionTool::Pointer);
        pointer_button_->setToolTip(QStringLiteral("Laser pointer (L)"));
        pen_button_ = create_tool_button(QStringLiteral("Pencil"), QStringLiteral("pencil"), InteractionTool::Pen);
        eraser_button_ = create_tool_button(QStringLiteral("Eraser"), QStringLiteral("eraser"), InteractionTool::Eraser);
        toolRow->addStretch(1);
        toolRow->addWidget(cursor_button_, 0, Qt::AlignCenter);
        toolRow->addWidget(pointer_button_, 0, Qt::AlignCenter);
        toolRow->addWidget(pen_button_, 0, Qt::AlignCenter);
        toolRow->addWidget(eraser_button_, 0, Qt::AlignCenter);
        toolRow->addStretch(1);
        rootLayout->addLayout(toolRow);

        settings_frame_ = new QFrame(this);
        settings_frame_->setObjectName(QStringLiteral("featureSettingsRow"));
        settings_frame_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        settings_layout_ = new QVBoxLayout(settings_frame_);
        settings_layout_->setContentsMargins(12, 10, 12, 10);
        settings_layout_->setSpacing(8);
        rootLayout->addWidget(settings_frame_);

        auto* bottomRow = new QHBoxLayout;
        bottomRow->setContentsMargins(0, 0, 0, 0);
        bottomRow->setSpacing(10);

        QToolButton* clearButton = create_bottom_button(QStringLiteral("Clear"), QStringLiteral("stock-clear"), QStringLiteral("featureDangerButton"), 92);
        QToolButton* closeButton = create_bottom_button(QStringLiteral("Close slideshow"), QStringLiteral("stock-quit"), QStringLiteral("featureCloseButton"), 148);
        QToolButton* firstButton = create_icon_bottom_button(QStringLiteral("First slide"), QStringLiteral("stock-goto-first"), QStringLiteral("featureNavButton"));
        QToolButton* previousButton = create_icon_bottom_button(QStringLiteral("Previous slide"), QStringLiteral("stock-go-back"), QStringLiteral("featureNavButton"));
        grid_button_ = create_icon_bottom_button(QStringLiteral("Show slide grid (G)"), QStringLiteral("stock_display-grid"), QStringLiteral("featureNavButton"));
        grid_button_->installEventFilter(this);
        QToolButton* nextButton = create_icon_bottom_button(QStringLiteral("Next slide"), QStringLiteral("stock-go-forward"), QStringLiteral("featureNavButton"));
        QToolButton* lastButton = create_icon_bottom_button(QStringLiteral("Last slide"), QStringLiteral("stock-goto-last"), QStringLiteral("featureNavButton"));

        connect(clearButton, &QToolButton::clicked, this, [this] {
            confirm_clear_annotations();
        });
        connect(closeButton, &QToolButton::clicked, this, [this] {
            AudienceWindow* audience = audience_;
            close_menu();
            audience->exit_fullscreen();
        });
        connect(firstButton, &QToolButton::clicked, this, [this] {
            emit audience_->first_requested();
        });
        connect(previousButton, &QToolButton::clicked, this, [this] {
            emit audience_->previous_requested();
        });
        connect(nextButton, &QToolButton::clicked, this, [this] {
            emit audience_->next_requested();
        });
        connect(lastButton, &QToolButton::clicked, this, [this] {
            emit audience_->last_requested();
        });

        bottomRow->addWidget(clearButton);
        bottomRow->addWidget(closeButton);
        bottomRow->addStretch(1);
        bottomRow->addWidget(firstButton);
        bottomRow->addWidget(previousButton);
        bottomRow->addWidget(grid_button_);
        bottomRow->addWidget(nextButton);
        bottomRow->addWidget(lastButton);
        rootLayout->addLayout(bottomRow);

        refresh_tool_buttons();
        rebuild_settings();
    }

    /** @brief Handles clicks outside the feature menu and relevant window events. */
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_G) {
                open_deck_overview();
                event->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_L) {
                select_tool(InteractionTool::Pointer);
                event->accept();
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonPress && grid_button_) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton
                && grid_button_global_rect().contains(mouseEvent->globalPosition().toPoint())) {
                open_deck_overview();
                event->accept();
                return true;
            }
        }

        if (watched == grid_button_ && event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return
                || keyEvent->key() == Qt::Key_Enter
                || keyEvent->key() == Qt::Key_Space) {
                open_deck_overview();
                event->accept();
                return true;
            }
        }

        return QWidget::eventFilter(watched, event);
    }

    /** @brief Creates a selectable interaction-tool button. */
    QToolButton* create_tool_button(const QString& text, const QString& icon_name, InteractionTool tool) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("featureToolButton"));
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(menu_icon(icon_name, kMenuIconColor, QSize(32, 32)));
        button->setIconSize(QSize(32, 32));
        button->setText(text);
        button->setFixedSize(118, 90);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(button, &QToolButton::clicked, this, [this, tool] {
            select_tool(tool);
        });
        return button;
    }

    /** @brief Creates a labeled button for the menu's bottom row. */
    QToolButton* create_bottom_button(const QString& text, const QString& icon_name, const QString& object_name, int minimum_width) {
        auto* button = new QToolButton(this);
        button->setObjectName(object_name);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(bluecurve::icon(icon_name, QSize(18, 18)));
        button->setIconSize(QSize(18, 18));
        button->setText(text);
        button->setMinimumSize(minimum_width, 38);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    }

    /** @brief Creates an icon-only button for the menu's bottom row. */
    QToolButton* create_icon_bottom_button(const QString& tool_tip, const QString& icon_name, const QString& object_name) {
        auto* button = new QToolButton(this);
        button->setObjectName(object_name);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(bluecurve::icon(icon_name, QSize(20, 20)));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tool_tip);
        button->setFixedSize(42, 38);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    }

    /** @brief Creates a selectable color-swatch button. */
    QToolButton* create_color_button(const QColor& color, bool checked, const QString& label) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("featureSwatchButton"));
        button->setCheckable(true);
        button->setChecked(checked);
        button->setIcon(color_swatch_icon(color));
        button->setIconSize(QSize(26, 26));
        button->setToolTip(label);
        button->setFixedSize(40, 38);
        return button;
    }

    /** @brief Creates a selectable annotation-size button. */
    QToolButton* create_size_button(int size, bool checked) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("featureSizeButton"));
        button->setCheckable(true);
        button->setChecked(checked);
        button->setText(QString::number(size));
        button->setFixedSize(42, 36);
        return button;
    }

    /** @brief Creates a consistently styled settings label. */
    QLabel* create_settings_label(const QString& text) {
        auto* label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("featureSettingsLabel"));
        label->setAlignment(Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return label;
    }

    /** @brief Creates a consistently configured settings row. */
    QHBoxLayout* create_settings_row() {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        return row;
    }

    /** @brief Activates an interaction tool and refreshes the menu. */
    void select_tool(InteractionTool tool) {
        switch (tool) {
        case InteractionTool::Cursor:
            audience_->set_cursor_tool();
            break;
        case InteractionTool::Pointer:
            audience_->set_pointer_tool();
            break;
        case InteractionTool::Pen:
            audience_->set_pen_tool();
            break;
        case InteractionTool::Eraser:
            audience_->set_eraser_tool();
            break;
        }

        refresh_tool_buttons();
        rebuild_settings();
    }

    /** @brief Updates checked states for interaction-tool buttons. */
    void refresh_tool_buttons() {
        cursor_button_->setChecked(audience_->interaction_tool_ == InteractionTool::Cursor);
        pointer_button_->setChecked(audience_->interaction_tool_ == InteractionTool::Pointer);
        pen_button_->setChecked(audience_->interaction_tool_ == InteractionTool::Pen);
        eraser_button_->setChecked(audience_->interaction_tool_ == InteractionTool::Eraser);
    }

    /** @brief Rebuilds settings controls for the active interaction tool. */
    void rebuild_settings() {
        clear_layout(settings_layout_);

        switch (audience_->interaction_tool_) {
        case InteractionTool::Cursor:
            if (auto* row = create_settings_row()) {
                row->addWidget(create_settings_label(QStringLiteral("No settings for classic pointer")), 0, Qt::AlignVCenter);
                row->addStretch(1);
                settings_layout_->addLayout(row);
            }
            break;
        case InteractionTool::Pointer:
            add_pointer_settings();
            break;
        case InteractionTool::Pen:
            add_pen_settings();
            break;
        case InteractionTool::Eraser:
            add_eraser_settings();
            break;
        }

        resize_menu_to_contents();
    }

    /** @brief Adds pointer-specific controls to the settings panel. */
    void add_pointer_settings() {
        auto* colorRow = create_settings_row();
        colorRow->addWidget(create_settings_label(QStringLiteral("Pointer color")), 0, Qt::AlignVCenter);
        const std::array<QPair<QString, QColor>, 3> colors{{
            {QStringLiteral("Bright red"), QColor(255, 36, 36)},
            {QStringLiteral("Bright green"), QColor(28, 255, 83)},
            {QStringLiteral("Bright purple"), QColor(190, 82, 255)}
        }};
        for (const auto& colorChoice : colors) {
            QToolButton* button = create_color_button(colorChoice.second, colorChoice.second == audience_->pointer_color_, colorChoice.first);
            connect(button, &QToolButton::clicked, this, [this, color = colorChoice.second] {
                audience_->set_pointer_color(color);
                rebuild_settings();
            });
            colorRow->addWidget(button, 0, Qt::AlignVCenter);
        }
        colorRow->addStretch(1);
        settings_layout_->addLayout(colorRow);

        auto* sizeRow = create_settings_row();
        sizeRow->addWidget(create_settings_label(QStringLiteral("Pointer size")), 0, Qt::AlignVCenter);
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("featureSizeSlider"));
        slider->setMinimumWidth(280);
        slider->setRange(kMinimumPointerSize, kMaximumPointerSize);
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setValue(audience_->pointer_size_);

        auto* sizeInput = new QSpinBox(this);
        sizeInput->setObjectName(QStringLiteral("pointerSizeInput"));
        sizeInput->setRange(kMinimumPointerSize, kMaximumPointerSize);
        sizeInput->setSuffix(QStringLiteral(" px"));
        sizeInput->setValue(audience_->pointer_size_);
        sizeInput->setFixedSize(90, 38);

        connect(slider, &QSlider::valueChanged, sizeInput, &QSpinBox::setValue);
        connect(sizeInput, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, audience_, &AudienceWindow::set_pointer_size);

        sizeRow->addWidget(slider, 1, Qt::AlignVCenter);
        sizeRow->addWidget(sizeInput, 0, Qt::AlignVCenter);
        settings_layout_->addLayout(sizeRow);
    }

    /** @brief Adds pen-specific controls to the settings panel. */
    void add_pen_settings() {
        auto* colorRow = create_settings_row();
        colorRow->addWidget(create_settings_label(QStringLiteral("Ink")), 0, Qt::AlignTop);

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
            QToolButton* button = create_color_button(colorChoice.second, colorChoice.second == audience_->annotation_color_, colorChoice.first);
            connect(button, &QToolButton::clicked, this, [this, color = colorChoice.second] {
                audience_->set_annotation_color(color);
                rebuild_settings();
            });
            colorGrid->addWidget(button, int(index / 7), int(index % 7), Qt::AlignCenter);
        }
        colorRow->addLayout(colorGrid);
        colorRow->addStretch(1);
        settings_layout_->addLayout(colorRow);

        auto* row = create_settings_row();
        row->addWidget(create_settings_label(QStringLiteral("Size")), 0, Qt::AlignVCenter);
        for (const int thickness : {3, 6, 10, 16, 24}) {
            QToolButton* button = create_size_button(thickness, thickness == audience_->annotation_thickness_);
            connect(button, &QToolButton::clicked, this, [this, thickness] {
                audience_->set_annotation_thickness(thickness);
                rebuild_settings();
            });
            row->addWidget(button, 0, Qt::AlignVCenter);
        }
        row->addStretch(1);
        settings_layout_->addLayout(row);
    }

    /** @brief Adds eraser-specific controls to the settings panel. */
    void add_eraser_settings() {
        auto* sliderRow = create_settings_row();
        auto* preview = new EraserSizePreview(this);
        preview->set_diameter(audience_->eraser_thickness_);
        sliderRow->addWidget(preview, 0, Qt::AlignVCenter);

        auto* sizeColumn = new QVBoxLayout;
        sizeColumn->setContentsMargins(0, 0, 0, 0);
        sizeColumn->setSpacing(4);
        auto* label = create_settings_label(QStringLiteral("Eraser size: %1 px").arg(audience_->eraser_thickness_));
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("featureSizeSlider"));
        slider->setMinimumWidth(340);
        slider->setRange(4, 64);
        slider->setSingleStep(2);
        slider->setPageStep(8);
        slider->setValue(audience_->eraser_thickness_);
        connect(slider, &QSlider::valueChanged, this, [this, preview, label](int value) {
            audience_->set_eraser_thickness(value);
            preview->set_diameter(value);
            label->setText(QStringLiteral("Eraser size: %1 px").arg(value));
        });
        sizeColumn->addWidget(label);
        sizeColumn->addWidget(slider);
        sliderRow->addLayout(sizeColumn, 1);
        settings_layout_->addLayout(sliderRow);

        auto* presetRow = create_settings_row();
        presetRow->addWidget(create_settings_label(QStringLiteral("Presets")), 0, Qt::AlignVCenter);
        for (const int thickness : {8, 16, 24, 36, 48, 64}) {
            QToolButton* button = create_size_button(thickness, thickness == audience_->eraser_thickness_);
            connect(button, &QToolButton::clicked, this, [this, thickness] {
                audience_->set_eraser_thickness(thickness);
                rebuild_settings();
            });
            presetRow->addWidget(button, 0, Qt::AlignVCenter);
        }
        presetRow->addStretch(1);
        settings_layout_->addLayout(presetRow);
    }

    /** @brief Resizes the feature menu to fit its current contents. */
    void resize_menu_to_contents() {
        invalidate_menu_layout();
        resize_to_contents();
        if (isVisible()) {
            move(bounded_popup_position(anchor_global_position_));
            QTimer::singleShot(0, this, [this] {
                if (!isVisible()) {
                    return;
                }
                invalidate_menu_layout();
                resize_to_contents();
                move(bounded_popup_position(anchor_global_position_));
            });
        }
    }

    /** @brief Confirms and clears the current slide's annotations. */
    void confirm_clear_annotations() {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Clear Writings"));
        box.setText(QStringLiteral("Clear all writings on this slide?"));
        box.setIcon(QMessageBox::Warning);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() == QMessageBox::Yes) {
            audience_->clear_annotations();
        }
    }

    /** @brief Closes the feature menu. */
    void close_menu() {
        close();
    }

    /** @brief Closes the feature menu and opens deck overview. */
    void open_deck_overview() {
        if (opening_deck_overview_) {
            return;
        }

        opening_deck_overview_ = true;
        QPointer<AudienceWindow> audience = audience_;
        qApp->removeEventFilter(this);
        hide();
        QTimer::singleShot(0, this, [this] {
            close_menu();
        });
        QTimer::singleShot(0, audience, [audience] {
            if (audience) {
                audience->enter_deck_overview();
            }
        });
    }

    /** @brief Returns the global rectangle occupied by the menu's grid button. */
    QRect grid_button_global_rect() const {
        if (!grid_button_) {
            return {};
        }

        return QRect(grid_button_->mapToGlobal(QPoint(0, 0)), grid_button_->size());
    }

    /** @brief Defers resizing until Qt has updated child size hints. */
    void resize_to_contents() {
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

    /** @brief Invalidates cached menu layout geometry. */
    void invalidate_menu_layout() {
        if (settings_layout_) {
            settings_layout_->invalidate();
        }
        if (settings_frame_) {
            settings_frame_->updateGeometry();
            if (QLayout* settingsFrameLayout = settings_frame_->layout()) {
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

    /** @brief Clamps a requested menu position to the available screen geometry. */
    QPoint bounded_popup_position(const QPoint& requestedPosition) const {
        QRect availableGeometry;
        if (QScreen* screenAtPosition = QGuiApplication::screenAt(requestedPosition)) {
            availableGeometry = screenAtPosition->availableGeometry();
        } else if (audience_ && audience_->windowHandle() && audience_->windowHandle()->screen()) {
            availableGeometry = audience_->windowHandle()->screen()->availableGeometry();
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

    AudienceWindow* audience_ = nullptr;
    QPoint anchor_global_position_;
    QToolButton* cursor_button_ = nullptr;
    QToolButton* pointer_button_ = nullptr;
    QToolButton* pen_button_ = nullptr;
    QToolButton* eraser_button_ = nullptr;
    QToolButton* grid_button_ = nullptr;
    QFrame* settings_frame_ = nullptr;
    QVBoxLayout* settings_layout_ = nullptr;
    bool opening_deck_overview_ = false;
};

AudienceWindow::AudienceWindow()
    : QWidget(nullptr, Qt::Window) {
    QSettings settings;
    pointer_size_ = std::clamp(
        settings.value(QString::fromLatin1(kPointerSizeSettingsKey), kDefaultPointerSize).toInt(),
        kMinimumPointerSize,
        kMaximumPointerSize);
    setWindowTitle(QStringLiteral("uil Audience"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/uil.png")));
    resize(960, 540);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    cursor_hide_timer_.setSingleShot(true);
    cursor_hide_timer_.setInterval(2000);
    connect(&cursor_hide_timer_, &QTimer::timeout, this, &AudienceWindow::hide_cursor);

    pointer_hide_timer_.setSingleShot(true);
    pointer_hide_timer_.setInterval(kPointerInactivityTimeoutMs);
    connect(&pointer_hide_timer_, &QTimer::timeout, this, &AudienceWindow::hide_pointer);
}

AudienceWindow::~AudienceWindow() = default;

void AudienceWindow::set_slide_image(const QString& texture_key, const QImage& image) {
    if (texture_key.isEmpty() || image.isNull()) {
        clear_slide_image();
        return;
    }

    current_texture_key_ = texture_key;
    current_slide_image_ = image;
    cache_slide_image(texture_key, image);
    emit annotation_overlay_changed(current_annotation_overlay_image());
    update_molecule_overlay_geometry();
    update();
}

void AudienceWindow::clear_slide_image() {
    current_texture_key_.clear();
    current_slide_image_ = {};
    video_frame_ = {};
    video_rect_ = {};
    has_video_overlay_ = false;
    clear_molecule_overlay();
    emit annotation_overlay_changed({});
    update();
}

void AudienceWindow::cache_slide_image(const QString& texture_key, const QImage& image) {
    if (texture_key.isEmpty() || image.isNull()) {
        return;
    }

    auto it = std::find_if(slide_cache_.begin(), slide_cache_.end(), [&texture_key](const CachedSlide& slide) {
        return slide.key == texture_key;
    });

    if (it != slide_cache_.end()) {
        it->image = image;
        if (texture_key == current_texture_key_) {
            current_slide_image_ = image;
        }
        if (it != slide_cache_.begin()) {
            CachedSlide slide = std::move(*it);
            slide_cache_.erase(it);
            slide_cache_.insert(slide_cache_.begin(), std::move(slide));
        }
        update();
        return;
    }

    slide_cache_.insert(slide_cache_.begin(), CachedSlide{texture_key, image});
    evict_old_slides();
    update();
}

void AudienceWindow::set_document_overview(int page_count, int current_page) {
    const bool pageCountChanged = page_count_ != std::max(0, page_count);
    page_count_ = std::max(0, page_count);
    current_page_index_ = current_page >= 0 && current_page < page_count_ ? current_page : -1;
    if (page_count_ == 0) {
        deck_overview_images_.clear();
        deck_overview_image_size_ = {};
        deck_overview_visible_ = false;
        deck_overview_scroll_y_ = 0;
    }

    if (deck_overview_visible_) {
        deck_overview_scroll_y_ = std::clamp(deck_overview_scroll_y_, 0, deck_overview_max_scroll_y());
        if (pageCountChanged) {
            request_visible_deck_overview_renders();
        }
        update();
    }
}

void AudienceWindow::set_deck_overview_slide_image(int page_index, const QSize& bounding_pixel_size, const QImage& image) {
    if (page_index < 0 || page_index >= page_count_ || !bounding_pixel_size.isValid()) {
        return;
    }

    if (deck_overview_image_size_.isValid() && deck_overview_image_size_ != bounding_pixel_size) {
        deck_overview_images_.clear();
    }
    deck_overview_image_size_ = bounding_pixel_size;

    if (image.isNull()) {
        deck_overview_images_.remove(page_index);
    } else {
        deck_overview_images_.insert(page_index, image);
    }

    if (deck_overview_visible_) {
        update();
    }
}

void AudienceWindow::set_video_frame(const QImage& image, QRectF slide_rect) {
    if (image.isNull() || !slide_rect.isValid()) {
        clear_video_overlay();
        return;
    }

    video_frame_ = image;
    video_rect_ = slide_rect;
    has_video_overlay_ = true;
    update();
}

void AudienceWindow::clear_video_overlay() {
    if (!has_video_overlay_ && video_frame_.isNull() && video_rect_.isNull()) {
        return;
    }

    video_frame_ = {};
    video_rect_ = {};
    has_video_overlay_ = false;
    update();
}

void AudienceWindow::set_molecule_overlay(
    const MoleculeGeometry& geometry,
    QRectF slide_rect) {
    if (!geometry.is_valid() || !slide_rect.isValid()) {
        clear_molecule_overlay();
        return;
    }

    if (!molecule_widget_) {
        molecule_widget_ = std::make_unique<MoleculeWidget>(this);
    }
    molecule_rect_ = slide_rect;
    molecule_widget_->set_geometry(geometry);
    molecule_widget_->raise();
    update_molecule_overlay_geometry();
}

void AudienceWindow::clear_molecule_overlay() {
    molecule_rect_ = {};
    if (molecule_widget_) {
        molecule_widget_->hide();
    }
}

void AudienceWindow::set_audience_screen(QScreen* screen) {
    if (!screen) {
        return;
    }

    screen_ = screen;
    apply_screen_geometry(is_fullscreen_);
    if (is_fullscreen_) {
        showFullScreen();
    }
    emit render_target_changed();
}

void AudienceWindow::enter_fullscreen() {
    is_fullscreen_ = true;
    apply_screen_geometry(true);
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
    show_cursor_temporarily();
    qCInfo(logUi) << "Audience fullscreen entered";
}

void AudienceWindow::toggle_fullscreen() {
    if (!is_fullscreen_) {
        enter_fullscreen();
    } else {
        exit_fullscreen();
    }
}

void AudienceWindow::exit_fullscreen() {
    if (!is_fullscreen_) {
        return;
    }

    is_fullscreen_ = false;
    is_annotating_ = false;
    hide_pointer();
    deck_overview_visible_ = false;
    showNormal();
    hide();
    clear_blank_screen();
    unsetCursor();
    emit presentation_closed();
    qCInfo(logUi) << "Audience fullscreen closed";
}

void AudienceWindow::toggle_black_screen() {
    blank_mode_ = (blank_mode_ == BlankMode::Black) ? BlankMode::None : BlankMode::Black;
    update();
}

void AudienceWindow::toggle_white_screen() {
    blank_mode_ = (blank_mode_ == BlankMode::White) ? BlankMode::None : BlankMode::White;
    update();
}

void AudienceWindow::clear_blank_screen() {
    if (blank_mode_ == BlankMode::None) {
        return;
    }
    blank_mode_ = BlankMode::None;
    update();
}

void AudienceWindow::set_cursor_tool() {
    interaction_tool_ = InteractionTool::Cursor;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update();
}

void AudienceWindow::set_pointer_tool() {
    interaction_tool_ = InteractionTool::Pointer;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update();
}

void AudienceWindow::set_pen_tool() {
    interaction_tool_ = InteractionTool::Pen;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update();
}

void AudienceWindow::set_eraser_tool() {
    interaction_tool_ = InteractionTool::Eraser;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update();
}

void AudienceWindow::set_pointer_color(const QColor& color) {
    if (color.isValid()) {
        pointer_color_ = color;
        update();
    }
}

void AudienceWindow::set_pointer_size(int size) {
    const int clampedSize = std::clamp(size, kMinimumPointerSize, kMaximumPointerSize);
    if (pointer_size_ == clampedSize) {
        return;
    }

    pointer_size_ = clampedSize;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kPointerSizeSettingsKey), pointer_size_);
    update();
}

void AudienceWindow::set_annotation_color(const QColor& color) {
    if (color.isValid()) {
        annotation_color_ = color;
    }
}

void AudienceWindow::set_annotation_thickness(int thickness) {
    annotation_thickness_ = std::clamp(thickness, 1, 64);
}

void AudienceWindow::set_eraser_thickness(int thickness) {
    eraser_thickness_ = std::clamp(thickness, 4, 64);
    update();
}

void AudienceWindow::clear_annotations() {
    if (!current_texture_key_.isEmpty() && annotation_images_.contains(current_texture_key_)) {
        annotation_images_[current_texture_key_].fill(Qt::transparent);
        emit annotation_overlay_changed(current_annotation_overlay_image());
        update();
    }
}

void AudienceWindow::clear_annotation_overlay_for_page(int page_index) {
    if (page_index < 0) {
        return;
    }

    QStringList keysToRemove;
    for (auto it = annotation_images_.constBegin(); it != annotation_images_.constEnd(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }

        bool ok = false;
        const int keyPageIndex = parts.at(1).toInt(&ok);
        if (ok && keyPageIndex == page_index) {
            keysToRemove.push_back(it.key());
        }
    }

    if (keysToRemove.isEmpty()) {
        return;
    }

    for (const QString& key : keysToRemove) {
        annotation_images_.remove(key);
    }

    emit annotation_overlay_changed(current_annotation_overlay_image());
    update();
}

void AudienceWindow::clear_all_annotation_overlays() {
    if (annotation_images_.isEmpty()) {
        return;
    }

    annotation_images_.clear();
    emit annotation_overlay_changed(current_annotation_overlay_image());
    update();
}

void AudienceWindow::set_annotation_overlays_by_texture_key(const QHash<QString, QImage>& overlays) {
    annotation_images_ = overlays;
    emit annotation_overlay_changed(current_annotation_overlay_image());
    update();
}

QHash<int, QImage> AudienceWindow::annotation_overlays_by_page() const {
    QHash<int, QImage> overlays;
    for (auto it = annotation_images_.constBegin(); it != annotation_images_.constEnd(); ++it) {
        if (it.value().isNull()) {
            continue;
        }

        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }

        bool ok = false;
        const int page_index = parts.at(1).toInt(&ok);
        if (ok && page_index >= 0) {
            overlays.insert(page_index, it.value());
        }
    }

    return overlays;
}

QImage AudienceWindow::current_annotated_slide_image() const {
    if (current_slide_image_.isNull()) {
        return {};
    }

    QImage result = current_slide_image_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage* annotation = current_annotation_image();
    if (annotation && !annotation->isNull()) {
        QPainter painter(&result);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(result.rect(), *annotation);
    }

    return result;
}

QImage AudienceWindow::current_annotation_overlay_image() const {
    const QImage* annotation = current_annotation_image();
    return annotation ? *annotation : QImage();
}

QSize AudienceWindow::render_logical_size() const {
    if (screen_) {
        return screen_->geometry().size();
    }

    return size();
}

qreal AudienceWindow::render_device_pixel_ratio() const {
    if (screen_) {
        return screen_->devicePixelRatio();
    }

    return devicePixelRatioF();
}

int AudienceWindow::pointer_size() const {
    return pointer_size_;
}

bool AudienceWindow::is_pointer_visible() const {
    return pointer_visible_;
}

bool AudienceWindow::is_pointer_tool_selected() const {
    return interaction_tool_ == InteractionTool::Pointer;
}

bool AudienceWindow::is_deck_overview_visible() const {
    return deck_overview_visible_;
}

void AudienceWindow::closeEvent(QCloseEvent* event) {
    is_fullscreen_ = false;
    is_annotating_ = false;
    hide_pointer();
    deck_overview_visible_ = false;
    clear_blank_screen();
    unsetCursor();
    emit presentation_closed();
    QWidget::closeEvent(event);
}

void AudienceWindow::contextMenuEvent(QContextMenuEvent* event) {
    show_feature_menu(event->globalPos());
    event->accept();
}

void AudienceWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), blank_mode_ == BlankMode::White ? Qt::white : Qt::black);
    update_molecule_overlay_geometry();

    if (blank_mode_ != BlankMode::None) {
        return;
    }

    if (deck_overview_visible_) {
        draw_deck_overview(painter);
        return;
    }

    if (current_slide_image_.isNull()) {
        return;
    }

    const QRectF slide_rect = slide_logical_rect(current_slide_image_.size());
    if (!slide_rect.isValid()) {
        return;
    }

    painter.drawImage(slide_rect, current_slide_image_, image_source_rect(current_slide_image_));

    if (has_video_overlay_ && !video_frame_.isNull() && video_rect_.isValid()) {
        const QRectF target(
            slide_rect.left() + video_rect_.left() * slide_rect.width(),
            slide_rect.top() + video_rect_.top() * slide_rect.height(),
            video_rect_.width() * slide_rect.width(),
            video_rect_.height() * slide_rect.height());
        painter.drawImage(target, video_frame_, image_source_rect(video_frame_));
    }

    const QImage* annotation = current_annotation_image();
    if (annotation && !annotation->isNull()) {
        painter.drawImage(slide_rect, *annotation, image_source_rect(*annotation));
    }

    draw_pointer(painter);
    draw_eraser_cursor(painter);
}

void AudienceWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update_molecule_overlay_geometry();
}

void AudienceWindow::keyPressEvent(QKeyEvent* event) {
    if (deck_overview_visible_) {
        switch (event->key()) {
        case Qt::Key_Escape:
            exit_deck_overview();
            event->accept();
            return;
        case Qt::Key_G:
            exit_deck_overview();
            event->accept();
            return;
        case Qt::Key_Up:
            scroll_deck_overview_by(-80);
            event->accept();
            return;
        case Qt::Key_Down:
            scroll_deck_overview_by(80);
            event->accept();
            return;
        case Qt::Key_PageUp:
            scroll_deck_overview_by(-deck_overview_viewport_rect().height());
            event->accept();
            return;
        case Qt::Key_PageDown:
            scroll_deck_overview_by(deck_overview_viewport_rect().height());
            event->accept();
            return;
        case Qt::Key_Home:
            deck_overview_scroll_y_ = 0;
            update();
            event->accept();
            return;
        case Qt::Key_End:
            deck_overview_scroll_y_ = deck_overview_max_scroll_y();
            update();
            event->accept();
            return;
        default:
            break;
        }
    }

    show_cursor_temporarily();

    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        emit next_requested();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        emit previous_requested();
        event->accept();
        return;
    case Qt::Key_Home:
        emit first_requested();
        event->accept();
        return;
    case Qt::Key_End:
        emit last_requested();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit play_pause_requested();
        event->accept();
        return;
    case Qt::Key_B:
        toggle_black_screen();
        event->accept();
        return;
    case Qt::Key_W:
        toggle_white_screen();
        event->accept();
        return;
    case Qt::Key_G:
        enter_deck_overview();
        event->accept();
        return;
    case Qt::Key_L:
        if (deck_overview_visible_) {
            exit_deck_overview();
        }
        set_pointer_tool();
        event->accept();
        return;
    case Qt::Key_F11:
        toggle_fullscreen();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (is_fullscreen_) {
            exit_fullscreen();
            event->accept();
            return;
        }
        if (blank_mode_ != BlankMode::None) {
            clear_blank_screen();
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
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update();
    QWidget::leaveEvent(event);
}

void AudienceWindow::mouseMoveEvent(QMouseEvent* event) {
    if (deck_overview_visible_) {
        cursor_hide_timer_.stop();
        setCursor(QCursor(Qt::ArrowCursor));
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (interaction_tool_ == InteractionTool::Pointer) {
        show_pointer_at(event->position());
        event->accept();
        return;
    }

    if (interaction_tool_ == InteractionTool::Eraser) {
        eraser_cursor_position_ = event->position();
        eraser_cursor_visible_ = true;
        if (is_annotating_ && (event->buttons() & Qt::LeftButton)) {
            draw_annotation_segment(last_annotation_point_, event->position());
            last_annotation_point_ = event->position();
        } else {
            update();
        }
        event->accept();
        return;
    }

    if (is_annotating_ && (event->buttons() & Qt::LeftButton)) {
        draw_annotation_segment(last_annotation_point_, event->position());
        last_annotation_point_ = event->position();
        event->accept();
        return;
    }

    show_cursor_temporarily();
    QWidget::mouseMoveEvent(event);
}

void AudienceWindow::mousePressEvent(QMouseEvent* event) {
    if (deck_overview_visible_ && event->button() == Qt::LeftButton) {
        const int page_index = deck_overview_page_at(event->position().toPoint());
        if (page_index >= 0) {
            exit_deck_overview();
            emit page_requested(page_index);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
        && (interaction_tool_ == InteractionTool::Pen || interaction_tool_ == InteractionTool::Eraser)) {
        is_annotating_ = true;
        last_annotation_point_ = event->position();
        if (interaction_tool_ == InteractionTool::Eraser) {
            eraser_cursor_position_ = event->position();
            eraser_cursor_visible_ = true;
        }
        draw_annotation_segment(last_annotation_point_, last_annotation_point_);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && interaction_tool_ == InteractionTool::Pointer) {
        show_pointer_at(event->position());
        event->accept();
        return;
    }

    show_cursor_temporarily();
    QWidget::mousePressEvent(event);
}

void AudienceWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        show_feature_menu(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && is_annotating_) {
        is_annotating_ = false;
        update();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void AudienceWindow::wheelEvent(QWheelEvent* event) {
    if (deck_overview_visible_) {
        const QPoint angleDelta = event->angleDelta();
        const QPoint pixelDelta = event->pixelDelta();
        int delta_y = pixelDelta.y();
        if (delta_y == 0) {
            delta_y = angleDelta.y() / 2;
        }
        scroll_deck_overview_by(-delta_y);
        event->accept();
        return;
    }

    const int angleDeltaY = event->angleDelta().y();
    const int pixelDeltaY = event->pixelDelta().y();
    const int delta_y = angleDeltaY != 0 ? angleDeltaY : pixelDeltaY;
    const int threshold = angleDeltaY != 0 ? 120 : 80;
    if (delta_y != 0 && page_count_ > 0 && blank_mode_ == BlankMode::None) {
        if ((delta_y > 0 && slide_wheel_remainder_y_ < 0)
            || (delta_y < 0 && slide_wheel_remainder_y_ > 0)) {
            slide_wheel_remainder_y_ = 0;
        }

        slide_wheel_remainder_y_ += delta_y;
        if (std::abs(slide_wheel_remainder_y_) >= threshold) {
            if (slide_wheel_remainder_y_ > 0) {
                if (current_page_index_ > 0) {
                    emit previous_requested();
                }
            } else {
                if (current_page_index_ < page_count_ - 1) {
                    emit next_requested();
                }
            }
            slide_wheel_remainder_y_ = 0;
        }

        show_cursor_temporarily();
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
}

void AudienceWindow::evict_old_slides() {
    for (int i = int(slide_cache_.size()) - 1; int(slide_cache_.size()) > kMaxAudienceSlides && i >= 0; --i) {
        if (slide_cache_.at(size_t(i)).key == current_texture_key_) {
            continue;
        }
        slide_cache_.erase(slide_cache_.begin() + i);
    }
}

void AudienceWindow::apply_screen_geometry(bool fullscreen) {
    if (!screen_) {
        return;
    }

    winId();
    if (QWindow* handle = windowHandle()) {
        handle->setScreen(screen_);
    }

    if (fullscreen) {
        setGeometry(screen_->geometry());
        return;
    }

    const QRect availableGeometry = screen_->availableGeometry();
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

void AudienceWindow::show_cursor_temporarily() {
    if (interaction_tool_ != InteractionTool::Cursor) {
        update_cursor_appearance();
        return;
    }

    unsetCursor();
    if (is_fullscreen_) {
        cursor_hide_timer_.start();
    }
}

void AudienceWindow::hide_cursor() {
    if (is_fullscreen_ && interaction_tool_ == InteractionTool::Cursor) {
        setCursor(QCursor(Qt::BlankCursor));
    }
}

void AudienceWindow::update_cursor_appearance() {
    cursor_hide_timer_.stop();
    switch (interaction_tool_) {
    case InteractionTool::Cursor:
        unsetCursor();
        if (is_fullscreen_) {
            cursor_hide_timer_.start();
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

QRectF AudienceWindow::slide_logical_rect(QSize texture_size) const {
    if (!texture_size.isValid() || height() <= 0 || width() <= 0) {
        return {};
    }

    const qreal viewportAspect = qreal(width()) / qreal(height());
    const qreal textureAspect = qreal(texture_size.width()) / qreal(texture_size.height());
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

void AudienceWindow::update_molecule_overlay_geometry() {
    if (!molecule_widget_) {
        return;
    }
    if (!molecule_rect_.isValid() || current_slide_image_.isNull()
        || blank_mode_ != BlankMode::None || deck_overview_visible_
        || interaction_tool_ != InteractionTool::Cursor) {
        molecule_widget_->hide();
        return;
    }

    const QRectF slide_rect = slide_logical_rect(current_slide_image_.size());
    if (!slide_rect.isValid()) {
        molecule_widget_->hide();
        return;
    }

    const QRect target = QRectF(
        slide_rect.left() + molecule_rect_.left() * slide_rect.width(),
        slide_rect.top() + molecule_rect_.top() * slide_rect.height(),
        molecule_rect_.width() * slide_rect.width(),
        molecule_rect_.height() * slide_rect.height())
                             .toAlignedRect();
    if (target.width() < 2 || target.height() < 2) {
        molecule_widget_->hide();
        return;
    }

    molecule_widget_->setGeometry(target);
    molecule_widget_->show();
    molecule_widget_->raise();
}

QPointF AudienceWindow::slide_image_point(QPointF window_point, QSize texture_size, bool* inside) const {
    const QRectF slide_rect = slide_logical_rect(texture_size);
    const bool contains = slide_rect.contains(window_point);
    if (inside) {
        *inside = contains;
    }
    if (!contains || !slide_rect.isValid()) {
        return {};
    }

    return QPointF(
        (window_point.x() - slide_rect.left()) / slide_rect.width() * texture_size.width(),
        (window_point.y() - slide_rect.top()) / slide_rect.height() * texture_size.height());
}

QImage& AudienceWindow::annotation_image_for_current_slide(QSize size) {
    QImage& image = annotation_images_[current_texture_key_];
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

const QImage* AudienceWindow::current_annotation_image() const {
    if (current_texture_key_.isEmpty()) {
        return nullptr;
    }

    const auto it = annotation_images_.constFind(current_texture_key_);
    return it == annotation_images_.constEnd() ? nullptr : &it.value();
}

void AudienceWindow::draw_annotation_segment(QPointF from_window_point, QPointF to_window_point) {
    if (current_texture_key_.isEmpty() || current_slide_image_.isNull()) {
        return;
    }

    const QSize targetSize = current_slide_image_.size();
    if (!targetSize.isValid()) {
        return;
    }

    bool fromInside = false;
    bool toInside = false;
    const QPointF from = slide_image_point(from_window_point, targetSize, &fromInside);
    const QPointF to = slide_image_point(to_window_point, targetSize, &toInside);
    if (!fromInside || !toInside) {
        return;
    }

    QImage& image = annotation_image_for_current_slide(targetSize);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(
        annotation_color_,
        interaction_tool_ == InteractionTool::Eraser ? eraser_thickness_ : annotation_thickness_,
        Qt::SolidLine,
        Qt::RoundCap,
        Qt::RoundJoin);
    if (interaction_tool_ == InteractionTool::Eraser) {
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

    emit annotation_overlay_changed(current_annotation_overlay_image());
    update();
}

void AudienceWindow::draw_pointer(QPainter& painter) const {
    if (!pointer_visible_ || interaction_tool_ != InteractionTool::Pointer) {
        return;
    }

    QColor glow = pointer_color_;
    glow.setAlpha(55);
    QColor fill = pointer_color_;
    fill.setAlpha(120);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(pointer_position_, pointer_size_ * 0.68, pointer_size_ * 0.68);
    painter.setBrush(fill);
    painter.drawEllipse(pointer_position_, pointer_size_ * 0.35, pointer_size_ * 0.35);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(pointer_color_, std::max(2.0, pointer_size_ * 0.087)));
    painter.drawEllipse(pointer_position_, pointer_size_ * 0.48, pointer_size_ * 0.48);
    painter.restore();
}

void AudienceWindow::show_pointer_at(const QPointF& position) {
    pointer_position_ = position;
    pointer_visible_ = true;
    pointer_hide_timer_.start();
    update();
}

void AudienceWindow::hide_pointer() {
    pointer_hide_timer_.stop();
    if (!pointer_visible_) {
        return;
    }

    pointer_visible_ = false;
    update();
}

void AudienceWindow::draw_eraser_cursor(QPainter& painter) const {
    if (!eraser_cursor_visible_ || interaction_tool_ != InteractionTool::Eraser || current_slide_image_.isNull()) {
        return;
    }

    const qreal diameter = eraser_logical_diameter();
    const qreal radius = diameter / 2.0;
    const QRectF circle(
        eraser_cursor_position_.x() - radius,
        eraser_cursor_position_.y() - radius,
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

qreal AudienceWindow::eraser_logical_diameter() const {
    if (current_slide_image_.isNull() || !current_slide_image_.size().isValid()) {
        return qreal(eraser_thickness_);
    }

    const QRectF slide_rect = slide_logical_rect(current_slide_image_.size());
    if (!slide_rect.isValid() || slide_rect.width() <= 0.0 || current_slide_image_.width() <= 0) {
        return qreal(eraser_thickness_);
    }

    const qreal slidePixelScale = slide_rect.width() / qreal(current_slide_image_.width());
    return std::max<qreal>(1.0, qreal(eraser_thickness_) * slidePixelScale);
}

void AudienceWindow::enter_deck_overview() {
    if (page_count_ <= 0) {
        return;
    }

    deck_overview_visible_ = true;
    blank_mode_ = BlankMode::None;
    is_annotating_ = false;
    hide_pointer();
    eraser_cursor_visible_ = false;
    cursor_hide_timer_.stop();
    setCursor(QCursor(Qt::ArrowCursor));

    const QRect viewport = deck_overview_viewport_rect();
    const QSize slideBounds = deck_overview_thumbnail_bounding_pixel_size();
    const int tileHeight = slideBounds.height() + kDeckOverviewFooterHeight;
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int row = std::max(0, current_page_index_) / kDeckOverviewColumns;
    const int focusedCenterY = row * (tileHeight + verticalSpacing) + tileHeight / 2;
    deck_overview_scroll_y_ = std::clamp(focusedCenterY - viewport.height() / 2, 0, deck_overview_max_scroll_y());

    request_visible_deck_overview_renders();
    update();
}

void AudienceWindow::exit_deck_overview() {
    if (!deck_overview_visible_) {
        return;
    }

    deck_overview_visible_ = false;
    deck_overview_scroll_y_ = 0;
    update_cursor_appearance();
    update();
}

void AudienceWindow::draw_deck_overview(QPainter& painter) {
    const QRect viewport = deck_overview_viewport_rect();
    const QRect gridViewport = viewport.adjusted(0, 0, -kDeckOverviewScrollbarGutter, 0);
    const QSize slideBounds = deck_overview_thumbnail_bounding_pixel_size();
    const int horizontalSpacing = std::max(14, gridViewport.width() / 70);
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int tileWidth = slideBounds.width();
    const int tileHeight = slideBounds.height() + kDeckOverviewFooterHeight;
    const int gridWidth = kDeckOverviewColumns * tileWidth + (kDeckOverviewColumns - 1) * horizontalSpacing;
    const int left = gridViewport.left() + std::max(0, (gridViewport.width() - gridWidth) / 2);
    deck_overview_scroll_y_ = std::clamp(deck_overview_scroll_y_, 0, deck_overview_max_scroll_y());
    const int top = viewport.top() - deck_overview_scroll_y_;

    painter.save();
    painter.fillRect(rect(), QColor(0x12, 0x12, 0x12));
    painter.setClipRect(viewport);

    for (int page = 0; page < page_count_; ++page) {
        const int row = page / kDeckOverviewColumns;
        const int column = page % kDeckOverviewColumns;
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

        const QImage image = deck_overview_images_.value(page);
        if (!image.isNull()) {
            const QRect imageRect = centered_rect_for_image(image.size(), slideFrame.adjusted(1, 1, -1, -1));
            painter.drawImage(imageRect, image);
        } else {
            painter.setPen(QColor(0x8a, 0x8a, 0x8a));
            painter.drawText(slideFrame, Qt::AlignCenter, QStringLiteral("..."));
        }

        const bool isSelected = page == current_page_index_;
        painter.setPen(QPen(isSelected ? QColor(0x00, 0xb3, 0xb3) : QColor(0x3a, 0x3a, 0x3a), isSelected ? 4 : 1));
        painter.drawRect(slideFrame.adjusted(0, 0, -1, -1));

        QRect numberRect(tileRect.left(), slideFrame.bottom() + 5, tileRect.width() - 2, kDeckOverviewFooterHeight - 5);
        painter.setPen(isSelected ? QColor(0xff, 0xff, 0xff) : QColor(0xb0, 0xb0, 0xb0));
        painter.drawText(numberRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(page + 1));
    }

    if (deck_overview_max_scroll_y() > 0) {
        const QRect track(viewport.right() - kDeckOverviewScrollTrackWidth, viewport.top(), kDeckOverviewScrollTrackWidth, viewport.height());
        const qreal visibleFraction = qreal(viewport.height()) / qreal(deck_overview_content_height());
        const int handleHeight = std::max(36, int(track.height() * visibleFraction));
        const int handleY = track.top() + int(qreal(track.height() - handleHeight) * qreal(deck_overview_scroll_y_) / qreal(deck_overview_max_scroll_y()));
        painter.fillRect(track, QColor(0x2c, 0x2c, 0x2c));
        painter.fillRect(QRect(track.left(), handleY, kDeckOverviewScrollTrackWidth, handleHeight), QColor(0x00, 0xb3, 0xb3));
    }

    painter.restore();
}

QSize AudienceWindow::deck_overview_thumbnail_bounding_pixel_size() const {
    const QRect viewport = deck_overview_viewport_rect().adjusted(0, 0, -kDeckOverviewScrollbarGutter, 0);
    const int horizontalSpacing = std::max(14, viewport.width() / 70);
    const int availableWidth = std::max(0, viewport.width() - (kDeckOverviewColumns - 1) * horizontalSpacing);
    const int slideWidth = std::max(96, availableWidth / kDeckOverviewColumns);
    const int slideHeight = std::max(54, int(qreal(slideWidth) * 9.0 / 16.0));
    return QSize(slideWidth, slideHeight);
}

QRect AudienceWindow::deck_overview_viewport_rect() const {
    const int horizontalMargin = std::clamp(width() / 28, 28, 72);
    const int verticalMargin = std::clamp(height() / 24, 24, 64);
    return rect().adjusted(horizontalMargin, verticalMargin, -horizontalMargin, -verticalMargin);
}

int AudienceWindow::deck_overview_content_height() const {
    if (page_count_ <= 0) {
        return 0;
    }

    const QRect viewport = deck_overview_viewport_rect();
    const QSize slideBounds = deck_overview_thumbnail_bounding_pixel_size();
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int rows = (page_count_ + kDeckOverviewColumns - 1) / kDeckOverviewColumns;
    return rows * (slideBounds.height() + kDeckOverviewFooterHeight) + std::max(0, rows - 1) * verticalSpacing;
}

int AudienceWindow::deck_overview_max_scroll_y() const {
    return std::max(0, deck_overview_content_height() - deck_overview_viewport_rect().height());
}

QPair<int, int> AudienceWindow::deck_overview_visible_page_range() const {
    if (page_count_ <= 0) {
        return {-1, -1};
    }

    const QRect viewport = deck_overview_viewport_rect();
    const QSize slide_bounds = deck_overview_thumbnail_bounding_pixel_size();
    const int vertical_spacing = std::max(16, viewport.height() / 36);
    const int row_stride = slide_bounds.height() + kDeckOverviewFooterHeight + vertical_spacing;
    const int first_row = std::max(0, deck_overview_scroll_y_ / row_stride);
    const int last_row = std::max(
        first_row,
        (deck_overview_scroll_y_ + std::max(0, viewport.height() - 1)) / row_stride);
    const int first_page = std::clamp(
        first_row * kDeckOverviewColumns,
        0,
        page_count_ - 1);
    const int last_page = std::clamp(
        (last_row + 1) * kDeckOverviewColumns - 1,
        first_page,
        page_count_ - 1);
    return {first_page, last_page};
}

void AudienceWindow::request_visible_deck_overview_renders() {
    if (!deck_overview_visible_ || page_count_ <= 0) {
        return;
    }

    const auto [first_visible_page, last_visible_page] = deck_overview_visible_page_range();
    emit deck_overview_renders_requested(
        deck_overview_thumbnail_bounding_pixel_size(),
        current_page_index_,
        first_visible_page,
        last_visible_page);
}

int AudienceWindow::deck_overview_page_at(const QPoint& position) const {
    const QRect viewport = deck_overview_viewport_rect();
    if (!viewport.contains(position)) {
        return -1;
    }

    const QRect gridViewport = viewport.adjusted(0, 0, -kDeckOverviewScrollbarGutter, 0);
    if (!gridViewport.contains(position)) {
        return -1;
    }

    const QSize slideBounds = deck_overview_thumbnail_bounding_pixel_size();
    const int horizontalSpacing = std::max(14, gridViewport.width() / 70);
    const int verticalSpacing = std::max(16, viewport.height() / 36);
    const int tileWidth = slideBounds.width();
    const int tileHeight = slideBounds.height() + kDeckOverviewFooterHeight;
    const int gridWidth = kDeckOverviewColumns * tileWidth + (kDeckOverviewColumns - 1) * horizontalSpacing;
    const int left = gridViewport.left() + std::max(0, (gridViewport.width() - gridWidth) / 2);
    const int contentX = position.x() - left;
    const int contentY = position.y() - viewport.top() + deck_overview_scroll_y_;
    if (contentX < 0 || contentY < 0) {
        return -1;
    }

    const int columnStride = tileWidth + horizontalSpacing;
    const int rowStride = tileHeight + verticalSpacing;
    const int column = contentX / columnStride;
    const int row = contentY / rowStride;
    if (column < 0 || column >= kDeckOverviewColumns) {
        return -1;
    }
    if (contentX % columnStride >= tileWidth || contentY % rowStride >= tileHeight) {
        return -1;
    }

    const int page_index = row * kDeckOverviewColumns + column;
    return page_index >= 0 && page_index < page_count_ ? page_index : -1;
}

void AudienceWindow::scroll_deck_overview_by(int delta_y) {
    const int nextScrollY = std::clamp(deck_overview_scroll_y_ + delta_y, 0, deck_overview_max_scroll_y());
    if (nextScrollY == deck_overview_scroll_y_) {
        return;
    }

    deck_overview_scroll_y_ = nextScrollY;
    request_visible_deck_overview_renders();
    update();
}

void AudienceWindow::show_feature_menu(const QPoint& global_position) {
    if (feature_menu_ && feature_menu_->isVisible()) {
        return;
    }
    if (feature_menu_) {
        feature_menu_->close();
        feature_menu_ = nullptr;
    }

    auto* menu = new FeatureMenuPanel(this);
    feature_menu_ = menu;
    menu->winId();
    if (QWindow* menuWindow = menu->windowHandle()) {
        if (QWindow* audience_window = windowHandle()) {
            menuWindow->setScreen(audience_window->screen());
        }
    }

    connect(menu, &QObject::destroyed, this, [this, menu] {
        if (feature_menu_ == menu) {
            feature_menu_ = nullptr;
        }
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        if (QWindow* handle = windowHandle()) {
            handle->requestActivate();
        }
        if (deck_overview_visible_) {
            cursor_hide_timer_.stop();
            setCursor(QCursor(Qt::ArrowCursor));
        } else {
            update_cursor_appearance();
        }
        update();
    });

    eraser_cursor_visible_ = false;
    update();
    setCursor(QCursor(Qt::ArrowCursor));
    menu->popup_at(global_position);
}

void AudienceWindow::save_annotated_slide_image() {
    const QImage image = current_annotated_slide_image();
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
