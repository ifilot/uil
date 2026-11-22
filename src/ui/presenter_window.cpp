#include "presenter_window.hpp"

#include "ui/deck_overview_visibility.hpp"
#include "app_controller.hpp"
#include "launcher/launcher_protocol.h"
#include "ui/bluecurve.hpp"
#include "ui/font_awesome.hpp"
#include "util/image_util.hpp"
#include "util/example_presentations.hpp"
#include "util/launcher_readiness.hpp"
#include "util/performance_log.hpp"
#include "util/spotlight_detector.hpp"

#include <QAction>
#include <QApplication>
#include <QBitmap>
#include <QComboBox>
#include <QCoreApplication>
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
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr int kMaxRecentPdfPaths = 5;
constexpr auto kRecentPdfPathsKey = "recent_pdf_paths";
constexpr auto kLastOpenDirectoryKey = "lastOpenDirectory";
constexpr auto kWindowGeometryKey = "presenterWindowGeometry";
constexpr auto kWindowStateKey = "presenterWindowState";
constexpr auto kAudienceScreenNameKey = "audienceScreenName";
constexpr int kResizeBorderWidth = 6;
constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 800;
constexpr qreal kWindowCornerRadius = 12.0;

/** @brief Escapes a filesystem path for display in a Qt menu. */
QString menu_safe_path_text(QString path) {
    return path.replace(QStringLiteral("&"), QStringLiteral("&&"));
}

/** @brief Returns whether one widget is a descendant of another. */
bool is_descendant_of(const QWidget* widget, const QWidget* ancestor) {
    for (const QWidget* current = widget; current; current = current->parentWidget()) {
        if (current == ancestor) {
            return true;
        }
    }

    return false;
}

/** @brief Returns the shared visible-overlay thumbnail icon. */
const QIcon& thumbnail_overlay_visible_icon() {
    static const QIcon icon = font_awesome::icon(
        font_awesome::Style::Regular,
        QStringLiteral("eye"),
        QColor(0xff, 0xff, 0xff),
        QSize(16, 16));
    return icon;
}

/** @brief Returns the shared hidden-overlay thumbnail icon. */
const QIcon& thumbnail_overlay_hidden_icon() {
    static const QIcon icon = font_awesome::icon(
        font_awesome::Style::Regular,
        QStringLiteral("eye-slash"),
        QColor(0x8a, 0x8a, 0x8a),
        QSize(16, 16));
    return icon;
}

/** @brief Returns the shared clear-overlay thumbnail icon. */
const QIcon& thumbnail_clear_overlay_icon() {
    static const QIcon icon = bluecurve::icon(QStringLiteral("stock-clear"));
    return icon;
}
}

SlidePreview::SlidePreview(QWidget* parent)
    : QLabel(parent) {
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
}

void SlidePreview::set_preview_image(const QImage& image) {
    image_ = image;
    update();
}

void SlidePreview::set_overlay_image(const QImage& image) {
    overlay_image_ = image;
    update();
}

void SlidePreview::set_overlay_visible(bool visible) {
    if (overlay_visible_ == visible) {
        return;
    }

    overlay_visible_ = visible;
    update();
}

void SlidePreview::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x11, 0x11, 0x11));

    if (image_.isNull()) {
        painter.setPen(QColor(0x85, 0x85, 0x85));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No slide"));
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect target = centered_rect_for_image(image_.size(), rect().adjusted(8, 8, -8, -8));
    painter.drawImage(target, image_);
    if (overlay_visible_ && !overlay_image_.isNull()) {
        painter.drawImage(target, overlay_image_);
    }
}

class SlideThumbnail final : public QWidget {
public:
    /** @brief Constructs a thumbnail for one presentation page. */
    explicit SlideThumbnail(int page_index, QWidget* parent = nullptr)
        : QWidget(parent),
          overlay_visible_icon_(thumbnail_overlay_visible_icon()),
          overlay_hidden_icon_(thumbnail_overlay_hidden_icon()),
          clear_overlay_icon_(thumbnail_clear_overlay_icon()),
          page_index_(page_index) {
        setObjectName(QStringLiteral("slideThumbnail"));
        setFixedSize(180, 128);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
        setToolTip(QStringLiteral("Click the thumbnail to jump to this slide. Click the eye to show or hide its drawing overlay. Click the eraser to clear this slide's overlay."));
    }

    /** @brief Returns the zero-based page represented by this thumbnail. */
    int page_index() const {
        return page_index_;
    }

    /** @brief Sets whether this thumbnail represents the selected page. */
    void set_selected(bool selected) {
        if (selected_ == selected) {
            return;
        }
        selected_ = selected;
        update();
    }

    /** @brief Sets the rendered slide image. */
    void set_image(const QImage& image) {
        image_ = image;
        update();
    }

    /** @brief Sets the annotation overlay image. */
    void set_overlay_image(const QImage& image) {
        overlay_image_ = image;
        update();
    }

    /** @brief Sets per-page annotation-overlay visibility. */
    void set_overlay_visible(bool visible) {
        if (overlay_visible_ == visible) {
            return;
        }

        overlay_visible_ = visible;
        update();
    }

    /** @brief Sets global annotation-overlay visibility. */
    void set_overlays_globally_visible(bool visible) {
        if (overlays_globally_visible_ == visible) {
            return;
        }

        overlays_globally_visible_ = visible;
        update();
    }

