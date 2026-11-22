#include "app_controller.hpp"

#include "package/uil_package.hpp"
#include "pdf/qt_pdf_backend.hpp"
#include "ui/audience_window.hpp"
#include "util/document_hash.hpp"
#include "util/image_util.hpp"
#include "util/performance_log.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QScreen>
#include <QStringList>
#include <QTemporaryDir>
#include <QtMath>

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>

Q_LOGGING_CATEGORY(log_render, "render")
Q_LOGGING_CATEGORY(log_screens, "screens")

namespace {
constexpr int kMaximumOverlayDimension = 8192;
constexpr qint64 kMaximumOverlayPixels = 34LL * 1024LL * 1024LL;
// Commit on the next event-loop turn so key-repeat bursts coalesce without a fixed delay.
constexpr int kAudienceSlideSwapDelayMs = 0;
// Larger PDFs are rendered straight from the file instead of from an in-memory copy.
constexpr qint64 kMaximumInMemoryPdfBytes = 512LL * 1024LL * 1024LL;
// Priorities of audience-window slide renders; thumbnails stay below kNextSlidePriority
// unless the audience is looking at its own slide grid.
constexpr int kCurrentSlidePriority = 1000;
constexpr int kNextSlidePriority = 75;
constexpr int kPreviousSlidePriority = 50;
constexpr int kSecondNextSlidePriority = 25;
// Frame spacing used when a video stream has no usable, increasing timestamps (~30 fps).
constexpr qint64 kFallbackVideoFrameIntervalMs = 33;

/** @brief Drops entries whose page index is outside [0, @p page_count). */
template <typename Container>
void remove_out_of_range_pages(Container& pages, int page_count) {
    for (auto it = pages.begin(); it != pages.end();) {
        const int page_index = [&] {
            if constexpr (std::is_same_v<Container, QSet<int>>) {
                return *it;
            } else {
                return it.key();
            }
        }();
        if (page_index < 0 || page_index >= page_count) {
            it = pages.erase(it);
        } else {
            ++it;
        }
    }
}

/** @brief Decodes a bounded PNG overlay from an extracted package. */
QImage read_safe_package_overlay(const QString& path) {
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (reader.format().toLower() != QByteArrayLiteral("png") || !size.isValid() ||
        size.width() > kMaximumOverlayDimension || size.height() > kMaximumOverlayDimension ||
        qint64(size.width()) * qint64(size.height()) > kMaximumOverlayPixels) {
        return {};
    }
    return reader.read();
}
}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent),
      backend_(std::make_unique<QtPdfBackend>()),
      slide_cache_(),
      render_scheduler_(this) {
    media_scan_pool_.setMaxThreadCount(1);
    media_scan_pool_.setExpiryTimeout(30'000);
    connect(&render_scheduler_, &RenderScheduler::render_started, this,
            &AppController::handle_render_started);
    connect(&render_scheduler_, &RenderScheduler::render_finished, this,
            &AppController::handle_render_finished);
    video_timer_.setSingleShot(true);
    // Windows' default coarse timers have ~15.6 ms granularity, which visibly judders video.
    video_timer_.setTimerType(Qt::PreciseTimer);
    connect(&video_timer_, &QTimer::timeout, this, &AppController::advance_video_frame);
    refresh_screens();
}

AppController::~AppController() {
    ++media_scan_generation_;
    media_scan_pool_.clear();
    media_scan_pool_.waitForDone();
    render_scheduler_.stop_and_wait();
    backend_.reset();
    package_temp_dir_.reset();
}

void AppController::set_audience_window(AudienceWindow* audience_window) {
    if (audience_window_) {
        disconnect(audience_window_, nullptr, this, nullptr);
    }

    audience_window_ = audience_window;
    if (audience_window_) {
        connect(audience_window_, &AudienceWindow::render_target_changed, this,
                &AppController::handle_audience_render_target_changed);
        connect(audience_window_, &AudienceWindow::annotation_overlay_changed, this,
                &AppController::current_annotation_overlay_changed);
        connect(audience_window_, &AudienceWindow::annotations_edited, this,
                &AppController::mark_annotations_modified);
        connect(audience_window_, &AudienceWindow::presentation_closed, this,
                &AppController::stop_media_playback);
        if (audience_screen_) {
            audience_window_->set_audience_screen(audience_screen_);
        }
        update_active_molecule();
        update_active_interactive_figure();
        update_active_atomic_orbital();
        update_active_molecular_symmetry();
    }
}

bool AppController::open_pdf(const QString& path) { return open_document(path, nullptr); }

bool AppController::open_document(const QString& path, const AnnotationState* preserved) {
    const QFileInfo input_info(path);
    const bool is_uil_package =
        input_info.suffix().compare(QStringLiteral("uil"), Qt::CaseInsensitive) == 0;
    performance_log::ScopedSpan open_span(
        QStringLiteral("document.open"),
        {{QStringLiteral("file_name"), input_info.fileName()},
         {QStringLiteral("file_size_bytes"), input_info.size()},
         {QStringLiteral("format"),
          is_uil_package ? QStringLiteral("uil") : QStringLiteral("pdf")}});
    document_open_timer_.start();
    awaiting_first_slide_image_ = false;

    stop_media_playback();
    open_span.checkpoint(QStringLiteral("stop_media_playback"));
    QString pdf_path = path;
    QString document_hash_path = path;
    QString package_root_path;
    QStringList package_movie_asset_paths;
    QStringList package_molecule_asset_paths;
    QString entry_pdf_relative_path = QStringLiteral("build/presentation.pdf");
    QHash<int, QImage> package_overlay_images;
    QSet<int> package_hidden_overlay_pages;
    bool package_overlays_globally_visible = true;
    std::shared_ptr<QTemporaryDir> packageTempDir;

    if (is_uil_package) {
        packageTempDir = std::make_shared<QTemporaryDir>();
        packageTempDir->setAutoRemove(true);
        if (!packageTempDir->isValid()) {
            open_span.add_field(QStringLiteral("error_stage"),
                                QStringLiteral("create_package_temporary_directory"));
            open_span.set_outcome(QStringLiteral("failed"));
            emit status_message_changed(
                QStringLiteral("Could not create temporary directory for UIL package"));
            return false;
        }
        open_span.checkpoint(QStringLiteral("create_package_temporary_directory"));

        UilPackageOpenResult package_result;
        QString package_error;
        if (!extract_uil_package(path, *packageTempDir, &package_result, &package_error)) {
            open_span.add_field(QStringLiteral("error_stage"),
                                QStringLiteral("extract_uil_package"));
            open_span.set_outcome(QStringLiteral("failed"));
            emit status_message_changed(
                QStringLiteral("Could not open UIL package: %1").arg(package_error));
            return false;
        }
        open_span.checkpoint(
            QStringLiteral("extract_uil_package"),
            {{QStringLiteral("movie_asset_count"), package_result.movie_asset_paths.size()},
             {QStringLiteral("molecule_asset_count"), package_result.molecule_asset_paths.size()},
             {QStringLiteral("overlay_count"), package_result.overlay_image_paths.size()}});

        pdf_path = package_result.entry_pdf_path;
        package_root_path = package_result.package_root_path;
        package_movie_asset_paths = package_result.movie_asset_paths;
        package_molecule_asset_paths = package_result.molecule_asset_paths;
        entry_pdf_relative_path = package_result.entry_pdf_relative_path;
        package_hidden_overlay_pages = package_result.hidden_overlay_pages;
        package_overlays_globally_visible = package_result.overlays_globally_visible;
        for (auto it = package_result.overlay_image_paths.constBegin();
             it != package_result.overlay_image_paths.constEnd(); ++it) {
            const QImage overlay = read_safe_package_overlay(packageTempDir->filePath(it.value()));
            if (!overlay.isNull()) {
                package_overlay_images.insert(it.key(), overlay);
            } else {
                performance_log::record_event(
                    QStringLiteral("package.overlay_rejected"),
                    {{QStringLiteral("page"), it.key() + 1}, {QStringLiteral("path"), it.value()}});
            }
        }
        open_span.checkpoint(
            QStringLiteral("decode_package_overlays"),
            {{QStringLiteral("decoded_overlay_count"), package_overlay_images.size()}});
    }

    // Render from an in-memory snapshot so later rewrites of the file (a LaTeX rebuild, or an
    // export onto the same path) cannot change or corrupt the open deck, and so no render
    // worker keeps an extracted package file open.
    QByteArray document_data;
    {
        QFile pdf_file(pdf_path);
        if (pdf_file.size() <= kMaximumInMemoryPdfBytes && pdf_file.open(QIODevice::ReadOnly)) {
            document_data = pdf_file.readAll();
        }
    }
    open_span.checkpoint(QStringLiteral("read_pdf_bytes"),
                         {{QStringLiteral("bytes"), document_data.size()}});

    auto backend = std::make_unique<QtPdfBackend>();
    QString error_message;
    const bool backend_opened = document_data.isEmpty()
                                    ? backend->open(pdf_path, &error_message)
                                    : backend->open_data(document_data, &error_message);
    if (!backend_opened) {
        open_span.add_field(QStringLiteral("error_stage"), QStringLiteral("open_pdf_backend"));
        open_span.set_outcome(QStringLiteral("failed"));
        emit status_message_changed(
            is_uil_package ? QStringLiteral("Could not open UIL package PDF: %1").arg(error_message)
                           : QStringLiteral("Could not open PDF: %1").arg(error_message));
        return false;
    }
    open_span.checkpoint(QStringLiteral("open_pdf_backend"),
                         {{QStringLiteral("page_count"), backend->page_count()}});

    QVector<QSizeF> page_sizes_points;
    page_sizes_points.reserve(backend->page_count());
    for (int page_index = 0; page_index < backend->page_count(); ++page_index) {
        page_sizes_points.push_back(backend->page_size_points(page_index));
    }
    open_span.checkpoint(QStringLiteral("cache_page_geometry"),
                         {{QStringLiteral("page_count"), page_sizes_points.size()}});

    if (preserved) {
        package_overlay_images = preserved->overlay_images;
        package_hidden_overlay_pages = preserved->hidden_overlay_pages;
        package_overlays_globally_visible = preserved->overlays_globally_visible;
    }
    remove_out_of_range_pages(package_overlay_images, page_sizes_points.size());
    remove_out_of_range_pages(package_hidden_overlay_pages, page_sizes_points.size());

    ++media_scan_generation_;
    media_scan_pool_.clear();
    backend_ = std::move(backend);
    document_data_ = std::move(document_data);
    page_sizes_points_ = std::move(page_sizes_points);
    package_temp_dir_ = packageTempDir;
    current_path_ = pdf_path;
    current_package_path_ = is_uil_package ? QFileInfo(path).absoluteFilePath() : QString();
    package_root_path_ = package_root_path;
    entry_pdf_relative_path_ = entry_pdf_relative_path;
    package_movie_asset_paths_ = package_movie_asset_paths;
    package_molecule_asset_paths_ = package_molecule_asset_paths;
    loaded_overlay_images_ = package_overlay_images;
    loaded_hidden_overlay_pages_ = package_hidden_overlay_pages;
    loaded_overlays_globally_visible_ = package_overlays_globally_visible;
    annotations_modified_ = preserved != nullptr;
    document_hash_ = document_hash_for_file(document_hash_path);
    open_span.checkpoint(QStringLiteral("commit_document_state"));
    media_scan_result_ = {};
    open_span.checkpoint(QStringLiteral("initialize_media_scan"));
    current_page_index_ = 0;
    slide_cache_.clear();
    render_scheduler_.clear();
    render_generation_ = render_scheduler_.generation();
    audience_slide_transition_pending_ = false;
    ++audience_slide_commit_sequence_;
    deck_overviews_ = {};
    open_span.checkpoint(QStringLiteral("reset_render_state"));
    if (audience_window_) {
        audience_window_->clear_slide_image();
        audience_window_->clear_video_overlay();
        QHash<QString, QImage> overlays_by_texture_key;
        for (auto it = loaded_overlay_images_.constBegin(); it != loaded_overlay_images_.constEnd();
             ++it) {
            if (it.key() >= 0 && it.key() < page_count() && !it.value().isNull()) {
                overlays_by_texture_key.insert(
                    texture_key_for_cache_key(cache_key_for_page(it.key())), it.value());
            }
        }
        audience_window_->set_annotation_overlays_by_texture_key(overlays_by_texture_key);
    }
    open_span.checkpoint(QStringLiteral("prepare_audience_window"));

    awaiting_first_slide_image_ = true;
    request_page_render(current_page_index_, kCurrentSlidePriority);
    open_span.checkpoint(QStringLiteral("queue_first_slide"));
    emit document_changed(page_count());
    open_span.checkpoint(QStringLiteral("notify_document_changed"));
    emit media_scan_changed(media_scan_result_);
    open_span.checkpoint(QStringLiteral("notify_media_scan_changed"));
    update_visible_slides();
    open_span.checkpoint(QStringLiteral("update_visible_slides"));
    open_span.checkpoint(QStringLiteral("defer_predictive_renders"));
    schedule_media_scan(pdf_path, package_root_path, package_movie_asset_paths,
                        package_molecule_asset_paths, document_hash_, media_scan_generation_,
                        package_temp_dir_);
    open_span.checkpoint(QStringLiteral("schedule_media_scan"));
    open_span.add_field(QStringLiteral("page_count"), page_count());
    open_span.set_outcome(QStringLiteral("opened"));
    return true;
}

bool AppController::reload_current_document(const std::optional<AnnotationState>& presenter_state) {
    if (!has_document()) {
        emit status_message_changed(QStringLiteral("No presentation is open to reload"));
        return false;
    }

    // Unsaved annotations must survive a reload (F5 rebuild-and-present workflow); saved ones
    // are reloaded from the package like everything else.
    std::optional<AnnotationState> preserved;
    if (annotations_modified_) {
        AnnotationState state = presenter_state.value_or(
            AnnotationState{loaded_overlay_images_, loaded_hidden_overlay_pages_,
                            loaded_overlays_globally_visible_});
        if (audience_window_) {
            const QHash<int, QImage> audience_overlays =
                audience_window_->annotation_overlays_by_page();
            for (auto it = audience_overlays.constBegin(); it != audience_overlays.constEnd();
                 ++it) {
                if (!it.value().isNull()) {
                    state.overlay_images.insert(it.key(), it.value());
                }
            }
        }
        preserved = std::move(state);
    }

    // UIL packages are extracted to a temporary PDF path. Reopen the package
    // itself so its manifest, assets, and overlays are refreshed as well.
    const QString path = current_package_path_.isEmpty() ? current_path_ : current_package_path_;
    const int page_to_restore = current_page_index_;
    if (!open_document(path, preserved ? &*preserved : nullptr)) {
        return false;
    }

    go_to_page(page_to_restore);
    emit status_message_changed(
        QStringLiteral("Reloaded presentation at slide %1").arg(current_page_index_ + 1));
    return true;
}

void AppController::schedule_media_scan(const QString& pdf_path, const QString& package_root_path,
                                        const QStringList& package_movie_asset_paths,
                                        const QStringList& package_molecule_asset_paths,
                                        const QString& document_hash, int generation,
                                        std::shared_ptr<QTemporaryDir> package_lifetime) {
    QPointer<AppController> self(this);
    auto* runnable = QRunnable::create(
        [self, pdf_path, package_root_path, package_movie_asset_paths, package_molecule_asset_paths,
         document_hash, generation, package_lifetime = std::move(package_lifetime)] {
            Q_UNUSED(package_lifetime);
            PdfMediaScanResult result =
                scan_pdf_media_annotations(pdf_path, package_root_path, package_movie_asset_paths,
                                           package_molecule_asset_paths);
            if (!self) {
                return;
            }

            QMetaObject::invokeMethod(
                self,
                [self, result = std::move(result), document_hash, generation]() mutable {
                    if (!self) {
                        return;
                    }
                    if (generation != self->media_scan_generation_ ||
                        document_hash != self->document_hash_) {
                        performance_log::record_event(QStringLiteral("pdf.media_scan_stale"));
                        return;
                    }

                    self->media_scan_result_ = std::move(result);
                    self->media_scan_applied_generation_ = generation;
                    if (self->current_package_path_.isEmpty()) {
                        self->package_movie_asset_paths_.clear();
                        self->package_molecule_asset_paths_.clear();
                        const QDir pdf_directory(QFileInfo(self->current_path_).absolutePath());
                        for (const PdfMediaAnnotation& annotation :
                             self->media_scan_result_.annotations) {
                            if (!annotation.resolved_file_path.isEmpty()) {
                                self->package_movie_asset_paths_.push_back(
                                    pdf_directory.relativeFilePath(annotation.resolved_file_path));
                            }
                        }
                        for (const PdfMoleculeAnnotation& annotation :
                             self->media_scan_result_.molecule_annotations) {
                            if (!annotation.resolved_file_path.isEmpty()) {
                                self->package_molecule_asset_paths_.push_back(
                                    pdf_directory.relativeFilePath(annotation.resolved_file_path));
                            }
                        }
                        self->package_movie_asset_paths_.removeDuplicates();
                        self->package_molecule_asset_paths_.removeDuplicates();
                    }
                    if (!self->audience_slide_transition_pending_) {
                        self->update_active_molecule();
                        self->update_active_interactive_figure();
                        self->update_active_atomic_orbital();
                        self->update_active_molecular_symmetry();
                    }
                    self->refresh_media_poster_pages();
                    emit self->media_scan_changed(self->media_scan_result_);
                    if (self->media_scan_result_.has_media()) {
                        emit self->status_message_changed(self->media_scan_result_.summary());
                    }
                    performance_log::record_event(
                        QStringLiteral("pdf.media_scan_applied"),
                        {{QStringLiteral("annotation_count"),
                          self->media_scan_result_.annotations.size() +
                              self->media_scan_result_.molecule_annotations.size() +
                              self->media_scan_result_.interactive_figure_annotations.size() +
                              self->media_scan_result_.atomic_orbital_annotations.size() +
                              self->media_scan_result_.molecular_symmetry_annotations.size()},
                         {QStringLiteral("generation"), generation}});
                },
                Qt::QueuedConnection);
        });
    media_scan_pool_.start(runnable);
}

void AppController::next_page() {
    if (!has_document()) {
        return;
    }
    go_to_page(current_page_index_ + 1);
}

void AppController::previous_page() {
    if (!has_document()) {
        return;
    }
    go_to_page(current_page_index_ - 1);
}

void AppController::go_to_page(int page_index) {
    if (!has_document()) {
        return;
    }

    const int clamped_page = std::clamp(page_index, 0, page_count() - 1);
    if (clamped_page == current_page_index_) {
        return;
    }

    stop_media_playback();
    if (audience_window_) {
        audience_window_->begin_slide_transition();
        audience_slide_transition_pending_ = true;
    }
    current_page_index_ = clamped_page;
    prune_stale_slide_renders();
    request_page_render(current_page_index_, kCurrentSlidePriority);
    update_visible_slides();
    schedule_predictive_renders();
}

void AppController::prune_stale_slide_renders() {
    // Holding an arrow key would otherwise queue full-resolution renders for every page passed
    // and make the slide the presenter stops on wait behind them.
    const int first_wanted = current_page_index_ - 1;
    const int last_wanted = current_page_index_ + 2;
    const int removed = render_scheduler_.retain_pending(
        [this, first_wanted, last_wanted](const RenderRequest& request) {
            if (request.page_index >= first_wanted && request.page_index <= last_wanted) {
                return true;
            }
            return request.pixel_size != cache_key_for_page(request.page_index).pixel_size;
        });
    if (removed > 0) {
        performance_log::record_event(QStringLiteral("render.pending_pruned"),
                                      {{QStringLiteral("count"), removed}});
    }
}

int AppController::current_page() const { return current_page_index_; }

int AppController::page_count() const { return backend_ ? backend_->page_count() : 0; }

void AppController::request_deck_overview_renders(const QSize& bounding_pixel_size,
                                                  int focused_page_index, int first_visible_page,
                                                  int last_visible_page) {
    request_overview_renders(OverviewView::Audience, bounding_pixel_size, focused_page_index,
                             first_visible_page, last_visible_page);
}

void AppController::request_presenter_thumbnail_renders(const QSize& bounding_pixel_size,
                                                        int focused_page_index,
                                                        int first_visible_page,
                                                        int last_visible_page) {
    request_overview_renders(OverviewView::Presenter, bounding_pixel_size, focused_page_index,
                             first_visible_page, last_visible_page);
}

void AppController::refresh_deck_overviews() {
    for (int view = 0; view < int(deck_overviews_.size()); ++view) {
        const DeckOverviewRequest overview = deck_overviews_[size_t(view)];
        if (overview.size.isValid()) {
            request_overview_renders(OverviewView(view), overview.size, current_page_index_,
                                     overview.first_page, overview.last_page);
        }
    }
}

void AppController::request_overview_renders(OverviewView view, const QSize& bounding_pixel_size,
                                             int focused_page_index, int first_visible_page,
                                             int last_visible_page) {
    DeckOverviewRequest& overview = deck_overviews_[size_t(view)];
    if (!bounding_pixel_size.isValid()) {
        // The view closed; stop re-requesting its thumbnails after cache resets.
        overview = {};
        return;
    }
    if (!has_document()) {
        return;
    }
    const bool foreground = view == OverviewView::Audience;

    performance_log::ScopedSpan span(
        QStringLiteral("render.deck_overview_request"),
        {{QStringLiteral("height"), bounding_pixel_size.height()},
         {QStringLiteral("page_count"), page_count()},
         {QStringLiteral("view"),
          foreground ? QStringLiteral("audience") : QStringLiteral("presenter")},
         {QStringLiteral("width"), bounding_pixel_size.width()}});

    const bool render_size_changed = overview.size != bounding_pixel_size;
    overview.size = bounding_pixel_size;
    const int focused_page = std::clamp(
        focused_page_index >= 0 ? focused_page_index : current_page_index_, 0, page_count() - 1);
    const int visible_first = std::clamp(
        first_visible_page >= 0 ? first_visible_page : focused_page, 0, page_count() - 1);
    const int visible_last =
        std::clamp(last_visible_page >= visible_first ? last_visible_page : visible_first,
                   visible_first, page_count() - 1);
    const int prefetch_count = std::clamp(visible_last - visible_first + 1, 4, 8);
    const int first_page = std::max(0, visible_first - prefetch_count);
    const int last_page = std::min(page_count() - 1, visible_last + prefetch_count);
    const int visible_center = std::clamp(focused_page, visible_first, visible_last);
    overview.first_page = visible_first;
    overview.last_page = visible_last;

    if (awaiting_first_slide_image_) {
        span.add_field(QStringLiteral("first_visible_page"), visible_first + 1);
        span.add_field(QStringLiteral("last_visible_page"), visible_last + 1);
        span.set_outcome(QStringLiteral("deferred_until_first_slide"));
        return;
    }

    int cache_hits = 0;
    for (int page = first_page; page <= last_page; ++page) {
        const SlideCacheKey key = cache_key_for_page_at_size(page, bounding_pixel_size);
        if (auto image = slide_cache_.get(key)) {
            ++cache_hits;
            emit deck_slide_image_changed(page, bounding_pixel_size, *image);
        }
    }
    span.checkpoint(QStringLiteral("check_cache"), {{QStringLiteral("cache_hits"), cache_hits}});

    // The presenter strip is always on screen, so its thumbnails must not delay the slides the
    // audience is about to see; the audience grid, when open, is what the audience sees.
    request_page_render_at_size(visible_center, bounding_pixel_size,
                                foreground ? 95 : kSecondNextSlidePriority - 1);
    const int maximum_visible_distance =
        std::max(visible_center - visible_first, visible_last - visible_center);
    for (int distance = 1; distance <= maximum_visible_distance; ++distance) {
        const int after_page = visible_center + distance;
        const int before_page = visible_center - distance;
        const int priority = foreground ? std::max(60, 90 - distance)
                                        : std::max(12, kSecondNextSlidePriority - 2 - distance);
        if (after_page <= visible_last) {
            request_page_render_at_size(after_page, bounding_pixel_size, priority);
        }
        if (before_page >= visible_first) {
            request_page_render_at_size(before_page, bounding_pixel_size, priority);
        }
    }

    const int maximum_prefetch_distance =
        std::max(visible_first - first_page, last_page - visible_last);
    for (int distance = 1; distance <= maximum_prefetch_distance; ++distance) {
        const int after_page = visible_last + distance;
        const int before_page = visible_first - distance;
        const int priority = foreground ? std::max(1, 40 - distance) : std::max(1, 11 - distance);
        if (after_page <= last_page) {
            request_page_render_at_size(after_page, bounding_pixel_size, priority);
        }
        if (before_page >= first_page) {
            request_page_render_at_size(before_page, bounding_pixel_size, priority);
        }
    }
    span.add_field(QStringLiteral("cache_hits"), cache_hits);
    span.add_field(QStringLiteral("focused_page"), focused_page + 1);
    span.add_field(QStringLiteral("first_visible_page"), visible_first + 1);
    span.add_field(QStringLiteral("last_visible_page"), visible_last + 1);
    span.add_field(QStringLiteral("first_page"), first_page + 1);
    span.add_field(QStringLiteral("last_page"), last_page + 1);
    span.add_field(QStringLiteral("render_attempt_count"), last_page - first_page + 1);
    span.add_field(QStringLiteral("render_size_changed"), render_size_changed);
    span.set_outcome(QStringLiteral("queued"));
}

QString AppController::current_path() const { return current_path_; }

QString AppController::document_display_path() const {
    return current_package_path_.isEmpty() ? current_path_ : current_package_path_;
}

bool AppController::has_unsaved_annotations() const { return annotations_modified_; }

void AppController::mark_annotations_modified() {
    if (has_document()) {
        annotations_modified_ = true;
    }
}

bool AppController::export_annotated_pdf(const QString& path,
                                         const QHash<int, QImage>& overlay_images,
                                         QString* error_message) {
    if (!has_document()) {
        if (error_message) {
            *error_message = QStringLiteral("No presentation is open");
        }
        return false;
    }
    if (path.isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("Missing export path");
        }
        return false;
    }

    const QFileInfo target_info(path);
    if ((!current_path_.isEmpty() && target_info == QFileInfo(current_path_)) ||
        (!current_package_path_.isEmpty() && target_info == QFileInfo(current_package_path_))) {
        if (error_message) {
            *error_message = QStringLiteral(
                "Choose a different file: the annotated PDF cannot replace the open presentation");
        }
        return false;
    }

    // Write to a temporary file and only replace the target once every page succeeded.
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error_message) {
            *error_message = QStringLiteral("Could not write %1: %2")
                                 .arg(QDir::toNativeSeparators(path), output.errorString());
        }
        return false;
    }

    QPdfWriter writer(&output);
    writer.setResolution(144);
    writer.setCreator(QStringLiteral("uil"));

    QPainter painter;
    for (int page_index = 0; page_index < page_count(); ++page_index) {
        const QSizeF page_size = page_size_points(page_index);
        if (!page_size.isValid()) {
            if (error_message) {
                *error_message = QStringLiteral("Could not determine page size for slide %1")
                                     .arg(page_index + 1);
            }
            return false;
        }

        const QPageSize writer_page_size(page_size, QPageSize::Point);
        writer.setPageSize(writer_page_size);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        if (page_index > 0 && !writer.newPage()) {
            if (error_message) {
                *error_message = QStringLiteral("Could not create PDF page %1").arg(page_index + 1);
            }
            return false;
        }

        if (!painter.isActive() && !painter.begin(&writer)) {
            if (error_message) {
                *error_message = QStringLiteral("Could not start PDF export");
            }
            return false;
        }

        const QSize target_pixel_size(qMax(1, int(qRound(page_size.width() * 2.0))),
                                      qMax(1, int(qRound(page_size.height() * 2.0))));
        QImage page_image = image_with_media_frames(
            page_index, backend_->render_page(page_index, target_pixel_size));
        if (page_image.isNull()) {
            if (error_message) {
                *error_message = QStringLiteral("Could not render slide %1").arg(page_index + 1);
            }
            return false;
        }

        const QImage overlay = overlay_images.value(page_index);
        if (!overlay.isNull()) {
            QPainter imagePainter(&page_image);
            imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            imagePainter.drawImage(page_image.rect(), overlay);
        }

        painter.drawImage(QRectF(QPointF(0, 0), page_size), page_image);
    }

    if (!painter.end() || !output.commit()) {
        if (error_message) {
            *error_message = QStringLiteral("Could not write %1: %2")
                                 .arg(QDir::toNativeSeparators(path), output.errorString());
        }
        return false;
    }
    return true;
}

