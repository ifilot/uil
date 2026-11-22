#pragma once

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QQuaternion>
#include <QVector3D>
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "orbital/atomic_orbital.hpp"

class QContextMenuEvent;
class QButtonGroup;
class QFrame;
class QLabel;
class QMouseEvent;
class QOpenGLShaderProgram;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QSlider;
class QWheelEvent;

/**
 * @brief Renders an atomic orbital, coordinate frame, and movable contour plane.
 *
 * The left panel shows both phase isosurfaces with the sampling plane and axes; the right panel
 * shows a signed log-scale contour map of the plane. Rendering uses an OpenGL 3.3 core context
 * owned by QOpenGLWidget. All methods must be called on the GUI thread. GL resources, including
 * a small LRU of recently shown volumes, are released when the context is about to be destroyed
 * (for example when the widget moves to another top-level window) and recreated on demand.
 */
class AtomicOrbitalWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT

 public:
  /** @brief Creates the widget and its overlay controls; GL work is deferred to initializeGL(). */
  explicit AtomicOrbitalWidget(QWidget* parent = nullptr);
  /** @brief Releases all GL resources under this widget's own context. */
  ~AtomicOrbitalWidget() override;

  /**
   * @brief Samples the wavefunction, meshes both isosurfaces, and displays the result.
   *
   * Sampling and meshing run synchronously on the calling (GUI) thread and can take tens to
   * hundreds of milliseconds for large grids. Presentation code should build volumes in the
   * background with AtomicOrbitalCache and call set_prepared_definition() instead. Otherwise
   * behaves like set_prepared_definition(): invalid definitions are ignored, an unchanged
   * definition is a no-op, and a new one resets rotation, zoom, and plane offset.
   */
  void set_definition(const AtomicOrbitalDefinition& definition);
  /**
   * @brief Installs precomputed geometry without sampling or meshing on the UI thread.
   *
   * Invalid definitions are ignored, as is a definition equal to the one already shown with a
   * valid volume. Otherwise rotation, zoom, and plane offset are reset, and the plane
   * orientation follows @p definition. When only presentation settings changed, the uploaded
   * GPU geometry is reused. If the context exists it is made current briefly for the upload.
   * @param volume Geometry built for @p definition; an invalid volume shows @p error instead.
   * @param error Construction error to display, or empty.
   */
  void set_prepared_definition(const AtomicOrbitalDefinition& definition,
                               const AtomicOrbitalVolume& volume, const QString& error = {});
  /** @brief Returns the number of actual 3D texture uploads for performance diagnostics. */
  quint64 geometry_upload_count() const;
  /** @brief Returns retained GPU geometry bytes, excluding the currently displayed orbital. */
  qint64 gpu_cache_bytes() const;
  /** @brief Returns the currently displayed definition. */
  const AtomicOrbitalDefinition& definition() const;
  /** @brief Receives right-clicks that should open the audience feature menu. */
  void set_context_menu_handler(std::function<void(const QPoint&)> handler);
  /** @brief Returns the normalized world-space sampling-plane offset. */
  double plane_offset() const;
  /** @brief Restores the initial rotation, zoom, and plane offset. */
  void reset_view();
  /** @brief Reports whether all OpenGL 3.3 renderer resources are ready. */
  bool renderer_available() const;
  /** @brief Returns the renderer or volume-construction error, if any. */
  QString renderer_error() const;
  /** @brief Captures the OpenGL frame together with the child controls (renders synchronously). */
  QImage capture_frame();
  /**
   * @brief Captures a clean, fitted atlas poster, restoring the interactive view afterward.
   * @param surface_only Omits the contour panel and gives the isosurface the full width.
   */
  QImage capture_poster(bool surface_only = false);

 protected:
  /**
   * @brief Compiles shaders, creates vertex arrays, and uploads any pending volume.
   *
   * Also (re)connects QOpenGLContext::aboutToBeDestroyed so resources are released under the
   * context that owns them. Called by Qt with the context current.
   */
  void initializeGL() override;
  /**
   * @brief Draws the surface panel, the contour panel, and the painter overlay.
   *
   * Uploads a pending volume first. Called by Qt with the context current.
   */
  void paintGL() override;
  /** @brief No-op: viewports are derived from the widget size on every paint. */
  void resizeGL(int width, int height) override;
  /** @brief Keeps the control strip docked to the bottom edge. */
  void resizeEvent(QResizeEvent* event) override;
  /** @brief Swallows the native context menu; right-clicks are delegated on release. */
  void contextMenuEvent(QContextMenuEvent* event) override;
  /** @brief Starts a trackball rotation inside the surface panel. */
  void mousePressEvent(QMouseEvent* event) override;
  /** @brief Rotates the orbital while the left button is held. */
  void mouseMoveEvent(QMouseEvent* event) override;
  /** @brief Ends a rotation, or forwards a right-click to the context-menu handler. */
  void mouseReleaseEvent(QMouseEvent* event) override;
  /** @brief Restores the default view on a left double-click. */
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  /** @brief Zooms the surface panel. */
  void wheelEvent(QWheelEvent* event) override;

 private:
  struct Mesh;
  struct CachedGpuVolume;
  /** @brief Deletes retained GL resources while this widget's context is current. */
  void clear_gpu_cache();

  /**
   * @brief Creates shader programs and vertex arrays for the current context.
   * @return False, with renderer_error_ set, if any GL object cannot be created.
   * Requires the widget's context to be current.
   */
  bool create_renderer();
  /**
   * @brief Releases every GL object, including cached volumes, and marks a valid volume for
   *        re-upload. The caller must make the owning context current first.
   */
  void destroy_renderer();
  /**
   * @brief Makes volume_ the displayed GPU geometry, reusing a cached upload when possible.
   *
   * The previously displayed geometry moves into the bounded GPU cache. Requires a current
   * context.
   * @return False, with renderer_error_ set, if the surface buffers cannot be created.
   */
  bool upload_volume();
  /** @brief Updates only the small palette texture when geometry can be reused. */
  void upload_colormap();
  /**
   * @brief Writes the sampling-plane quad for the current plane and offset.
   *
   * Skips the GL upload when the quad is unchanged. Requires a current context.
   */
  void update_plane_buffer();
  /** @brief Switches the sampling plane and recentres its offset at the origin. */
  void set_plane(AtomicOrbitalDefinition::Plane plane);
  /** @brief Checks the plane button that matches definition_.plane. */
  void update_plane_buttons();
  /** @brief Draws one lit isosurface mesh; requires a current context. */
  void draw_surface(const Mesh& mesh, const QColor& color, const QMatrix4x4& model,
                    const QMatrix4x4& view, const QMatrix4x4& projection);
  /** @brief Draws the translucent, contour-coloured sampling plane in the surface panel. */
  void draw_sampling_plane(const QMatrix4x4& model, const QMatrix4x4& view,
                           const QMatrix4x4& projection);
  /** @brief Fills @p pixel_viewport with the contour map of the sampling plane. */
  void draw_contour(const QRect& pixel_viewport);
  /**
   * @brief Draws the x/y/z axes and their line-drawn letters in the surface panel.
   *
   * The line geometry depends only on the volume extent and is uploaded only when it changes.
   */
  void draw_world_axes(const QMatrix4x4& model, const QMatrix4x4& view,
                       const QMatrix4x4& projection);
  /** @brief Paints titles, panel frames, the colour bar, and any error with QPainter. */
  void draw_labels_and_colorbar();
  /**
   * @brief Returns the colour-bar gradient for definition_.colormap at @p logical_size.
   *
   * The image has a one-pixel transparent margin on every side for antialiased edges and is
   * rebuilt only when the colormap, size, or device-pixel ratio changes.
   */
  const QImage& colorbar_image(const QSize& logical_size);
  /** @brief Creates the offset slider, plane selector, and reset button strip. */
  void create_controls();
  /** @brief Places the control strip along the bottom edge. */
  void position_controls();
  /** @brief Shows the plane name and its offset in Bohr radii. */
  void update_offset_label();
  /** @brief Maps a widget position onto the virtual trackball over the surface panel. */
  QVector3D trackball_point(const QPointF& position) const;
  /** @brief Returns the surface panel in logical widget coordinates. */
  QRect left_logical_viewport() const;
  /** @brief Returns the contour panel in logical widget coordinates. */
  QRect right_logical_viewport() const;
  /** @brief Converts a logical, top-left-origin rectangle to a bottom-left GL pixel viewport. */
  QRect to_gl_viewport(const QRect& logical) const;

  AtomicOrbitalDefinition definition_;
  QString orbital_label_;
  AtomicOrbitalVolume volume_;
  QString renderer_error_;
  QQuaternion rotation_;
  QVector3D last_trackball_point_;
  float zoom_factor_ = 1.0f;
  bool rotating_ = false;
  bool poster_mode_ = false;
  bool poster_surface_only_ = false;
  bool renderer_ready_ = false;
  bool volume_dirty_ = false;
  GLuint volume_texture_ = 0;
  GLuint colormap_texture_ = 0;
  AtomicOrbitalDefinition uploaded_definition_;
  qint64 uploaded_bytes_ = 0;
  qint64 gpu_cache_bytes_ = 0;
  quint64 geometry_upload_count_ = 0;
  std::vector<std::unique_ptr<CachedGpuVolume>> gpu_cache_;
  QMetaObject::Connection context_cleanup_connection_;
  std::unique_ptr<QOpenGLShaderProgram> surface_program_;
  std::unique_ptr<QOpenGLShaderProgram> plane_program_;
  std::unique_ptr<QOpenGLShaderProgram> contour_program_;
  std::unique_ptr<QOpenGLShaderProgram> axis_program_;
  std::unique_ptr<Mesh> positive_mesh_;
  std::unique_ptr<Mesh> negative_mesh_;
  QOpenGLBuffer plane_buffer_{QOpenGLBuffer::VertexBuffer};
  std::array<QVector3D, 6> plane_buffer_vertices_{};
  bool plane_buffer_current_ = false;
  QOpenGLVertexArrayObject plane_vao_;
  QOpenGLVertexArrayObject quad_vao_;
  QOpenGLBuffer axis_buffer_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject axis_vao_;
  float axis_buffer_extent_ = 0.0f;
  int axis_vertex_count_ = 0;
  QImage colorbar_cache_;
  QString colorbar_cache_colormap_;
  QFrame* controls_panel_ = nullptr;
  QLabel* offset_label_ = nullptr;
  QSlider* offset_slider_ = nullptr;
  QButtonGroup* plane_button_group_ = nullptr;
  QPushButton* reset_button_ = nullptr;
  std::function<void(const QPoint&)> context_menu_handler_;
};