    /** @brief Called with the page index when the thumbnail is clicked. */
    std::function<void(int)> activated;
    /** @brief Called with the page index and new state when the overlay eye icon is toggled. */
    std::function<void(int, bool)> overlay_visibility_changed;
    /** @brief Called with the page index when the clear-overlay icon is clicked. */
    std::function<void(int)> overlay_clear_requested;

protected:
    /** @brief Handles Qt hover state changes. */
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Leave) {
            set_hovered_button(HoveredButton::None);
        }

        return QWidget::event(event);
    }

    /** @brief Handles thumbnail activation and overlay controls. */
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && overlay_icon_rect().contains(event->position().toPoint())) {
            overlay_visible_ = !overlay_visible_;
            update();
            if (overlay_visibility_changed) {
                overlay_visibility_changed(page_index_, overlay_visible_);
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && clear_overlay_icon_rect().contains(event->position().toPoint())) {
            if (overlay_clear_requested) {
                overlay_clear_requested(page_index_);
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && activated) {
            activated(page_index_);
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    /** @brief Updates overlay-control hover state. */
    void mouseMoveEvent(QMouseEvent* event) override {
        if (overlay_icon_rect().contains(event->position().toPoint())) {
            set_hovered_button(HoveredButton::OverlayVisibility);
        } else if (clear_overlay_icon_rect().contains(event->position().toPoint())) {
            set_hovered_button(HoveredButton::ClearOverlay);
        } else {
            set_hovered_button(HoveredButton::None);
        }

        QWidget::mouseMoveEvent(event);
    }

    /** @brief Paints the slide thumbnail and its overlay controls. */
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(0x1e, 0x1e, 0x1e));

        const QRect slide_frame = rect().adjusted(10, 8, -10, -24);
        painter.fillRect(slide_frame, QColor(0x11, 0x11, 0x11));

        if (!image_.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QRect image_rect = centered_rect_for_image(image_.size(), slide_frame.adjusted(1, 1, -1, -1));
            painter.drawImage(image_rect, image_);
            if (overlay_visible_ && overlays_globally_visible_ && !overlay_image_.isNull()) {
                painter.drawImage(image_rect, overlay_image_);
            }
        } else {
            painter.setPen(QColor(0x85, 0x85, 0x85));
            painter.drawText(slide_frame, Qt::AlignCenter, QStringLiteral("..."));
        }

        const QColor border_color = selected_ ? QColor(0x00, 0x8c, 0x8c) : QColor(0x3c, 0x3c, 0x3c);
        painter.setPen(QPen(border_color, selected_ ? 2 : 1));
        painter.drawRect(slide_frame.adjusted(0, 0, -1, -1));

        const QRect icon_rect = overlay_icon_rect();
        draw_icon_highlight(painter, icon_rect, hovered_button_ == HoveredButton::OverlayVisibility);
        draw_icon_highlight(painter, clear_overlay_icon_rect(), hovered_button_ == HoveredButton::ClearOverlay);
        const QIcon& overlay_icon = overlay_visible_ ? overlay_visible_icon_ : overlay_hidden_icon_;
        overlay_icon.paint(&painter, icon_rect);
        clear_overlay_icon_.paint(&painter, clear_overlay_icon_rect());

        painter.setPen(selected_ ? QColor(0xff, 0xff, 0xff) : QColor(0x8a, 0x8a, 0x8a));
        painter.drawText(QRect(width() - 52, height() - 21, 42, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(page_index_ + 1));
    }

private:
    enum class HoveredButton {
        None,
        OverlayVisibility,
        ClearOverlay
    };

    /** @brief Sets the thumbnail control currently under the pointer. */
    void set_hovered_button(HoveredButton hovered_button) {
        if (hovered_button_ == hovered_button) {
            return;
        }

        hovered_button_ = hovered_button;
        update();
    }

    /** @brief Draws hover emphasis behind a thumbnail control icon. */
    void draw_icon_highlight(QPainter& painter, const QRect& icon_rect, bool hovered) const {
        if (!hovered) {
            return;
        }

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(QColor(0x00, 0x8c, 0x8c));
        painter.setPen(QPen(QColor(0x00, 0xb3, 0xb3), 1));
        painter.drawRoundedRect(icon_rect.adjusted(-4, -2, 4, 2), 3, 3);
        painter.restore();
    }

    /** @brief Returns the overlay-visibility control rectangle. */
    QRect overlay_icon_rect() const {
        const int group_width = 42;
        return QRect((width() - group_width) / 2, height() - 21, 18, 18);
    }

    /** @brief Returns the clear-overlay control rectangle. */
    QRect clear_overlay_icon_rect() const {
        return overlay_icon_rect().translated(24, 0);
    }

    QImage image_;
    QImage overlay_image_;
    QIcon overlay_visible_icon_;
    QIcon overlay_hidden_icon_;
    QIcon clear_overlay_icon_;
    int page_index_ = -1;
    bool selected_ = false;
    bool overlay_visible_ = true;
    bool overlays_globally_visible_ = true;
    HoveredButton hovered_button_ = HoveredButton::None;
};

class SlideDeckOverview final : public QScrollArea {
public:
    /** @brief Constructs the scrollable deck-overview widget. */
    explicit SlideDeckOverview(QWidget* parent = nullptr)
        : QScrollArea(parent),
          content_(new QWidget(this)),
          grid_(new QGridLayout(content_)),
          render_request_timer_(new QTimer(this)) {
        setObjectName(QStringLiteral("deckOverview"));
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        grid_->setContentsMargins(8, 8, 8, 8);
        grid_->setHorizontalSpacing(8);
        grid_->setVerticalSpacing(8);
        content_->setObjectName(QStringLiteral("deckOverviewContent"));
        setWidget(content_);
        render_request_timer_->setSingleShot(true);
        render_request_timer_->setInterval(30);
        connect(render_request_timer_, &QTimer::timeout, this, [this] {
            request_renders();
        });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            schedule_render_request();
        });
    }

    /** @brief Returns the requested pixel size for overview thumbnails. */
    QSize thumbnail_bounding_pixel_size() const {
        return QSize(176, 104);
    }

    /** @brief Rebuilds overview thumbnails for a new page count. */
    void set_page_count(int page_count) {
        performance_log::ScopedSpan span(
            QStringLiteral("ui.deck_overview.rebuild"),
            {{QStringLiteral("page_count"), page_count}});
        while (QLayoutItem* item = grid_->takeAt(0)) {
            delete item;
        }
        if (row_count_ > 0) {
            grid_->setRowStretch(row_count_, 0);
        }
        if (column_count_ > 0) {
            grid_->setColumnStretch(column_count_, 0);
        }
        qDeleteAll(thumbnails_);
        thumbnails_.clear();
        current_page_ = -1;
        column_count_ = 0;
        row_count_ = 0;
        span.checkpoint(QStringLiteral("clear_previous_thumbnails"));

        for (int page = 0; page < page_count; ++page) {
            auto* thumbnail = new SlideThumbnail(page, content_);
            thumbnail->activated = [this](int page_index) {
                if (page_activated) {
                    page_activated(page_index);
                }
            };
            thumbnail->overlay_visibility_changed = [this](int page_index, bool visible) {
                if (page_overlay_visibility_changed) {
                    page_overlay_visibility_changed(page_index, visible);
                }
            };
            thumbnail->overlay_clear_requested = [this](int page_index) {
                if (page_overlay_clear_requested) {
                    page_overlay_clear_requested(page_index);
                }
            };
            thumbnails_.append(thumbnail);
        }
        span.checkpoint(
            QStringLiteral("construct_thumbnails"),
            {{QStringLiteral("thumbnail_count"), thumbnails_.size()}});

        relayout();
        span.checkpoint(QStringLiteral("layout_thumbnails"));
        schedule_render_request();
        span.checkpoint(QStringLiteral("schedule_renders"));
        span.set_outcome(QStringLiteral("ready"));
    }

    /** @brief Selects and reveals the current overview page. */
    void set_current_page(int page_index) {
        current_page_ = page_index;
        for (SlideThumbnail* thumbnail : std::as_const(thumbnails_)) {
            thumbnail->set_selected(thumbnail->page_index() == page_index);
        }

        if (page_index >= 0 && page_index < thumbnails_.size()) {
            ensureWidgetVisible(thumbnails_.at(page_index), 24, 24);
        }
        schedule_render_request();
    }

    /** @brief Assigns a rendered image to one overview thumbnail. */
    void set_slide_image(int page_index, const QSize& bounding_pixel_size, const QImage& image) {
        if (bounding_pixel_size != thumbnail_bounding_pixel_size()
            || page_index < 0
            || page_index >= thumbnails_.size()) {
            return;
        }

        thumbnails_.at(page_index)->set_image(image);
    }

    /** @brief Sets annotation visibility for one overview thumbnail. */
    void set_page_overlay_visible(int page_index, bool visible) {
        if (page_index < 0 || page_index >= thumbnails_.size()) {
            return;
        }

        thumbnails_.at(page_index)->set_overlay_visible(visible);
    }

    /** @brief Assigns an annotation image to one overview thumbnail. */
    void set_page_overlay_image(int page_index, const QImage& image) {
        if (page_index < 0 || page_index >= thumbnails_.size()) {
            return;
        }

        thumbnails_.at(page_index)->set_overlay_image(image);
    }

    /** @brief Sets global annotation visibility for every overview thumbnail. */
    void set_overlays_globally_visible(bool visible) {
        for (SlideThumbnail* thumbnail : std::as_const(thumbnails_)) {
            thumbnail->set_overlays_globally_visible(visible);
        }
    }

    /** @brief Forwarded SlideThumbnail::activated. */
    std::function<void(int)> page_activated;
    /** @brief Forwarded SlideThumbnail::overlay_visibility_changed. */
    std::function<void(int, bool)> page_overlay_visibility_changed;
    /** @brief Forwarded SlideThumbnail::overlay_clear_requested. */
    std::function<void(int)> page_overlay_clear_requested;
    /** @brief Requests thumbnails: (bounding pixel size, focused page, first visible, last visible). */
    std::function<void(const QSize&, int, int, int)> render_batch_requested;

protected:
    /** @brief Relays out thumbnails after a Qt resize event. */
    void resizeEvent(QResizeEvent* event) override {
        QScrollArea::resizeEvent(event);
        relayout();
        schedule_render_request();
    }

    /** @brief Requests visible thumbnails after Qt has shown and laid out the viewport. */
    void showEvent(QShowEvent* event) override {
        QScrollArea::showEvent(event);
        relayout();
        schedule_render_request();
    }

private:
    /** @brief Arranges overview thumbnails into the available columns. */
    void relayout() {
        const int tile_width = 180;
        const int available_width = qMax(tile_width, viewport()->width() - 16);
        const int columns = qMax(1, available_width / (tile_width + grid_->horizontalSpacing()));
        if (columns == column_count_ && grid_->count() == thumbnails_.size()) {
            return;
        }

        while (QLayoutItem* item = grid_->takeAt(0)) {
            delete item;
        }
        if (row_count_ > 0) {
            grid_->setRowStretch(row_count_, 0);
        }
        if (column_count_ > 0) {
            grid_->setColumnStretch(column_count_, 0);
        }
        for (int i = 0; i < thumbnails_.size(); ++i) {
            grid_->addWidget(thumbnails_.at(i), i / columns, i % columns);
        }
        row_count_ = (thumbnails_.size() + columns - 1) / columns;
        column_count_ = columns;
        grid_->setRowStretch(row_count_, 1);
        grid_->setColumnStretch(columns, 1);
        grid_->activate();
    }

    /** @brief Coalesces render requests caused by layout, selection, and scrolling. */
    void schedule_render_request() {
        if (render_request_timer_) {
            render_request_timer_->start();
        }
    }

    /** @brief Returns whether scroll geometry is ready for a reliable visibility query. */
    bool is_visibility_layout_ready() const {
        if (!isVisible()) {
            return false;
        }

        const bool content_needs_scrolling = grid_->sizeHint().height() > viewport()->height();
        return !content_needs_scrolling || verticalScrollBar()->maximum() > 0;
    }

    /** @brief Returns the first and last thumbnail intersecting the viewport. */
    QPair<int, int> visible_page_range() const {
        return deck_overview::visible_page_range(
            thumbnails_,
            viewport(),
            current_page_);
    }

    /** @brief Requests renders for visible thumbnails and a controller prefetch margin. */
    void request_renders() {
        if (!render_batch_requested || thumbnails_.isEmpty()) {
            return;
        }

        if (!is_visibility_layout_ready()) {
            schedule_render_request();
            return;
        }

        const auto [first_visible, last_visible] = visible_page_range();
        render_batch_requested(
            thumbnail_bounding_pixel_size(),
            current_page_,
            first_visible,
            last_visible);
    }

    QWidget* content_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QTimer* render_request_timer_ = nullptr;
    QList<SlideThumbnail*> thumbnails_;
    int current_page_ = -1;
    int column_count_ = 0;
    int row_count_ = 0;
};

class IconToolButton final : public QToolButton {
public:
    /** @brief Constructs a tool button with hover-dependent icons. */
    explicit IconToolButton(QWidget* parent = nullptr)
        : QToolButton(parent) {
    }

    /** @brief Sets icons used for normal and hovered button states. */
    void set_state_icons(const QIcon& normal_icon, const QIcon& hover_icon) {
        normal_icon_ = normal_icon;
        hover_icon_ = hover_icon;
        setIcon(is_hovered_ ? hover_icon_ : normal_icon_);
    }

protected:
    /** @brief Switches the icon in response to Qt hover events. */
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Enter) {
            is_hovered_ = true;
            setIcon(hover_icon_);
        } else if (event->type() == QEvent::Leave) {
            is_hovered_ = false;
            setIcon(normal_icon_);
        }

        return QToolButton::event(event);
    }

