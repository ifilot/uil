#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QQuaternion>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

#include "molecule/molecule_geometry.hpp"

class QMouseEvent;
class QContextMenuEvent;
class QFrame;
class QHideEvent;
class QOpenGLShaderProgram;
class QPainter;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QToolButton;
class QWheelEvent;

/** @brief Displays an interactive ball-and-stick molecular geometry. */
class MoleculeWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  /** @brief Stereo output modes supported by the embedded renderer. */
  enum class StereoMode { Mono, RedCyanAnaglyph };

  /** @brief Constructs a molecular rendering surface. */
  explicit MoleculeWidget(QWidget* parent = nullptr);
  ~MoleculeWidget() override;

  /** @brief Replaces the displayed molecular geometry and resets the view. */
  void set_geometry(const MoleculeGeometry& geometry);
  /** @brief Selects mono or red/cyan anaglyph output. */
  void set_stereo_mode(StereoMode mode);
  /** @brief Returns the active stereo output mode. */
  StereoMode stereo_mode() const;
  /** @brief Shows or hides the orientation-axis gizmo. */
  void set_axes_visible(bool visible);
  /** @brief Returns whether the orientation-axis gizmo is visible. */
  bool axes_visible() const;
  /** @brief Starts or pauses normal-mode animation when displacement data exists. */
  void set_vibration_playing(bool playing);
  /** @brief Returns whether normal-mode animation is running. */
  bool vibration_playing() const;
  /** @brief Enables or disables continuous rotation around the molecule's Z axis. */
  void set_auto_rotation_enabled(bool enabled);
  /** @brief Returns whether continuous Z-axis rotation is enabled. */
  bool auto_rotation_enabled() const;
  /** @brief Expands or collapses the floating molecule toolbar. */
  void set_toolbar_expanded(bool expanded);
  /** @brief Returns whether the floating molecule toolbar is expanded. */
  bool toolbar_expanded() const;
  /** @brief Delegates right-click menu requests to the presentation window. */
  void set_context_menu_handler(std::function<void(const QPoint&)> handler);

 protected:
  /** @brief Initializes the OpenGL-backed widget. */
  void initializeGL() override;
  /** @brief Paints the current molecular geometry. */
  void paintGL() override;
  /** @brief Keeps the floating toolbar anchored to the top-right corner. */
  void resizeEvent(QResizeEvent* event) override;
  /** @brief Stops animation while the molecule overlay is not visible. */
  void hideEvent(QHideEvent* event) override;
  /** @brief Resumes enabled automatic rotation when the molecule becomes visible. */
  void showEvent(QShowEvent* event) override;
  /** @brief Suppresses duplicate native context-menu handling after right-click delegation. */
  void contextMenuEvent(QContextMenuEvent* event) override;
  /** @brief Starts arcball-style rotation. */
  void mousePressEvent(QMouseEvent* event) override;
  /** @brief Rotates the molecule while dragging. */
  void mouseMoveEvent(QMouseEvent* event) override;
  /** @brief Completes a rotation gesture. */
  void mouseReleaseEvent(QMouseEvent* event) override;
  /** @brief Zooms the molecule around its center. */
  void wheelEvent(QWheelEvent* event) override;
  /** @brief Restores the initial camera orientation. */
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  struct Mesh;

  /** @brief Restores the default orientation and zoom. */
  void reset_view();
  /** @brief Creates shader and mesh resources for the current context. */
  bool create_renderer();
  /** @brief Releases resources while their OpenGL context is current. */
  void destroy_renderer();
  /** @brief Draws one transformed and colored mesh. */
  void draw_mesh(Mesh& mesh, const QMatrix4x4& model, const QVector3D& color,
                 const QMatrix4x4& view, const QMatrix4x4& projection);
  /** @brief Draws one eye into the supplied framebuffer viewport. */
  void draw_eye(const QRect& pixel_viewport, float eye_offset, const QVector<QVector3D>& positions);
  /** @brief Paints a Blender-style orientation gizmo over the OpenGL scene. */
  void draw_axis_gizmo(QPainter* painter);
  /** @brief Creates and connects the collapsible toolbar controls. */
  void create_toolbar();
  /** @brief Repositions the toolbar after a resize or expansion change. */
  void position_toolbar();
  /** @brief Refreshes toolbar icons, checks, labels, and enabled states. */
  void update_toolbar_state();

  MoleculeGeometry geometry_;
  QVector3D center_;
  QQuaternion rotation_;
  QPointF last_mouse_position_;
  float bounding_radius_ = 1.0f;
  float camera_distance_factor_ = 1.0f;
  float vibration_phase_ = 0.0f;
  bool rotating_ = false;
  bool renderer_ready_ = false;
  bool axes_visible_ = true;
  bool toolbar_expanded_ = true;
  bool auto_rotation_enabled_ = false;
  StereoMode stereo_mode_ = StereoMode::Mono;
  QString renderer_error_;
  std::unique_ptr<QOpenGLShaderProgram> shader_program_;
  std::unique_ptr<Mesh> sphere_mesh_;
  std::unique_ptr<Mesh> cylinder_mesh_;
  QFrame* toolbar_panel_ = nullptr;
  QToolButton* toolbar_toggle_button_ = nullptr;
  QToolButton* stereo_button_ = nullptr;
  QToolButton* vibration_button_ = nullptr;
  QToolButton* auto_rotation_button_ = nullptr;
  QToolButton* axes_button_ = nullptr;
  QToolButton* reset_button_ = nullptr;
  QTimer* vibration_timer_ = nullptr;
  QTimer* auto_rotation_timer_ = nullptr;
  std::function<void(const QPoint&)> context_menu_handler_;
};