bool AppController::save_uil_package(const QString& path, const QHash<int, QImage>& overlay_images,
                                     const QSet<int>& hidden_overlay_pages,
                                     bool overlays_globally_visible, QString* error_message) {
    if (!has_document()) {
        if (error_message) {
            *error_message = QStringLiteral("No presentation is open");
        }
        return false;
    }

    // For a plain PDF the movie and molecule files to bundle are discovered by the media scan.
    if (current_package_path_.isEmpty() && !wait_for_media_scan()) {
        if (error_message) {
            *error_message =
                QStringLiteral("Media files are still being analysed; please try again");
        }
        return false;
    }

    const QString entry_pdf = entry_pdf_relative_path_.isEmpty()
                                  ? QStringLiteral("build/presentation.pdf")
                                  : entry_pdf_relative_path_;
    QHash<int, QImage> package_overlay_images = overlay_images;
    if (audience_window_) {
        const QHash<int, QImage> audience_overlays =
            audience_window_->annotation_overlays_by_page();
        for (auto it = audience_overlays.constBegin(); it != audience_overlays.constEnd(); ++it) {
            if (!it.value().isNull()) {
                package_overlay_images.insert(it.key(), it.value());
            }
        }
    }

    // Package the bytes being presented rather than the file on disk, which may have been
    // rebuilt since opening; overlays and asset lists describe the presented version.
    QTemporaryDir snapshot_directory;
    QString source_pdf_path = current_path_;
    if (!document_data_.isEmpty()) {
        QFile snapshot(snapshot_directory.filePath(QStringLiteral("presentation.pdf")));
        if (!snapshot_directory.isValid() || !snapshot.open(QIODevice::WriteOnly) ||
            snapshot.write(document_data_) != document_data_.size() || !snapshot.flush()) {
            if (error_message) {
                *error_message = QStringLiteral("Could not prepare the presentation for saving");
            }
            return false;
        }
        source_pdf_path = snapshot.fileName();
    }

    if (!write_uil_package(path, source_pdf_path, entry_pdf,
                           package_root_path_.isEmpty() ? QFileInfo(current_path_).absolutePath()
                                                        : package_root_path_,
                           package_movie_asset_paths_, package_molecule_asset_paths_,
                           package_overlay_images, hidden_overlay_pages, overlays_globally_visible,
                           error_message)) {
        return false;
    }

    current_package_path_ = QFileInfo(path).absoluteFilePath();
    loaded_overlay_images_ = package_overlay_images;
    loaded_hidden_overlay_pages_ = hidden_overlay_pages;
    loaded_overlays_globally_visible_ = overlays_globally_visible;
    annotations_modified_ = false;
    return true;
}