private:
    QIcon normal_icon_;
    QIcon hover_icon_;
    bool is_hovered_ = false;
};

class SlideNavButton final : public QToolButton {
public:
    /** @brief Constructs a direction-aware slide navigation button. */
    explicit SlideNavButton(const QString& text, const QString& icon_name, bool icon_on_right, QWidget* parent = nullptr)
        : QToolButton(parent),
          icon_on_right_(icon_on_right) {
        setObjectName(QStringLiteral("slideNavButton"));
        setText(text);
        setIcon(bluecurve::icon(icon_name));
        setIconSize(QSize(14, 14));
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setMinimumSize(sizeHint());
    }

    /** @brief Returns a size hint that fits the navigation icon and text. */
    QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        const int content_width = iconSize().width() + kIconTextSpacing + metrics.horizontalAdvance(text());
        return QSize(std::max(86, content_width + 28), 34);
    }

protected:
    /** @brief Paints navigation text and its direction-aware icon. */
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);

        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = {};
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);

        const QFontMetrics metrics(font());
        const int text_width = metrics.horizontalAdvance(text());
        const QSize icon_extent = iconSize();
        const int content_width = icon_extent.width() + kIconTextSpacing + text_width;
        const int content_left = (width() - content_width) / 2;
        const int center_y = height() / 2;
        const QRect text_rect(
            icon_on_right_ ? content_left : content_left + icon_extent.width() + kIconTextSpacing,
            0,
            text_width,
            height());
        const QRect icon_rect(
            icon_on_right_ ? content_left + text_width + kIconTextSpacing : content_left,
            center_y - icon_extent.height() / 2,
            icon_extent.width(),
            icon_extent.height());

        const QIcon::Mode iconMode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
        icon().paint(&painter, icon_rect, Qt::AlignCenter, iconMode, isDown() ? QIcon::On : QIcon::Off);
        const QPalette::ColorGroup colorGroup = isEnabled() ? QPalette::Active : QPalette::Disabled;
        painter.setPen(palette().color(colorGroup, QPalette::ButtonText));
        painter.drawText(text_rect, Qt::AlignCenter, text());
    }

private:
    static constexpr int kIconTextSpacing = 8;
    bool icon_on_right_ = false;
};

PresenterWindow::PresenterWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent),
      controller_(controller) {
    performance_log::ScopedSpan span(QStringLiteral("startup.presenter_window"));
    setWindowTitle(QStringLiteral("uil Presenter"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/uil.png")));
    setWindowFlag(Qt::FramelessWindowHint, true);
    span.checkpoint(QStringLiteral("configure_window"));
    create_actions();
    span.checkpoint(QStringLiteral("create_actions"));
    spotlight_detector_ = new SpotlightDetector(this);
    create_layout();
    span.checkpoint(QStringLiteral("create_layout"));
    create_connections();
    span.checkpoint(QStringLiteral("create_connections"));
    qApp->installEventFilter(this);
    update_screen_list();
    span.checkpoint(QStringLiteral("update_screen_list"));
    load_settings();
    span.checkpoint(QStringLiteral("load_settings"));
    if (size().isEmpty()) {
        resize(kDefaultWindowWidth, kDefaultWindowHeight);
    }
    window_mask_timer_ = new QTimer(this);
    window_mask_timer_->setSingleShot(true);
    window_mask_timer_->setInterval(50);
    connect(
        window_mask_timer_,
        &QTimer::timeout,
        this,
        &PresenterWindow::apply_rounded_window_mask);
    apply_rounded_window_mask();
    span.checkpoint(QStringLiteral("apply_window_mask"));
    span.set_outcome(QStringLiteral("ready"));
}

PresenterWindow::~PresenterWindow() {
    qApp->removeEventFilter(this);
    clear_resize_cursor();
}

bool PresenterWindow::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool is_presenter_widget = widget && (widget == this || isAncestorOf(widget)) && widget->window() == this;

    if (!is_presenter_widget) {
        if (resize_cursor_active_ && event->type() == QEvent::MouseMove) {
            clear_resize_cursor();
        }

        return QMainWindow::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonDblClick: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton
            && is_title_drag_area_at(mouse_event->globalPosition().toPoint())) {
            toggle_maximized();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() != Qt::LeftButton) {
            break;
        }

        const QPoint global_position = mouse_event->globalPosition().toPoint();
        const Qt::Edges edges = resize_edges_at(mapFromGlobal(global_position));
        if (edges) {
            clear_resize_cursor();
            if (windowHandle() && windowHandle()->startSystemResize(edges)) {
                return true;
            }

            begin_manual_resize(edges, global_position);
            return true;
        }

        if (is_title_drag_area_at(global_position)) {
            clear_resize_cursor();
            if (windowHandle() && windowHandle()->startSystemMove()) {
                return true;
            }

            begin_manual_move(global_position);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        const QPoint global_position = mouse_event->globalPosition().toPoint();
        if (manual_resize_active_) {
            update_manual_resize(global_position);
            return true;
        }

        if (manual_move_active_) {
            update_manual_move(global_position);
            return true;
        }

        update_resize_cursor(resize_edges_at(mapFromGlobal(global_position)));
        break;
    }
    case QEvent::MouseButtonRelease:
        finish_manual_resize();
        finish_manual_move();
        break;
    case QEvent::Leave:
        if (!manual_resize_active_) {
            clear_resize_cursor();
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
        clear_resize_cursor();
        update_maximize_button();
        apply_rounded_window_mask();
    }
}

void PresenterWindow::closeEvent(QCloseEvent* event) {
    if (!confirm_unsaved_annotations()) {
        event->ignore();
        return;
    }
    save_settings();
    controller_->close_audience_window();
    QMainWindow::closeEvent(event);
}

void PresenterWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    schedule_window_mask_update();
}

void PresenterWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!first_show_recorded_) {
        first_show_recorded_ = true;
        performance_log::record_duration(
            QStringLiteral("startup.process_to_presenter_show"),
            performance_log::process_elapsed_ms());
    }
}

void PresenterWindow::paintEvent(QPaintEvent* event) {
    QMainWindow::paintEvent(event);
    if (!first_paint_recorded_) {
        first_paint_recorded_ = true;
        performance_log::record_duration(
            QStringLiteral("startup.process_to_first_presenter_paint"),
            performance_log::process_elapsed_ms());
        launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_FIRST_PAINT_READY);
        QTimer::singleShot(0, this, [] {
            launcher_readiness::signal_ready();
        });
    }
}

void PresenterWindow::open_pdf() {
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Presentation"),
        settings.value(QString::fromLatin1(kLastOpenDirectoryKey)).toString(),
        QStringLiteral("Presentations (*.pdf *.uil);;PDF files (*.pdf);;UIL packages (*.uil);;All files (*.*)"));

    if (path.isEmpty()) {
        return;
    }

    open_pdf_path(path);
}

bool PresenterWindow::save_package() {
    const QString package_path = controller_->current_package_path();
    if (package_path.isEmpty()) {
        return save_package_as();
    }

    return save_package_to_path(package_path);
}

bool PresenterWindow::save_package_as() {
    if (controller_->current_path().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Save UIL Package"), QStringLiteral("Open a presentation before saving."));
        return false;
    }

    QSettings settings;
    QString suggested_path = controller_->current_package_path();
    if (suggested_path.isEmpty()) {
        const QFileInfo current_info(controller_->current_path());
        suggested_path = QFileInfo(
            settings.value(QString::fromLatin1(kLastOpenDirectoryKey), current_info.absolutePath()).toString(),
            current_info.completeBaseName() + QStringLiteral(".uil")).absoluteFilePath();
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save UIL Package"),
        suggested_path,
        QStringLiteral("UIL packages (*.uil);;All files (*.*)"));
    if (path.isEmpty()) {
        return false;
    }

    if (QFileInfo(path).suffix().compare(QStringLiteral("uil"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".uil");
    }

    return save_package_to_path(path);
}

bool PresenterWindow::confirm_unsaved_annotations() {
    if (!controller_ || !controller_->has_unsaved_annotations()) {
        return true;
    }

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Warning);
    prompt.setWindowTitle(QStringLiteral("Unsaved Annotations"));
    prompt.setText(QStringLiteral("Save your annotations before closing this presentation?"));
    prompt.setInformativeText(
        QStringLiteral("Drawings are kept only when saved as a .uil package."));
    prompt.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    prompt.setDefaultButton(QMessageBox::Save);
    switch (prompt.exec()) {
    case QMessageBox::Save:
        return save_package();
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

void PresenterWindow::export_as_pdf() {
    if (controller_->current_path().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export as PDF"), QStringLiteral("Open a presentation before exporting."));
        return;
    }

    // For a .uil package, current_path() is the extracted PDF in a temporary directory.
    const QFileInfo current_info(controller_->document_display_path());
    QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export as PDF"),
        QFileInfo(current_info.absolutePath(), current_info.completeBaseName() + QStringLiteral("-annotated.pdf")).absoluteFilePath(),
        QStringLiteral("PDF files (*.pdf);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".pdf");
    }

    QString error_message;
    if (!controller_->export_annotated_pdf(path, package_overlay_images_for_save(), &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Export as PDF"), error_message);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Exported annotated PDF: %1").arg(QFileInfo(path).absoluteFilePath()));
}

void PresenterWindow::jump_to_page() {
    const int page_count = controller_->page_count();
    if (page_count <= 0) {
        return;
    }

    bool ok = false;
    const int page = QInputDialog::getInt(
        this,
        QStringLiteral("Jump to Page"),
        QStringLiteral("Page:"),
        controller_->current_page() + 1,
        1,
        page_count,
        1,
        &ok);
    if (ok) {
        controller_->go_to_page(page - 1);
    }
}

void PresenterWindow::show_slide_overview() {
    const int page_count = controller_->page_count();
    if (page_count <= 0) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Slide Overview"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    for (int i = 0; i < page_count; ++i) {
        auto* item = new QListWidgetItem(QStringLiteral("Page %1").arg(i + 1), list);
        item->setData(Qt::UserRole, i);
    }
    list->setCurrentRow(controller_->current_page());
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
        controller_->go_to_page(list->currentItem()->data(Qt::UserRole).toInt());
    }
}

