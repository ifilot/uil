#include "audience_window.hpp"

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
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <array>
#include <utility>

#include "orbital/atomic_orbital_cache.hpp"
#include "ui/atomic_orbital_widget.hpp"
#include "ui/bluecurve.hpp"
#include "ui/font_awesome.hpp"
#include "ui/interactive_figure_widget.hpp"
#include "ui/molecule_widget.hpp"
#include "ui/molecular_symmetry_widget.hpp"

Q_LOGGING_CATEGORY(log_ui, "ui")

namespace {
// Interval at which in-progress strokes are mirrored to the presenter window. Publishing on
// every mouse move would copy the full-resolution overlay for each input event.
constexpr int kAnnotationSyncIntervalMs = 100;
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
QRect centered_rect_for_image(const QSize& image_size, const QRect& bounds) {
    if (!image_size.isValid() || !bounds.isValid()) {
        return {};
    }

    const QSize scaled_size = image_size.scaled(bounds.size(), Qt::KeepAspectRatio);
    return QRect(
        bounds.x() + (bounds.width() - scaled_size.width()) / 2,
        bounds.y() + (bounds.height() - scaled_size.height()) / 2,
        scaled_size.width(),
        scaled_size.height());
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
    const QColor border_color = color.lightness() < 45 ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x44, 0x44, 0x44);
    painter.setPen(QPen(border_color, 1.0));
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
        : QWidget(audience),
          audience_(audience) {
        setAttribute(Qt::WA_DeleteOnClose, true);
        setAttribute(Qt::WA_StyledBackground, true);
        setFocusPolicy(Qt::StrongFocus);
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
        setFocus(Qt::PopupFocusReason);
    }

private:
    /** @brief Constructs the feature menu controls and layouts. */
    void build_ui() {
        setObjectName(QStringLiteral("featureMenuPanel"));
        setMinimumWidth(560);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        auto* root_layout = new QVBoxLayout(this);
        root_layout->setContentsMargins(14, 14, 14, 14);
        root_layout->setSpacing(10);
        root_layout->setSizeConstraint(QLayout::SetFixedSize);

        auto* tool_row = new QHBoxLayout;
        tool_row->setContentsMargins(0, 0, 0, 0);
        tool_row->setSpacing(14);
        cursor_button_ = create_tool_button(QStringLiteral("Classic\npointer"), QStringLiteral("arrow-pointer"), InteractionTool::Cursor);
        pointer_button_ = create_tool_button(QStringLiteral("Laser\npointer"), QStringLiteral("location-crosshairs"), InteractionTool::Pointer);
        pointer_button_->setToolTip(QStringLiteral("Laser pointer (L)"));
        pen_button_ = create_tool_button(QStringLiteral("Pencil"), QStringLiteral("pencil"), InteractionTool::Pen);
        eraser_button_ = create_tool_button(QStringLiteral("Eraser"), QStringLiteral("eraser"), InteractionTool::Eraser);
        tool_row->addStretch(1);
        tool_row->addWidget(cursor_button_, 0, Qt::AlignCenter);
        tool_row->addWidget(pointer_button_, 0, Qt::AlignCenter);
        tool_row->addWidget(pen_button_, 0, Qt::AlignCenter);
        tool_row->addWidget(eraser_button_, 0, Qt::AlignCenter);
        tool_row->addStretch(1);
        root_layout->addLayout(tool_row);

        settings_frame_ = new QFrame(this);
        settings_frame_->setObjectName(QStringLiteral("featureSettingsRow"));
        settings_frame_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        settings_layout_ = new QVBoxLayout(settings_frame_);
        settings_layout_->setContentsMargins(12, 10, 12, 10);
        settings_layout_->setSpacing(8);
        root_layout->addWidget(settings_frame_);

        auto* bottom_row = new QHBoxLayout;
        bottom_row->setContentsMargins(0, 0, 0, 0);
        bottom_row->setSpacing(10);

        QToolButton* clear_button = create_bottom_button(QStringLiteral("Clear"), QStringLiteral("stock-clear"), QStringLiteral("featureDangerButton"), 92);
        QToolButton* close_button = create_bottom_button(QStringLiteral("Close slideshow"), QStringLiteral("stock-quit"), QStringLiteral("featureCloseButton"), 148);
        QToolButton* first_button = create_icon_bottom_button(QStringLiteral("First slide"), QStringLiteral("stock-goto-first"), QStringLiteral("featureNavButton"));
        QToolButton* previous_button = create_icon_bottom_button(QStringLiteral("Previous slide"), QStringLiteral("stock-go-back"), QStringLiteral("featureNavButton"));
        grid_button_ = create_icon_bottom_button(QStringLiteral("Show slide grid (G)"), QStringLiteral("stock_display-grid"), QStringLiteral("featureNavButton"));
        grid_button_->installEventFilter(this);
        QToolButton* next_button = create_icon_bottom_button(QStringLiteral("Next slide"), QStringLiteral("stock-go-forward"), QStringLiteral("featureNavButton"));
        QToolButton* last_button = create_icon_bottom_button(QStringLiteral("Last slide"), QStringLiteral("stock-goto-last"), QStringLiteral("featureNavButton"));

        connect(clear_button, &QToolButton::clicked, this, [this] {
            confirm_clear_annotations();
        });
        connect(close_button, &QToolButton::clicked, this, [this] {
            AudienceWindow* audience = audience_;
            close_menu();
            audience->exit_fullscreen();
        });
        connect(first_button, &QToolButton::clicked, this, [this] {
            emit audience_->first_requested();
        });
        connect(previous_button, &QToolButton::clicked, this, [this] {
            emit audience_->previous_requested();
        });
        connect(next_button, &QToolButton::clicked, this, [this] {
            emit audience_->next_requested();
        });
        connect(last_button, &QToolButton::clicked, this, [this] {
            emit audience_->last_requested();
        });

        bottom_row->addWidget(clear_button);
        bottom_row->addWidget(close_button);
        bottom_row->addStretch(1);
        bottom_row->addWidget(first_button);
        bottom_row->addWidget(previous_button);
        bottom_row->addWidget(grid_button_);
        bottom_row->addWidget(next_button);
        bottom_row->addWidget(last_button);
        root_layout->addLayout(bottom_row);

        refresh_tool_buttons();
        rebuild_settings();
    }

    /** @brief Handles clicks outside the feature menu and relevant window events. */
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            auto* key_event = static_cast<QKeyEvent*>(event);
            if (key_event->key() == Qt::Key_Escape) {
                close_menu();
                event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_G) {
                open_deck_overview();
                event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_L) {
                select_tool(InteractionTool::Pointer);
                event->accept();
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonPress && grid_button_) {
            auto* mouse_event = static_cast<QMouseEvent*>(event);
            const QRect menu_rect(mapToGlobal(QPoint(0, 0)), size());
            if (!menu_rect.contains(mouse_event->globalPosition().toPoint())) {
                close_menu();
                event->accept();
                return true;
            }
            if (mouse_event->button() == Qt::LeftButton
                && grid_button_global_rect().contains(mouse_event->globalPosition().toPoint())) {
                open_deck_overview();
                event->accept();
                return true;
            }
        }