bool AppController::wait_for_media_scan() {
    if (media_scan_applied_generation_ == media_scan_generation_) {
        return true;
    }

    // The scan posts its result back to this object; deliver it now instead of on the next
    // event-loop turn.
    media_scan_pool_.waitForDone();
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    return media_scan_applied_generation_ == media_scan_generation_;
}

void AppController::refresh_media_poster_pages() {
    // Pages rendered before the scan finished lack the video poster frames that
    // image_with_media_frames() paints in. Drop them so they are rendered again.
    QSet<int> poster_pages;
    for (const PdfMediaAnnotation& annotation : media_scan_result_.annotations) {
        if (annotation.has_first_frame()) {
            poster_pages.insert(annotation.page_index);
        }
    }
    bool current_page_changed = false;
    for (int page_index : std::as_const(poster_pages)) {
        if (slide_cache_.remove_page(document_hash_, page_index) > 0 &&
            page_index == current_page_index_) {
            current_page_changed = true;
        }
    }
    if (poster_pages.isEmpty()) {
        return;
    }

    if (current_page_changed) {
        request_page_render(current_page_index_, kCurrentSlidePriority);
    }
    schedule_predictive_renders();
    refresh_deck_overviews();
}

void AppController::clear_annotation_overlay_for_page(int page_index) {
    if (audience_window_) {
        audience_window_->clear_annotation_overlay_for_page(page_index);
    }
    loaded_overlay_images_.remove(page_index);
    mark_annotations_modified();
}

