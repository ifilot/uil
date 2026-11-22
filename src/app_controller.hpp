#pragma once

#include "cache/slide_cache.hpp"
#include "media/video_frame_buffer.hpp"
#include "media/video_frame_extractor.hpp"
#include "pdf/pdf_backend.hpp"
#include "render/render_scheduler.hpp"
#include "util/pdf_media_detector.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QVector>

#include <array>
#include <memory>
#include <optional>

class AudienceWindow;

/** @brief Annotation overlays and their visibility, keyed by zero-based page index. */
struct AnnotationState {
    QHash<int, QImage> overlay_images;
    QSet<int> hidden_overlay_pages;
    bool overlays_globally_visible = true;
};

/**
 * @brief Owns the open presentation and coordinates rendering, media and both windows.
 *
 * GUI-thread only. Background work (rendering, media scans, video decoding) reports back
 * through queued connections and is discarded when it belongs to a superseded document.
 */
class AppController : public QObject {
    Q_OBJECT

public:
    /** @brief Constructs the application controller. */
    explicit AppController(QObject* parent = nullptr);
    /** @brief Stops background media analysis before controller destruction. */
    ~AppController() override;

    /** @brief Associates the controller with the audience presentation window. */
    void set_audience_window(AudienceWindow* audience_window);

    /**
     * @brief Opens a PDF or UIL presentation from @p path, replacing the current one.
     *
     * Runs synchronously on the GUI thread: a `.uil` package is extracted and the PDF is read
     * into memory before this returns, then rendering and a background media scan start.
     * On failure the previous presentation stays open and the reason is reported only through
     * status_message_changed(). Unsaved annotations of the previous presentation are dropped;
     * callers should check has_unsaved_annotations() first.
     * @return True when the new presentation is open.
     */
    bool open_pdf(const QString& path);
    /**
     * @brief Reopens the active presentation from disk and restores the current page.
     *
     * Intended for picking up a rebuilt PDF. Unsaved annotations survive the reload: they are
     * taken from @p presenter_state (or the loaded state) merged with the audience window's
     * overlays, and overlays for pages that no longer exist are dropped.
     * @return True when the presentation was reopened.
     */
    bool reload_current_document(
        const std::optional<AnnotationState>& presenter_state = std::nullopt);
    /** @brief Advances to the next page when one is available. */
    void next_page();
    /** @brief Returns to the previous page when one is available. */
    void previous_page();
    /** @brief Navigates to the requested zero-based page index. */
    void go_to_page(int page_index);
    /** @brief Returns the current zero-based page index. */
    int current_page() const;
    /** @brief Returns the number of pages in the active document. */
    int page_count() const;
    /**
     * @brief Requests thumbnails for the audience window's full-screen slide grid.
     *
     * Visible thumbnails are rendered ahead of slide prefetching because the grid is then the
     * surface the audience is looking at. Results arrive via deck_slide_image_changed().
     */
    void request_deck_overview_renders(const QSize& bounding_pixel_size,
                                       int focused_page_index = -1, int first_visible_page = -1,
                                       int last_visible_page = -1);
    /**
     * @brief Requests thumbnails for the presenter window's always-visible slide strip.
     *
     * These run behind current, next and previous slide renders. Results arrive via
     * deck_slide_image_changed() with @p bounding_pixel_size.
     */
    void request_presenter_thumbnail_renders(const QSize& bounding_pixel_size,
                                             int focused_page_index = -1,
                                             int first_visible_page = -1,
                                             int last_visible_page = -1);

