#pragma once

#include "molecule/molecule_geometry.hpp"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

class QCloseEvent;
class QContextMenuEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class MoleculeWidget;

class AudienceWindow : public QWidget {
    Q_OBJECT

public:
    /** @brief Constructs the audience presentation window. */
    explicit AudienceWindow();
    /** @brief Destroys the audience presentation window. */
    ~AudienceWindow() override;

    /** @brief Displays and caches a rendered slide image. */
    void set_slide_image(const QString& texture_key, const QImage& image);
    /** @brief Clears the currently displayed slide image. */
    void clear_slide_image();
    /** @brief Adds a rendered slide image to the local texture cache. */
    void cache_slide_image(const QString& texture_key, const QImage& image);
    /** @brief Updates the page count and current page used by deck overview. */
    void set_document_overview(int page_count, int current_page);
    /** @brief Stores a rendered thumbnail for deck overview. */
    void set_deck_overview_slide_image(int page_index, const QSize& bounding_pixel_size, const QImage& image);
    /** @brief Displays a decoded video frame over its slide rectangle. */
    void set_video_frame(const QImage& image, QRectF slide_rect);
    /** @brief Clears the active video overlay. */
    void clear_video_overlay();
    /** @brief Displays an interactive molecule over a normalized slide rectangle. */
    void set_molecule_overlay(const MoleculeGeometry& geometry, QRectF slide_rect);
    /** @brief Clears the active interactive molecule. */
    void clear_molecule_overlay();
    /** @brief Selects the screen on which the audience window appears. */
    void set_audience_screen(QScreen* screen);
    /** @brief Enters full-screen presentation mode. */
    void enter_fullscreen();
    /** @brief Toggles full-screen presentation mode. */
    void toggle_fullscreen();
    /** @brief Leaves full-screen presentation mode. */
    void exit_fullscreen();
    /** @brief Toggles a black audience screen. */
    void toggle_black_screen();
    /** @brief Toggles a white audience screen. */
    void toggle_white_screen();
    /** @brief Restores slide display from a blank-screen mode. */
    void clear_blank_screen();
    /** @brief Selects the cursor interaction tool. */
    void set_cursor_tool();
    /** @brief Selects the pointer interaction tool. */
    void set_pointer_tool();
    /** @brief Selects the pen annotation tool. */
    void set_pen_tool();
    /** @brief Selects the annotation eraser tool. */
    void set_eraser_tool();
    /** @brief Sets the pointer color. */
    void set_pointer_color(const QColor& color);
    /** @brief Sets the pointer size in logical pixels. */
    void set_pointer_size(int size);
    /** @brief Sets the annotation pen color. */
    void set_annotation_color(const QColor& color);
    /** @brief Sets the annotation pen thickness. */
    void set_annotation_thickness(int thickness);
    /** @brief Sets the annotation eraser thickness. */
    void set_eraser_thickness(int thickness);
    /** @brief Clears annotations for the current slide. */
    void clear_annotations();
    /** @brief Clears the annotation overlay for one page. */
    void clear_annotation_overlay_for_page(int page_index);
    /** @brief Clears all annotation overlays. */
    void clear_all_annotation_overlays();
    /** @brief Replaces annotation overlays using their slide texture keys. */
    void set_annotation_overlays_by_texture_key(const QHash<QString, QImage>& overlays);
    /** @brief Returns annotation overlays indexed by page. */
    QHash<int, QImage> annotation_overlays_by_page() const;
    /** @brief Returns the current slide composited with its annotations. */
    QImage current_annotated_slide_image() const;
    /** @brief Returns the current slide's annotation overlay image. */
    QImage current_annotation_overlay_image() const;
    /** @brief Returns the logical size used for slide rendering. */
    QSize render_logical_size() const;
    /** @brief Returns the device pixel ratio used for slide rendering. */
    qreal render_device_pixel_ratio() const;

signals:
    /** @brief Emitted when the user requests the next slide. */
    void next_requested();
    /** @brief Emitted when the user requests the previous slide. */
    void previous_requested();
    /** @brief Emitted when the user requests the first slide. */
    void first_requested();
    /** @brief Emitted when the user requests the last slide. */
    void last_requested();
    /** @brief Emitted when the user requests a specific page. */
    void page_requested(int page_index);
    /** @brief Emitted when deck-overview thumbnails need rendering. */
    void deck_overview_renders_requested(
        const QSize& bounding_pixel_size,
        int focused_page_index,
        int first_visible_page,
        int last_visible_page);
    /** @brief Emitted when the user toggles media playback. */
    void play_pause_requested();
    /** @brief Emitted when audience render dimensions change. */
    void render_target_changed();
    /** @brief Emitted when the current annotation overlay changes. */
    void annotation_overlay_changed(const QImage& image);
    /** @brief Emitted when the presentation window closes. */
    void presentation_closed();

protected:
    /** @brief Handles Qt close events. */
    void closeEvent(QCloseEvent* event) override;
    /** @brief Handles Qt context-menu events. */
    void contextMenuEvent(QContextMenuEvent* event) override;
    /** @brief Paints the audience slide, media, and annotations. */
    void paintEvent(QPaintEvent* event) override;
    /** @brief Repositions interactive content after an audience-window resize. */
    void resizeEvent(QResizeEvent* event) override;
    /** @brief Handles Qt keyboard events for presentation controls. */
    void keyPressEvent(QKeyEvent* event) override;
    /** @brief Handles Qt pointer-leave events. */
    void leaveEvent(QEvent* event) override;
    /** @brief Handles Qt pointer-press events. */
    void mousePressEvent(QMouseEvent* event) override;
    /** @brief Handles Qt pointer-move events. */
    void mouseMoveEvent(QMouseEvent* event) override;
    /** @brief Handles Qt pointer-release events. */
    void mouseReleaseEvent(QMouseEvent* event) override;
    /** @brief Handles Qt mouse-wheel navigation events. */
    void wheelEvent(QWheelEvent* event) override;

private:
    class FeatureMenuPanel;

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