void AppController::clear_all_annotation_overlays() {
    if (audience_window_) {
        audience_window_->clear_all_annotation_overlays();
    }
    loaded_overlay_images_.clear();
    mark_annotations_modified();
}

QString AppController::current_package_path() const { return current_package_path_; }

QHash<int, QImage> AppController::loaded_overlay_images() const { return loaded_overlay_images_; }

QSet<int> AppController::loaded_hidden_overlay_pages() const {
    return loaded_hidden_overlay_pages_;
}

bool AppController::loaded_overlays_globally_visible() const {
    return loaded_overlays_globally_visible_;
}

QScreen* AppController::selected_audience_screen() const { return audience_screen_; }

void AppController::set_audience_screen(QScreen* screen) {
    if (!screen) {
        return;
    }

    const bool screen_changed = screen != audience_screen_;
    audience_screen_ = screen;
    qCInfo(log_screens) << "Audience screen selected:" << screen->name() << screen->geometry();
    emit audience_screen_changed(screen);

    if (audience_window_) {
        audience_window_->set_audience_screen(screen);
    }

    if (has_document()) {
        if (screen_changed) {
            slide_cache_.clear();
            render_scheduler_.clear();
            render_generation_ = render_scheduler_.generation();
        }
        update_visible_slides();
        schedule_predictive_renders();
        refresh_deck_overviews();
    }
}