void PresenterWindow::show_about() {
    QMessageBox about_box(this);
    about_box.setWindowTitle(QStringLiteral("About uil"));
    about_box.setWindowIcon(QIcon(QStringLiteral(":/icons/uil.png")));
    about_box.setIconPixmap(QIcon(QStringLiteral(":/icons/uil.png")).pixmap(QSize(96, 96)));
    about_box.setTextFormat(Qt::RichText);
    about_box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    about_box.setText(QStringLiteral(
        "<h2>uil %1</h2>"
        "<p>Pronunciation: /&#8239;&oelig;yl&#8239;/</p>"
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

    const QString build_config = QStringLiteral(UIL_BUILD_CONFIG).isEmpty()
        ? QStringLiteral("unspecified")
        : QStringLiteral(UIL_BUILD_CONFIG);
    const QString compiler = QStringLiteral(UIL_COMPILER_ID " " UIL_COMPILER_VERSION).trimmed();
    const QString compiler_timestamp = QStringLiteral(__DATE__ " " __TIME__);
    const QString cpp_standard = QString::number(__cplusplus);

    QString ffmpeg_line;
#ifdef UIL_HAVE_FFMPEG
    ffmpeg_line = QStringLiteral("<li><b>FFmpeg libraries</b>: libavformat, libavcodec, libavutil, libswscale for MP4/media frame extraction. FFmpeg is licensed under LGPL/GPL depending on the linked build configuration.</li>");
#else
    ffmpeg_line = QStringLiteral("<li><b>FFmpeg libraries</b>: optional media support dependency; not linked in this build.</li>");
#endif
    QString hidapi_line;
#ifdef UIL_HAVE_HIDAPI
    hidapi_line = QStringLiteral("<li><b>HIDAPI</b>: read-only USB HID enumeration for Logitech Spotlight receiver presence; BSD/GPL/custom licensing.</li>");
#else
    hidapi_line = QStringLiteral("<li><b>HIDAPI</b>: optional Logitech Spotlight presence-detection dependency; not linked in this build.</li>");
#endif
    QString informative_text = QStringLiteral(
        "<p>A Windows-focused Qt PDF presentation app for Beamer-style slide decks.</p>"
        "<p><b>Compilation details:</b></p>"
        "<ul>"
        "<li><b>Compiled</b>: %1.</li>"
        "<li><b>Compiler</b>: %2; C++ value %3.</li>"
        "<li><b>Build configuration</b>: %4; commit %5; CMake %6.</li>"
        "<li><b>Qt runtime</b>: %7; Qt build: %8.</li>"
        "</ul>"
        "<p><b>External packages and assets used by this build:</b></p>"
        "<ul>"
        "<li><b>Qt %7</b>: Core, Gui, Widgets, Pdf, and Svg modules for the application framework, PDF rendering, audience output, and SVG rendering. The Windows deployment also includes Qt plugins such as the platform and SVG icon plugins. Qt is available under LGPL/GPL/commercial licensing depending on distribution; Qt Pdf includes PDFium and its third-party components.</li>"
        "<li><b>zlib</b>: compression library used through ZLIB::ZLIB for PDF media stream handling and .uil package extraction; zlib License.</li>"
        "%9"
        "<li><b>Font Awesome Free 7.2.0</b>: vendored SVG icon assets under resources/fontawesome. The SVG icons are licensed under CC BY 4.0. The upstream package also includes MIT-licensed code and SIL OFL 1.1 fonts; this app uses the SVG assets. Copyright Fonticons, Inc.</li>"
        "<li><b>Red Hat Bluecurve icons</b>: a focused set of original size-specific action SVGs from the Bluecurve restoration project, licensed under GNU GPL v3.0.</li>"
        "<li><b>MSYS2/GCC runtime libraries</b>: deployed on Windows as needed by the toolchain and audited by the installer staging script.</li>"
        "</ul>"
        "<p>Application rendering, scheduling, caching, and presentation control code is local to uil.</p>"
        "<p>Windows installers include THIRD_PARTY_NOTICES.txt and third-party/package-inventory.tsv with the complete staged dependency inventory and copied license files.</p>")
        .arg(
            compiler_timestamp,
            compiler,
            cpp_standard,
            build_config,
            QStringLiteral(UIL_GIT_COMMIT),
            QStringLiteral(UIL_CMAKE_VERSION),
            QString::fromLatin1(qVersion()),
            QLibraryInfo::build(),
            ffmpeg_line + hidapi_line);
    about_box.setInformativeText(informative_text);
    about_box.setStandardButtons(QMessageBox::Ok);
    about_box.exec();
}

void PresenterWindow::start_presentation_mode() {
    if (controller_->page_count() > 0
        && !controller_->reload_current_document(AnnotationState{
            package_overlay_images_for_save(),
            hidden_overlay_pages_,
            show_audience_overlay_action_ ? show_audience_overlay_action_->isChecked() : true})) {
        return;
    }

    QScreen* primary_screen = QGuiApplication::primaryScreen();
    if (primary_screen) {
        if (windowHandle()) {
            windowHandle()->setScreen(primary_screen);
        }

        const QRect available_geometry = primary_screen->availableGeometry();
        const QSize target_size = size().boundedTo(available_geometry.size());
        const QPoint centered_top_left(
            available_geometry.x() + (available_geometry.width() - target_size.width()) / 2,
            available_geometry.y() + (available_geometry.height() - target_size.height()) / 2);

        showNormal();
        setGeometry(QRect(centered_top_left, target_size));
    }

    show();
    raise();
    activateWindow();

    if (screen_combo_) {
        if (QScreen* screen = screen_combo_->currentData().value<QScreen*>()) {
            if (screen != controller_->selected_audience_screen()) {
                controller_->set_audience_screen(screen);
            }
        }
    }
    controller_->enter_audience_fullscreen();
}

void PresenterWindow::update_media_label(const PdfMediaScanResult& result) {
    if (!result.has_media()) {
        media_label_->setText(QStringLiteral("Media: none"));
        media_label_->setToolTip(QString());
        return;
    }

    media_label_->setText(
        QStringLiteral("Media: %1 item(s)")
            .arg(result.annotations.size() + result.molecule_annotations.size()
                 + result.interactive_figure_annotations.size()
                 + result.atomic_orbital_annotations.size()));
    media_label_->setToolTip(result.summary());
    statusBar()->showMessage(result.summary());
}

void PresenterWindow::update_document_overview(int page_count) {
    if (!deck_overview_) {
        return;
    }

    hidden_overlay_pages_ = controller_->loaded_hidden_overlay_pages();
    page_overlay_images_ = controller_->loaded_overlay_images();
    if (show_audience_overlay_action_) {
        QSignalBlocker blocker(show_audience_overlay_action_);
        show_audience_overlay_action_->setChecked(controller_->loaded_overlays_globally_visible());
        update_overlay_visibility_button(show_audience_overlay_action_->isChecked());
    }
    deck_overview_->set_page_count(page_count);
    deck_overview_->set_overlays_globally_visible(show_audience_overlay_action_->isChecked());
    for (auto it = page_overlay_images_.constBegin(); it != page_overlay_images_.constEnd(); ++it) {
        deck_overview_->set_page_overlay_image(it.key(), it.value());
    }
    for (int page_index : std::as_const(hidden_overlay_pages_)) {
        deck_overview_->set_page_overlay_visible(page_index, false);
    }
    deck_overview_->set_current_page(controller_->current_page());
    update_current_preview_overlay_visibility();
}

void PresenterWindow::update_page_label(int page_index, int page_count) {
    if (page_count <= 0) {
        page_label_->setText(QStringLiteral("Page: - / -"));
        if (deck_overview_) {
            deck_overview_->set_current_page(-1);
        }
        update_current_preview_overlay_visibility();
        return;
    }

    page_label_->setText(QStringLiteral("Page: %1 / %2").arg(page_index + 1).arg(page_count));
    if (deck_overview_) {
        deck_overview_->set_current_page(page_index);
    }
    update_current_preview_overlay_visibility();
}

void PresenterWindow::update_screen_list() {
    QSignalBlocker blocker(screen_combo_);
    screen_combo_->clear();

    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        screen_combo_->addItem(screen->name(), QVariant::fromValue(screen));
    }

    update_audience_screen_selection(controller_->selected_audience_screen());
}

void PresenterWindow::update_audience_screen_selection(QScreen* screen) {
    if (!screen) {
        return;
    }

    for (int i = 0; i < screen_combo_->count(); ++i) {
        if (screen_combo_->itemData(i).value<QScreen*>() == screen) {
            QSignalBlocker blocker(screen_combo_);
            screen_combo_->setCurrentIndex(i);
            return;
        }
    }
}

void PresenterWindow::create_actions() {
    menu_bar_ = new QMenuBar(this);
    menu_bar_->setObjectName(QStringLiteral("titleMenuBar"));
    menu_bar_->setNativeMenuBar(false);
    menu_bar_->setFixedHeight(31);

    open_action_ = new QAction(QStringLiteral("&Open Presentation"), this);
    open_action_->setIcon(bluecurve::icon(QStringLiteral("stock-open")));
    open_action_->setShortcut(QKeySequence::Open);

    save_action_ = new QAction(QStringLiteral("&Save"), this);
    save_action_->setIcon(bluecurve::icon(QStringLiteral("stock-save")));
    save_action_->setShortcut(QKeySequence::Save);

    save_as_action_ = new QAction(QStringLiteral("Save &As"), this);
    save_as_action_->setIcon(bluecurve::icon(QStringLiteral("stock-save-as")));
    save_as_action_->setShortcut(QKeySequence::SaveAs);

    export_pdf_action_ = new QAction(QStringLiteral("Export as PDF"), this);
    export_pdf_action_->setIcon(bluecurve::icon(QStringLiteral("stock_save-pdf")));

    next_action_ = new QAction(QStringLiteral("Next"), this);
    next_action_->setIcon(bluecurve::icon(QStringLiteral("stock-go-forward")));
    next_action_->setShortcuts({
        QKeySequence(Qt::Key_Right),
        QKeySequence(Qt::Key_Down),
        QKeySequence(Qt::Key_PageDown),
        QKeySequence(Qt::Key_Space)
    });

    previous_action_ = new QAction(QStringLiteral("Previous"), this);
    previous_action_->setIcon(bluecurve::icon(QStringLiteral("stock-go-back")));
    previous_action_->setShortcuts({
        QKeySequence(Qt::Key_Left),
        QKeySequence(Qt::Key_Up),
        QKeySequence(Qt::Key_PageUp),
        QKeySequence(Qt::Key_Backspace)
    });

    first_action_ = new QAction(QStringLiteral("First"), this);
    first_action_->setIcon(bluecurve::icon(QStringLiteral("stock-goto-first")));
    first_action_->setShortcut(Qt::Key_Home);

    last_action_ = new QAction(QStringLiteral("Last"), this);
    last_action_->setIcon(bluecurve::icon(QStringLiteral("stock-goto-last")));
    last_action_->setShortcut(Qt::Key_End);

    start_presentation_action_ = new QAction(QStringLiteral("Reload and Start Presentation"), this);
    start_presentation_action_->setIcon(bluecurve::icon(QStringLiteral("icon-resize-screen")));
    start_presentation_action_->setShortcut(Qt::Key_F5);

    play_pause_media_action_ = new QAction(QStringLiteral("Play/Pause Media"), this);
    play_pause_media_action_->setIcon(bluecurve::icon(QStringLiteral("stock-media-pause")));
    play_pause_media_action_->setShortcut(Qt::Key_Return);

    jump_to_page_action_ = new QAction(QStringLiteral("Jump to Page"), this);
    jump_to_page_action_->setIcon(bluecurve::icon(QStringLiteral("stock-jump-to")));
    jump_to_page_action_->setShortcut(Qt::Key_J);

    slide_overview_action_ = new QAction(QStringLiteral("Slide Overview"), this);
    slide_overview_action_->setIcon(bluecurve::icon(QStringLiteral("stock_display-grid")));
    slide_overview_action_->setShortcut(Qt::Key_O);

    black_screen_action_ = new QAction(QStringLiteral("Black Screen"), this);
    black_screen_action_->setShortcut(Qt::Key_B);

    white_screen_action_ = new QAction(QStringLiteral("White Screen"), this);
    white_screen_action_->setShortcut(Qt::Key_W);

    fullscreen_action_ = new QAction(QStringLiteral("Toggle Audience Fullscreen"), this);
    fullscreen_action_->setIcon(bluecurve::icon(QStringLiteral("stock-fullscreen")));
    fullscreen_action_->setShortcut(Qt::Key_F11);

    show_audience_overlay_action_ = new QAction(QStringLiteral("Show Audience Overlay"), this);
    show_audience_overlay_action_->setCheckable(true);
    show_audience_overlay_action_->setChecked(true);

    quit_action_ = new QAction(QStringLiteral("&Quit"), this);
    quit_action_->setIcon(bluecurve::icon(QStringLiteral("stock-quit")));
    quit_action_->setShortcut(QKeySequence::Quit);

    about_action_ = new QAction(QStringLiteral("About uil"), this);
    about_action_->setIcon(bluecurve::icon(QStringLiteral("stock-about")));

    QMenu* file_menu = menu_bar_->addMenu(QStringLiteral("&File"));
    file_menu->addAction(open_action_);
    QMenu* examples_menu = file_menu->addMenu(QStringLiteral("&Examples"));
    examples_menu->setIcon(bluecurve::icon(QStringLiteral("stock-open")));
    const QVector<ExamplePresentation> examples = installed_example_presentations(
        QCoreApplication::applicationDirPath());
    for (const ExamplePresentation& example : examples) {
        QAction* action = examples_menu->addAction(example.title);
        action->setData(example.absolute_path);
        connect(action, &QAction::triggered, this, [this, action] {
            open_pdf_path(action->data().toString());
        });
    }
    if (examples.isEmpty()) {
        QAction* unavailable_action = examples_menu->addAction(
            QStringLiteral("No examples installed"));
        unavailable_action->setEnabled(false);
    }
    file_menu->addAction(save_action_);
    file_menu->addAction(save_as_action_);
    file_menu->addAction(export_pdf_action_);
    open_recent_menu_ = file_menu->addMenu(QStringLiteral("Open &Recent"));
    open_recent_menu_->setIcon(bluecurve::icon(QStringLiteral("stock-history")));
    rebuild_open_recent_menu();
    file_menu->addSeparator();
    file_menu->addAction(quit_action_);

    QMenu* presentation_menu = menu_bar_->addMenu(QStringLiteral("&Presentation"));
    presentation_menu->addAction(start_presentation_action_);
    presentation_menu->addSeparator();
    presentation_menu->addAction(next_action_);
    presentation_menu->addAction(previous_action_);
    presentation_menu->addSeparator();
    presentation_menu->addAction(first_action_);
    presentation_menu->addAction(last_action_);
    presentation_menu->addAction(jump_to_page_action_);
    presentation_menu->addAction(slide_overview_action_);
    presentation_menu->addSeparator();
    presentation_menu->addAction(play_pause_media_action_);
    presentation_menu->addAction(black_screen_action_);
    presentation_menu->addAction(white_screen_action_);
    presentation_menu->addAction(show_audience_overlay_action_);
    presentation_menu->addSeparator();
    presentation_menu->addAction(fullscreen_action_);

    QMenu* help_menu = menu_bar_->addMenu(QStringLiteral("&Help"));
    help_menu->addAction(about_action_);

    addAction(open_action_);
    addAction(save_action_);
    addAction(save_as_action_);
    addAction(export_pdf_action_);
    addAction(next_action_);
    addAction(previous_action_);
    addAction(first_action_);
    addAction(last_action_);
    addAction(start_presentation_action_);
    addAction(play_pause_media_action_);
    addAction(jump_to_page_action_);
    addAction(slide_overview_action_);
    addAction(black_screen_action_);
    addAction(white_screen_action_);
    addAction(fullscreen_action_);
    addAction(show_audience_overlay_action_);
    addAction(quit_action_);
}

QWidget* PresenterWindow::create_title_bar() {
    title_bar_ = new QWidget(this);
    title_bar_->setObjectName(QStringLiteral("titleBar"));
    title_bar_->setFixedHeight(32);

    auto* title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(0);

    auto* icon_label = new QLabel(title_bar_);
    icon_label->setObjectName(QStringLiteral("titleIcon"));
    icon_label->setFixedSize(36, 32);
    icon_label->setAlignment(Qt::AlignCenter);
    icon_label->setPixmap(QIcon(QStringLiteral(":/icons/uil.png")).pixmap(QSize(16, 16)));

    title_layout->addWidget(icon_label);
    title_layout->addWidget(menu_bar_, 0, Qt::AlignTop);

    auto* drag_area = new QWidget(title_bar_);
    drag_area->setObjectName(QStringLiteral("titleDragArea"));
    drag_area->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    title_layout->addWidget(drag_area, 1);

    auto create_control_button = [this](const QString& tool_tip, const QString& icon_name) {
        auto* button = new IconToolButton(title_bar_);
        button->setObjectName(QStringLiteral("windowControlButton"));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(46, 32);
        button->setToolTip(tool_tip);
        button->setIconSize(QSize(12, 12));
        button->set_state_icons(
            font_awesome::icon(font_awesome::Style::Solid, icon_name, QColor(0xcc, 0xcc, 0xcc), QSize(16, 16)),
            font_awesome::icon(font_awesome::Style::Solid, icon_name, QColor(0xff, 0xff, 0xff), QSize(16, 16)));
        return button;
    };

    minimize_button_ = create_control_button(QStringLiteral("Minimize"), QStringLiteral("window-minimize"));
    maximize_button_ = create_control_button(QStringLiteral("Maximize"), QStringLiteral("window-maximize"));
    close_button_ = create_control_button(QStringLiteral("Close"), QStringLiteral("xmark"));
    close_button_->setObjectName(QStringLiteral("windowCloseButton"));

    connect(minimize_button_, &QToolButton::clicked, this, &PresenterWindow::showMinimized);
    connect(maximize_button_, &QToolButton::clicked, this, &PresenterWindow::toggle_maximized);
    connect(close_button_, &QToolButton::clicked, this, &PresenterWindow::close);

    title_layout->addWidget(minimize_button_);
    title_layout->addWidget(maximize_button_);
    title_layout->addWidget(close_button_);
    update_maximize_button();

    return title_bar_;
}

void PresenterWindow::create_layout() {
    setMenuWidget(create_title_bar());

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("presenterRoot"));
    auto* root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(16, 16, 16, 10);
    root_layout->setSpacing(12);

    auto create_pane = [](const QString& title, QWidget* content) {
        auto* pane = new QWidget;
        pane->setObjectName(QStringLiteral("previewPane"));
        auto* pane_layout = new QVBoxLayout(pane);
        pane_layout->setContentsMargins(0, 0, 0, 0);
        pane_layout->setSpacing(0);

        auto* heading = new QLabel(title, pane);
        heading->setObjectName(QStringLiteral("previewHeading"));
        pane_layout->addWidget(heading);
        pane_layout->addWidget(content, 1);
        return pane;
    };

    auto* main_layout = new QHBoxLayout;
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(12);

    auto* current_slide_panel = new QWidget(central);
    current_slide_panel->setObjectName(QStringLiteral("currentSlidePanel"));
    auto* current_slide_layout = new QVBoxLayout(current_slide_panel);
    current_slide_layout->setContentsMargins(0, 0, 0, 0);
    current_slide_layout->setSpacing(8);
    current_preview_ = new SlidePreview(central);
    current_slide_layout->addWidget(current_preview_, 1);

    auto* navigation_layout = new QHBoxLayout;
    navigation_layout->setContentsMargins(10, 0, 10, 10);
    navigation_layout->setSpacing(8);

    auto* first_button = new SlideNavButton(QStringLiteral("First"), QStringLiteral("stock-goto-first"), false, current_slide_panel);
    first_button->setToolTip(QStringLiteral("First slide"));
    auto* previous_button = new SlideNavButton(QStringLiteral("Previous"), QStringLiteral("stock-go-back"), false, current_slide_panel);
    previous_button->setToolTip(QStringLiteral("Previous slide"));
    auto* next_button = new SlideNavButton(QStringLiteral("Next"), QStringLiteral("stock-go-forward"), true, current_slide_panel);
    next_button->setToolTip(QStringLiteral("Next slide"));
    auto* last_button = new SlideNavButton(QStringLiteral("Last"), QStringLiteral("stock-goto-last"), true, current_slide_panel);
    last_button->setToolTip(QStringLiteral("Last slide"));

    connect(first_button, &QToolButton::clicked, first_action_, &QAction::trigger);
    connect(previous_button, &QToolButton::clicked, previous_action_, &QAction::trigger);
    connect(next_button, &QToolButton::clicked, next_action_, &QAction::trigger);
    connect(last_button, &QToolButton::clicked, last_action_, &QAction::trigger);

    navigation_layout->addStretch(1);
    navigation_layout->addWidget(first_button);
    navigation_layout->addWidget(previous_button);
    navigation_layout->addWidget(next_button);
    navigation_layout->addWidget(last_button);
    navigation_layout->addStretch(1);
    current_slide_layout->addLayout(navigation_layout);

    deck_overview_ = new SlideDeckOverview(central);
    deck_overview_->page_activated = [this](int page_index) {
        controller_->go_to_page(page_index);
    };
    deck_overview_->page_overlay_visibility_changed = [this](int page_index, bool visible) {
        set_page_overlay_visible(page_index, visible);
    };
    deck_overview_->page_overlay_clear_requested = [this](int page_index) {
        confirm_clear_page_overlay(page_index);
    };
    deck_overview_->render_batch_requested = [this](
        const QSize& bounding_size,
        int focused_page_index,
        int first_visible_page,
        int last_visible_page) {
        controller_->request_presenter_thumbnail_renders(
            bounding_size,
            focused_page_index,
            first_visible_page,
            last_visible_page);
    };

    main_layout->addWidget(create_pane(QStringLiteral("Current slide"), current_slide_panel), 3);
    main_layout->addWidget(create_pane(QStringLiteral("Slide deck"), deck_overview_), 2);

    auto* status_layout = new QHBoxLayout;
    status_layout->setContentsMargins(0, 0, 0, 0);
    status_layout->setSpacing(8);
    page_label_ = new QLabel(QStringLiteral("Page: - / -"), central);
    media_label_ = new QLabel(QStringLiteral("Media: none"), central);
    auto* screen_label = new QLabel(QStringLiteral("Audience:"), central);
    screen_combo_ = new QComboBox(central);
    screen_combo_->setMinimumWidth(280);
    for (QLabel* label : {page_label_, media_label_}) {
        label->setObjectName(QStringLiteral("statusPill"));
    }
    screen_label->setObjectName(QStringLiteral("fieldLabel"));
    auto* start_presentation_button = new QToolButton(central);
    start_presentation_button->setObjectName(QStringLiteral("statusIconButton"));
    start_presentation_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    start_presentation_button->setAutoRaise(true);
    start_presentation_button->setFocusPolicy(Qt::NoFocus);
    start_presentation_button->setFixedSize(31, 30);
    start_presentation_button->setDefaultAction(start_presentation_action_);
    start_presentation_button->setIconSize(QSize(16, 16));
    start_presentation_button->setToolTip(QStringLiteral("Reload and start presentation (F5)"));
    start_presentation_button->setAccessibleName(QStringLiteral("Reload and start presentation"));

    clear_all_overlays_button_ = new QToolButton(central);
    clear_all_overlays_button_->setObjectName(QStringLiteral("statusIconButton"));
    clear_all_overlays_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    clear_all_overlays_button_->setAutoRaise(true);
    clear_all_overlays_button_->setFocusPolicy(Qt::NoFocus);
    clear_all_overlays_button_->setFixedSize(31, 30);
    clear_all_overlays_button_->setIcon(bluecurve::icon(QStringLiteral("stock-clear")));
    clear_all_overlays_button_->setIconSize(QSize(16, 16));
    clear_all_overlays_button_->setToolTip(QStringLiteral("Clear all drawing overlays"));

    overlay_visibility_button_ = new QToolButton(central);
    overlay_visibility_button_->setObjectName(QStringLiteral("statusIconButton"));
    overlay_visibility_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    overlay_visibility_button_->setAutoRaise(true);
    overlay_visibility_button_->setFocusPolicy(Qt::NoFocus);
    overlay_visibility_button_->setFixedSize(31, 30);
    overlay_visibility_button_->setDefaultAction(show_audience_overlay_action_);
    update_overlay_visibility_button(show_audience_overlay_action_->isChecked());

    pointer_device_status_ = new QToolButton(central);
    pointer_device_status_->setObjectName(QStringLiteral("pointerDeviceStatus"));
    pointer_device_status_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pointer_device_status_->setAutoRaise(true);
    pointer_device_status_->setFocusPolicy(Qt::NoFocus);
    pointer_device_status_->setFixedHeight(30);
    pointer_device_status_->setIconSize(QSize(16, 16));
    pointer_device_status_->setAccessibleName(QStringLiteral("Logitech Spotlight status"));
    update_pointer_device_status(spotlight_detector_->is_present());

    status_layout->addWidget(start_presentation_button);
    status_layout->addWidget(clear_all_overlays_button_);
    status_layout->addWidget(page_label_);
    status_layout->addWidget(overlay_visibility_button_);
    status_layout->addWidget(media_label_);
    status_layout->addWidget(pointer_device_status_);
    status_layout->addStretch(1);
    status_layout->addWidget(screen_label);
    status_layout->addWidget(screen_combo_);

    root_layout->addLayout(main_layout, 1);
    root_layout->addLayout(status_layout);
    setCentralWidget(central);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->showMessage(QStringLiteral("Ready"));
    auto* build_id_label = new QLabel(
        QStringLiteral("%1 · %2").arg(
            QStringLiteral(UIL_VERSION_DISPLAY),
            QStringLiteral(UIL_GIT_COMMIT)),
        statusBar());
    build_id_label->setObjectName(QStringLiteral("buildIdLabel"));
    build_id_label->setToolTip(QStringLiteral("uil %1, commit %2").arg(
        QStringLiteral(UIL_VERSION),
        QStringLiteral(UIL_GIT_COMMIT)));
    statusBar()->addPermanentWidget(build_id_label);
}