    /**
     * @brief Returns the PDF file being rendered.
     *
     * For a `.uil` package this is the extracted PDF inside a temporary directory that lives
     * only as long as the package is open; use document_display_path() for anything shown to
     * the user or used as a default save location.
     */
    QString current_path() const;
    /** @brief Returns the file the user opened or last saved: the `.uil` package if any, otherwise
     * the PDF. */
    QString document_display_path() const;
    /** @brief Returns the screen selected for the audience window. */
    QScreen* selected_audience_screen() const;
    /** @brief Selects the screen used by the audience window. */
    void set_audience_screen(QScreen* screen);
    /** @brief Refreshes the list of available display screens. */
    void refresh_screens();
    /** @brief Enters full-screen audience presentation mode. */
    void enter_audience_fullscreen();
    /** @brief Toggles full-screen audience presentation mode. */
    void toggle_audience_fullscreen();
    /** @brief Toggles the audience black-screen mode. */
    void toggle_black_screen();
    /** @brief Toggles the audience white-screen mode. */
    void toggle_white_screen();
    /** @brief Toggles playback of media attached to the current slide. */
    void toggle_media_playback();
    /** @brief Closes the audience presentation window. */
    void close_audience_window();
    /**
     * @brief Saves the presentation, its media assets and annotations as a `.uil` package.
     *
     * Audience-window overlays take precedence over @p overlay_images for the same page. If
     * the background media scan is still running it is awaited first, so that referenced
     * videos and molecule files are included. The package is written atomically.
     * @param[out] error_message Receives a readable reason on failure; may be null.
     * @return True on success; the package then becomes the current package and the
     *         annotations count as saved.
     */
    bool save_uil_package(const QString& path, const QHash<int, QImage>& overlay_images,
                          const QSet<int>& hidden_overlay_pages, bool overlays_globally_visible,
                          QString* error_message = nullptr);
    /**
     * @brief Exports every slide with its annotation overlay as a raster PDF.
     *
     * The file is written atomically, so a failed export never leaves a partial file or
     * replaces an existing one. Exporting onto the open presentation's own file is refused.
     * @param[out] error_message Receives a readable reason on failure; may be null.
     * @return True when the file was written.
     */
    bool export_annotated_pdf(const QString& path, const QHash<int, QImage>& overlay_images,
                              QString* error_message = nullptr);
    /** @brief Clears the annotation overlay for one page and marks annotations as modified. */
    void clear_annotation_overlay_for_page(int page_index);
    /** @brief Clears every annotation overlay and marks annotations as modified. */
    void clear_all_annotation_overlays();
    /** @brief Returns whether annotations changed since the presentation was opened or saved. */
    bool has_unsaved_annotations() const;
    /** @brief Records an annotation change made outside the audience window, such as visibility
     * toggles. */
    void mark_annotations_modified();
    /** @brief Returns the path of the currently open UIL package. */
    QString current_package_path() const;
    /** @brief Returns the annotation images loaded from the UIL package. */
    QHash<int, QImage> loaded_overlay_images() const;
    /** @brief Returns the pages whose loaded overlays are hidden. */
    QSet<int> loaded_hidden_overlay_pages() const;
    /** @brief Returns whether loaded overlays are globally visible. */
    bool loaded_overlays_globally_visible() const;

signals:
    /** @brief Emitted after the active document changes. */
    void document_changed(int page_count);
    /** @brief Emitted after the current page changes. */
    void page_changed(int page_index, int page_count);
    /** @brief Emitted when the current rendered slide image changes. */
    void current_slide_image_changed(const QImage& image);
    /** @brief Emitted when the current annotation overlay changes. */
    void current_annotation_overlay_changed(const QImage& image);
    /** @brief Emitted when the next-slide preview image changes. */
    void next_slide_image_changed(const QImage& image);
    /** @brief Emitted when a deck-overview thumbnail becomes available. */
    void deck_slide_image_changed(int page_index, const QSize& bounding_pixel_size,
                                  const QImage& image);
    /** @brief Emitted when the user-facing status message changes. */
    void status_message_changed(const QString& message);
    /** @brief Emitted when the selected audience screen changes. */
    void audience_screen_changed(QScreen* screen);
    /** @brief Emitted when the available display-screen list changes. */
    void screen_list_changed();
    /** @brief Emitted when PDF media detection produces a new result. */
    void media_scan_changed(const PdfMediaScanResult& result);

private:
    /** @brief Calculates the audience render size for a page. */
    QSize audience_render_pixel_size(int page_index) const;
    /** @brief Builds a cache key for a page and explicit render size. */
    SlideCacheKey cache_key_for_page_at_size(int page_index,
                                             const QSize& bounding_pixel_size) const;
    /** @brief Builds a cache key for a page at the audience render size. */
    SlideCacheKey cache_key_for_page(int page_index) const;
    /** @brief Creates the audience texture key associated with a cache key. */
    QString texture_key_for_cache_key(const SlideCacheKey& key) const;
    /** @brief Builds a render request for a page and explicit render size. */
    RenderRequest render_request_for_page_at_size(int page_index,
                                                  const QSize& bounding_pixel_size) const;
    /** @brief Builds a render request for a page at the audience render size. */
    RenderRequest render_request_for_page(int page_index) const;
    /** @brief Opens @p path, optionally replacing the package's overlays with @p preserved. */
    bool open_document(const QString& path, const AnnotationState* preserved);
    /** @brief Identifies one of the two thumbnail views that request renders. */
    enum class OverviewView { Audience = 0, Presenter = 1 };
    /** @brief Queues thumbnail renders for @p view; see request_deck_overview_renders(). */
    void request_overview_renders(OverviewView view, const QSize& bounding_pixel_size,
                                  int focused_page_index, int first_visible_page,
                                  int last_visible_page);
    /** @brief Re-requests visible thumbnails of every active overview, e.g. after a cache reset. */
    void refresh_deck_overviews();
    /** @brief Drops queued audience-size renders that are no longer near the current page. */
    void prune_stale_slide_renders();
    /** @brief Blocks until the current media scan result has been applied; returns false if it
     * never arrives. */
    bool wait_for_media_scan();
    /** @brief Re-renders cached pages whose video poster frames became available after the media
     * scan. */
    void refresh_media_poster_pages();
    /** @brief Updates the current and next slide images. */
    void update_visible_slides();
    /** @brief Queues an atomic audience-frame swap after a short compositor grace period. */
    void queue_audience_slide_commit(int page_index, const SlideCacheKey& key, const QImage& image);
    /** @brief Atomically commits a rendered slide and its interactive overlays. */
    void commit_audience_slide(const QString& texture_key, const QImage& image);
    /** @brief Schedules likely upcoming pages for background rendering. */
    void schedule_predictive_renders();
    /** @brief Queues a page render at an explicit size and priority. */
    void request_page_render_at_size(int page_index, const QSize& bounding_pixel_size,
                                     int priority);
    /** @brief Queues a page render at the audience size and priority. */
    void request_page_render(int page_index, int priority);
    /** @brief Composites active video frames over a rendered slide. */
    QImage image_with_media_frames(int page_index, const QImage& image) const;
    /** @brief Returns playable media attached to the current page, if any. */
    const PdfMediaAnnotation* current_playable_media_annotation() const;
    /** @brief Normalizes a PDF media rectangle to slide-relative coordinates. */
    QRectF normalized_media_rect(const PdfMediaAnnotation& annotation) const;
    /** @brief Normalizes an arbitrary PDF rectangle to slide-relative coordinates. */
    QRectF normalized_pdf_rect(int page_index, const QRectF& rect) const;
    /** @brief Shows the first ready molecule attached to the current page. */
    void update_active_molecule();
    /** @brief Shows the first ready embedded interactive figure on the current page. */
    void update_active_interactive_figure();
    /** @brief Shows all ready embedded atomic orbitals on the current page. */
    void update_active_atomic_orbital();
    /** @brief Shows the embedded molecular-symmetry player on the current page. */
    void update_active_molecular_symmetry();
    /** @brief Starts playback of the current slide's media. */
    void start_media_playback();
    /** @brief Stops active media playback and clears its frame overlay. */
    void stop_media_playback();
    /** @brief Takes the next decoded frame, synthesizing increasing timestamps when the stream
     * lacks them. */
    std::optional<DecodedVideoFrame> take_video_frame();
    /** @brief Advances playback to the next buffered video frame. */
    void advance_video_frame();
    /** @brief Handles notification that a buffered video frame is available. */
    void handle_buffered_video_frame_available();
    /** @brief Handles normal completion of video decoding. */
    void handle_video_decode_finished();
    /** @brief Handles a video-decoding failure. */
    void handle_video_decode_failed(const QString& error_message);
    /** @brief Handles a change in the audience render target. */
    void handle_audience_render_target_changed();
    /** @brief Handles the start of a scheduled render. */
    void handle_render_started(const RenderRequest& request);
    /** @brief Handles completion of a scheduled render. */
    void handle_render_finished(const RenderRequest& request, const QImage& image,
                                qint64 elapsed_ms, const QString& error_message);
    /** @brief Scans the active document for media on a background worker. */
    void schedule_media_scan(const QString& pdf_path, const QString& package_root_path,
                             const QStringList& package_movie_asset_paths,
                             const QStringList& package_molecule_asset_paths,
                             const QString& document_hash, int generation,
                             std::shared_ptr<QTemporaryDir> package_lifetime);
    /** @brief Returns whether a PDF document is currently open. */
    bool has_document() const;
    /** @brief Returns cached page geometry without re-entering the PDF backend. */
    QSizeF page_size_points(int page_index) const;