void AppController::refresh_screens() {
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    QScreen* preferred_audience = primary;

    for (QScreen* screen : screens) {
        qCInfo(log_screens) << "Screen:" << screen->name() << "primary:" << (screen == primary)
                            << "geometry:" << screen->geometry()
                            << "dpr:" << screen->devicePixelRatio();
        if (screen != primary && preferred_audience == primary) {
            preferred_audience = screen;
        }
    }

    const bool selected_screen_gone = !screens.contains(audience_screen_);
    if (selected_screen_gone) {
        audience_screen_ = preferred_audience;
        slide_cache_.clear();
        render_scheduler_.clear();
        render_generation_ = render_scheduler_.generation();
        emit audience_screen_changed(audience_screen_);
        if (audience_window_ && audience_screen_) {
            audience_window_->set_audience_screen(audience_screen_);
        }
    }

    emit screen_list_changed();

    if (selected_screen_gone && has_document()) {
        update_visible_slides();
        schedule_predictive_renders();
        refresh_deck_overviews();
    }
}

void AppController::toggle_audience_fullscreen() {
    if (audience_window_) {
        if (audience_screen_) {
            audience_window_->set_audience_screen(audience_screen_);
        }
        audience_window_->toggle_fullscreen();
    }
}

void AppController::toggle_media_playback() {
    if (video_playing_) {
        stop_media_playback();
        emit status_message_changed(QStringLiteral("Media stopped"));
        return;
    }

    start_media_playback();
}

