#include "app_controller.hpp"

#include "package/uil_package.hpp"
#include "pdf/qt_pdf_backend.hpp"
#include "ui/audience_window.hpp"
#include "util/document_hash.hpp"
#include "util/image_util.hpp"
#include "util/performance_log.hpp"

#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QScreen>
#include <QStringList>
#include <QTemporaryDir>
#include <QtMath>

#include <algorithm>
#include <optional>
#include <utility>

Q_LOGGING_CATEGORY(logRender, "render")
Q_LOGGING_CATEGORY(logScreens, "screens")

namespace {
constexpr int kMaximumOverlayDimension = 8192;
constexpr qint64 kMaximumOverlayPixels = 34LL * 1024LL * 1024LL;

/** @brief Decodes a bounded PNG overlay from an extracted package. */
QImage read_safe_package_overlay(const QString& path) {
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (reader.format().toLower() != QByteArrayLiteral("png")
        || !size.isValid()
        || size.width() > kMaximumOverlayDimension
        || size.height() > kMaximumOverlayDimension
        || qint64(size.width()) * qint64(size.height()) > kMaximumOverlayPixels) {
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
    connect(&render_scheduler_, &RenderScheduler::render_started, this, &AppController::handle_render_started);
    connect(&render_scheduler_, &RenderScheduler::render_finished, this, &AppController::handle_render_finished);
    connect(&video_timer_, &QTimer::timeout, this, &AppController::advance_video_frame);
    refresh_screens();
}

AppController::~AppController() {
    ++media_scan_generation_;
    media_scan_pool_.clear();
    media_scan_pool_.waitForDone();
}

void AppController::set_audience_window(AudienceWindow* audience_window) {
    if (audience_window_) {
        disconnect(audience_window_, nullptr, this, nullptr);
    }

    audience_window_ = audience_window;
    if (audience_window_) {
        connect(audience_window_, &AudienceWindow::render_target_changed, this, &AppController::handle_audience_render_target_changed);
        connect(audience_window_, &AudienceWindow::annotation_overlay_changed, this, &AppController::current_annotation_overlay_changed);
        connect(audience_window_, &AudienceWindow::presentation_closed, this, &AppController::stop_media_playback);
        if (audience_screen_) {
            audience_window_->set_audience_screen(audience_screen_);
        }
        update_active_molecule();
    }
}

bool AppController::open_pdf(const QString& path) {
    const QFileInfo input_info(path);
    const bool isUilPackage = input_info.suffix().compare(QStringLiteral("uil"), Qt::CaseInsensitive) == 0;
    performance_log::ScopedSpan open_span(QStringLiteral("document.open"), {
        {QStringLiteral("file_name"), input_info.fileName()},
        {QStringLiteral("file_size_bytes"), input_info.size()},
        {QStringLiteral("format"), isUilPackage ? QStringLiteral("uil") : QStringLiteral("pdf")}
    });
    document_open_timer_.start();
    awaiting_first_slide_image_ = false;

    stop_media_playback();
    open_span.checkpoint(QStringLiteral("stop_media_playback"));
    QString pdfPath = path;
    QString documentHashPath = path;
    QString package_root_path;
    QStringList package_movie_asset_paths;
    QStringList package_molecule_asset_paths;
    QString entry_pdf_relative_path = QStringLiteral("build/presentation.pdf");
    QHash<int, QImage> packageOverlayImages;
    QSet<int> packageHiddenOverlayPages;
    bool packageOverlaysGloballyVisible = true;
    std::shared_ptr<QTemporaryDir> packageTempDir;

    if (isUilPackage) {
        packageTempDir = std::make_shared<QTemporaryDir>();
        packageTempDir->setAutoRemove(true);
        if (!packageTempDir->isValid()) {
            open_span.add_field(QStringLiteral("error_stage"), QStringLiteral("create_package_temporary_directory"));
            open_span.set_outcome(QStringLiteral("failed"));
            emit status_message_changed(QStringLiteral("Could not create temporary directory for UIL package"));
            return false;
        }
        open_span.checkpoint(QStringLiteral("create_package_temporary_directory"));

        UilPackageOpenResult packageResult;
        QString packageError;
        if (!extract_uil_package(path, *packageTempDir, &packageResult, &packageError)) {
            open_span.add_field(QStringLiteral("error_stage"), QStringLiteral("extract_uil_package"));
            open_span.set_outcome(QStringLiteral("failed"));
            emit status_message_changed(QStringLiteral("Could not open UIL package: %1").arg(packageError));
            return false;
        }
        open_span.checkpoint(QStringLiteral("extract_uil_package"), {
            {QStringLiteral("movie_asset_count"), packageResult.movie_asset_paths.size()},
            {QStringLiteral("molecule_asset_count"), packageResult.molecule_asset_paths.size()},
            {QStringLiteral("overlay_count"), packageResult.overlay_image_paths.size()}
        });

        pdfPath = packageResult.entry_pdf_path;
        package_root_path = packageResult.package_root_path;
        package_movie_asset_paths = packageResult.movie_asset_paths;
        package_molecule_asset_paths = packageResult.molecule_asset_paths;
        entry_pdf_relative_path = packageResult.entry_pdf_relative_path;
        packageHiddenOverlayPages = packageResult.hidden_overlay_pages;
        packageOverlaysGloballyVisible = packageResult.overlays_globally_visible;
        for (auto it = packageResult.overlay_image_paths.constBegin(); it != packageResult.overlay_image_paths.constEnd(); ++it) {
            const QImage overlay = read_safe_package_overlay(packageTempDir->filePath(it.value()));
            if (!overlay.isNull()) {
                packageOverlayImages.insert(it.key(), overlay);
            } else {
                performance_log::record_event(QStringLiteral("package.overlay_rejected"), {
                    {QStringLiteral("page"), it.key() + 1},
                    {QStringLiteral("path"), it.value()}
                });
            }
        }
        open_span.checkpoint(
            QStringLiteral("decode_package_overlays"),
            {{QStringLiteral("decoded_overlay_count"), packageOverlayImages.size()}});
    }

    auto backend = std::make_unique<QtPdfBackend>();
    QString error_message;
    if (!backend->open(pdfPath, &error_message)) {
        open_span.add_field(QStringLiteral("error_stage"), QStringLiteral("open_pdf_backend"));
        open_span.set_outcome(QStringLiteral("failed"));
        emit status_message_changed(isUilPackage
            ? QStringLiteral("Could not open UIL package PDF: %1").arg(error_message)
            : QStringLiteral("Could not open PDF: %1").arg(error_message));
        return false;
    }
    open_span.checkpoint(
        QStringLiteral("open_pdf_backend"),
        {{QStringLiteral("page_count"), backend->page_count()}});

    QVector<QSizeF> page_sizes_points;
    page_sizes_points.reserve(backend->page_count());
    for (int page_index = 0; page_index < backend->page_count(); ++page_index) {
        page_sizes_points.push_back(backend->page_size_points(page_index));
    }
    open_span.checkpoint(
        QStringLiteral("cache_page_geometry"),
        {{QStringLiteral("page_count"), page_sizes_points.size()}});

    ++media_scan_generation_;
    media_scan_pool_.clear();
    backend_ = std::move(backend);
    page_sizes_points_ = std::move(page_sizes_points);
    package_temp_dir_ = packageTempDir;
    current_path_ = pdfPath;
    current_package_path_ = isUilPackage ? QFileInfo(path).absoluteFilePath() : QString();
    package_root_path_ = package_root_path;
    entry_pdf_relative_path_ = entry_pdf_relative_path;
    package_movie_asset_paths_ = package_movie_asset_paths;
    package_molecule_asset_paths_ = package_molecule_asset_paths;
    loaded_overlay_images_ = packageOverlayImages;
    loaded_hidden_overlay_pages_ = packageHiddenOverlayPages;
    loaded_overlays_globally_visible_ = packageOverlaysGloballyVisible;
    document_hash_ = document_hash_for_file(documentHashPath);
    open_span.checkpoint(QStringLiteral("commit_document_state"));
    media_scan_result_ = {};
    open_span.checkpoint(QStringLiteral("initialize_media_scan"));
    current_page_index_ = 0;
    slide_cache_.clear();
    render_scheduler_.clear();
    render_generation_ = render_scheduler_.generation();
    deck_overview_render_size_ = {};
    deck_overview_first_page_ = -1;
    deck_overview_last_page_ = -1;
    open_span.checkpoint(QStringLiteral("reset_render_state"));
    if (audience_window_) {
        audience_window_->clear_slide_image();
        audience_window_->clear_video_overlay();
        QHash<QString, QImage> overlaysByTextureKey;
        for (auto it = loaded_overlay_images_.constBegin(); it != loaded_overlay_images_.constEnd(); ++it) {
            if (it.key() >= 0 && it.key() < page_count() && !it.value().isNull()) {
                overlaysByTextureKey.insert(texture_key_for_cache_key(cache_key_for_page(it.key())), it.value());
            }
        }
        audience_window_->set_annotation_overlays_by_texture_key(overlaysByTextureKey);
    }
    open_span.checkpoint(QStringLiteral("prepare_audience_window"));

    awaiting_first_slide_image_ = true;
    request_page_render(current_page_index_, 1000);
    open_span.checkpoint(QStringLiteral("queue_first_slide"));
    emit document_changed(page_count());
    open_span.checkpoint(QStringLiteral("notify_document_changed"));
    emit media_scan_changed(media_scan_result_);
    open_span.checkpoint(QStringLiteral("notify_media_scan_changed"));
    update_visible_slides();
    open_span.checkpoint(QStringLiteral("update_visible_slides"));
    open_span.checkpoint(QStringLiteral("defer_predictive_renders"));
    schedule_media_scan(
        pdfPath,
        package_root_path,
        package_movie_asset_paths,
        package_molecule_asset_paths,
        document_hash_,
        media_scan_generation_,
        package_temp_dir_);
    open_span.checkpoint(QStringLiteral("schedule_media_scan"));
    open_span.add_field(QStringLiteral("page_count"), page_count());
    open_span.set_outcome(QStringLiteral("opened"));
    return true;
}

void AppController::schedule_media_scan(
    const QString& pdf_path,
    const QString& package_root_path,
    const QStringList& package_movie_asset_paths,
    const QStringList& package_molecule_asset_paths,
    const QString& document_hash,
    int generation,
    std::shared_ptr<QTemporaryDir> package_lifetime) {
    QPointer<AppController> self(this);
    auto* runnable = QRunnable::create([
        self,
        pdf_path,
        package_root_path,
        package_movie_asset_paths,
        package_molecule_asset_paths,
        document_hash,
        generation,
        package_lifetime = std::move(package_lifetime)] {
        Q_UNUSED(package_lifetime);
        PdfMediaScanResult result = scan_pdf_media_annotations(
            pdf_path,
            package_root_path,
            package_movie_asset_paths,
            package_molecule_asset_paths);
        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, result = std::move(result), document_hash, generation]() mutable {
            if (!self) {
                return;
            }
            if (generation != self->media_scan_generation_
                || document_hash != self->document_hash_) {
                performance_log::record_event(QStringLiteral("pdf.media_scan_stale"));
                return;
            }

            self->media_scan_result_ = std::move(result);
            if (self->current_package_path_.isEmpty()) {
                self->package_movie_asset_paths_.clear();
                self->package_molecule_asset_paths_.clear();
                const QDir pdf_directory(QFileInfo(self->current_path_).absolutePath());
                for (const PdfMediaAnnotation& annotation : self->media_scan_result_.annotations) {
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
            self->update_active_molecule();
            emit self->media_scan_changed(self->media_scan_result_);
            if (self->media_scan_result_.has_media()) {
                emit self->status_message_changed(self->media_scan_result_.summary());
            }
            performance_log::record_event(QStringLiteral("pdf.media_scan_applied"), {
                {QStringLiteral("annotation_count"),
                 self->media_scan_result_.annotations.size()
                     + self->media_scan_result_.molecule_annotations.size()},
                {QStringLiteral("generation"), generation}
            });
        }, Qt::QueuedConnection);
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

    const int clampedPage = std::clamp(page_index, 0, page_count() - 1);
    if (clampedPage == current_page_index_) {
        return;
    }

    stop_media_playback();
    current_page_index_ = clampedPage;
    update_active_molecule();
    request_page_render(current_page_index_, 1000);
    update_visible_slides();
    schedule_predictive_renders();
}

int AppController::current_page() const {
    return current_page_index_;
}

int AppController::page_count() const {
    return backend_ ? backend_->page_count() : 0;
}

void AppController::request_deck_overview_renders(
    const QSize& bounding_pixel_size,
    int focused_page_index,
    int first_visible_page,
    int last_visible_page) {
    if (!has_document() || !bounding_pixel_size.isValid()) {
        return;
    }

    performance_log::ScopedSpan span(QStringLiteral("render.deck_overview_request"), {
        {QStringLiteral("height"), bounding_pixel_size.height()},
        {QStringLiteral("page_count"), page_count()},
        {QStringLiteral("width"), bounding_pixel_size.width()}
    });

    const bool renderSizeChanged = deck_overview_render_size_ != bounding_pixel_size;
    deck_overview_render_size_ = bounding_pixel_size;
    const int focusedPage = std::clamp(focused_page_index >= 0 ? focused_page_index : current_page_index_, 0, page_count() - 1);
    const int visible_first = std::clamp(
        first_visible_page >= 0 ? first_visible_page : focusedPage,
        0,
        page_count() - 1);
    const int visible_last = std::clamp(
        last_visible_page >= visible_first ? last_visible_page : visible_first,
        visible_first,
        page_count() - 1);
    const int prefetch_count = std::clamp(visible_last - visible_first + 1, 4, 8);
    const int first_page = std::max(0, visible_first - prefetch_count);
    const int last_page = std::min(page_count() - 1, visible_last + prefetch_count);
    const int visible_center = std::clamp(focusedPage, visible_first, visible_last);
    deck_overview_first_page_ = visible_first;
    deck_overview_last_page_ = visible_last;

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

    request_page_render_at_size(visible_center, bounding_pixel_size, 95);
    const int maximum_visible_distance = std::max(
        visible_center - visible_first,
        visible_last - visible_center);
    for (int distance = 1; distance <= maximum_visible_distance; ++distance) {
        const int after_page = visible_center + distance;
        const int before_page = visible_center - distance;
        const int priority = std::max(60, 90 - distance);
        if (after_page <= visible_last) {
            request_page_render_at_size(after_page, bounding_pixel_size, priority);
        }
        if (before_page >= visible_first) {
            request_page_render_at_size(before_page, bounding_pixel_size, priority);
        }
    }

    const int maximum_prefetch_distance = std::max(
        visible_first - first_page,
        last_page - visible_last);
    for (int distance = 1; distance <= maximum_prefetch_distance; ++distance) {
        const int after_page = visible_last + distance;
        const int before_page = visible_first - distance;
        const int priority = std::max(1, 40 - distance);
        if (after_page <= last_page) {
            request_page_render_at_size(after_page, bounding_pixel_size, priority);
        }
        if (before_page >= first_page) {
            request_page_render_at_size(before_page, bounding_pixel_size, priority);
        }
    }
    span.add_field(QStringLiteral("cache_hits"), cache_hits);
    span.add_field(QStringLiteral("focused_page"), focusedPage + 1);
    span.add_field(QStringLiteral("first_visible_page"), visible_first + 1);
    span.add_field(QStringLiteral("last_visible_page"), visible_last + 1);
    span.add_field(QStringLiteral("first_page"), first_page + 1);
    span.add_field(QStringLiteral("last_page"), last_page + 1);
    span.add_field(QStringLiteral("render_attempt_count"), last_page - first_page + 1);
    span.add_field(QStringLiteral("render_size_changed"), renderSizeChanged);
    span.set_outcome(QStringLiteral("queued"));
}

QString AppController::current_path() const {
    return current_path_;
}

bool AppController::export_annotated_pdf(const QString& path, const QHash<int, QImage>& overlay_images, QString* error_message) {
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

    QPdfWriter writer(path);
    writer.setResolution(144);
    writer.setCreator(QStringLiteral("uil"));

    QPainter painter;
    for (int page_index = 0; page_index < page_count(); ++page_index) {
        const QSizeF page_size = page_size_points(page_index);
        if (!page_size.isValid()) {
            if (error_message) {
                *error_message = QStringLiteral("Could not determine page size for slide %1").arg(page_index + 1);
            }
            return false;
        }

        const QPageSize pageSize(page_size, QPageSize::Point);
        writer.setPageSize(pageSize);
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

        const QSize target_pixel_size(
            qMax(1, int(qRound(page_size.width() * 2.0))),
            qMax(1, int(qRound(page_size.height() * 2.0))));
        QImage pageImage = image_with_media_frames(page_index, backend_->render_page(page_index, target_pixel_size));
        if (pageImage.isNull()) {
            if (error_message) {
                *error_message = QStringLiteral("Could not render slide %1").arg(page_index + 1);
            }
            return false;
        }

        const QImage overlay = overlay_images.value(page_index);
        if (!overlay.isNull()) {
            QPainter imagePainter(&pageImage);
            imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            imagePainter.drawImage(pageImage.rect(), overlay);
        }

        painter.drawImage(QRectF(QPointF(0, 0), page_size), pageImage);
    }

    painter.end();
    return true;
}

bool AppController::save_uil_package(
    const QString& path,
    const QHash<int, QImage>& overlay_images,
    const QSet<int>& hidden_overlay_pages,
    bool overlays_globally_visible,
    QString* error_message) {
    if (!has_document()) {
        if (error_message) {
            *error_message = QStringLiteral("No presentation is open");
        }
        return false;
    }

    const QString entryPdf = entry_pdf_relative_path_.isEmpty()
        ? QStringLiteral("build/presentation.pdf")
        : entry_pdf_relative_path_;
    QHash<int, QImage> packageOverlayImages = overlay_images;
    if (audience_window_) {
        const QHash<int, QImage> audienceOverlays = audience_window_->annotation_overlays_by_page();
        for (auto it = audienceOverlays.constBegin(); it != audienceOverlays.constEnd(); ++it) {
            if (!it.value().isNull()) {
                packageOverlayImages.insert(it.key(), it.value());
            }
        }
    }

    if (!write_uil_package(
            path,
            current_path_,
            entryPdf,
            package_root_path_.isEmpty() ? QFileInfo(current_path_).absolutePath()
                                         : package_root_path_,
            package_movie_asset_paths_,
            package_molecule_asset_paths_,
            packageOverlayImages,
            hidden_overlay_pages,
            overlays_globally_visible,
            error_message)) {
        return false;
    }

    current_package_path_ = QFileInfo(path).absoluteFilePath();
    loaded_overlay_images_ = packageOverlayImages;
    loaded_hidden_overlay_pages_ = hidden_overlay_pages;
    loaded_overlays_globally_visible_ = overlays_globally_visible;
    return true;
}

void AppController::clear_annotation_overlay_for_page(int page_index) {
    if (audience_window_) {
        audience_window_->clear_annotation_overlay_for_page(page_index);
    }
    loaded_overlay_images_.remove(page_index);
}

void AppController::clear_all_annotation_overlays() {
    if (audience_window_) {
        audience_window_->clear_all_annotation_overlays();
    }
    loaded_overlay_images_.clear();
}

QString AppController::current_package_path() const {
    return current_package_path_;
}

QHash<int, QImage> AppController::loaded_overlay_images() const {
    return loaded_overlay_images_;
}

QSet<int> AppController::loaded_hidden_overlay_pages() const {
    return loaded_hidden_overlay_pages_;
}

bool AppController::loaded_overlays_globally_visible() const {
    return loaded_overlays_globally_visible_;
}

QScreen* AppController::selected_audience_screen() const {
    return audience_screen_;
}

void AppController::set_audience_screen(QScreen* screen) {
    if (!screen) {
        return;
    }

    const bool screenChanged = screen != audience_screen_;
    audience_screen_ = screen;
    qCInfo(logScreens) << "Audience screen selected:" << screen->name() << screen->geometry();
    emit audience_screen_changed(screen);

    if (audience_window_) {
        audience_window_->set_audience_screen(screen);
    }

    if (has_document()) {
        if (screenChanged) {
            slide_cache_.clear();
            render_scheduler_.clear();
            render_generation_ = render_scheduler_.generation();
        }
        update_visible_slides();
        schedule_predictive_renders();
        if (deck_overview_render_size_.isValid()) {
            request_deck_overview_renders(
                deck_overview_render_size_,
                current_page_index_,
                deck_overview_first_page_,
                deck_overview_last_page_);
        }
    }
}

void AppController::refresh_screens() {
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    QScreen* preferredAudience = primary;

    for (QScreen* screen : screens) {
        qCInfo(logScreens) << "Screen:" << screen->name()
                           << "primary:" << (screen == primary)
                           << "geometry:" << screen->geometry()
                           << "dpr:" << screen->devicePixelRatio();
        if (screen != primary && preferredAudience == primary) {
            preferredAudience = screen;
        }
    }

    const bool selectedScreenGone = !screens.contains(audience_screen_);
    if (selectedScreenGone) {
        audience_screen_ = preferredAudience;
        slide_cache_.clear();
        render_scheduler_.clear();
        render_generation_ = render_scheduler_.generation();
        emit audience_screen_changed(audience_screen_);
        if (audience_window_ && audience_screen_) {
            audience_window_->set_audience_screen(audience_screen_);
        }
    }

    emit screen_list_changed();

    if (selectedScreenGone && has_document()) {
        update_visible_slides();
        schedule_predictive_renders();
        if (deck_overview_render_size_.isValid()) {
            request_deck_overview_renders(
                deck_overview_render_size_,
                current_page_index_,
                deck_overview_first_page_,
                deck_overview_last_page_);
        }
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

    QSize logicalSize;
    qreal devicePixelRatio = 1.0;
    if (audience_screen_) {
        logicalSize = audience_screen_->geometry().size();
        devicePixelRatio = audience_screen_->devicePixelRatio();
    } else if (audience_window_) {
        logicalSize = audience_window_->render_logical_size();
        devicePixelRatio = audience_window_->render_device_pixel_ratio();
    }

    if (!logicalSize.isValid()) {
        logicalSize = QSize(1280, 720);
    }

    const QSize boundingPixels(qMax(1, int(qRound(logicalSize.width() * devicePixelRatio))),
                               qMax(1, int(qRound(logicalSize.height() * devicePixelRatio))));

    return contained_size_for_aspect(page_size_points(page_index), boundingPixels);
}

SlideCacheKey AppController::cache_key_for_page(int page_index) const {
    return cache_key_for_page_at_size(page_index, audience_render_pixel_size(page_index));
}

SlideCacheKey AppController::cache_key_for_page_at_size(int page_index, const QSize& bounding_pixel_size) const {
    if (!has_document() || !bounding_pixel_size.isValid()) {
        return {};
    }

    return SlideCacheKey{
        document_hash_,
        page_index,
        contained_size_for_aspect(page_size_points(page_index), bounding_pixel_size),
        0
    };
}

QString AppController::texture_key_for_cache_key(const SlideCacheKey& key) const {
    return QStringLiteral("%1:%2:%3x%4:%5")
        .arg(key.document_hash)
        .arg(key.page_index)
        .arg(key.pixel_size.width())
        .arg(key.pixel_size.height())
        .arg(key.rotation);
}

bool AppController::has_document() const {
    return backend_ && backend_->page_count() > 0;
}

QSizeF AppController::page_size_points(int page_index) const {
    if (page_index < 0 || page_index >= page_sizes_points_.size()) {
        return {};
    }
    return page_sizes_points_.at(page_index);
}

RenderRequest AppController::render_request_for_page(int page_index) const {
    return render_request_for_page_at_size(page_index, audience_render_pixel_size(page_index));
}

RenderRequest AppController::render_request_for_page_at_size(int page_index, const QSize& bounding_pixel_size) const {
    const SlideCacheKey sizedKey = cache_key_for_page_at_size(page_index, bounding_pixel_size);
    return RenderRequest{
        current_path_,
        sizedKey.document_hash,
        sizedKey.page_index,
        sizedKey.pixel_size,
        sizedKey.rotation,
        render_generation_
    };
}

void AppController::update_visible_slides() {
    if (!has_document()) {
        return;
    }

    const SlideCacheKey currentKey = cache_key_for_page(current_page_index_);
    if (auto currentImage = slide_cache_.get(currentKey)) {
        emit current_slide_image_changed(*currentImage);
        if (audience_window_) {
            audience_window_->set_slide_image(texture_key_for_cache_key(currentKey), *currentImage);
            if (!video_playing_) {
                audience_window_->clear_video_overlay();
            }
        }
        emit status_message_changed(QStringLiteral("Cache hit: page %1").arg(current_page_index_ + 1));
    } else {
        emit current_slide_image_changed(QImage());
        emit current_annotation_overlay_changed({});
        if (audience_window_) {
            audience_window_->clear_video_overlay();
        }
        emit status_message_changed(QStringLiteral("Rendering page %1").arg(current_page_index_ + 1));
    }

    const int next_page = current_page_index_ + 1;
    if (next_page < page_count()) {
        const SlideCacheKey nextKey = cache_key_for_page(next_page);
        if (auto nextImage = slide_cache_.get(nextKey)) {
            emit next_slide_image_changed(*nextImage);
        } else {
            emit next_slide_image_changed(QImage());
        }
    } else {
        emit next_slide_image_changed(QImage());
    }

    emit page_changed(current_page_index_, page_count());
}

void AppController::schedule_predictive_renders() {
    if (!has_document()) {
        return;
    }

    request_page_render(current_page_index_, 100);
    request_page_render(current_page_index_ + 1, 75);
    request_page_render(current_page_index_ - 1, 50);
    request_page_render(current_page_index_ + 2, 25);
}

void AppController::request_page_render(int page_index, int priority) {
    request_page_render_at_size(page_index, audience_render_pixel_size(page_index), priority);
}

void AppController::request_page_render_at_size(int page_index, const QSize& bounding_pixel_size, int priority) {
    if (!has_document() || page_index < 0 || page_index >= page_count()) {
        return;
    }

    const SlideCacheKey key = cache_key_for_page_at_size(page_index, bounding_pixel_size);
    if (slide_cache_.contains(key)) {
        return;
    }

    render_scheduler_.request_render(render_request_for_page_at_size(page_index, bounding_pixel_size), priority);
}

QImage AppController::image_with_media_frames(int page_index, const QImage& image) const {
    if (image.isNull() || !backend_) {
        return image;
    }

    QImage result = image;
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QSizeF pageSize = page_size_points(page_index);
    if (!pageSize.isValid()) {
        return result;
    }

    const double scaleX = double(result.width()) / pageSize.width();
    const double scaleY = double(result.height()) / pageSize.height();

    for (const PdfMediaAnnotation& annotation : media_scan_result_.annotations) {
        if (annotation.page_index != page_index || !annotation.has_first_frame() || !annotation.rect.isValid()) {
            continue;
        }

        const QRectF target(
            annotation.rect.left() * scaleX,
            (pageSize.height() - annotation.rect.bottom()) * scaleY,
            annotation.rect.width() * scaleX,
            annotation.rect.height() * scaleY);
        painter.drawImage(target.toRect(), annotation.first_frame);
    }

    return result;
}

const PdfMediaAnnotation* AppController::current_playable_media_annotation() const {
    for (const PdfMediaAnnotation& annotation : media_scan_result_.annotations) {
        if (annotation.page_index == current_page_index_
            && annotation.is_mp4()
            && !annotation.resolved_file_path.isEmpty()
            && annotation.rect.isValid()) {
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

    const QSizeF pageSize = page_size_points(page_index);
    if (!pageSize.isValid()) {
        return {};
    }

    return QRectF(
        rect.left() / pageSize.width(),
        (pageSize.height() - rect.bottom()) / pageSize.height(),
        rect.width() / pageSize.width(),
        rect.height() / pageSize.height());
}

void AppController::update_active_molecule() {
    if (!audience_window_) {
        return;
    }

    for (const PdfMoleculeAnnotation& annotation : media_scan_result_.molecule_annotations) {
        if (annotation.page_index == current_page_index_ && annotation.is_ready()) {
            audience_window_->set_molecule_overlay(
                annotation.geometry,
                normalized_pdf_rect(annotation.page_index, annotation.rect));
            return;
        }
    }
    audience_window_->clear_molecule_overlay();
}

void AppController::start_media_playback() {
    const PdfMediaAnnotation* annotation = current_playable_media_annotation();
    if (!annotation) {
        emit status_message_changed(QStringLiteral("No playable MP4 media on this slide"));
        return;
    }

    auto buffer = std::make_unique<VideoFrameBuffer>();
    connect(buffer.get(), &VideoFrameBuffer::frame_available, this, &AppController::handle_buffered_video_frame_available);
    connect(buffer.get(), &VideoFrameBuffer::finished, this, &AppController::handle_video_decode_finished);
    connect(buffer.get(), &VideoFrameBuffer::failed, this, &AppController::handle_video_decode_failed);
    video_buffer_ = std::move(buffer);
    active_video_rect_ = normalized_media_rect(*annotation);
    last_video_pts_ms_ = -1;
    waiting_for_video_frame_ = true;
    video_playing_ = true;
    emit status_message_changed(QStringLiteral("Playing media: %1").arg(annotation->fileName));
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
    last_video_pts_ms_ = -1;
    if (audience_window_) {
        audience_window_->clear_video_overlay();
    }
}

void AppController::advance_video_frame() {
    if (!video_buffer_ || !video_playing_) {
        stop_media_playback();
        return;
    }

    std::optional<DecodedVideoFrame> frame = video_buffer_->take_frame();
    if (!frame) {
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
        video_timer_.start(30);
        return;
    }

    waiting_for_video_frame_ = false;
    if (audience_window_) {
        audience_window_->set_video_frame(frame->image, active_video_rect_);
    }

    int nextDelayMs = 33;
    if (last_video_pts_ms_ >= 0 && frame->pts_ms > last_video_pts_ms_) {
        nextDelayMs = int(std::clamp<qint64>(frame->pts_ms - last_video_pts_ms_, 1, 100));
    }
    last_video_pts_ms_ = frame->pts_ms;
    video_timer_.start(nextDelayMs);
}

void AppController::handle_buffered_video_frame_available() {
    if (video_playing_ && waiting_for_video_frame_ && !video_timer_.isActive()) {
        advance_video_frame();
    }
}

void AppController::handle_video_decode_finished() {
    if (video_playing_ && waiting_for_video_frame_ && (!video_buffer_ || !video_buffer_->has_frames())) {
        stop_media_playback();
        emit status_message_changed(QStringLiteral("Media finished"));
    }
}

void AppController::handle_video_decode_failed(const QString& error_message) {
    if (!video_playing_) {
        return;
    }

    if (video_buffer_ && video_buffer_->has_frames()) {
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
        const QHash<int, QImage> overlaysByPage = audience_window_->annotation_overlays_by_page();
        QHash<QString, QImage> overlaysByTextureKey;
        for (auto it = overlaysByPage.constBegin(); it != overlaysByPage.constEnd(); ++it) {
            if (it.key() >= 0 && it.key() < page_count() && !it.value().isNull()) {
                overlaysByTextureKey.insert(texture_key_for_cache_key(cache_key_for_page(it.key())), it.value());
            }
        }
        audience_window_->set_annotation_overlays_by_texture_key(overlaysByTextureKey);
    }

    update_visible_slides();
    schedule_predictive_renders();
    if (deck_overview_render_size_.isValid()) {
        request_deck_overview_renders(
            deck_overview_render_size_,
            current_page_index_,
            deck_overview_first_page_,
            deck_overview_last_page_);
    }
}

void AppController::handle_render_started(const RenderRequest& request) {
    Q_UNUSED(request);
}

void AppController::handle_render_finished(const RenderRequest& request, const QImage& image, qint64 elapsed_ms, const QString& error_message) {
    if (request.generation != render_generation_ || request.document_hash != document_hash_) {
        qCInfo(logRender) << "Ignoring stale render result page" << request.page_index + 1;
        return;
    }

    if (!error_message.isEmpty() || image.isNull()) {
        qCWarning(logRender) << "Render failed page" << request.page_index + 1 << error_message;
        if (request.page_index == current_page_index_) {
            emit status_message_changed(QStringLiteral("Failed to render page %1").arg(request.page_index + 1));
        }
        return;
    }

    const SlideCacheKey key{
        request.document_hash,
        request.page_index,
        request.pixel_size,
        request.rotation
    };
    const QImage displayImage = image_with_media_frames(request.page_index, image);
    slide_cache_.put(key, displayImage);
    const QString texture_key = texture_key_for_cache_key(key);
    const bool isAudienceRender = key.pixel_size == cache_key_for_page(request.page_index).pixel_size;
    if (audience_window_ && isAudienceRender) {
        audience_window_->cache_slide_image(texture_key, displayImage);
    }

    if (request.page_index == current_page_index_ && isAudienceRender) {
        if (awaiting_first_slide_image_ && document_open_timer_.isValid()) {
            performance_log::record_duration(
                QStringLiteral("document.first_slide_ready"),
                document_open_timer_.elapsed(),
                {
                    {QStringLiteral("page"), request.page_index + 1},
                    {QStringLiteral("render_duration_ms"), elapsed_ms},
                    {QStringLiteral("render_height"), request.pixel_size.height()},
                    {QStringLiteral("render_width"), request.pixel_size.width()}
                });
            awaiting_first_slide_image_ = false;
            schedule_predictive_renders();
            if (deck_overview_render_size_.isValid()) {
                request_deck_overview_renders(
                    deck_overview_render_size_,
                    current_page_index_,
                    deck_overview_first_page_,
                    deck_overview_last_page_);
            }
        }
        emit current_slide_image_changed(displayImage);
        if (audience_window_) {
            audience_window_->set_slide_image(texture_key, displayImage);
        }
        emit status_message_changed(QStringLiteral("Rendered page %1 in %2 ms").arg(request.page_index + 1).arg(elapsed_ms));
    }

    if (request.page_index == current_page_index_ + 1 && isAudienceRender) {
        emit next_slide_image_changed(displayImage);
    }

    if (deck_overview_render_size_.isValid()
        && key.pixel_size == cache_key_for_page_at_size(request.page_index, deck_overview_render_size_).pixel_size) {
        emit deck_slide_image_changed(request.page_index, deck_overview_render_size_, displayImage);
    }
}