        if (watched == grid_button_ && event->type() == QEvent::KeyPress) {
            auto* key_event = static_cast<QKeyEvent*>(event);
            if (key_event->key() == Qt::Key_Return
                || key_event->key() == Qt::Key_Enter
                || key_event->key() == Qt::Key_Space) {
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
            close_menu();
            return;
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
        auto* color_row = create_settings_row();
        color_row->addWidget(create_settings_label(QStringLiteral("Pointer color")), 0, Qt::AlignVCenter);
        const std::array<QPair<QString, QColor>, 3> colors{{
            {QStringLiteral("Bright red"), QColor(255, 36, 36)},
            {QStringLiteral("Bright green"), QColor(28, 255, 83)},
            {QStringLiteral("Bright purple"), QColor(190, 82, 255)}
        }};
        for (const auto& color_choice : colors) {
            QToolButton* button = create_color_button(color_choice.second, color_choice.second == audience_->pointer_color_, color_choice.first);
            connect(button, &QToolButton::clicked, this, [this, color = color_choice.second] {
                audience_->set_pointer_color(color);
                rebuild_settings();
            });
            color_row->addWidget(button, 0, Qt::AlignVCenter);
        }
        color_row->addStretch(1);
        settings_layout_->addLayout(color_row);

        auto* size_row = create_settings_row();
        size_row->addWidget(create_settings_label(QStringLiteral("Pointer size")), 0, Qt::AlignVCenter);
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("featureSizeSlider"));
        slider->setMinimumWidth(280);
        slider->setRange(kMinimumPointerSize, kMaximumPointerSize);
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setValue(audience_->pointer_size_);

        auto* size_input = new QSpinBox(this);
        size_input->setObjectName(QStringLiteral("pointerSizeInput"));
        size_input->setRange(kMinimumPointerSize, kMaximumPointerSize);
        size_input->setSuffix(QStringLiteral(" px"));
        size_input->setValue(audience_->pointer_size_);
        size_input->setFixedSize(90, 38);

        connect(slider, &QSlider::valueChanged, size_input, &QSpinBox::setValue);
        connect(size_input, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, audience_, &AudienceWindow::set_pointer_size);

        size_row->addWidget(slider, 1, Qt::AlignVCenter);
        size_row->addWidget(size_input, 0, Qt::AlignVCenter);
        settings_layout_->addLayout(size_row);
    }

    /** @brief Adds pen-specific controls to the settings panel. */
    void add_pen_settings() {
        auto* color_row = create_settings_row();
        color_row->addWidget(create_settings_label(QStringLiteral("Ink")), 0, Qt::AlignTop);

        auto* color_grid = new QGridLayout;
        color_grid->setContentsMargins(0, 0, 0, 0);
        color_grid->setHorizontalSpacing(8);
        color_grid->setVerticalSpacing(6);

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
            const auto& color_choice = colors[std::size_t(index)];
            QToolButton* button = create_color_button(color_choice.second, color_choice.second == audience_->annotation_color_, color_choice.first);
            connect(button, &QToolButton::clicked, this, [this, color = color_choice.second] {
                audience_->set_annotation_color(color);
                rebuild_settings();
            });
            color_grid->addWidget(button, int(index / 7), int(index % 7), Qt::AlignCenter);
        }
        color_row->addLayout(color_grid);
        color_row->addStretch(1);
        settings_layout_->addLayout(color_row);

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
        auto* slider_row = create_settings_row();
        auto* preview = new EraserSizePreview(this);
        preview->set_diameter(audience_->eraser_thickness_);
        slider_row->addWidget(preview, 0, Qt::AlignVCenter);

        auto* size_column = new QVBoxLayout;
        size_column->setContentsMargins(0, 0, 0, 0);
        size_column->setSpacing(4);
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
        size_column->addWidget(label);
        size_column->addWidget(slider);
        slider_row->addLayout(size_column, 1);
        settings_layout_->addLayout(slider_row);

        auto* preset_row = create_settings_row();
        preset_row->addWidget(create_settings_label(QStringLiteral("Presets")), 0, Qt::AlignVCenter);
        for (const int thickness : {8, 16, 24, 36, 48, 64}) {
            QToolButton* button = create_size_button(thickness, thickness == audience_->eraser_thickness_);
            connect(button, &QToolButton::clicked, this, [this, thickness] {
                audience_->set_eraser_thickness(thickness);
                rebuild_settings();
            });
            preset_row->addWidget(button, 0, Qt::AlignVCenter);
        }
        preset_row->addStretch(1);
        settings_layout_->addLayout(preset_row);
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
        if (QLayout* root_layout = layout()) {
            root_layout->activate();
            const QSize target_size = root_layout->totalSizeHint()
                .expandedTo(root_layout->totalMinimumSize())
                .expandedTo(minimumSize());
            resize(target_size);
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
        if (QLayout* root_layout = layout()) {
            root_layout->invalidate();
            root_layout->activate();
        }
    }

    /** @brief Clamps a requested global position to the audience child area. */
    QPoint bounded_popup_position(const QPoint& requested_position) const {
        if (!audience_) {
            return {};
        }

        const QRect available_geometry = audience_->rect();
        const QSize panel_size = size().expandedTo(sizeHint()).expandedTo(minimumSizeHint());
        QPoint position = audience_->mapFromGlobal(requested_position);
        if (position.x() + panel_size.width() > available_geometry.right()) {
            position.setX(available_geometry.right() - panel_size.width());
        }
        if (position.y() + panel_size.height() > available_geometry.bottom()) {
            position.setY(available_geometry.bottom() - panel_size.height());
        }
        position.setX(std::max(position.x(), available_geometry.left()));
        position.setY(std::max(position.y(), available_geometry.top()));
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
  atomic_orbital_cache_ = std::make_unique<AtomicOrbitalCache>();
  connect(atomic_orbital_cache_.get(), &AtomicOrbitalCache::volume_ready, this,
          [this](const QByteArray& key, const AtomicOrbitalVolume& volume, const QString& error) {
            for (auto& overlay : atomic_orbital_overlays_) {
              if (!overlay.slide_rect.isValid() || overlay.geometry_key != key || overlay.ready)
                continue;
              overlay.widget->set_prepared_definition(overlay.definition, volume, error);
              overlay.ready = volume.is_valid();
            }
            update_atomic_orbital_overlay_geometry();
            update();
          });
  // QOpenGLWidget changes a top-level widget to OpenGL-backed composition. If
  // the first one is added after the audience window is visible, Qt may
  // recreate the native window and briefly expose the desktop. Keep one
  // hidden orbital widget in the hierarchy before the first show and reuse it.
  AtomicOrbitalOverlayState initial_orbital;
  initial_orbital.widget = create_atomic_orbital_widget();
  atomic_orbital_overlays_.push_back(std::move(initial_orbital));
  QSettings settings;
  pointer_size_ = std::clamp(
      settings.value(QString::fromLatin1(kPointerSizeSettingsKey), kDefaultPointerSize).toInt(),
      kMinimumPointerSize, kMaximumPointerSize);
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

  // Presentation navigation must not depend on keyboard focus. In particular,
  // native QOpenGLWidget surfaces can consume key delivery even when every
  // embedded control has Qt::NoFocus. Window-scoped shortcuts are therefore
  // the durable navigation path; keep this when adding future live widgets.
  const auto add_navigation_shortcut = [this](Qt::Key key, bool forward) {
    auto* shortcut = new QShortcut(QKeySequence(key), this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, key, forward] {
      show_cursor_temporarily();
      if (deck_overview_visible_ && key == Qt::Key_PageDown) {
        scroll_deck_overview_by(deck_overview_viewport_rect().height());
      } else if (deck_overview_visible_ && key == Qt::Key_PageUp) {
        scroll_deck_overview_by(-deck_overview_viewport_rect().height());
      } else if (deck_overview_visible_ && key == Qt::Key_Down) {
        scroll_deck_overview_by(80);
      } else if (deck_overview_visible_ && key == Qt::Key_Up) {
        scroll_deck_overview_by(-80);
      } else if (forward) {
        emit next_requested();
      } else {
        emit previous_requested();
      }
    });
  };
  add_navigation_shortcut(Qt::Key_Right, true);
  add_navigation_shortcut(Qt::Key_Down, true);
  add_navigation_shortcut(Qt::Key_PageDown, true);
  add_navigation_shortcut(Qt::Key_Left, false);
  add_navigation_shortcut(Qt::Key_Up, false);
  add_navigation_shortcut(Qt::Key_PageUp, false);