    /** @brief Evicts old slide textures when the local cache exceeds its limit. */
    void evict_old_slides();
    /** @brief Applies the selected screen geometry for windowed or full-screen mode. */
    void apply_screen_geometry(bool fullscreen);
    /** @brief Shows the cursor and restarts its automatic hide timer. */
    void show_cursor_temporarily();
    /** @brief Hides the cursor when presentation tools permit it. */
    void hide_cursor();
    /** @brief Updates the cursor shape for the active interaction tool. */
    void update_cursor_appearance();
    /** @brief Calculates the slide rectangle in logical window coordinates. */
    QRectF slide_logical_rect(QSize texture_size) const;
    /** @brief Maps a window point into slide-image coordinates. */
    QPointF slide_image_point(QPointF window_point, QSize texture_size, bool* inside) const;
    /** @brief Returns a writable annotation image for the current slide. */
    QImage& annotation_image_for_current_slide(QSize size);
    /** @brief Returns the current annotation image when one exists. */
    const QImage* current_annotation_image() const;
    /** @brief Draws one annotation segment between two window positions. */
    void draw_annotation_segment(QPointF from_window_point, QPointF to_window_point);
    /** @brief Draws the presentation pointer. */
    void draw_pointer(QPainter& painter) const;
    /** @brief Draws the annotation eraser cursor. */
    void draw_eraser_cursor(QPainter& painter) const;
    /** @brief Returns the eraser diameter in logical pixels. */
    qreal eraser_logical_diameter() const;
    /** @brief Opens the presentation feature menu at a global position. */
    void show_feature_menu(const QPoint& global_position);
    /** @brief Enters the visual deck overview. */
    void enter_deck_overview();
    /** @brief Leaves the visual deck overview. */
    void exit_deck_overview();
    /** @brief Draws the visual deck overview. */
    void draw_deck_overview(QPainter& painter);
    /** @brief Returns the target pixel size for deck thumbnails. */
    QSize deck_overview_thumbnail_bounding_pixel_size() const;
    /** @brief Returns the viewport available to the deck overview. */
    QRect deck_overview_viewport_rect() const;
    /** @brief Returns the total height of deck-overview content. */
    int deck_overview_content_height() const;
    /** @brief Returns the largest valid deck-overview scroll offset. */
    int deck_overview_max_scroll_y() const;
    /** @brief Returns the first and last pages intersecting the deck viewport. */
    QPair<int, int> deck_overview_visible_page_range() const;
    /** @brief Requests thumbnail renders for the visible audience overview rows. */
    void request_visible_deck_overview_renders();
    /** @brief Returns the page at a deck-overview position. */
    int deck_overview_page_at(const QPoint& position) const;
    /** @brief Scrolls the deck overview by a logical-pixel delta. */
    void scroll_deck_overview_by(int delta_y);
    /** @brief Saves the current annotated slide as an image. */
    void save_annotated_slide_image();
    /** @brief Repositions and shows or hides the active molecular rendering surface. */
    void update_molecule_overlay_geometry();

    QString current_texture_key_;
    QImage current_slide_image_;
    std::vector<CachedSlide> slide_cache_;
    QImage video_frame_;
    QRectF video_rect_;
    std::unique_ptr<MoleculeWidget> molecule_widget_;
    QRectF molecule_rect_;
    QHash<int, QImage> deck_overview_images_;
    QSize deck_overview_image_size_;
    QHash<QString, QImage> annotation_images_;
    QColor pointer_color_ = QColor(255, 36, 36);
    QColor annotation_color_ = QColor(0xe3, 0x1a, 0x1c);
    QPointF last_annotation_point_;
    QPointF pointer_position_;
    QPointF eraser_cursor_position_;
    InteractionTool interaction_tool_ = InteractionTool::Cursor;
    bool is_annotating_ = false;
    bool pointer_visible_ = false;
    bool eraser_cursor_visible_ = false;
    bool has_video_overlay_ = false;
    bool deck_overview_visible_ = false;
    QPointer<QScreen> screen_;
    QTimer cursor_hide_timer_;
    BlankMode blank_mode_ = BlankMode::None;
    bool is_fullscreen_ = false;
    int annotation_thickness_ = 6;
    int eraser_thickness_ = 24;
    int pointer_size_ = 25;
    int page_count_ = 0;
    int current_page_index_ = -1;
    int deck_overview_scroll_y_ = 0;
    int slide_wheel_remainder_y_ = 0;
    QPointer<QWidget> feature_menu_;
};