void AppController::toggle_black_screen() {
    if (audience_window_) {
        audience_window_->toggle_black_screen();
    }
}

void AppController::toggle_white_screen() {
    if (audience_window_) {
        audience_window_->toggle_white_screen();
    }
}

void AppController::close_audience_window() {
    stop_media_playback();
    if (audience_window_) {
        audience_window_->close();
    }
}

void AppController::enter_audience_fullscreen() {
    if (audience_window_) {
        if (!audience_screen_) {
            refresh_screens();
        }
        if (audience_screen_) {
            audience_window_->set_audience_screen(audience_screen_);
        }
        audience_window_->enter_fullscreen();
    }
}

QSize AppController::audience_render_pixel_size(int page_index) const {
    if (!has_document()) {
        return {};
    }

    QSize logical_size;
    qreal device_pixel_ratio = 1.0;
    if (audience_screen_) {
        logical_size = audience_screen_->geometry().size();
        device_pixel_ratio = audience_screen_->devicePixelRatio();
    } else if (audience_window_) {
        logical_size = audience_window_->render_logical_size();
        device_pixel_ratio = audience_window_->render_device_pixel_ratio();
    }

    if (!logical_size.isValid()) {
        logical_size = QSize(1280, 720);
    }

    const QSize bounding_pixels(qMax(1, int(qRound(logical_size.width() * device_pixel_ratio))),
                                qMax(1, int(qRound(logical_size.height() * device_pixel_ratio))));

    return contained_size_for_aspect(page_size_points(page_index), bounding_pixels);
}

SlideCacheKey AppController::cache_key_for_page(int page_index) const {
    return cache_key_for_page_at_size(page_index, audience_render_pixel_size(page_index));
}

SlideCacheKey AppController::cache_key_for_page_at_size(int page_index,
                                                        const QSize& bounding_pixel_size) const {
    if (!has_document() || !bounding_pixel_size.isValid()) {
        return {};
    }

    return SlideCacheKey{
        document_hash_, page_index,
        contained_size_for_aspect(page_size_points(page_index), bounding_pixel_size), 0};
}

QString AppController::texture_key_for_cache_key(const SlideCacheKey& key) const {
    return QStringLiteral("%1:%2:%3x%4:%5")
        .arg(key.document_hash)
        .arg(key.page_index)
        .arg(key.pixel_size.width())
        .arg(key.pixel_size.height())
        .arg(key.rotation);
}

bool AppController::has_document() const { return backend_ && backend_->page_count() > 0; }

QSizeF AppController::page_size_points(int page_index) const {
    if (page_index < 0 || page_index >= page_sizes_points_.size()) {
        return {};
    }
    return page_sizes_points_.at(page_index);
}

RenderRequest AppController::render_request_for_page(int page_index) const {
    return render_request_for_page_at_size(page_index, audience_render_pixel_size(page_index));
}

RenderRequest AppController::render_request_for_page_at_size(
    int page_index, const QSize& bounding_pixel_size) const {
    const SlideCacheKey sizedKey = cache_key_for_page_at_size(page_index, bounding_pixel_size);
    return RenderRequest{current_path_,       sizedKey.document_hash, sizedKey.page_index,
                         sizedKey.pixel_size, sizedKey.rotation,      render_generation_,
                         document_data_};
}

void AppController::update_visible_slides() {
    if (!has_document()) {
        return;
    }

    const SlideCacheKey currentKey = cache_key_for_page(current_page_index_);
    if (auto current_image = slide_cache_.get(currentKey)) {
        emit current_slide_image_changed(*current_image);
        if (audience_window_) {
            queue_audience_slide_commit(current_page_index_, currentKey, *current_image);
            if (!video_playing_) {
                audience_window_->clear_video_overlay();
            }
        }
        emit status_message_changed(
            QStringLiteral("Cache hit: page %1").arg(current_page_index_ + 1));
    } else {
        emit current_slide_image_changed(QImage());
        emit current_annotation_overlay_changed({});
        if (audience_window_) {
            audience_window_->clear_video_overlay();
        }
        emit status_message_changed(
            QStringLiteral("Rendering page %1").arg(current_page_index_ + 1));
    }

    const int next_page = current_page_index_ + 1;
    if (next_page < page_count()) {
        const SlideCacheKey nextKey = cache_key_for_page(next_page);
        if (auto next_image = slide_cache_.get(nextKey)) {
            emit next_slide_image_changed(*next_image);
        } else {
            emit next_slide_image_changed(QImage());
        }
    } else {
        emit next_slide_image_changed(QImage());
    }

    emit page_changed(current_page_index_, page_count());
}

void AppController::queue_audience_slide_commit(int page_index, const SlideCacheKey& key,
                                                const QImage& image) {
    if (!audience_window_ || image.isNull()) {
        return;
    }

    const quint64 sequence = ++audience_slide_commit_sequence_;
    const QString texture_key = texture_key_for_cache_key(key);
    QTimer::singleShot(
        kAudienceSlideSwapDelayMs, this, [this, sequence, page_index, texture_key, image] {
            if (sequence != audience_slide_commit_sequence_ || page_index != current_page_index_ ||
                texture_key != texture_key_for_cache_key(cache_key_for_page(page_index))) {
                return;
            }
            commit_audience_slide(texture_key, image);
        });
}

void AppController::commit_audience_slide(const QString& texture_key, const QImage& image) {
    if (!audience_window_) {
        audience_slide_transition_pending_ = false;
        return;
    }

    AudienceWindow* window = audience_window_;
    window->set_slide_image(texture_key, image);
    update_active_molecule();
    update_active_interactive_figure();
    update_active_atomic_orbital();
    update_active_molecular_symmetry();
    window->prepare_interactive_overlays_for_display();
    audience_slide_transition_pending_ = false;
    window->repaint();
}

void AppController::schedule_predictive_renders() {
    if (!has_document()) {
        return;
    }

    request_page_render(current_page_index_, 100);
    request_page_render(current_page_index_ + 1, kNextSlidePriority);
    request_page_render(current_page_index_ - 1, kPreviousSlidePriority);
    request_page_render(current_page_index_ + 2, kSecondNextSlidePriority);
}

void AppController::request_page_render(int page_index, int priority) {
    request_page_render_at_size(page_index, audience_render_pixel_size(page_index), priority);
}