  pointer_hide_timer_.setSingleShot(true);
  pointer_hide_timer_.setInterval(kPointerInactivityTimeoutMs);
  connect(&pointer_hide_timer_, &QTimer::timeout, this, &AudienceWindow::hide_pointer);

  annotation_sync_timer_.setSingleShot(true);
  annotation_sync_timer_.setInterval(kAnnotationSyncIntervalMs);
  connect(&annotation_sync_timer_, &QTimer::timeout, this, &AudienceWindow::publish_annotation_overlay);
}

AudienceWindow::~AudienceWindow() {
    if (feature_menu_) {
        QWidget* menu = feature_menu_.data();
        feature_menu_ = nullptr;
        disconnect(menu, nullptr, this, nullptr);
        delete menu;
    }
}

void AudienceWindow::set_slide_image(const QString& texture_key, const QImage& image) {
    if (texture_key.isEmpty() || image.isNull()) {
        clear_slide_image();
        return;
    }

    if (texture_key != current_texture_key_) {
        slide_transition_active_ = false;
        molecule_snapshot_frame_ = {};
        interactive_figure_snapshot_frame_ = {};
        molecular_symmetry_snapshot_frame_ = {};
        for (auto& overlay : atomic_orbital_overlays_) overlay.snapshot_frame = {};
    }
    current_texture_key_ = texture_key;
    current_slide_image_ = image;
    publish_annotation_overlay();
    update_molecule_overlay_geometry();
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();
    update();
    // The first slide shown after entering fullscreen (F5 reloads asynchronously) and slides
    // with live OpenGL content need an explicit present; see present_fullscreen_frame(). Decide
    // on the next turn: the controller configures this slide's overlays right after this call.
    if (is_fullscreen_) {
        QTimer::singleShot(0, this, [this] {
            if (is_fullscreen_ && (!fullscreen_slide_presented_ || has_visible_opengl_overlay())) {
                fullscreen_slide_presented_ = true;
                present_fullscreen_frame();
            }
        });
    }
}

bool AudienceWindow::has_visible_opengl_overlay() const {
    if (molecule_widget_ && molecule_widget_->isVisible()) {
        return true;
    }
    if (molecular_symmetry_widget_ && molecular_symmetry_widget_->isVisible()) {
        return true;
    }
    return std::any_of(atomic_orbital_overlays_.cbegin(), atomic_orbital_overlays_.cend(),
        [](const AtomicOrbitalOverlayState& overlay) {
            return overlay.widget && overlay.widget->isVisible();
        });
}

void AudienceWindow::begin_slide_transition() {
    if (slide_transition_active_ || current_slide_image_.isNull()) {
        return;
    }

    slide_transition_active_ = true;
}

void AudienceWindow::prepare_interactive_overlays_for_display() {
    if (molecule_widget_ && molecule_widget_->isVisible()) {
        molecule_widget_->grabFramebuffer();
    }
    for (auto& overlay : atomic_orbital_overlays_) {
        if (overlay.widget->isVisible()) {
            overlay.widget->capture_frame();
        }
    }
    if (molecular_symmetry_widget_ && molecular_symmetry_widget_->isVisible()) {
        molecular_symmetry_widget_->capture_frame();
    }
}

void AudienceWindow::clear_slide_image() {
    slide_transition_active_ = false;
    current_texture_key_.clear();
    current_slide_image_ = {};
    video_frame_ = {};
    video_rect_ = {};
    has_video_overlay_ = false;
    molecule_snapshot_frame_ = {};
    clear_molecule_overlay();
    interactive_figure_snapshot_frame_ = {};
    clear_interactive_figure_overlay();
    clear_atomic_orbital_overlay();
    molecular_symmetry_snapshot_frame_ = {};
    clear_molecular_symmetry_overlay();
    if (molecular_symmetry_widget_) molecular_symmetry_widget_->clear_saved_slide_states();
    emit annotation_overlay_changed({});
    update();
}

void AudienceWindow::cache_slide_image(const QString& texture_key, const QImage& image) {
    if (texture_key.isEmpty() || image.isNull() || texture_key != current_texture_key_) {
        return;
    }

    current_slide_image_ = image;
    update();
}

void AudienceWindow::set_document_overview(int page_count, int current_page) {
    const bool page_count_changed = page_count_ != std::max(0, page_count);
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
        if (page_count_changed) {
            request_visible_deck_overview_renders();
        }
        update();
    }
}

void AudienceWindow::set_deck_overview_slide_image(int page_index, const QSize& bounding_pixel_size, const QImage& image) {
    if (page_index < 0 || page_index >= page_count_ || !bounding_pixel_size.isValid()) {
        return;
    }

    // The presenter's thumbnail strip receives renders through the same signal at its own
    // size; those must neither be shown here nor evict this grid's thumbnails.
    if (bounding_pixel_size != deck_overview_thumbnail_bounding_pixel_size()) {
        return;
    }
    if (deck_overview_image_size_ != bounding_pixel_size) {
        deck_overview_images_.clear();
        deck_overview_image_size_ = bounding_pixel_size;
    }

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

    const bool same_area = has_video_overlay_ && video_rect_ == slide_rect;
    video_frame_ = image;
    video_rect_ = slide_rect;
    has_video_overlay_ = true;

    // Repaint only the video area for steady playback instead of the whole slide.
    const QRectF slide_area = slide_logical_rect(current_slide_image_.size());
    if (same_area && slide_area.isValid() && blank_mode_ == BlankMode::None && !deck_overview_visible_) {
        const QRectF target(
            slide_area.left() + video_rect_.left() * slide_area.width(),
            slide_area.top() + video_rect_.top() * slide_area.height(),
            video_rect_.width() * slide_area.width(),
            video_rect_.height() * slide_area.height());
        update(target.toAlignedRect().adjusted(-1, -1, 1, 1));
        return;
    }
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
        molecule_widget_->set_context_menu_handler([this](const QPoint& global_position) {
            show_feature_menu(global_position);
        });
    }
    if (molecule_suspended_for_feature_menu_) {
        molecule_snapshot_frame_ = {};
    }
    molecule_rect_ = slide_rect;
    molecule_widget_->set_geometry(geometry);
    molecule_widget_->raise();
    update_molecule_overlay_geometry();
}

void AudienceWindow::clear_molecule_overlay() {
    molecule_rect_ = {};
    molecule_snapshot_frame_ = {};
    if (molecule_widget_) {
        molecule_widget_->hide();
    }
}