bool PresenterWindow::is_title_drag_area_at(const QPoint& global_position) const {
    if (!title_bar_) {
        return false;
    }

    const QPoint title_position = title_bar_->mapFromGlobal(global_position);
    if (!title_bar_->rect().contains(title_position)) {
        return false;
    }

    QWidget* child = title_bar_->childAt(title_position);
    return !is_descendant_of(child, menu_bar_)
        && !is_descendant_of(child, minimize_button_)
        && !is_descendant_of(child, maximize_button_)
        && !is_descendant_of(child, close_button_);
}

Qt::Edges PresenterWindow::resize_edges_at(const QPoint& position) const {
    if (isMaximized() || isFullScreen()) {
        return {};
    }

    Qt::Edges edges;
    const QRect window_rect = rect();
    if (position.x() >= window_rect.left() && position.x() <= window_rect.left() + kResizeBorderWidth) {
        edges |= Qt::LeftEdge;
    } else if (position.x() >= window_rect.right() - kResizeBorderWidth && position.x() <= window_rect.right()) {
        edges |= Qt::RightEdge;
    }

    if (position.y() >= window_rect.top() && position.y() <= window_rect.top() + kResizeBorderWidth) {
        edges |= Qt::TopEdge;
    } else if (position.y() >= window_rect.bottom() - kResizeBorderWidth && position.y() <= window_rect.bottom()) {
        edges |= Qt::BottomEdge;
    }

    return edges;
}