void AppController::request_page_render_at_size(int page_index, const QSize& bounding_pixel_size,
                                                int priority) {
    if (!has_document() || page_index < 0 || page_index >= page_count()) {
        return;
    }

    const SlideCacheKey key = cache_key_for_page_at_size(page_index, bounding_pixel_size);
    if (slide_cache_.contains(key)) {
        return;
    }

    render_scheduler_.request_render(
        render_request_for_page_at_size(page_index, bounding_pixel_size), priority);
}

QImage AppController::image_with_media_frames(int page_index, const QImage& image) const {
    if (image.isNull() || !backend_) {
        return image;
    }
    // Avoid detaching (deep-copying) the full-resolution slide when there is nothing to paint.
    const bool has_poster =
        std::any_of(media_scan_result_.annotations.cbegin(), media_scan_result_.annotations.cend(),
                    [page_index](const PdfMediaAnnotation& annotation) {
                        return annotation.page_index == page_index &&
                               annotation.has_first_frame() && annotation.rect.isValid();
                    });
    if (!has_poster) {
        return image;
    }

    QImage result = image;
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QSizeF page_size = page_size_points(page_index);
    if (!page_size.isValid()) {
        return result;
    }

    const double scale_x = double(result.width()) / page_size.width();
    const double scale_y = double(result.height()) / page_size.height();

    for (const PdfMediaAnnotation& annotation : media_scan_result_.annotations) {
        if (annotation.page_index != page_index || !annotation.has_first_frame() ||
            !annotation.rect.isValid()) {
            continue;
        }

        const QRectF target(annotation.rect.left() * scale_x,
                            (page_size.height() - annotation.rect.bottom()) * scale_y,
                            annotation.rect.width() * scale_x, annotation.rect.height() * scale_y);
        painter.drawImage(target.toRect(), annotation.first_frame);
    }

    return result;
}

const PdfMediaAnnotation* AppController::current_playable_media_annotation() const {
    for (const PdfMediaAnnotation& annotation : media_scan_result_.annotations) {
        if (annotation.page_index == current_page_index_ && annotation.is_mp4() &&
            !annotation.resolved_file_path.isEmpty() && annotation.rect.isValid()) {
            return &annotation;
        }
    }
    return nullptr;
}

QRectF AppController::normalized_media_rect(const PdfMediaAnnotation& annotation) const {
    return normalized_pdf_rect(annotation.page_index, annotation.rect);
}

QRectF AppController::normalized_pdf_rect(int page_index, const QRectF& rect) const {
    if (!backend_ || !rect.isValid()) {
        return {};
    }

    const QSizeF page_size = page_size_points(page_index);
    if (!page_size.isValid()) {
        return {};
    }

    return QRectF(rect.left() / page_size.width(),
                  (page_size.height() - rect.bottom()) / page_size.height(),
                  rect.width() / page_size.width(), rect.height() / page_size.height());
}

void AppController::update_active_molecule() {
    if (!audience_window_) {
        return;
    }

    for (const PdfMoleculeAnnotation& annotation : media_scan_result_.molecule_annotations) {
        if (annotation.page_index == current_page_index_ && annotation.is_ready()) {
            audience_window_->set_molecule_overlay(
                annotation.geometry, normalized_pdf_rect(annotation.page_index, annotation.rect));
            return;
        }
    }
    audience_window_->clear_molecule_overlay();
}

void AppController::update_active_interactive_figure() {
    if (!audience_window_) {
        return;
    }
    for (const PdfInteractiveFigureAnnotation& annotation :
         media_scan_result_.interactive_figure_annotations) {
        if (annotation.page_index == current_page_index_ && annotation.is_ready()) {
            audience_window_->set_interactive_figure_overlay(
                annotation.definition, normalized_pdf_rect(annotation.page_index, annotation.rect));
            return;
        }
    }
    audience_window_->clear_interactive_figure_overlay();
}

void AppController::update_active_atomic_orbital() {
    if (!audience_window_) return;
    QVector<AudienceWindow::AtomicOrbitalOverlay> overlays;
    for (const PdfAtomicOrbitalAnnotation& annotation :
         media_scan_result_.atomic_orbital_annotations) {
        if (annotation.page_index == current_page_index_ && annotation.is_ready()) {
            overlays.push_back({annotation.definition,
                                normalized_pdf_rect(annotation.page_index, annotation.rect)});
        }
    }
    QVector<AtomicOrbitalDefinition> nearby;
    // Keep speculative work local; navigating again replaces any unstarted builds.
    for (const int offset : {1, -1, 2}) {
        for (const auto& annotation : media_scan_result_.atomic_orbital_annotations) {
            if (annotation.page_index == current_page_index_ + offset && annotation.is_ready())
                nearby.push_back(annotation.definition);
        }
    }
    audience_window_->set_atomic_orbital_overlays(
        overlays, nearby, texture_key_for_cache_key(cache_key_for_page(current_page_index_)));
}

void AppController::update_active_molecular_symmetry() {
    if (!audience_window_) return;
    for (const PdfMolecularSymmetryAnnotation& annotation :
         media_scan_result_.molecular_symmetry_annotations) {
        if (annotation.page_index == current_page_index_ && annotation.is_ready()) {
            audience_window_->set_molecular_symmetry_overlay(
                annotation.definition, normalized_pdf_rect(annotation.page_index, annotation.rect));
            return;
        }
    }
    audience_window_->clear_molecular_symmetry_overlay();
}

void AppController::start_media_playback() {
    const PdfMediaAnnotation* annotation = current_playable_media_annotation();
    if (!annotation) {
        emit status_message_changed(QStringLiteral("No playable MP4 media on this slide"));
        return;
    }

    auto buffer = std::make_unique<VideoFrameBuffer>();
    connect(buffer.get(), &VideoFrameBuffer::frame_available, this,
            &AppController::handle_buffered_video_frame_available);
    connect(buffer.get(), &VideoFrameBuffer::finished, this,
            &AppController::handle_video_decode_finished);
    connect(buffer.get(), &VideoFrameBuffer::failed, this,
            &AppController::handle_video_decode_failed);
    video_buffer_ = std::move(buffer);
    active_video_rect_ = normalized_media_rect(*annotation);
    pending_video_frame_.reset();
    last_video_frame_pts_ms_ = -1;
    video_clock_.invalidate();
    waiting_for_video_frame_ = true;
    video_playing_ = true;
    emit status_message_changed(QStringLiteral("Playing media: %1").arg(annotation->file_name));
    video_buffer_->start(annotation->resolved_file_path, 3000);
    advance_video_frame();
}

void AppController::stop_media_playback() {
    video_timer_.stop();
    if (video_buffer_) {
        video_buffer_->stop();
        video_buffer_.reset();
    }
    video_playing_ = false;
    waiting_for_video_frame_ = false;
    active_video_rect_ = {};
    pending_video_frame_.reset();
    last_video_frame_pts_ms_ = -1;
    video_clock_.invalidate();
    if (audience_window_) {
        audience_window_->clear_video_overlay();
    }
}