void AudienceWindow::set_interactive_figure_overlay(
    const InteractiveFigureDefinition& definition,
    QRectF slide_rect) {
    if (!definition.is_valid() || !slide_rect.isValid()) {
        clear_interactive_figure_overlay();
        return;
    }
    if (!interactive_figure_widget_) {
        interactive_figure_widget_ = std::make_unique<InteractiveFigureWidget>(this);
        interactive_figure_widget_->set_context_menu_handler(
            [this](const QPoint& global_position) { show_feature_menu(global_position); });
    }
    interactive_figure_snapshot_frame_ = {};
    interactive_figure_rect_ = slide_rect;
    interactive_figure_widget_->set_definition(definition);
    update_interactive_figure_overlay_geometry();
}

void AudienceWindow::clear_interactive_figure_overlay() {
    interactive_figure_rect_ = {};
    interactive_figure_snapshot_frame_ = {};
    if (interactive_figure_widget_) {
        interactive_figure_widget_->hide();
    }
}

void AudienceWindow::set_atomic_orbital_overlay(
    const AtomicOrbitalDefinition& definition,
    QRectF slide_rect) {
  set_atomic_orbital_overlays({{definition, slide_rect}});
}

void AudienceWindow::set_atomic_orbital_overlays(const QVector<AtomicOrbitalOverlay>& overlays,
                                                 const QVector<AtomicOrbitalDefinition>& nearby,
                                                 const QString& texture_key) {
  clear_atomic_orbital_overlay();
  atomic_orbital_slide_identity_ =
      texture_key.isEmpty() ? QString() : texture_key.section(':', 0, 1);
  QVector<AtomicOrbitalDefinition> visible;
  size_t index = 0;
  for (const auto& overlay : overlays) {
    if (!overlay.definition.is_valid() || !overlay.slide_rect.isValid()) continue;
    if (index == atomic_orbital_overlays_.size()) {
      AtomicOrbitalOverlayState state;
      state.widget = create_atomic_orbital_widget();
      atomic_orbital_overlays_.push_back(std::move(state));
    }
    auto& state = atomic_orbital_overlays_[index++];
    state.slide_rect = overlay.slide_rect;
    state.definition = overlay.definition;
    state.geometry_key = atomic_orbital_geometry_key(overlay.definition);
    visible.push_back(overlay.definition);
    const auto volume = atomic_orbital_cache_->get(state.geometry_key);
    if (volume.is_valid()) {
      state.widget->set_prepared_definition(overlay.definition, volume);
      state.ready = true;
    }
  }
  atomic_orbital_cache_->prepare(visible, nearby);
  update_atomic_orbital_overlay_geometry();
  update();
}

void AudienceWindow::clear_atomic_orbital_overlay() {
  atomic_orbital_cache_->prepare({});
  atomic_orbital_slide_identity_.clear();
  for (auto& overlay : atomic_orbital_overlays_) {
    overlay.slide_rect = {};
    overlay.snapshot_frame = {};
    overlay.ready = false;
    overlay.geometry_key.clear();
    overlay.widget->hide();
  }
  update();
}

void AudienceWindow::set_molecular_symmetry_overlay(
    const MolecularSymmetryDefinition& definition, QRectF slide_rect) {
  if (!definition.is_valid() || !slide_rect.isValid()) {
    clear_molecular_symmetry_overlay();
    return;
  }
  if (!molecular_symmetry_widget_) {
    molecular_symmetry_widget_ = std::make_unique<MolecularSymmetryWidget>(this);
    molecular_symmetry_widget_->set_context_menu_handler(
        [this](const QPoint& position) { show_feature_menu(position); });
    if (auto* surface = dynamic_cast<MoleculeWidget*>(
            molecular_symmetry_widget_->findChild<QWidget*>(
                QStringLiteral("symmetryMoleculeOpenGLWidget")))) {
      surface->installEventFilter(this);
    }
  }
  molecular_symmetry_snapshot_frame_ = {};
  molecular_symmetry_rect_ = slide_rect;
  molecular_symmetry_widget_->set_definition(
      definition, current_texture_key_.section(':', 0, 1));
  update_molecular_symmetry_overlay_geometry();
}

void AudienceWindow::clear_molecular_symmetry_overlay() {
  molecular_symmetry_rect_ = {};
  molecular_symmetry_snapshot_frame_ = {};
  if (molecular_symmetry_widget_) molecular_symmetry_widget_->hide();
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
    fullscreen_slide_presented_ = false;
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
        present_fullscreen_frame();
    });
    // On Windows the native fullscreen swap can finish after Qt's first queued
    // paint. Present once more after the compositor has attached the OpenGL
    // child surfaces; otherwise their first frame can remain black until input.
    QTimer::singleShot(75, this, &AudienceWindow::present_fullscreen_frame);
    show_cursor_temporarily();
    qCInfo(log_ui) << "Audience fullscreen entered";
}

void AudienceWindow::present_fullscreen_frame() {
    if (!is_fullscreen_ || !isVisible()) {
        return;
    }

    update_molecule_overlay_geometry();
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();

    const auto present_widget = [](QWidget* widget) {
        if (!widget || !widget->isVisible()) {
            return;
        }
        widget->update();
        widget->repaint();
    };
    present_widget(molecule_widget_.get());
    present_widget(interactive_figure_widget_.get());
    for (auto& overlay : atomic_orbital_overlays_) {
        present_widget(overlay.widget.get());
    }
    if (molecular_symmetry_widget_ && molecular_symmetry_widget_->isVisible()) {
        present_widget(dynamic_cast<MoleculeWidget*>(
            molecular_symmetry_widget_->findChild<QWidget*>(
                QStringLiteral("symmetryMoleculeOpenGLWidget"))));
        present_widget(molecular_symmetry_widget_.get());
    }

    prepare_interactive_overlays_for_display();
    if (QWindow* handle = windowHandle()) {
        handle->requestUpdate();
    }
    update();
    repaint();
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
    qCInfo(log_ui) << "Audience fullscreen closed";
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
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();
    update();
}

void AudienceWindow::set_pointer_tool() {
    capture_molecule_frame();
    capture_interactive_figure_frame();
    capture_atomic_orbital_frame();
    capture_molecular_symmetry_frame();
    interaction_tool_ = InteractionTool::Pointer;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();
    update();
}

void AudienceWindow::set_pen_tool() {
    capture_molecule_frame();
    capture_interactive_figure_frame();
    capture_atomic_orbital_frame();
    capture_molecular_symmetry_frame();
    interaction_tool_ = InteractionTool::Pen;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();
    update();
}

void AudienceWindow::set_eraser_tool() {
    capture_molecule_frame();
    capture_interactive_figure_frame();
    capture_atomic_orbital_frame();
    capture_molecular_symmetry_frame();
    interaction_tool_ = InteractionTool::Eraser;
    hide_pointer();
    eraser_cursor_visible_ = false;
    is_annotating_ = false;
    update_cursor_appearance();
    update_molecule_overlay_geometry();
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();
    update();
}

void AudienceWindow::set_pointer_color(const QColor& color) {
    if (color.isValid()) {
        pointer_color_ = color;
        update();
    }
}

void AudienceWindow::set_pointer_size(int size) {
    const int clamped_size = std::clamp(size, kMinimumPointerSize, kMaximumPointerSize);
    if (pointer_size_ == clamped_size) {
        return;
    }

    pointer_size_ = clamped_size;
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
        emit annotations_edited();
        publish_annotation_overlay();
        update();
    }
}

