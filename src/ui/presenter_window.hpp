#pragma once

#include <QImage>
#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QStringList>

#include "util/pdf_media_detector.hpp"

class AppController;
class QCloseEvent;
class QComboBox;
class QEvent;
class QMenu;
class QMenuBar;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QToolButton;
class QWidget;
class SlideDeckOverview;
class SpotlightDetector;

class SlidePreview final : public QLabel {
    Q_OBJECT

public:
    /** @brief Constructs a slide preview widget. */
    explicit SlidePreview(QWidget* parent = nullptr);
    /** @brief Sets the rendered slide displayed by the preview. */
    void set_preview_image(const QImage& image);
    /** @brief Sets the annotation overlay displayed by the preview. */
    void set_overlay_image(const QImage& image);
    /** @brief Controls whether the annotation overlay is visible. */
    void set_overlay_visible(bool visible);

protected:
    /** @brief Paints the slide preview and optional annotation overlay. */
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    QImage overlay_image_;
    bool overlay_visible_ = true;
};

class PresenterWindow final : public QMainWindow {
    Q_OBJECT

public:
    /** @brief Constructs the presenter window for @p controller. */
    explicit PresenterWindow(AppController* controller, QWidget* parent = nullptr);
    /** @brief Destroys the presenter window. */
    ~PresenterWindow() override;

protected:
    /** @brief Filters Qt events used for custom window interactions. */
    bool eventFilter(QObject* watched, QEvent* event) override;
    /** @brief Handles Qt window-state changes. */
    void changeEvent(QEvent* event) override;
    /** @brief Handles Qt close events and persists settings. */
    void closeEvent(QCloseEvent* event) override;
    /** @brief Handles Qt resize events for the custom window mask. */
    void resizeEvent(QResizeEvent* event) override;
    /** @brief Records when Qt first exposes the presenter window. */
    void showEvent(QShowEvent* event) override;
    /** @brief Records when Qt first paints the presenter window. */
    void paintEvent(QPaintEvent* event) override;

private slots:
    /** @brief Opens a presentation selected by the user. */
    void open_pdf();
    /** @brief Saves the active presentation package. */
    void save_package();
    /** @brief Saves the active presentation package to a new path. */
    void save_package_as();
    /** @brief Exports the annotated presentation as PDF. */
    void export_as_pdf();
    /** @brief Prompts for and navigates to a page number. */
    void jump_to_page();
    /** @brief Opens the slide-deck overview. */
    void show_slide_overview();
    /** @brief Shows application and build information. */
    void show_about();
    /** @brief Starts audience presentation mode. */
    void start_presentation_mode();
    /** @brief Updates the detected-media summary. */
    void update_media_label(const PdfMediaScanResult& result);
    /** @brief Updates the deck overview for a new document. */
    void update_document_overview(int page_count);
    /** @brief Updates the current-page label and preview state. */
    void update_page_label(int page_index, int page_count);
    /** @brief Rebuilds the audience-screen selector. */
    void update_screen_list();
    /** @brief Synchronizes the screen selector with the controller. */
    void update_audience_screen_selection(QScreen* screen);

private:
    /** @brief Creates menu and toolbar actions. */
    void create_actions();
    /** @brief Creates and returns the custom title bar. */
    QWidget* create_title_bar();
    /** @brief Creates the presenter window layout. */
    void create_layout();
    /** @brief Connects application signals and slots. */
    void create_connections();
    /** @brief Returns whether a global point lies in the draggable title area. */
    bool is_title_drag_area_at(const QPoint& global_position) const;
    /** @brief Returns the resize edges active at a local position. */
    Qt::Edges resize_edges_at(const QPoint& position) const;
    /** @brief Updates the cursor for the active resize edges. */
    void update_resize_cursor(Qt::Edges edges);
    /** @brief Restores the normal cursor after manual resize detection. */
    void clear_resize_cursor();
    /** @brief Begins a native-style manual window resize. */
    void begin_manual_resize(Qt::Edges edges, const QPoint& global_position);
    /** @brief Updates an active manual window resize. */
    void update_manual_resize(const QPoint& global_position);
    /** @brief Finishes an active manual window resize. */
    void finish_manual_resize();
    /** @brief Begins a manual window move. */
    void begin_manual_move(const QPoint& global_position);
    /** @brief Updates an active manual window move. */
    void update_manual_move(const QPoint& global_position);
    /** @brief Finishes an active manual window move. */
    void finish_manual_move();
    /** @brief Toggles the presenter window's maximized state. */
    void toggle_maximized();
    /** @brief Updates the maximize button for the current window state. */
    void update_maximize_button();
    /** @brief Updates the global overlay-visibility action. */
    void update_overlay_visibility_button(bool visible);
    /** @brief Updates the Logitech Spotlight presence indicator. */
    void update_pointer_device_status(bool present);
    /** @brief Refreshes overlay visibility for the current preview. */
    void update_current_preview_overlay_visibility();
    /** @brief Stores and displays the current page's annotation overlay. */
    void set_current_page_overlay_image(const QImage& image);
    /** @brief Synchronizes overlay visibility across deck thumbnails. */
    void update_deck_overlay_visibility();
    /** @brief Returns whether the overlay for @p page_index is visible. */
    bool is_page_overlay_visible(int page_index) const;
    /** @brief Sets overlay visibility for one page. */
    void set_page_overlay_visible(int page_index, bool visible);
    /** @brief Confirms and clears the overlay for one page. */
    void confirm_clear_page_overlay(int page_index);
    /** @brief Confirms and clears all annotation overlays. */
    void confirm_clear_all_overlays();
    /** @brief Applies the rounded custom-window mask. */
    void apply_rounded_window_mask();
    /** @brief Debounces rounded-window mask updates during interactive resize. */
    void schedule_window_mask_update();
    /** @brief Selects the audience screen represented by a combo-box index. */
    void select_screen_from_combo(int index);
    /** @brief Opens a PDF or UIL presentation at an explicit path. */
    bool open_pdf_path(const QString& path);
    /** @brief Saves the current UIL package to an explicit path. */
    bool save_package_to_path(const QString& path);
    /** @brief Returns annotation overlays that should be persisted. */
    QHash<int, QImage> package_overlay_images_for_save() const;
    /** @brief Returns the stored recent-presentation paths. */
    QStringList recent_pdf_paths() const;
    /** @brief Persists the recent-presentation path list. */
    void save_recent_pdf_paths(const QStringList& paths);
    /** @brief Adds a path to the recent-presentation list. */
    void add_recent_pdf_path(const QString& path);
    /** @brief Removes a path from the recent-presentation list. */
    void remove_recent_pdf_path(const QString& path);
    /** @brief Rebuilds the Open Recent menu. */
    void rebuild_open_recent_menu();
    /** @brief Loads persisted presenter settings. */
    void load_settings();
    /** @brief Saves persisted presenter settings. */
    void save_settings();