void PresenterWindow::update_resize_cursor(Qt::Edges edges) {
    if (!edges) {
        clear_resize_cursor();
        return;
    }

    Qt::CursorShape cursorShape = Qt::ArrowCursor;
    const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
    const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
    if (horizontal && vertical) {
        const bool forward_diagonal = (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge))
            || (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge));
        cursorShape = forward_diagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    } else if (horizontal) {
        cursorShape = Qt::SizeHorCursor;
    } else if (vertical) {
        cursorShape = Qt::SizeVerCursor;
    }

    if (resize_cursor_active_) {
        QGuiApplication::changeOverrideCursor(QCursor(cursorShape));
    } else {
        QGuiApplication::setOverrideCursor(QCursor(cursorShape));
        resize_cursor_active_ = true;
    }
}

void PresenterWindow::clear_resize_cursor() {
    if (!resize_cursor_active_) {
        return;
    }

    QGuiApplication::restoreOverrideCursor();
    resize_cursor_active_ = false;
}

void PresenterWindow::begin_manual_resize(Qt::Edges edges, const QPoint& global_position) {
    manual_resize_active_ = true;
    resize_edges_ = edges;
    resize_start_geometry_ = geometry();
    resize_start_global_position_ = global_position;
}

void PresenterWindow::update_manual_resize(const QPoint& global_position) {
    QRect resized_geometry = resize_start_geometry_;
    const QPoint delta = global_position - resize_start_global_position_;
    const QSize minimum = minimumSize();

    if (resize_edges_.testFlag(Qt::LeftEdge)) {
        resized_geometry.setLeft(std::min(resize_start_geometry_.left() + delta.x(),
            resize_start_geometry_.right() - minimum.width() + 1));
    } else if (resize_edges_.testFlag(Qt::RightEdge)) {
        resized_geometry.setRight(std::max(resize_start_geometry_.right() + delta.x(),
            resize_start_geometry_.left() + minimum.width() - 1));
    }

    if (resize_edges_.testFlag(Qt::TopEdge)) {
        resized_geometry.setTop(std::min(resize_start_geometry_.top() + delta.y(),
            resize_start_geometry_.bottom() - minimum.height() + 1));
    } else if (resize_edges_.testFlag(Qt::BottomEdge)) {
        resized_geometry.setBottom(std::max(resize_start_geometry_.bottom() + delta.y(),
            resize_start_geometry_.top() + minimum.height() - 1));
    }

    setGeometry(resized_geometry);
}