void AudienceWindow::clear_annotation_overlay_for_page(int page_index) {
    if (page_index < 0) {
        return;
    }

    QStringList keys_to_remove;
    for (auto it = annotation_images_.constBegin(); it != annotation_images_.constEnd(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }

        bool ok = false;
        const int key_page_index = parts.at(1).toInt(&ok);
        if (ok && key_page_index == page_index) {
            keys_to_remove.push_back(it.key());
        }
    }

    if (keys_to_remove.isEmpty()) {
        return;
    }

    for (const QString& key : keys_to_remove) {
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
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();

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

    const bool molecule_is_interactive = molecule_widget_ && molecule_widget_->isVisible()
        && interaction_tool_ == InteractionTool::Cursor
        && !molecule_suspended_for_feature_menu_;
    if (!molecule_is_interactive && !molecule_snapshot_frame_.isNull()
        && molecule_rect_.isValid()) {
        const QRectF target(
            slide_rect.left() + molecule_rect_.left() * slide_rect.width(),
            slide_rect.top() + molecule_rect_.top() * slide_rect.height(),
            molecule_rect_.width() * slide_rect.width(),
            molecule_rect_.height() * slide_rect.height());
        painter.drawImage(target, molecule_snapshot_frame_,
            image_source_rect(molecule_snapshot_frame_));
    }

    const bool figure_is_interactive = interactive_figure_widget_
        && interactive_figure_widget_->isVisible()
        && interaction_tool_ == InteractionTool::Cursor
        && !interactive_figure_suspended_for_feature_menu_;
    if (!figure_is_interactive && !interactive_figure_snapshot_frame_.isNull()
        && interactive_figure_rect_.isValid()) {
        const QRectF target(
            slide_rect.left() + interactive_figure_rect_.left() * slide_rect.width(),
            slide_rect.top() + interactive_figure_rect_.top() * slide_rect.height(),
            interactive_figure_rect_.width() * slide_rect.width(),
            interactive_figure_rect_.height() * slide_rect.height());
        painter.drawImage(target, interactive_figure_snapshot_frame_,
            image_source_rect(interactive_figure_snapshot_frame_));
    }

    for (const auto& overlay : atomic_orbital_overlays_) {
      const bool orbital_is_interactive = overlay.widget && overlay.widget->isVisible() &&
                                          interaction_tool_ == InteractionTool::Cursor &&
                                          !atomic_orbital_suspended_for_feature_menu_;
      if (!orbital_is_interactive && !overlay.snapshot_frame.isNull() &&
          overlay.slide_rect.isValid()) {
        const QRectF target(slide_rect.left() + overlay.slide_rect.left() * slide_rect.width(),
                            slide_rect.top() + overlay.slide_rect.top() * slide_rect.height(),
                            overlay.slide_rect.width() * slide_rect.width(),
                            overlay.slide_rect.height() * slide_rect.height());
        painter.drawImage(target, overlay.snapshot_frame,
                          image_source_rect(overlay.snapshot_frame));
      }
    }
    const bool symmetry_is_interactive = molecular_symmetry_widget_
        && molecular_symmetry_widget_->isVisible()
        && interaction_tool_ == InteractionTool::Cursor
        && !molecular_symmetry_suspended_for_feature_menu_;
    if (symmetry_is_interactive && molecular_symmetry_rect_.isValid()) {
      // The PDF contains a static fallback poster at this location. The live
      // widget intentionally has transparent spacing, so blank the fallback
      // first or its border pixels show through between child panels.
      const QRectF target(
          slide_rect.left() + molecular_symmetry_rect_.left() * slide_rect.width(),
          slide_rect.top() + molecular_symmetry_rect_.top() * slide_rect.height(),
          molecular_symmetry_rect_.width() * slide_rect.width(),
          molecular_symmetry_rect_.height() * slide_rect.height());
      painter.fillRect(target.adjusted(-1.0, -1.0, 1.0, 1.0), Qt::white);
    }
    if (!symmetry_is_interactive && !molecular_symmetry_snapshot_frame_.isNull()
        && molecular_symmetry_rect_.isValid()) {
      const QRectF target(
          slide_rect.left() + molecular_symmetry_rect_.left() * slide_rect.width(),
          slide_rect.top() + molecular_symmetry_rect_.top() * slide_rect.height(),
          molecular_symmetry_rect_.width() * slide_rect.width(),
          molecular_symmetry_rect_.height() * slide_rect.height());
      painter.drawImage(target, molecular_symmetry_snapshot_frame_,
                        image_source_rect(molecular_symmetry_snapshot_frame_));
    }

    if (molecule_is_interactive && molecule_rect_.isValid()) {
        const QRectF target(
            slide_rect.left() + molecule_rect_.left() * slide_rect.width(),
            slide_rect.top() + molecule_rect_.top() * slide_rect.height(),
            molecule_rect_.width() * slide_rect.width(),
            molecule_rect_.height() * slide_rect.height());
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(105, 110, 118, 180), 2.0));
        painter.drawRect(target);
        painter.restore();
    }

    draw_pointer(painter);
    draw_eraser_cursor(painter);
}

void AudienceWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update_molecule_overlay_geometry();
    update_interactive_figure_overlay_geometry();
    update_atomic_orbital_overlay_geometry();
    update_molecular_symmetry_overlay_geometry();
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
    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        emit next_requested();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Up:
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

bool AudienceWindow::eventFilter(QObject* watched, QEvent* event) {
    if (molecular_symmetry_widget_ && event
        && (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && (widget == molecular_symmetry_widget_.get()
                       || molecular_symmetry_widget_->isAncestorOf(widget))) {
            show_cursor_temporarily();
        }
    }
    return QWidget::eventFilter(watched, event);
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
        const QRect previous_cursor = eraser_cursor_visible_
            ? eraser_cursor_bounds(eraser_cursor_position_)
            : QRect();
        eraser_cursor_position_ = event->position();
        eraser_cursor_visible_ = true;
        if (is_annotating_ && (event->buttons() & Qt::LeftButton)) {
            draw_annotation_segment(last_annotation_point_, event->position());
            last_annotation_point_ = event->position();
        }
        update(previous_cursor.united(eraser_cursor_bounds(eraser_cursor_position_)));
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
            const QRect previous_cursor = eraser_cursor_visible_
                ? eraser_cursor_bounds(eraser_cursor_position_)
                : QRect();
            eraser_cursor_position_ = event->position();
            eraser_cursor_visible_ = true;
            update(previous_cursor.united(eraser_cursor_bounds(eraser_cursor_position_)));
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
        publish_annotation_overlay();
        update();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void AudienceWindow::wheelEvent(QWheelEvent* event) {
    if (deck_overview_visible_) {
        const QPoint angle_delta = event->angleDelta();
        const QPoint pixel_delta = event->pixelDelta();
        int delta_y = pixel_delta.y();
        if (delta_y == 0) {
            delta_y = angle_delta.y() / 2;
        }
        scroll_deck_overview_by(-delta_y);
        event->accept();
        return;
    }

    const int angle_delta_y = event->angleDelta().y();
    const int pixel_delta_y = event->pixelDelta().y();
    const int delta_y = angle_delta_y != 0 ? angle_delta_y : pixel_delta_y;
    const int threshold = angle_delta_y != 0 ? 120 : 80;
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

    const QRect available_geometry = screen_->availableGeometry();
    QSize target_size = size();
    if (!target_size.isValid()) {
        target_size = QSize(960, 540);
    }
    target_size = target_size.boundedTo(available_geometry.size());

    const QPoint top_left(
        available_geometry.x() + (available_geometry.width() - target_size.width()) / 2,
        available_geometry.y() + (available_geometry.height() - target_size.height()) / 2);
    setGeometry(QRect(top_left, target_size));
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
        if (molecular_symmetry_surface_is_active()) {
            unsetCursor();
            return;
        }
        setCursor(QCursor(Qt::BlankCursor));
    }
}

