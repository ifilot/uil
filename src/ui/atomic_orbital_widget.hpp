#pragma once

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QQuaternion>
#include <QVector3D>
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

/** @brief Renders an atomic orbital, coordinate frame, and movable contour plane. */
class AtomicOrbitalWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT

 public:
  explicit AtomicOrbitalWidget(QWidget* parent = nullptr);
  ~AtomicOrbitalWidget() override;

  /** @brief Rebuilds the sampled volume and phase isosurfaces. */
  void set_definition(const AtomicOrbitalDefinition& definition);
  /** @brief Installs precomputed geometry without sampling or meshing on the UI thread. */
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
  /** @brief Captures the OpenGL frame together with the child controls. */
  QImage capture_frame();
  /** @brief Captures a clean, fitted atlas poster, restoring the interactive view afterward. */
  QImage capture_poster(bool surface_only = false);

 protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int width, int height) override;
  void resizeEvent(QResizeEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  struct Mesh;
  struct CachedGpuVolume;
  /** @brief Deletes retained GL resources while this widget's context is current. */
  void clear_gpu_cache();

  bool create_renderer();
  void destroy_renderer();
  bool upload_volume();
  /** @brief Updates only the small palette texture when geometry can be reused. */
  void upload_colormap();
  void update_plane_buffer();
  void set_plane(AtomicOrbitalDefinition::Plane plane);
  void update_plane_buttons();
  void draw_surface(const Mesh& mesh, const QColor& color, const QMatrix4x4& model,
                    const QMatrix4x4& view, const QMatrix4x4& projection);
  void draw_sampling_plane(const QMatrix4x4& model, const QMatrix4x4& view,
                           const QMatrix4x4& projection);
  void draw_contour(const QRect& pixel_viewport);
  void draw_world_axes(const QMatrix4x4& model, const QMatrix4x4& view,
                       const QMatrix4x4& projection);
  void draw_labels_and_colorbar();
  void create_controls();
  void position_controls();
  void update_offset_label();
  QVector3D trackball_point(const QPointF& position) const;
  QRect left_logical_viewport() const;
  QRect right_logical_viewport() const;
  QRect to_gl_viewport(const QRect& logical) const;

  AtomicOrbitalDefinition definition_;
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
  QOpenGLVertexArrayObject plane_vao_;
  QOpenGLVertexArrayObject quad_vao_;
  QOpenGLBuffer axis_buffer_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject axis_vao_;
  QFrame* controls_panel_ = nullptr;
  QLabel* offset_label_ = nullptr;
  QSlider* offset_slider_ = nullptr;
  QButtonGroup* plane_button_group_ = nullptr;
  QPushButton* reset_button_ = nullptr;
  std::function<void(const QPoint&)> context_menu_handler_;
};