    std::unique_ptr<PdfBackend> backend_;
    SlideCache slide_cache_;
    RenderScheduler render_scheduler_;
    QThreadPool media_scan_pool_;
    QTimer video_timer_;
    std::unique_ptr<VideoFrameBuffer> video_buffer_;
    std::shared_ptr<QTemporaryDir> package_temp_dir_;
    QPointer<AudienceWindow> audience_window_;
    QPointer<QScreen> audience_screen_;
    QString current_path_;
    QString current_package_path_;
    QString package_root_path_;
    QString entry_pdf_relative_path_;
    QStringList package_movie_asset_paths_;
    QStringList package_molecule_asset_paths_;
    QString document_hash_;
    QVector<QSizeF> page_sizes_points_;
    PdfMediaScanResult media_scan_result_;
    QRectF active_video_rect_;
    /** @brief Requested size and visible range of one thumbnail view. */
    struct DeckOverviewRequest {
        QSize size;
        int first_page = -1;
        int last_page = -1;
    };
    std::array<DeckOverviewRequest, 2> deck_overviews_;
    QByteArray document_data_;
    QElapsedTimer document_open_timer_;
    QElapsedTimer video_clock_;
    qint64 video_clock_origin_pts_ms_ = 0;
    std::optional<DecodedVideoFrame> pending_video_frame_;
    qint64 last_video_frame_pts_ms_ = -1;
    bool waiting_for_video_frame_ = false;
    bool video_playing_ = false;
    bool loaded_overlays_globally_visible_ = true;
    bool awaiting_first_slide_image_ = false;
    bool audience_slide_transition_pending_ = false;
    quint64 audience_slide_commit_sequence_ = 0;
    int current_page_index_ = 0;
    int render_generation_ = 0;
    int media_scan_generation_ = 0;
    int media_scan_applied_generation_ = -1;
    bool annotations_modified_ = false;
    QHash<int, QImage> loaded_overlay_images_;
    QSet<int> loaded_hidden_overlay_pages_;
};