bool AudienceWindow::molecular_symmetry_surface_is_active() const {
    return molecular_symmetry_widget_ && molecular_symmetry_widget_->isVisible()
        && interaction_tool_ == InteractionTool::Cursor
        && !molecular_symmetry_suspended_for_feature_menu_;
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

    const qreal viewport_aspect = qreal(width()) / qreal(height());
    const qreal texture_aspect = qreal(texture_size.width()) / qreal(texture_size.height());
    QSizeF displayed_size(width(), height());
    if (texture_aspect > viewport_aspect) {
        displayed_size.setHeight(displayed_size.width() / texture_aspect);
    } else {
        displayed_size.setWidth(displayed_size.height() * texture_aspect);
    }

    // Slides are rendered at the screen's pixel size. When the texture matches the displayed
    // size to within rounding, place it 1:1 on whole device pixels so QPainter can blit it
    // instead of resampling the full-resolution image on every paint.
    const qreal dpr = devicePixelRatioF();
    if (std::abs(displayed_size.width() * dpr - texture_size.width()) <= 1.0
        && std::abs(displayed_size.height() * dpr - texture_size.height()) <= 1.0) {
        displayed_size = QSizeF(texture_size) / dpr;
        return QRectF(
            std::round((width() - displayed_size.width()) / 2.0 * dpr) / dpr,
            std::round((height() - displayed_size.height()) / 2.0 * dpr) / dpr,
            displayed_size.width(),
            displayed_size.height());
    }

    return QRectF(
        (width() - displayed_size.width()) / 2.0,
        (height() - displayed_size.height()) / 2.0,
        displayed_size.width(),
        displayed_size.height());
}