    AppController* controller_ = nullptr;
    SlidePreview* current_preview_ = nullptr;
    SlideDeckOverview* deck_overview_ = nullptr;
    QLabel* page_label_ = nullptr;
    QLabel* media_label_ = nullptr;
    QComboBox* screen_combo_ = nullptr;
    QMenuBar* menu_bar_ = nullptr;
    QWidget* title_bar_ = nullptr;
    QToolButton* minimize_button_ = nullptr;
    QToolButton* maximize_button_ = nullptr;
    QToolButton* close_button_ = nullptr;
    QToolButton* clear_all_overlays_button_ = nullptr;
    QToolButton* overlay_visibility_button_ = nullptr;
    QToolButton* pointer_device_status_ = nullptr;
    QAction* open_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* export_pdf_action_ = nullptr;
    QMenu* open_recent_menu_ = nullptr;
    QAction* next_action_ = nullptr;
    QAction* previous_action_ = nullptr;
    QAction* first_action_ = nullptr;
    QAction* last_action_ = nullptr;
    QAction* start_presentation_action_ = nullptr;
    QAction* play_pause_media_action_ = nullptr;
    QAction* jump_to_page_action_ = nullptr;
    QAction* slide_overview_action_ = nullptr;
    QAction* black_screen_action_ = nullptr;
    QAction* white_screen_action_ = nullptr;
    QAction* fullscreen_action_ = nullptr;
    QAction* show_audience_overlay_action_ = nullptr;
    QAction* quit_action_ = nullptr;
    QAction* about_action_ = nullptr;
    QTimer* window_mask_timer_ = nullptr;
    SpotlightDetector* spotlight_detector_ = nullptr;
    Qt::Edges resize_edges_;
    QRect resize_start_geometry_;
    QPoint resize_start_global_position_;
    QPoint move_offset_;
    QHash<int, QImage> page_overlay_images_;
    QSet<int> hidden_overlay_pages_;
    bool manual_resize_active_ = false;
    bool manual_move_active_ = false;
    bool resize_cursor_active_ = false;
    bool first_show_recorded_ = false;
    bool first_paint_recorded_ = false;
};