void PresenterWindow::finish_manual_resize() {
    manual_resize_active_ = false;
    resize_edges_ = {};
}

void PresenterWindow::begin_manual_move(const QPoint& global_position) {
    manual_move_active_ = true;
    move_offset_ = global_position - frameGeometry().topLeft();
}

void PresenterWindow::update_manual_move(const QPoint& global_position) {
    move(global_position - move_offset_);
}

void PresenterWindow::finish_manual_move() {
    manual_move_active_ = false;
}

void PresenterWindow::toggle_maximized() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    update_maximize_button();
}

void PresenterWindow::update_maximize_button() {
    if (!maximize_button_) {
        return;
    }

    maximize_button_->setToolTip(isMaximized() ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
    const QString icon_name = isMaximized() ? QStringLiteral("window-restore") : QStringLiteral("window-maximize");
    if (auto* icon_button = dynamic_cast<IconToolButton*>(maximize_button_)) {
        icon_button->set_state_icons(
            font_awesome::icon(font_awesome::Style::Solid, icon_name, QColor(0xcc, 0xcc, 0xcc), QSize(16, 16)),
            font_awesome::icon(font_awesome::Style::Solid, icon_name, QColor(0xff, 0xff, 0xff), QSize(16, 16)));
    }
}

void PresenterWindow::update_overlay_visibility_button(bool visible) {
    if (!show_audience_overlay_action_) {
        return;
    }

    const QString icon_name = visible ? QStringLiteral("eye") : QStringLiteral("eye-slash");
    show_audience_overlay_action_->setIcon(font_awesome::icon(font_awesome::Style::Regular, icon_name, QColor(0xff, 0xff, 0xff), QSize(16, 16)));
    show_audience_overlay_action_->setToolTip(visible ? QStringLiteral("Hide drawing overlay") : QStringLiteral("Show drawing overlay"));
    if (overlay_visibility_button_) {
        overlay_visibility_button_->setChecked(visible);
    }
}

void PresenterWindow::update_pointer_device_status(bool present) {
    if (!pointer_device_status_) {
        return;
    }

    const bool available = spotlight_detector_ && spotlight_detector_->detection_available();
    pointer_device_status_->setProperty("connected", available && present);
    pointer_device_status_->setText(
        !available ? QStringLiteral("Pointer: unavailable")
                   : present ? QStringLiteral("Pointer: connected")
                             : QStringLiteral("Pointer: absent"));
    pointer_device_status_->setIcon(font_awesome::icon(
        font_awesome::Style::Solid,
        QStringLiteral("satellite-dish"),
        available && present ? QColor(0x48, 0xd5, 0x88) : QColor(0x85, 0x85, 0x85),
        QSize(16, 16)));
    pointer_device_status_->setToolTip(
        !available ? QStringLiteral("Pointer-device detection is unavailable in this build")
                   : present ? QStringLiteral("Logitech Spotlight receiver connected")
                             : QStringLiteral("Logitech Spotlight receiver not detected"));
    pointer_device_status_->style()->unpolish(pointer_device_status_);
    pointer_device_status_->style()->polish(pointer_device_status_);
}

void PresenterWindow::update_current_preview_overlay_visibility() {
    if (!current_preview_ || !show_audience_overlay_action_) {
        return;
    }

    const int page_index = controller_ ? controller_->current_page() : -1;
    current_preview_->set_overlay_visible(show_audience_overlay_action_->isChecked() && is_page_overlay_visible(page_index));
}

void PresenterWindow::set_current_page_overlay_image(const QImage& image) {
    const int page_index = controller_ ? controller_->current_page() : -1;
    if (page_index >= 0) {
        if (image.isNull()) {
            page_overlay_images_.remove(page_index);
        } else {
            page_overlay_images_.insert(page_index, image);
        }
        if (deck_overview_) {
            deck_overview_->set_page_overlay_image(page_index, image);
        }
    }

    if (current_preview_) {
        current_preview_->set_overlay_image(image);
    }
}

void PresenterWindow::update_deck_overlay_visibility() {
    if (!deck_overview_ || !show_audience_overlay_action_) {
        return;
    }

    deck_overview_->set_overlays_globally_visible(show_audience_overlay_action_->isChecked());
}

bool PresenterWindow::is_page_overlay_visible(int page_index) const {
    return page_index < 0 || !hidden_overlay_pages_.contains(page_index);
}

void PresenterWindow::set_page_overlay_visible(int page_index, bool visible) {
    if (page_index < 0) {
        return;
    }

    if (visible) {
        hidden_overlay_pages_.remove(page_index);
    } else {
        hidden_overlay_pages_.insert(page_index);
    }
    controller_->mark_annotations_modified();

    if (deck_overview_) {
        deck_overview_->set_page_overlay_visible(page_index, visible);
    }
    if (controller_ && controller_->current_page() == page_index) {
        update_current_preview_overlay_visibility();
    }
}

void PresenterWindow::confirm_clear_page_overlay(int page_index) {
    if (page_index < 0) {
        return;
    }

    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(QStringLiteral("Clear Slide Overlay"));
    confirm.setText(QStringLiteral("Clear the drawing overlay for slide %1?").arg(page_index + 1));
    confirm.setInformativeText(QStringLiteral("This removes only the overlay for this single slide."));
    QPushButton* clear_button = confirm.addButton(QStringLiteral("Clear Slide Overlay"), QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != clear_button) {
        return;
    }

    page_overlay_images_.remove(page_index);
    if (deck_overview_) {
        deck_overview_->set_page_overlay_image(page_index, QImage());
    }
    controller_->clear_annotation_overlay_for_page(page_index);
    if (controller_->current_page() == page_index && current_preview_) {
        current_preview_->set_overlay_image(QImage());
    }
    statusBar()->showMessage(QStringLiteral("Cleared overlay for slide %1").arg(page_index + 1));
}

void PresenterWindow::confirm_clear_all_overlays() {
    if (page_overlay_images_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Clear All Overlays"), QStringLiteral("There are no drawing overlays to clear."));
        return;
    }

    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(QStringLiteral("Clear All Overlays"));
    confirm.setText(QStringLiteral("This will permanently clear every drawing overlay in the entire slide deck."));
    confirm.setInformativeText(QStringLiteral("This cannot be undone. All overlays will be lost."));
    QPushButton* clear_button = confirm.addButton(QStringLiteral("Clear All Overlays"), QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != clear_button) {
        return;
    }

    const QSet<int> previously_hidden_pages = hidden_overlay_pages_;
    for (int page_index : page_overlay_images_.keys()) {
        if (deck_overview_) {
            deck_overview_->set_page_overlay_image(page_index, QImage());
        }
    }
    for (int page_index : previously_hidden_pages) {
        if (deck_overview_) {
            deck_overview_->set_page_overlay_visible(page_index, true);
        }
    }
    page_overlay_images_.clear();
    hidden_overlay_pages_.clear();
    controller_->clear_all_annotation_overlays();
    if (current_preview_) {
        current_preview_->set_overlay_image(QImage());
    }
    statusBar()->showMessage(QStringLiteral("Cleared all drawing overlays"));
}

void PresenterWindow::apply_rounded_window_mask() {
    if (isMaximized() || isFullScreen() || width() <= 0 || height() <= 0) {
        clearMask();
        return;
    }

    QBitmap mask(size());
    mask.fill(Qt::color0);

    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::color1);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), kWindowCornerRadius, kWindowCornerRadius);
    setMask(mask);
}