void AudienceWindow::update_molecule_overlay_geometry() {
    if (!molecule_widget_) {
        return;
    }
    if (molecule_suspended_for_feature_menu_ || !molecule_rect_.isValid()
        || current_slide_image_.isNull()
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

    place_overlay_widget(molecule_widget_.get(), target);
    molecule_snapshot_frame_ = {};
}

void AudienceWindow::capture_molecule_frame() {
    if (!molecule_widget_ || !molecule_widget_->isVisible()) {
        return;
    }

    const QImage frame = molecule_widget_->grabFramebuffer();
    if (!frame.isNull()) {
        molecule_snapshot_frame_ = frame;
    }
}

void AudienceWindow::update_interactive_figure_overlay_geometry() {
    if (!interactive_figure_widget_) {
        return;
    }
    if (interactive_figure_suspended_for_feature_menu_
        || !interactive_figure_rect_.isValid()
        || current_slide_image_.isNull()
        || blank_mode_ != BlankMode::None
        || deck_overview_visible_
        || interaction_tool_ != InteractionTool::Cursor) {
        interactive_figure_widget_->hide();
        return;
    }

    const QRectF slide_rect = slide_logical_rect(current_slide_image_.size());
    if (!slide_rect.isValid()) {
        interactive_figure_widget_->hide();
        return;
    }
    const QRect target = QRectF(
        slide_rect.left() + interactive_figure_rect_.left() * slide_rect.width(),
        slide_rect.top() + interactive_figure_rect_.top() * slide_rect.height(),
        interactive_figure_rect_.width() * slide_rect.width(),
        interactive_figure_rect_.height() * slide_rect.height()).toAlignedRect();
    if (target.width() < 2 || target.height() < 2) {
        interactive_figure_widget_->hide();
        return;
    }
    place_overlay_widget(interactive_figure_widget_.get(), target);
    interactive_figure_snapshot_frame_ = {};
}

void AudienceWindow::capture_interactive_figure_frame() {
    if (!interactive_figure_widget_ || !interactive_figure_widget_->isVisible()) {
        return;
    }
    const QPixmap snapshot = interactive_figure_widget_->grab();
    if (!snapshot.isNull()) {
        interactive_figure_snapshot_frame_ = snapshot.toImage();
    }
}

void AudienceWindow::update_atomic_orbital_overlay_geometry() {
  for (auto& overlay : atomic_orbital_overlays_) {
    if (!overlay.ready ||
        (!atomic_orbital_slide_identity_.isEmpty() &&
         atomic_orbital_slide_identity_ != current_texture_key_.section(':', 0, 1)) ||
        atomic_orbital_suspended_for_feature_menu_ || !overlay.slide_rect.isValid() ||
        current_slide_image_.isNull() || blank_mode_ != BlankMode::None || deck_overview_visible_ ||
        interaction_tool_ != InteractionTool::Cursor) {
      overlay.widget->hide();
      continue;
    }
    const QRectF slide_rect = slide_logical_rect(current_slide_image_.size());
    if (!slide_rect.isValid()) {
      overlay.widget->hide();
      continue;
    }
    const QRect target = QRectF(slide_rect.left() + overlay.slide_rect.left() * slide_rect.width(),
                                slide_rect.top() + overlay.slide_rect.top() * slide_rect.height(),
                                overlay.slide_rect.width() * slide_rect.width(),
                                overlay.slide_rect.height() * slide_rect.height())
                             .toAlignedRect();
    if (target.width() < 2 || target.height() < 2) {
      overlay.widget->hide();
      continue;
    }
    place_overlay_widget(overlay.widget.get(), target);
    overlay.snapshot_frame = {};
  }
}

void AudienceWindow::capture_atomic_orbital_frame() {
  for (auto& overlay : atomic_orbital_overlays_) {
    if (!overlay.widget->isVisible()) continue;
    const QImage snapshot = overlay.widget->capture_frame();
    if (!snapshot.isNull()) overlay.snapshot_frame = snapshot;
  }
}

void AudienceWindow::update_molecular_symmetry_overlay_geometry() {
  if (!molecular_symmetry_widget_) return;
  if (molecular_symmetry_suspended_for_feature_menu_ || !molecular_symmetry_rect_.isValid()
      || current_slide_image_.isNull() || blank_mode_ != BlankMode::None
      || deck_overview_visible_ || interaction_tool_ != InteractionTool::Cursor) {
    molecular_symmetry_widget_->hide();
    return;
  }
  const QRectF slide_rect = slide_logical_rect(current_slide_image_.size());
  if (!slide_rect.isValid()) {
    molecular_symmetry_widget_->hide();
    return;
  }
  const QRect target = QRectF(
      slide_rect.left() + molecular_symmetry_rect_.left() * slide_rect.width(),
      slide_rect.top() + molecular_symmetry_rect_.top() * slide_rect.height(),
      molecular_symmetry_rect_.width() * slide_rect.width(),
      molecular_symmetry_rect_.height() * slide_rect.height()).toAlignedRect();
  if (target.width() < 2 || target.height() < 2) {
    molecular_symmetry_widget_->hide();
    return;
  }
  const bool newly_placed = place_overlay_widget(molecular_symmetry_widget_.get(), target);
  molecular_symmetry_snapshot_frame_ = {};
  // Reveal the cursor when the player appears, not on every repaint (that would keep
  // restarting the hide timer, e.g. throughout video playback).
  if (newly_placed) show_cursor_temporarily();
}

bool AudienceWindow::place_overlay_widget(QWidget* widget, const QRect& target) {
  const bool geometry_changed = widget->geometry() != target;
  const bool was_hidden = widget->isHidden();
  if (geometry_changed) widget->setGeometry(target);
  if (was_hidden) widget->show();
  if (geometry_changed || was_hidden) widget->raise();
  return geometry_changed || was_hidden;
}

void AudienceWindow::capture_molecular_symmetry_frame() {
  if (!molecular_symmetry_widget_ || !molecular_symmetry_widget_->isVisible()) return;
  const QImage snapshot = molecular_symmetry_widget_->capture_frame();
  if (!snapshot.isNull()) molecular_symmetry_snapshot_frame_ = snapshot;
}

std::unique_ptr<AtomicOrbitalWidget> AudienceWindow::create_atomic_orbital_widget() {
  auto widget = std::make_unique<AtomicOrbitalWidget>(this);
  widget->set_context_menu_handler(
      [this](const QPoint& global_position) { show_feature_menu(global_position); });
  widget->hide();
  return widget;
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

    const QSize target_size = current_slide_image_.size();
    if (!target_size.isValid()) {
        return;
    }

    bool from_inside = false;
    bool to_inside = false;
    const QPointF from = slide_image_point(from_window_point, target_size, &from_inside);
    const QPointF to = slide_image_point(to_window_point, target_size, &to_inside);
    if (!from_inside || !to_inside) {
        return;
    }

    QImage& image = annotation_image_for_current_slide(target_size);
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
    painter.end();

    emit annotations_edited();
    if (!annotation_sync_timer_.isActive()) {
        annotation_sync_timer_.start();
    }

    // Repaint only the stroke's bounding box (in window coordinates) instead of the slide.
    const QRectF slide_rect = slide_logical_rect(target_size);
    const qreal scale = slide_rect.width() / qreal(target_size.width());
    const qreal half_pen = pen.widthF() / 2.0 + 2.0;
    const QRectF image_area = QRectF(from, to).normalized().adjusted(-half_pen, -half_pen, half_pen, half_pen);
    const QRectF window_area(
        slide_rect.left() + image_area.left() * scale,
        slide_rect.top() + image_area.top() * scale,
        image_area.width() * scale,
        image_area.height() * scale);
    update(window_area.toAlignedRect().adjusted(-2, -2, 2, 2));
}

void AudienceWindow::publish_annotation_overlay() {
    annotation_sync_timer_.stop();
    emit annotation_overlay_changed(current_annotation_overlay_image());
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
    const QRect previous = pointer_visible_ ? pointer_bounds(pointer_position_) : QRect();
    pointer_position_ = position;
    pointer_visible_ = true;
    pointer_hide_timer_.start();
    // Only the old and new pointer areas change; avoid repainting the whole slide per move.
    update(previous.united(pointer_bounds(pointer_position_)));
}

void AudienceWindow::hide_pointer() {
    pointer_hide_timer_.stop();
    if (!pointer_visible_) {
        return;
    }

    pointer_visible_ = false;
    update(pointer_bounds(pointer_position_));
}

QRect AudienceWindow::pointer_bounds(const QPointF& position) const {
    // Matches draw_pointer(): outer glow radius plus half the ring pen and anti-aliasing.
    const qreal radius = pointer_size_ * 0.68 + std::max(2.0, pointer_size_ * 0.087) / 2.0 + 2.0;
    return QRectF(position.x() - radius, position.y() - radius, 2.0 * radius, 2.0 * radius)
        .toAlignedRect();
}

QRect AudienceWindow::eraser_cursor_bounds(const QPointF& position) const {
    // Matches draw_eraser_cursor(): circle plus its 3 px shadow pen and anti-aliasing.
    const qreal radius = eraser_logical_diameter() / 2.0 + 3.0;
    return QRectF(position.x() - radius, position.y() - radius, 2.0 * radius, 2.0 * radius)
        .toAlignedRect();
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

    const qreal slide_pixel_scale = slide_rect.width() / qreal(current_slide_image_.width());
    return std::max<qreal>(1.0, qreal(eraser_thickness_) * slide_pixel_scale);
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
    const QSize slide_bounds = deck_overview_thumbnail_bounding_pixel_size();
    const int tile_height = slide_bounds.height() + kDeckOverviewFooterHeight;
    const int vertical_spacing = std::max(16, viewport.height() / 36);
    const int row = std::max(0, current_page_index_) / kDeckOverviewColumns;
    const int focused_center_y = row * (tile_height + vertical_spacing) + tile_height / 2;
    deck_overview_scroll_y_ = std::clamp(focused_center_y - viewport.height() / 2, 0, deck_overview_max_scroll_y());

    request_visible_deck_overview_renders();
    update();
}

void AudienceWindow::exit_deck_overview() {
    if (!deck_overview_visible_) {
        return;
    }

    deck_overview_visible_ = false;
    deck_overview_scroll_y_ = 0;
    // An empty size withdraws the grid's thumbnail request so it no longer competes with slides.
    emit deck_overview_renders_requested(QSize(), current_page_index_, -1, -1);
    update_cursor_appearance();
    update();
}

void AudienceWindow::draw_deck_overview(QPainter& painter) {
    const QRect viewport = deck_overview_viewport_rect();
    const QRect grid_viewport = viewport.adjusted(0, 0, -kDeckOverviewScrollbarGutter, 0);
    const QSize slide_bounds = deck_overview_thumbnail_bounding_pixel_size();
    const int horizontal_spacing = std::max(14, grid_viewport.width() / 70);
    const int vertical_spacing = std::max(16, viewport.height() / 36);
    const int tile_width = slide_bounds.width();
    const int tile_height = slide_bounds.height() + kDeckOverviewFooterHeight;
    const int grid_width = kDeckOverviewColumns * tile_width + (kDeckOverviewColumns - 1) * horizontal_spacing;
    const int left = grid_viewport.left() + std::max(0, (grid_viewport.width() - grid_width) / 2);
    deck_overview_scroll_y_ = std::clamp(deck_overview_scroll_y_, 0, deck_overview_max_scroll_y());
    const int top = viewport.top() - deck_overview_scroll_y_;

    painter.save();
    painter.fillRect(rect(), QColor(0x12, 0x12, 0x12));
    painter.setClipRect(viewport);

    for (int page = 0; page < page_count_; ++page) {
        const int row = page / kDeckOverviewColumns;
        const int column = page % kDeckOverviewColumns;
        const QRect tile_rect(
            left + column * (tile_width + horizontal_spacing),
            top + row * (tile_height + vertical_spacing),
            tile_width,
            tile_height);
        if (!tile_rect.intersects(viewport)) {
            continue;
        }

        const QRect slide_frame(tile_rect.left(), tile_rect.top(), tile_rect.width(), slide_bounds.height());
        painter.fillRect(slide_frame, QColor(0x08, 0x08, 0x08));

        const QImage image = deck_overview_images_.value(page);
        if (!image.isNull()) {
            const QRect image_rect = centered_rect_for_image(image.size(), slide_frame.adjusted(1, 1, -1, -1));
            painter.drawImage(image_rect, image);
        } else {
            painter.setPen(QColor(0x8a, 0x8a, 0x8a));
            painter.drawText(slide_frame, Qt::AlignCenter, QStringLiteral("..."));
        }

        const bool is_selected = page == current_page_index_;
        painter.setPen(QPen(is_selected ? QColor(0x00, 0xb3, 0xb3) : QColor(0x3a, 0x3a, 0x3a), is_selected ? 4 : 1));
        painter.drawRect(slide_frame.adjusted(0, 0, -1, -1));

        QRect number_rect(tile_rect.left(), slide_frame.bottom() + 5, tile_rect.width() - 2, kDeckOverviewFooterHeight - 5);
        painter.setPen(is_selected ? QColor(0xff, 0xff, 0xff) : QColor(0xb0, 0xb0, 0xb0));
        painter.drawText(number_rect, Qt::AlignRight | Qt::AlignVCenter, QString::number(page + 1));
    }

    if (deck_overview_max_scroll_y() > 0) {
        const QRect track(viewport.right() - kDeckOverviewScrollTrackWidth, viewport.top(), kDeckOverviewScrollTrackWidth, viewport.height());
        const qreal visible_fraction = qreal(viewport.height()) / qreal(deck_overview_content_height());
        const int handle_height = std::max(36, int(track.height() * visible_fraction));
        const int handle_y = track.top() + int(qreal(track.height() - handle_height) * qreal(deck_overview_scroll_y_) / qreal(deck_overview_max_scroll_y()));
        painter.fillRect(track, QColor(0x2c, 0x2c, 0x2c));
        painter.fillRect(QRect(track.left(), handle_y, kDeckOverviewScrollTrackWidth, handle_height), QColor(0x00, 0xb3, 0xb3));
    }

    painter.restore();
}

QSize AudienceWindow::deck_overview_thumbnail_bounding_pixel_size() const {
    const QRect viewport = deck_overview_viewport_rect().adjusted(0, 0, -kDeckOverviewScrollbarGutter, 0);
    const int horizontal_spacing = std::max(14, viewport.width() / 70);
    const int available_width = std::max(0, viewport.width() - (kDeckOverviewColumns - 1) * horizontal_spacing);
    const int slide_width = std::max(96, available_width / kDeckOverviewColumns);
    const int slide_height = std::max(54, int(qreal(slide_width) * 9.0 / 16.0));
    return QSize(slide_width, slide_height);
}

QRect AudienceWindow::deck_overview_viewport_rect() const {
    const int horizontal_margin = std::clamp(width() / 28, 28, 72);
    const int vertical_margin = std::clamp(height() / 24, 24, 64);
    return rect().adjusted(horizontal_margin, vertical_margin, -horizontal_margin, -vertical_margin);
}

int AudienceWindow::deck_overview_content_height() const {
    if (page_count_ <= 0) {
        return 0;
    }

    const QRect viewport = deck_overview_viewport_rect();
    const QSize slide_bounds = deck_overview_thumbnail_bounding_pixel_size();
    const int vertical_spacing = std::max(16, viewport.height() / 36);
    const int rows = (page_count_ + kDeckOverviewColumns - 1) / kDeckOverviewColumns;
    return rows * (slide_bounds.height() + kDeckOverviewFooterHeight) + std::max(0, rows - 1) * vertical_spacing;
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

    const QRect grid_viewport = viewport.adjusted(0, 0, -kDeckOverviewScrollbarGutter, 0);
    if (!grid_viewport.contains(position)) {
        return -1;
    }

    const QSize slide_bounds = deck_overview_thumbnail_bounding_pixel_size();
    const int horizontal_spacing = std::max(14, grid_viewport.width() / 70);
    const int vertical_spacing = std::max(16, viewport.height() / 36);
    const int tile_width = slide_bounds.width();
    const int tile_height = slide_bounds.height() + kDeckOverviewFooterHeight;
    const int grid_width = kDeckOverviewColumns * tile_width + (kDeckOverviewColumns - 1) * horizontal_spacing;
    const int left = grid_viewport.left() + std::max(0, (grid_viewport.width() - grid_width) / 2);
    const int content_x = position.x() - left;
    const int content_y = position.y() - viewport.top() + deck_overview_scroll_y_;
    if (content_x < 0 || content_y < 0) {
        return -1;
    }

    const int column_stride = tile_width + horizontal_spacing;
    const int row_stride = tile_height + vertical_spacing;
    const int column = content_x / column_stride;
    const int row = content_y / row_stride;
    if (column < 0 || column >= kDeckOverviewColumns) {
        return -1;
    }
    if (content_x % column_stride >= tile_width || content_y % row_stride >= tile_height) {
        return -1;
    }

    const int page_index = row * kDeckOverviewColumns + column;
    return page_index >= 0 && page_index < page_count_ ? page_index : -1;
}

void AudienceWindow::scroll_deck_overview_by(int delta_y) {
    const int next_scroll_y = std::clamp(deck_overview_scroll_y_ + delta_y, 0, deck_overview_max_scroll_y());
    if (next_scroll_y == deck_overview_scroll_y_) {
        return;
    }

    deck_overview_scroll_y_ = next_scroll_y;
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

    // Preserve the last completed OpenGL frame beneath the embedded menu,
    // avoiding live OpenGL composition while keeping the visual state stable.
    if (molecule_widget_ && molecule_widget_->isVisible()) {
        capture_molecule_frame();
        molecule_suspended_for_feature_menu_ = true;
        molecule_widget_->hide();
        repaint();
    }
    if (interactive_figure_widget_ && interactive_figure_widget_->isVisible()) {
        capture_interactive_figure_frame();
        interactive_figure_suspended_for_feature_menu_ = true;
        interactive_figure_widget_->hide();
        repaint();
    }
    capture_atomic_orbital_frame();
    atomic_orbital_suspended_for_feature_menu_ = true;
    update_atomic_orbital_overlay_geometry();
    repaint();
    if (molecular_symmetry_widget_ && molecular_symmetry_widget_->isVisible()) {
        capture_molecular_symmetry_frame();
        molecular_symmetry_suspended_for_feature_menu_ = true;
        molecular_symmetry_widget_->hide();
        repaint();
    }

    auto* menu = new FeatureMenuPanel(this);
    feature_menu_ = menu;

    connect(menu, &QObject::destroyed, this, [this, menu] {
        if (feature_menu_ == menu) {
            feature_menu_ = nullptr;
        }
        setFocus(Qt::ActiveWindowFocusReason);
        if (deck_overview_visible_) {
            cursor_hide_timer_.stop();
            setCursor(QCursor(Qt::ArrowCursor));
        } else {
            update_cursor_appearance();
        }
        molecule_suspended_for_feature_menu_ = false;
        interactive_figure_suspended_for_feature_menu_ = false;
        atomic_orbital_suspended_for_feature_menu_ = false;
        molecular_symmetry_suspended_for_feature_menu_ = false;
        update_molecule_overlay_geometry();
        update_interactive_figure_overlay_geometry();
        update_atomic_orbital_overlay_geometry();
        update_molecular_symmetry_overlay_geometry();
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

    QString default_directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (default_directory.isEmpty()) {
        default_directory = QDir::homePath();
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Annotated Slide"),
        QDir(default_directory).filePath(QStringLiteral("annotated-slide.png")),
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