void AppController::advance_video_frame() {
    if (!video_buffer_ || !video_playing_) {
        stop_media_playback();
        return;
    }

    if (!pending_video_frame_) {
        pending_video_frame_ = take_video_frame();
    }
    if (!pending_video_frame_) {
        if (video_buffer_->is_finished()) {
            const QString error_message = video_buffer_->error_message();
            stop_media_playback();
            if (!error_message.isEmpty()) {
                emit status_message_changed(QStringLiteral("Media stopped: %1").arg(error_message));
            } else {
                emit status_message_changed(QStringLiteral("Media finished"));
            }
            return;
        }
        if (!waiting_for_video_frame_) {
            emit status_message_changed(QStringLiteral("Buffering media..."));
        }
        waiting_for_video_frame_ = true;
        // Re-anchor the clock on the next frame instead of skipping ahead after a stall.
        video_clock_.invalidate();
        video_timer_.start(30);
        return;
    }

    waiting_for_video_frame_ = false;
    if (!video_clock_.isValid()) {
        video_clock_.start();
        video_clock_origin_pts_ms_ = pending_video_frame_->pts_ms;
    }

    // Frames are presented against a monotonic clock anchored at the first shown frame, so
    // timer jitter never accumulates into drift. Frames that are already late are dropped.
    const qint64 now_pts_ms = video_clock_origin_pts_ms_ + video_clock_.elapsed();
    if (pending_video_frame_->pts_ms > now_pts_ms) {
        video_timer_.start(int(std::min<qint64>(pending_video_frame_->pts_ms - now_pts_ms, 100)));
        return;
    }

    DecodedVideoFrame frame = std::move(*pending_video_frame_);
    pending_video_frame_.reset();
    while (std::optional<DecodedVideoFrame> next = take_video_frame()) {
        if (next->pts_ms > now_pts_ms) {
            pending_video_frame_ = std::move(next);
            break;
        }
        frame = std::move(*next);
    }

    if (audience_window_) {
        audience_window_->set_video_frame(frame.image, active_video_rect_);
    }

    const qint64 next_delay_ms =
        pending_video_frame_ ? pending_video_frame_->pts_ms - now_pts_ms : 1000 / 60;
    video_timer_.start(int(std::clamp<qint64>(next_delay_ms, 1, 100)));
}

std::optional<DecodedVideoFrame> AppController::take_video_frame() {
    std::optional<DecodedVideoFrame> frame = video_buffer_->take_frame();
    if (frame) {
        // Streams without timestamps report 0 for every frame; without this they would be
        // drained as "late" and play as fast as they decode. Also covers timestamps going back.
        if (last_video_frame_pts_ms_ >= 0 && frame->pts_ms <= last_video_frame_pts_ms_) {
            frame->pts_ms = last_video_frame_pts_ms_ + kFallbackVideoFrameIntervalMs;
        }
        last_video_frame_pts_ms_ = frame->pts_ms;
    }
    return frame;
}

void AppController::handle_buffered_video_frame_available() {
    if (video_playing_ && waiting_for_video_frame_ && !video_timer_.isActive()) {
        advance_video_frame();
    }
}

void AppController::handle_video_decode_finished() {
    if (video_playing_ && waiting_for_video_frame_ && !pending_video_frame_ &&
        (!video_buffer_ || !video_buffer_->has_frames())) {
        stop_media_playback();
        emit status_message_changed(QStringLiteral("Media finished"));
    }
}

void AppController::handle_video_decode_failed(const QString& error_message) {
    if (!video_playing_) {
        return;
    }

    if (pending_video_frame_ || (video_buffer_ && video_buffer_->has_frames())) {
        return;
    }

    stop_media_playback();
    emit status_message_changed(QStringLiteral("Media stopped: %1").arg(error_message));
}

void AppController::handle_audience_render_target_changed() {
    if (!has_document()) {
        return;
    }

    if (audience_window_) {
        const QHash<int, QImage> overlays_by_page = audience_window_->annotation_overlays_by_page();
        QHash<QString, QImage> overlays_by_texture_key;
        for (auto it = overlays_by_page.constBegin(); it != overlays_by_page.constEnd(); ++it) {
            if (it.key() >= 0 && it.key() < page_count() && !it.value().isNull()) {
                overlays_by_texture_key.insert(
                    texture_key_for_cache_key(cache_key_for_page(it.key())), it.value());
            }
        }
        audience_window_->set_annotation_overlays_by_texture_key(overlays_by_texture_key);
    }

    update_visible_slides();
    schedule_predictive_renders();
    refresh_deck_overviews();
}

void AppController::handle_render_started(const RenderRequest& request) { Q_UNUSED(request); }

void AppController::handle_render_finished(const RenderRequest& request, const QImage& image,
                                           qint64 elapsed_ms, const QString& error_message) {
    if (request.generation != render_generation_ || request.document_hash != document_hash_) {
        qCInfo(log_render) << "Ignoring stale render result page" << request.page_index + 1;
        return;
    }

    if (!error_message.isEmpty() || image.isNull()) {
        qCWarning(log_render) << "Render failed page" << request.page_index + 1 << error_message;
        if (request.page_index == current_page_index_) {
            emit status_message_changed(
                QStringLiteral("Failed to render page %1").arg(request.page_index + 1));
        }
        return;
    }

    const SlideCacheKey key{request.document_hash, request.page_index, request.pixel_size,
                            request.rotation};
    const QImage display_image = image_with_media_frames(request.page_index, image);
    slide_cache_.put(key, display_image);
    const QString texture_key = texture_key_for_cache_key(key);
    const bool is_audience_render =
        key.pixel_size == cache_key_for_page(request.page_index).pixel_size;
    if (audience_window_ && is_audience_render) {
        audience_window_->cache_slide_image(texture_key, display_image);
    }

    if (request.page_index == current_page_index_ && is_audience_render) {
        if (awaiting_first_slide_image_ && document_open_timer_.isValid()) {
            performance_log::record_duration(
                QStringLiteral("document.first_slide_ready"), document_open_timer_.elapsed(),
                {{QStringLiteral("page"), request.page_index + 1},
                 {QStringLiteral("render_duration_ms"), elapsed_ms},
                 {QStringLiteral("render_height"), request.pixel_size.height()},
                 {QStringLiteral("render_width"), request.pixel_size.width()}});
            awaiting_first_slide_image_ = false;
            schedule_predictive_renders();
            refresh_deck_overviews();
        }
        emit current_slide_image_changed(display_image);
        if (audience_window_) {
            queue_audience_slide_commit(request.page_index, key, display_image);
        }
        emit status_message_changed(QStringLiteral("Rendered page %1 in %2 ms")
                                        .arg(request.page_index + 1)
                                        .arg(elapsed_ms));
    }

    if (request.page_index == current_page_index_ + 1 && is_audience_render) {
        emit next_slide_image_changed(display_image);
    }

    QSize delivered_overview_size;
    for (const DeckOverviewRequest& overview : deck_overviews_) {
        if (overview.size.isValid() && overview.size != delivered_overview_size &&
            key.pixel_size ==
                cache_key_for_page_at_size(request.page_index, overview.size).pixel_size) {
            emit deck_slide_image_changed(request.page_index, overview.size, display_image);
            delivered_overview_size = overview.size;
        }
    }
}
