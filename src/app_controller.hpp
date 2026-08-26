#pragma once

#include "cache/slide_cache.hpp"
#include "pdf/pdf_backend.hpp"
#include "media/video_frame_buffer.hpp"
#include "media/video_frame_extractor.hpp"
#include "render/render_scheduler.hpp"
#include "util/pdf_media_detector.hpp"

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
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

#include <memory>

class AudienceWindow;

class AppController : public QObject {
    Q_OBJECT

public:
    /** @brief Constructs the application controller. */
    explicit AppController(QObject* parent = nullptr);
    /** @brief Stops background media analysis before controller destruction. */
    ~AppController() override;

    /** @brief Associates the controller with the audience presentation window. */
    void set_audience_window(AudienceWindow* audience_window);

    /** @brief Opens a PDF or UIL presentation from @p path. */
    bool open_pdf(const QString& path);
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
    /** @brief Requests thumbnail renders for the deck overview. */
    void request_deck_overview_renders(
        const QSize& bounding_pixel_size,
        int focused_page_index = -1,
        int first_visible_page = -1,
        int last_visible_page = -1);

    /** @brief Returns the path of the active PDF document. */
    QString current_path() const;
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
    /** @brief Saves the current presentation and annotations as a UIL package. */
    bool save_uil_package(const QString& path, const QHash<int, QImage>& overlay_images, const QSet<int>& hidden_overlay_pages, bool overlays_globally_visible, QString* error_message = nullptr);
    /** @brief Exports the current document and annotation overlays as a PDF. */
    bool export_annotated_pdf(const QString& path, const QHash<int, QImage>& overlay_images, QString* error_message = nullptr);
    /** @brief Clears the loaded annotation overlay for one page. */
    void clear_annotation_overlay_for_page(int page_index);
    /** @brief Clears every loaded annotation overlay. */
    void clear_all_annotation_overlays();
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
    void deck_slide_image_changed(int page_index, const QSize& bounding_pixel_size, const QImage& image);
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
    SlideCacheKey cache_key_for_page_at_size(int page_index, const QSize& bounding_pixel_size) const;
    /** @brief Builds a cache key for a page at the audience render size. */
    SlideCacheKey cache_key_for_page(int page_index) const;
    /** @brief Creates the audience texture key associated with a cache key. */
    QString texture_key_for_cache_key(const SlideCacheKey& key) const;
    /** @brief Builds a render request for a page and explicit render size. */
    RenderRequest render_request_for_page_at_size(int page_index, const QSize& bounding_pixel_size) const;
    /** @brief Builds a render request for a page at the audience render size. */
    RenderRequest render_request_for_page(int page_index) const;
    /** @brief Updates the current and next slide images. */
    void update_visible_slides();
    /** @brief Schedules likely upcoming pages for background rendering. */
    void schedule_predictive_renders();
    /** @brief Queues a page render at an explicit size and priority. */
    void request_page_render_at_size(int page_index, const QSize& bounding_pixel_size, int priority);
    /** @brief Queues a page render at the audience size and priority. */
    void request_page_render(int page_index, int priority);
    /** @brief Composites active video frames over a rendered slide. */
    QImage image_with_media_frames(int page_index, const QImage& image) const;
    /** @brief Returns playable media attached to the current page, if any. */
    const PdfMediaAnnotation* current_playable_media_annotation() const;
    /** @brief Normalizes a PDF media rectangle to slide-relative coordinates. */
    QRectF normalized_media_rect(const PdfMediaAnnotation& annotation) const;
    /** @brief Starts playback of the current slide's media. */
    void start_media_playback();
    /** @brief Stops active media playback and clears its frame overlay. */
    void stop_media_playback();
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
    void handle_render_finished(const RenderRequest& request, const QImage& image, qint64 elapsed_ms, const QString& error_message);
    /** @brief Scans the active document for media on a background worker. */
    void schedule_media_scan(
        const QString& pdf_path,
        const QString& package_root_path,
        const QStringList& package_movie_asset_paths,
        const QString& document_hash,
        int generation,
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
    QString document_hash_;
    QVector<QSizeF> page_sizes_points_;
    PdfMediaScanResult media_scan_result_;
    QRectF active_video_rect_;
    QSize deck_overview_render_size_;
    int deck_overview_first_page_ = -1;
    int deck_overview_last_page_ = -1;
    QElapsedTimer document_open_timer_;
    qint64 last_video_pts_ms_ = -1;
    bool waiting_for_video_frame_ = false;
    bool video_playing_ = false;
    bool loaded_overlays_globally_visible_ = true;
    bool awaiting_first_slide_image_ = false;
    int current_page_index_ = 0;
    int render_generation_ = 0;
    int media_scan_generation_ = 0;
    QHash<int, QImage> loaded_overlay_images_;
    QSet<int> loaded_hidden_overlay_pages_;
};