void PresenterWindow::schedule_window_mask_update() {
    if (window_mask_timer_) {
        window_mask_timer_->start();
    }
}

void PresenterWindow::create_connections() {
    connect(open_action_, &QAction::triggered, this, &PresenterWindow::open_pdf);
    connect(save_action_, &QAction::triggered, this, &PresenterWindow::save_package);
    connect(save_as_action_, &QAction::triggered, this, &PresenterWindow::save_package_as);
    connect(export_pdf_action_, &QAction::triggered, this, &PresenterWindow::export_as_pdf);
    connect(next_action_, &QAction::triggered, controller_, &AppController::next_page);
    connect(previous_action_, &QAction::triggered, controller_, &AppController::previous_page);
    connect(first_action_, &QAction::triggered, this, [this] {
        controller_->go_to_page(0);
    });
    connect(last_action_, &QAction::triggered, this, [this] {
        controller_->go_to_page(controller_->page_count() - 1);
    });
    connect(start_presentation_action_, &QAction::triggered, this, &PresenterWindow::start_presentation_mode);
    connect(play_pause_media_action_, &QAction::triggered, controller_, &AppController::toggle_media_playback);
    connect(jump_to_page_action_, &QAction::triggered, this, &PresenterWindow::jump_to_page);
    connect(slide_overview_action_, &QAction::triggered, this, &PresenterWindow::show_slide_overview);
    connect(quit_action_, &QAction::triggered, qApp, &QApplication::quit);
    connect(about_action_, &QAction::triggered, this, &PresenterWindow::show_about);
    connect(black_screen_action_, &QAction::triggered, controller_, &AppController::toggle_black_screen);
    connect(white_screen_action_, &QAction::triggered, controller_, &AppController::toggle_white_screen);
    connect(fullscreen_action_, &QAction::triggered, controller_, &AppController::toggle_audience_fullscreen);
    connect(show_audience_overlay_action_, &QAction::toggled, this, &PresenterWindow::update_overlay_visibility_button);
    connect(show_audience_overlay_action_, &QAction::toggled, this, &PresenterWindow::update_current_preview_overlay_visibility);
    connect(show_audience_overlay_action_, &QAction::toggled, this, &PresenterWindow::update_deck_overlay_visibility);
    // triggered (unlike toggled) fires only for user changes, not for state loaded from a package.
    connect(show_audience_overlay_action_, &QAction::triggered, this, [this] {
        controller_->mark_annotations_modified();
    });
    connect(clear_all_overlays_button_, &QToolButton::clicked, this, &PresenterWindow::confirm_clear_all_overlays);
    connect(screen_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PresenterWindow::select_screen_from_combo);
    connect(spotlight_detector_, &SpotlightDetector::presence_changed, this, &PresenterWindow::update_pointer_device_status);

    connect(controller_, &AppController::page_changed, this, &PresenterWindow::update_page_label);
    connect(controller_, &AppController::document_changed, this, &PresenterWindow::update_document_overview);
    connect(controller_, &AppController::media_scan_changed, this, &PresenterWindow::update_media_label);
    connect(controller_, &AppController::current_slide_image_changed, current_preview_, &SlidePreview::set_preview_image);
    connect(controller_, &AppController::current_annotation_overlay_changed, this, &PresenterWindow::set_current_page_overlay_image);
    connect(controller_, &AppController::deck_slide_image_changed, this,
        [this](int page_index, const QSize& bounding_pixel_size, const QImage& image) {
            if (deck_overview_) {
                deck_overview_->set_slide_image(page_index, bounding_pixel_size, image);
            }
        });
    connect(controller_, &AppController::status_message_changed, this, [this](const QString& message) {
        statusBar()->showMessage(message);
    });
    connect(controller_, &AppController::screen_list_changed, this, &PresenterWindow::update_screen_list);
    connect(controller_, &AppController::audience_screen_changed, this, &PresenterWindow::update_audience_screen_selection);
}

void PresenterWindow::select_screen_from_combo(int index) {
    if (index < 0) {
        return;
    }

    QScreen* screen = screen_combo_->itemData(index).value<QScreen*>();
    controller_->set_audience_screen(screen);
}

bool PresenterWindow::open_pdf_path(const QString& path) {
    if (!confirm_unsaved_annotations()) {
        return false;
    }

    const QString absolute_path = QFileInfo(path).absoluteFilePath();
    if (controller_->open_pdf(absolute_path)) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kLastOpenDirectoryKey), QFileInfo(absolute_path).absolutePath());
        add_recent_pdf_path(absolute_path);
        return true;
    }

    remove_recent_pdf_path(absolute_path);
    return false;
}

bool PresenterWindow::save_package_to_path(const QString& path) {
    const QString absolute_path = QFileInfo(path).absoluteFilePath();
    QString error_message;
    if (!controller_->save_uil_package(
            absolute_path,
            package_overlay_images_for_save(),
            hidden_overlay_pages_,
            show_audience_overlay_action_ ? show_audience_overlay_action_->isChecked() : true,
            &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Save UIL Package"), error_message);
        return false;
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kLastOpenDirectoryKey), QFileInfo(absolute_path).absolutePath());
    page_overlay_images_ = controller_->loaded_overlay_images();
    add_recent_pdf_path(absolute_path);
    statusBar()->showMessage(QStringLiteral("Saved UIL package: %1").arg(absolute_path));
    return true;
}

QHash<int, QImage> PresenterWindow::package_overlay_images_for_save() const {
    QHash<int, QImage> overlays;
    for (auto it = page_overlay_images_.constBegin(); it != page_overlay_images_.constEnd(); ++it) {
        if (!it.value().isNull()) {
            overlays.insert(it.key(), it.value());
        }
    }

    return overlays;
}

QStringList PresenterWindow::recent_pdf_paths() const {
    QSettings settings;
    return settings.value(QString::fromLatin1(kRecentPdfPathsKey)).toStringList();
}

void PresenterWindow::save_recent_pdf_paths(const QStringList& paths) {
    QSettings settings;
    settings.setValue(QString::fromLatin1(kRecentPdfPathsKey), paths);
}

void PresenterWindow::add_recent_pdf_path(const QString& path) {
    QStringList paths = recent_pdf_paths();
    paths.removeAll(path);
    paths.prepend(path);

    while (paths.size() > kMaxRecentPdfPaths) {
        paths.removeLast();
    }

    save_recent_pdf_paths(paths);
    rebuild_open_recent_menu();
}

void PresenterWindow::remove_recent_pdf_path(const QString& path) {
    QStringList paths = recent_pdf_paths();
    if (!paths.removeAll(path)) {
        return;
    }

    save_recent_pdf_paths(paths);
    rebuild_open_recent_menu();
}

void PresenterWindow::rebuild_open_recent_menu() {
    if (!open_recent_menu_) {
        return;
    }

    open_recent_menu_->clear();

    const QStringList paths = recent_pdf_paths();
    open_recent_menu_->setEnabled(!paths.isEmpty());
    for (const QString& path : paths) {
        QAction* action = open_recent_menu_->addAction(menu_safe_path_text(path));
        action->setData(path);
        connect(action, &QAction::triggered, this, [this, action] {
            open_pdf_path(action->data().toString());
        });
    }
}

void PresenterWindow::load_settings() {
    QSettings settings;
    const QByteArray geometry = settings.value(QString::fromLatin1(kWindowGeometryKey)).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(kDefaultWindowWidth, kDefaultWindowHeight);
    }

    const QByteArray state = settings.value(QString::fromLatin1(kWindowStateKey)).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    const QString saved_screen_name = settings.value(QString::fromLatin1(kAudienceScreenNameKey)).toString();
    if (!saved_screen_name.isEmpty()) {
        for (int i = 0; i < screen_combo_->count(); ++i) {
            QScreen* screen = screen_combo_->itemData(i).value<QScreen*>();
            if (screen && screen->name() == saved_screen_name) {
                screen_combo_->setCurrentIndex(i);
                controller_->set_audience_screen(screen);
                break;
            }
        }
    }
}

void PresenterWindow::save_settings() {
    QSettings settings;
    settings.setValue(QString::fromLatin1(kWindowGeometryKey), saveGeometry());
    settings.setValue(QString::fromLatin1(kWindowStateKey), saveState());
    if (QScreen* screen = controller_->selected_audience_screen()) {
        settings.setValue(QString::fromLatin1(kAudienceScreenNameKey), screen->name());
    }
}
