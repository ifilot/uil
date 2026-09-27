#include "ui/molecule_widget.hpp"

#include <QColor>
#include <QContextMenuEvent>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSurfaceFormat>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "molecule/molecule_camera.hpp"
#include "ui/font_awesome.hpp"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kBondRadius = 0.065f;
constexpr int kSphereStacks = 24;
constexpr int kSphereSlices = 32;
constexpr int kCylinderSlices = 24;
constexpr int kDiscSlices = 64;
constexpr int kToolbarButtonSize = 34;
constexpr int kToolbarMargin = 8;
constexpr int kAnimationIntervalMs = 16;
constexpr float kVibrationCyclesPerSecond = 1.25f;
constexpr float kAutoRotationDegreesPerSecond = 24.0f;

struct MeshVertex {
  float position[3];
  float normal[3];
};

QColor element_color(const QString& element) {
  if (element == QStringLiteral("H")) {
    return QColor(245, 245, 245);
  }
  if (element == QStringLiteral("C")) {
    return QColor(55, 58, 64);
  }
  if (element == QStringLiteral("N")) {
    return QColor(48, 80, 220);
  }
  if (element == QStringLiteral("O")) {
    return QColor(225, 45, 45);
  }
  if (element == QStringLiteral("F") || element == QStringLiteral("Cl")) {
    return QColor(45, 190, 70);
  }
  if (element == QStringLiteral("P")) {
    return QColor(235, 130, 35);
  }
  if (element == QStringLiteral("S")) {
    return QColor(235, 205, 45);
  }
  if (element == QStringLiteral("Br")) {
    return QColor(145, 45, 35);
  }
  if (element == QStringLiteral("I")) {
    return QColor(115, 55, 160);
  }
  if (element == QStringLiteral("Fe")) {
    return QColor(205, 105, 45);
  }
  return QColor(125, 145, 165);
}

float element_display_radius(const QString& element) {
  if (element == QStringLiteral("H")) {
    return 0.28f;
  }
  if (element == QStringLiteral("C")) {
    return 0.43f;
  }
  if (element == QStringLiteral("N")) {
    return 0.40f;
  }
  if (element == QStringLiteral("O") || element == QStringLiteral("F")) {
    return 0.38f;
  }
  if (element == QStringLiteral("P") || element == QStringLiteral("S")) {
    return 0.50f;
  }
  return 0.46f;
}

QVector3D linear_color(const QColor& color) {
  auto linear_channel = [](float channel) {
    return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
  };
  return QVector3D(linear_channel(float(color.redF())), linear_channel(float(color.greenF())),
                   linear_channel(float(color.blueF())));
}

void append_vertex(QVector<MeshVertex>* vertices, QVector3D position, QVector3D normal) {
  vertices->push_back(MeshVertex{
      {position.x(), position.y(), position.z()},
      {normal.x(), normal.y(), normal.z()},
  });
}

void create_sphere_geometry(QVector<MeshVertex>* vertices, QVector<quint32>* indices) {
  vertices->reserve((kSphereStacks + 1) * (kSphereSlices + 1));
  indices->reserve(kSphereStacks * kSphereSlices * 6);
  for (int stack = 0; stack <= kSphereStacks; ++stack) {
    const float phi = kPi * float(stack) / float(kSphereStacks);
    const float ring_radius = std::sin(phi);
    const float y = std::cos(phi);
    for (int slice = 0; slice <= kSphereSlices; ++slice) {
      const float theta = 2.0f * kPi * float(slice) / float(kSphereSlices);
      const QVector3D normal(ring_radius * std::cos(theta), y, ring_radius * std::sin(theta));
      append_vertex(vertices, normal, normal);
    }
  }

  const int stride = kSphereSlices + 1;
  for (int stack = 0; stack < kSphereStacks; ++stack) {
    for (int slice = 0; slice < kSphereSlices; ++slice) {
      const quint32 first = quint32(stack * stride + slice);
      const quint32 second = first + quint32(stride);
      indices->append({first, second, first + 1, first + 1, second, second + 1});
    }
  }
}

void create_cylinder_geometry(QVector<MeshVertex>* vertices, QVector<quint32>* indices) {
  vertices->reserve((kCylinderSlices + 1) * 4 + 2);
  indices->reserve(kCylinderSlices * 12);

  for (int slice = 0; slice <= kCylinderSlices; ++slice) {
    const float theta = 2.0f * kPi * float(slice) / float(kCylinderSlices);
    const QVector3D normal(std::cos(theta), std::sin(theta), 0.0f);
    append_vertex(vertices, QVector3D(normal.x(), normal.y(), 0.0f), normal);
    append_vertex(vertices, QVector3D(normal.x(), normal.y(), 1.0f), normal);
  }
  for (int slice = 0; slice < kCylinderSlices; ++slice) {
    const quint32 first = quint32(slice * 2);
    indices->append({first, first + 1, first + 2, first + 2, first + 1, first + 3});
  }

  const quint32 bottom_center = quint32(vertices->size());
  append_vertex(vertices, QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, -1.0f));
  const quint32 bottom_ring = quint32(vertices->size());
  for (int slice = 0; slice <= kCylinderSlices; ++slice) {
    const float theta = 2.0f * kPi * float(slice) / float(kCylinderSlices);
    append_vertex(vertices, QVector3D(std::cos(theta), std::sin(theta), 0.0f),
                  QVector3D(0.0f, 0.0f, -1.0f));
  }

  const quint32 top_center = quint32(vertices->size());
  append_vertex(vertices, QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 0.0f, 1.0f));
  const quint32 top_ring = quint32(vertices->size());
  for (int slice = 0; slice <= kCylinderSlices; ++slice) {
    const float theta = 2.0f * kPi * float(slice) / float(kCylinderSlices);
    append_vertex(vertices, QVector3D(std::cos(theta), std::sin(theta), 1.0f),
                  QVector3D(0.0f, 0.0f, 1.0f));
  }

  for (int slice = 0; slice < kCylinderSlices; ++slice) {
    const quint32 offset = quint32(slice);
    indices->append({
        bottom_center,
        bottom_ring + offset + 1,
        bottom_ring + offset,
        top_center,
        top_ring + offset,
        top_ring + offset + 1,
    });
  }
}

void create_disc_geometry(QVector<MeshVertex>* vertices, QVector<quint32>* indices) {
  vertices->reserve(kDiscSlices + 2);
  indices->reserve(kDiscSlices * 3);
  append_vertex(vertices, QVector3D(), QVector3D(0.0f, 0.0f, 1.0f));
  for (int slice = 0; slice <= kDiscSlices; ++slice) {
    const float theta = 2.0f * kPi * float(slice) / float(kDiscSlices);
    append_vertex(vertices, QVector3D(std::cos(theta), std::sin(theta), 0.0f),
                  QVector3D(0.0f, 0.0f, 1.0f));
  }
  for (int slice = 0; slice < kDiscSlices; ++slice) {
    indices->append({0u, quint32(slice + 1), quint32(slice + 2)});
  }
}
}  // namespace

struct MoleculeWidget::Mesh {
  QOpenGLBuffer vertex_buffer{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer index_buffer{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject vao;
  int index_count = 0;
  QVector3D orbital_centroid;

  bool create(QOpenGLShaderProgram* program, const QVector<MeshVertex>& vertices,
              const QVector<quint32>& indices) {
    if (!program || !vao.create() || !vertex_buffer.create() || !index_buffer.create()) {
      return false;
    }
    {
      QOpenGLVertexArrayObject::Binder binder(&vao);
      if (!vertex_buffer.bind()) return false;
      vertex_buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
      vertex_buffer.allocate(vertices.constData(), int(vertices.size() * sizeof(MeshVertex)));
      if (!index_buffer.bind()) {
        return false;
      }
      index_buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
      index_buffer.allocate(indices.constData(), int(indices.size() * sizeof(quint32)));

      const int position_location = program->attributeLocation("vertex_position");
      const int normal_location = program->attributeLocation("vertex_normal");
      program->enableAttributeArray(position_location);
      program->setAttributeBuffer(position_location, GL_FLOAT, int(offsetof(MeshVertex, position)),
                                  3, int(sizeof(MeshVertex)));
      program->enableAttributeArray(normal_location);
      program->setAttributeBuffer(normal_location, GL_FLOAT, int(offsetof(MeshVertex, normal)), 3,
                                  int(sizeof(MeshVertex)));
      vertex_buffer.release();
    }
    index_buffer.release();
    index_count = indices.size();
    return index_count > 0;
  }

  void destroy() {
    vao.destroy();
    vertex_buffer.destroy();
    index_buffer.destroy();
    index_count = 0;
  }
};

MoleculeWidget::MoleculeWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setObjectName(QStringLiteral("moleculeWidget"));
  QSurfaceFormat surface_format = format();
  surface_format.setVersion(3, 3);
  surface_format.setProfile(QSurfaceFormat::CoreProfile);
  surface_format.setDepthBufferSize(24);
  surface_format.setSamples(4);
  setFormat(surface_format);
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  setMinimumSize(48, 48);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  reset_view();
  create_toolbar();
}

MoleculeWidget::~MoleculeWidget() {
  if (vibration_timer_) {
    vibration_timer_->stop();
  }
  if (auto_rotation_timer_) {
    auto_rotation_timer_->stop();
  }
  if (context() && context()->isValid()) {
    makeCurrent();
    destroy_renderer();
    doneCurrent();
  }
}

void MoleculeWidget::set_geometry(const MoleculeGeometry& geometry) {
  set_vibration_playing(false);
  geometry_ = geometry;
  orbitals_.clear();
  coordinate_transform_.setToIdentity();
  atom_shape_transform_.setToIdentity();
  symmetry_element_ = SymmetryElement::None;
  vibration_phase_ = 0.0f;
  center_ = {};
  if (!geometry_.atoms.isEmpty()) {
    for (const MoleculeAtom& atom : geometry_.atoms) {
      center_ += atom.position;
    }
    center_ /= float(geometry_.atoms.size());
  }
  coordinate_origin_ = center_;

  bounding_radius_ = 1.0f;
  for (const MoleculeAtom& atom : geometry_.atoms) {
    bounding_radius_ =
        std::max(bounding_radius_, (atom.position - center_).length() + atom.vibration.length() +
                                       element_display_radius(atom.element));
  }
  reset_view();
  update_toolbar_state();
  update();
}

void MoleculeWidget::set_orbitals(const QVector<SymmetryOrbital>& orbitals) {
  if (!valid_symmetry_orbitals(orbitals, geometry_.atoms.size())) return;
  orbitals_ = orbitals;
  bounding_radius_ = 1.0f;
  for (const auto& atom : geometry_.atoms) {
    bounding_radius_ =
        std::max(bounding_radius_, (atom.position - center_).length() + atom.vibration.length() +
                                       element_display_radius(atom.element));
  }
  for (const auto& orbital : orbitals_) {
    bounding_radius_ = std::max(
        bounding_radius_,
        (geometry_.atoms.at(orbital.atom - 1).position - center_).length() + orbital.scale);
  }
  update();
}

const QVector<SymmetryOrbital>& MoleculeWidget::orbitals() const { return orbitals_; }

void MoleculeWidget::set_stereo_mode(StereoMode mode) {
  if (stereo_mode_ == mode) {
    return;
  }
  stereo_mode_ = mode;
  update_toolbar_state();
  update();
}

MoleculeWidget::StereoMode MoleculeWidget::stereo_mode() const { return stereo_mode_; }

void MoleculeWidget::set_axes_visible(bool visible) {
  if (axes_visible_ == visible) {
    return;
  }
  axes_visible_ = visible;
  update_toolbar_state();
  update();
}

bool MoleculeWidget::axes_visible() const { return axes_visible_; }

void MoleculeWidget::set_world_axes_visible(bool visible) {
  if (world_axes_visible_ == visible) return;
  world_axes_visible_ = visible;
  update();
}

bool MoleculeWidget::world_axes_visible() const { return world_axes_visible_; }

void MoleculeWidget::set_vibration_playing(bool playing) {
  const bool should_play = playing && geometry_.has_vibration();
  if (vibration_timer_) {
    if (should_play && !vibration_timer_->isActive()) {
      vibration_timer_->start();
    } else if (!should_play && vibration_timer_->isActive()) {
      vibration_timer_->stop();
    }
  }
  update_toolbar_state();
  update();
}

bool MoleculeWidget::vibration_playing() const {
  return vibration_timer_ && vibration_timer_->isActive();
}

void MoleculeWidget::set_auto_rotation_enabled(bool enabled) {
  if (auto_rotation_enabled_ == enabled) {
    return;
  }

  auto_rotation_enabled_ = enabled;
  if (auto_rotation_timer_) {
    if (enabled && isVisible()) {
      auto_rotation_timer_->start();
    } else {
      auto_rotation_timer_->stop();
    }
  }
  update_toolbar_state();
  update();
}

bool MoleculeWidget::auto_rotation_enabled() const { return auto_rotation_enabled_; }

void MoleculeWidget::set_toolbar_expanded(bool expanded) {
  if (toolbar_expanded_ == expanded) {
    return;
  }
  toolbar_expanded_ = expanded;
  update_toolbar_state();
  position_toolbar();
}

bool MoleculeWidget::toolbar_expanded() const { return toolbar_expanded_; }

void MoleculeWidget::set_coordinate_origin(const QVector3D& origin) {
  coordinate_origin_ = origin;
  update();
}

QVector3D MoleculeWidget::coordinate_origin() const { return coordinate_origin_; }

void MoleculeWidget::set_coordinate_transform(const QMatrix4x4& transform) {
  coordinate_transform_ = transform;
  update();
}

void MoleculeWidget::clear_coordinate_transform() {
  coordinate_transform_.setToIdentity();
  atom_shape_transform_.setToIdentity();
  update();
}

void MoleculeWidget::set_atom_reflection_shape(const QVector3D& normal, double progress) {
  atom_shape_transform_.setToIdentity();
  const QVector3D axis = normal.normalized();
  // Keep the normal matrix invertible at the midpoint. This visual cue changes
  // atom surfaces only; the symmetry operation still determines all centers.
  const float scale = std::max(0.12f, float(std::abs(1.0 - 2.0 * std::clamp(progress, 0.0, 1.0))));
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      atom_shape_transform_(row, column) -= (1.0f - scale) * axis[row] * axis[column];
    }
  }
  update();
}

void MoleculeWidget::set_reference_geometry_visible(bool visible) {
  if (reference_geometry_visible_ == visible) return;
  reference_geometry_visible_ = visible;
  update();
}

bool MoleculeWidget::reference_geometry_visible() const { return reference_geometry_visible_; }

void MoleculeWidget::set_reference_geometry_opacity(float opacity) {
  const float clamped_opacity = std::clamp(opacity, 0.0f, 1.0f);
  if (qFuzzyCompare(reference_geometry_opacity_, clamped_opacity)) return;
  reference_geometry_opacity_ = clamped_opacity;
  update();
}

float MoleculeWidget::reference_geometry_opacity() const { return reference_geometry_opacity_; }

void MoleculeWidget::set_builtin_controls_visible(bool visible) {
  if (builtin_controls_visible_ == visible) return;
  builtin_controls_visible_ = visible;
  update_toolbar_state();
  position_toolbar();
}

void MoleculeWidget::set_default_view_rotation(const QQuaternion& rotation) {
  default_rotation_ = rotation.isNull() ? QQuaternion() : rotation.normalized();
  reset_view();
  update();
}

void MoleculeWidget::set_default_camera_distance_factor(float factor) {
  default_camera_distance_factor_ = std::clamp(factor, 0.35f, 5.0f);
  camera_distance_factor_ = default_camera_distance_factor_;
  update();
}

float MoleculeWidget::camera_distance_factor() const { return camera_distance_factor_; }

void MoleculeWidget::reset_camera() {
  reset_view();
  update();
}

void MoleculeWidget::set_symmetry_element(SymmetryElement element, QVector3D axis, QColor color) {
  symmetry_element_ = element;
  if (color.isValid()) symmetry_element_color_ = color;
  if (axis.lengthSquared() > 1.0e-8f) {
    symmetry_element_axis_ = axis.normalized();
  } else {
    symmetry_element_axis_ = QVector3D(0.0f, 0.0f, 1.0f);
  }
  update();
}

MoleculeWidget::SymmetryElement MoleculeWidget::symmetry_element() const {
  return symmetry_element_;
}

QVector3D MoleculeWidget::symmetry_element_axis() const { return symmetry_element_axis_; }

QColor MoleculeWidget::symmetry_element_color() const { return symmetry_element_color_; }

void MoleculeWidget::set_context_menu_handler(std::function<void(const QPoint&)> handler) {
  context_menu_handler_ = std::move(handler);
}

void MoleculeWidget::initializeGL() {
  initializeOpenGLFunctions();
  renderer_ready_ = create_renderer();
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDisable(GL_CULL_FACE);
  glEnable(GL_MULTISAMPLE);
}

bool MoleculeWidget::create_renderer() {
  destroy_renderer();
  renderer_error_.clear();

  static constexpr char kVertexShader[] = R"(#version 330 core
        layout(location = 0) in vec3 vertex_position;
        layout(location = 1) in vec3 vertex_normal;
        uniform mat4 model_view_projection;
        uniform mat4 model_view;
        uniform mat3 normal_matrix;
        out vec3 view_position;
        out vec3 view_normal;
        out vec3 local_position;
        void main() {
            local_position = vertex_position;
            vec4 position = model_view * vec4(vertex_position, 1.0);
            view_position = position.xyz;
            view_normal = normalize(normal_matrix * vertex_normal);
            gl_Position = model_view_projection * vec4(vertex_position, 1.0);
        }
    )";
  static constexpr char kFragmentShader[] = R"(#version 330 core
        uniform vec3 base_color;
        uniform float opacity;
        uniform bool unlit;
        uniform bool cutaway;
        uniform bool neon;
        in vec3 local_position;
        in vec3 view_position;
        in vec3 view_normal;
        out vec4 fragment_color;
        void main() {
            vec3 normal = normalize(view_normal);
            if (cutaway && local_position.x > 0.0 && local_position.z > 0.0) discard;
            vec3 light = normalize(vec3(-0.45, 0.65, 1.0));
            vec3 view_direction = normalize(-view_position);
            // Shade only the near shell: drawing front and back at 50% each
            // would make a nominally half-transparent orbital 75% opaque.
            if (neon && dot(normal, view_direction) < 0.0) discard;
            vec3 half_direction = normalize(light + view_direction);
            float diffuse = max(dot(normal, light), 0.0);
            float specular = pow(max(dot(normal, half_direction), 0.0), 42.0);
            vec3 linear_rgb = unlit
                ? base_color
                : base_color * (0.25 + 0.72 * diffuse) + vec3(0.45 * specular);
            if (neon) {
                float rim = pow(1.0 - max(dot(normal, view_direction), 0.0), 2.5);
                linear_rgb = base_color * (0.25 + 0.65 * diffuse + 0.55 * rim)
                           + vec3(0.5 * specular);
            }
            vec3 display_rgb = pow(clamp(linear_rgb, 0.0, 1.0), vec3(1.0 / 2.2));
            fragment_color = vec4(display_rgb, opacity);
        }
    )";

  shader_program_ = std::make_unique<QOpenGLShaderProgram>();
  if (!shader_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
      !shader_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
      !shader_program_->link()) {
    renderer_error_ = shader_program_->log();
    qWarning().noquote() << "Molecule shader initialization failed:" << renderer_error_;
    shader_program_.reset();
    return false;
  }

  QVector<MeshVertex> sphere_vertices;
  QVector<quint32> sphere_indices;
  create_sphere_geometry(&sphere_vertices, &sphere_indices);
  sphere_mesh_ = std::make_unique<Mesh>();
  if (!sphere_mesh_->create(shader_program_.get(), sphere_vertices, sphere_indices)) {
    renderer_error_ = QStringLiteral("Could not create sphere mesh buffers");
    destroy_renderer();
    return false;
  }

  QVector<MeshVertex> cylinder_vertices;
  QVector<quint32> cylinder_indices;
  create_cylinder_geometry(&cylinder_vertices, &cylinder_indices);
  cylinder_mesh_ = std::make_unique<Mesh>();
  if (!cylinder_mesh_->create(shader_program_.get(), cylinder_vertices, cylinder_indices)) {
    renderer_error_ = QStringLiteral("Could not create cylinder mesh buffers");
    destroy_renderer();
    return false;
  }

  QVector<MeshVertex> disc_vertices;
  QVector<quint32> disc_indices;
  create_disc_geometry(&disc_vertices, &disc_indices);
  disc_mesh_ = std::make_unique<Mesh>();
  if (!disc_mesh_->create(shader_program_.get(), disc_vertices, disc_indices)) {
    renderer_error_ = QStringLiteral("Could not create symmetry-plane mesh buffers");
    destroy_renderer();
    return false;
  }
  return true;
}

void MoleculeWidget::destroy_renderer() {
  for (auto& mesh : orbital_meshes_) {
    if (mesh) mesh->destroy();
    mesh.reset();
  }
  if (sphere_mesh_) {
    sphere_mesh_->destroy();
    sphere_mesh_.reset();
  }
  if (cylinder_mesh_) {
    cylinder_mesh_->destroy();
    cylinder_mesh_.reset();
  }
  if (disc_mesh_) {
    disc_mesh_->destroy();
    disc_mesh_.reset();
  }
  shader_program_.reset();
  renderer_ready_ = false;
}

void MoleculeWidget::paintGL() {
  // QPainter uses the same OpenGL context for the axis overlay and may leave
  // clipping or blend state enabled. Restore the molecule pass explicitly on
  // every frame; initializeGL() alone is insufficient after the first paint.
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_COLOR_LOGIC_OP);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glClearColor(0.90f, 0.92f, 0.95f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!renderer_ready_ || !geometry_.is_valid() || width() <= 0 || height() <= 0) {
    QPainter painter(this);
    painter.setPen(QColor(80, 85, 95));
    const QString message = geometry_.is_valid() && !renderer_ready_
                                ? QStringLiteral("Molecule renderer unavailable")
                                : QStringLiteral("Molecule unavailable");
    painter.drawText(rect(), Qt::AlignCenter, message);
    return;
  }

  const qreal pixel_ratio = devicePixelRatioF();
  const int pixel_width = std::max(1, int(std::round(width() * pixel_ratio)));
  const int pixel_height = std::max(1, int(std::round(height() * pixel_ratio)));
  const QRect full_viewport(0, 0, pixel_width, pixel_height);
  QVector<QVector3D> positions = geometry_.positions_at_phase(vibration_phase_);
  for (QVector3D& position : positions) {
    position = coordinate_origin_ + coordinate_transform_.mapVector(position - coordinate_origin_);
  }

  if (stereo_mode_ == StereoMode::RedCyanAnaglyph) {
    const float aspect = float(pixel_width) / float(pixel_height);
    const float camera_distance =
        molecule_camera::distance(bounding_radius_, aspect, camera_distance_factor_);
    const float separation = molecule_camera::stereo_eye_separation(camera_distance);

    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_eye(full_viewport, -separation * 0.5f, positions);
    glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_eye(full_viewport, separation * 0.5f, positions);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  } else {
    draw_eye(full_viewport, 0.0f, positions);
  }

  glViewport(0, 0, pixel_width, pixel_height);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  if (axes_visible_) {
    draw_axis_gizmo(&painter);
  }
}

void MoleculeWidget::draw_eye(const QRect& pixel_viewport, float eye_offset,
                              const QVector<QVector3D>& positions) {
  if (pixel_viewport.width() <= 0 || pixel_viewport.height() <= 0 ||
      positions.size() != geometry_.atoms.size()) {
    return;
  }

  glViewport(pixel_viewport.x(), pixel_viewport.y(), pixel_viewport.width(),
             pixel_viewport.height());
  const float aspect = float(pixel_viewport.width()) / float(pixel_viewport.height());
  const float camera_distance =
      molecule_camera::distance(bounding_radius_, aspect, camera_distance_factor_);
  const float near_plane = std::max(0.01f, camera_distance - bounding_radius_ * 1.25f);
  const float far_plane = camera_distance + bounding_radius_ * 1.75f;

  QMatrix4x4 projection;
  projection.perspective(molecule_camera::kVerticalFieldOfViewDegrees, aspect, near_plane,
                         far_plane);
  const molecule_camera::ViewFrame camera =
      molecule_camera::default_view(camera_distance, eye_offset);
  QMatrix4x4 view;
  view.lookAt(camera.eye, camera.center, camera.up);
  QMatrix4x4 scene_model;
  scene_model.rotate(rotation_);
  const QVector3D coordinate_origin_offset = coordinate_origin_ - center_;

  shader_program_->bind();
  if (world_axes_visible_ && cylinder_mesh_ && sphere_mesh_) {
    const float length = std::max(1.0f, bounding_radius_) * 1.25f;
    const float radius = std::clamp(length * 0.012f, 0.012f, 0.026f);
    const auto draw_axis_segment = [&](const QVector3D& from, const QVector3D& to,
                                       const QColor& color, float segment_radius) {
      const QVector3D direction = to - from;
      const float segment_length = direction.length();
      if (segment_length <= 1.0e-6f) return;
      QMatrix4x4 model = scene_model;
      model.translate(coordinate_origin_offset + from);
      model.rotate(
          QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, 1.0f), direction / segment_length));
      model.scale(segment_radius, segment_radius, segment_length);
      draw_mesh(*cylinder_mesh_, model, linear_color(color), view, projection, 1.0f, true);
    };
    struct CartesianAxis {
      QVector3D direction;
      QColor color;
      QChar label;
    };
    const CartesianAxis axes[] = {
        {QVector3D(1.0f, 0.0f, 0.0f), QColor(205, 63, 63), QLatin1Char('x')},
        {QVector3D(0.0f, 1.0f, 0.0f), QColor(48, 145, 75), QLatin1Char('y')},
        {QVector3D(0.0f, 0.0f, 1.0f), QColor(55, 105, 205), QLatin1Char('z')},
    };
    const QVector3D camera_forward = (camera.center - camera.eye).normalized();
    const QVector3D camera_right = QVector3D::crossProduct(camera_forward, camera.up).normalized();
    const QVector3D camera_up = QVector3D::crossProduct(camera_right, camera_forward).normalized();
    const QQuaternion inverse_rotation = rotation_.conjugated();
    const QVector3D glyph_right = inverse_rotation.rotatedVector(camera_right);
    const QVector3D glyph_up = inverse_rotation.rotatedVector(camera_up);
    const float glyph_size = length * 0.085f;
    const float glyph_radius = radius * 0.62f;
    const float label_gap = length * 0.15f;
    for (const CartesianAxis& axis : axes) {
      draw_axis_segment(QVector3D(), axis.direction * length, axis.color, radius);

      QMatrix4x4 endpoint = scene_model;
      endpoint.translate(coordinate_origin_offset + axis.direction * length);
      endpoint.scale(radius * 2.4f);
      draw_mesh(*sphere_mesh_, endpoint, linear_color(axis.color), view, projection, 1.0f, true);

      const QVector3D anchor = axis.direction * (length + label_gap);
      const auto glyph_point = [&](float x, float y) {
        return anchor + glyph_right * (x * glyph_size) + glyph_up * (y * glyph_size);
      };
      if (axis.label == QLatin1Char('x')) {
        draw_axis_segment(glyph_point(-0.5f, -0.65f), glyph_point(0.5f, 0.65f), axis.color,
                          glyph_radius);
        draw_axis_segment(glyph_point(-0.5f, 0.65f), glyph_point(0.5f, -0.65f), axis.color,
                          glyph_radius);
      } else if (axis.label == QLatin1Char('y')) {
        draw_axis_segment(glyph_point(-0.5f, 0.65f), glyph_point(0.0f, 0.0f), axis.color,
                          glyph_radius);
        draw_axis_segment(glyph_point(0.5f, 0.65f), glyph_point(0.0f, 0.0f), axis.color,
                          glyph_radius);
        draw_axis_segment(glyph_point(0.0f, 0.0f), glyph_point(0.0f, -0.68f), axis.color,
                          glyph_radius);
      } else {
        draw_axis_segment(glyph_point(-0.5f, 0.65f), glyph_point(0.5f, 0.65f), axis.color,
                          glyph_radius);
        draw_axis_segment(glyph_point(0.5f, 0.65f), glyph_point(-0.5f, -0.65f), axis.color,
                          glyph_radius);
        draw_axis_segment(glyph_point(-0.5f, -0.65f), glyph_point(0.5f, -0.65f), axis.color,
                          glyph_radius);
      }
    }
  }
  const bool shows_rotation_axis = symmetry_element_ == SymmetryElement::RotationAxis ||
                                   symmetry_element_ == SymmetryElement::ImproperAxisAndPlane;
  const bool shows_mirror_plane = symmetry_element_ == SymmetryElement::MirrorPlane ||
                                  symmetry_element_ == SymmetryElement::ImproperAxisAndPlane;
  const float guide_span = std::max(1.0f, bounding_radius_);
  if (shows_rotation_axis && cylinder_mesh_) {
    const float half_length = guide_span * 1.14f;
    const float radius = std::clamp(guide_span * 0.018f, 0.025f, 0.055f);
    QMatrix4x4 model = scene_model;
    model.translate(coordinate_origin_offset - symmetry_element_axis_ * half_length);
    model.rotate(QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, 1.0f), symmetry_element_axis_));
    model.scale(radius, radius, half_length * 2.0f);
    draw_mesh(*cylinder_mesh_, model, linear_color(symmetry_element_color_), view, projection, 1.0f,
              true);
  }
  const auto draw_molecule = [&](const QVector<QVector3D>& molecule_positions, float opacity,
                                 bool deform_atoms) {
    for (const MoleculeBond& bond : geometry_.bonds) {
      if (bond.first_atom < 0 || bond.second_atom < 0 ||
          bond.first_atom >= geometry_.atoms.size() || bond.second_atom >= geometry_.atoms.size()) {
        continue;
      }

      const QVector3D first = molecule_positions.at(bond.first_atom) - center_;
      const QVector3D second = molecule_positions.at(bond.second_atom) - center_;
      const QVector3D midpoint = (first + second) * 0.5f;
      const QVector3D segment_starts[] = {first, midpoint};
      const QVector3D segment_ends[] = {midpoint, second};
      const int atom_indices[] = {bond.first_atom, bond.second_atom};
      for (int segment = 0; segment < 2; ++segment) {
        const QVector3D direction = segment_ends[segment] - segment_starts[segment];
        const float length = direction.length();
        if (length <= 0.0001f) continue;
        QMatrix4x4 model = scene_model;
        model.translate(segment_starts[segment]);
        model.rotate(QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, 1.0f), direction / length));
        model.scale(kBondRadius, kBondRadius, length);
        draw_mesh(*cylinder_mesh_, model,
                  linear_color(element_color(geometry_.atoms.at(atom_indices[segment]).element)),
                  view, projection, opacity);
      }
    }

    for (int atom_index = 0; atom_index < geometry_.atoms.size(); ++atom_index) {
      const MoleculeAtom& atom = geometry_.atoms.at(atom_index);
      QMatrix4x4 model = scene_model;
      model.translate(molecule_positions.at(atom_index) - center_);
      if (deform_atoms) model *= atom_shape_transform_;
      const bool has_orbital =
          std::any_of(orbitals_.begin(), orbitals_.end(),
                      [atom_index](const auto& orbital) { return orbital.atom == atom_index + 1; });
      const float radius = element_display_radius(atom.element) * (has_orbital ? 0.22f : 1.0f);
      model.scale(radius, radius, radius);
      draw_mesh(*sphere_mesh_, model, linear_color(element_color(atom.element)), view, projection,
                opacity);
    }
  };

  if (reference_geometry_visible_ && reference_geometry_opacity_ > 0.0f) {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // The moving, opaque pose must remain legible even when it passes behind
    // the reference pose, so the translucent pass does not claim depth.
    glDepthMask(GL_FALSE);
    draw_molecule(geometry_.positions_at_phase(vibration_phase_), reference_geometry_opacity_,
                  false);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  draw_molecule(positions, 1.0f, true);

  // Signed lobe colors travel with their vertices. Applying the complete
  // coordinate transform (not just an atom translation) preserves parity and
  // phase under reflections and inversion, including atoms fixed by the operation.
  struct OrbitalDraw {
    Mesh* mesh;
    QMatrix4x4 model;
    QVector3D color;
    float opacity;
    bool cutaway;
    float depth;
  };
  QVector<OrbitalDraw> orbital_draws;
  const auto draw_orbitals = [&](bool reference) {
    for (const auto& orbital : orbitals_) {
      const int index = symmetry_orbital_names().indexOf(orbital.orbital);
      const auto& baked = symmetry_orbital_mesh(orbital.orbital);
      for (int phase = 0; phase < 2; ++phase) {
        auto& mesh = orbital_meshes_[index * 2 + phase];
        const auto& vertices = phase == 0 ? baked.positive : baked.negative;
        if (vertices.isEmpty()) continue;
        if (!mesh) {
          QVector<MeshVertex> uploaded;
          QVector<quint32> indices;
          uploaded.reserve(vertices.size());
          indices.reserve(vertices.size());
          for (const auto& vertex : vertices) {
            indices.push_back(quint32(indices.size()));
            append_vertex(&uploaded, vertex.position, vertex.normal);
          }
          mesh = std::make_unique<Mesh>();
          for (const auto& vertex : vertices) mesh->orbital_centroid += vertex.position;
          mesh->orbital_centroid /= float(vertices.size());
          if (!mesh->create(shader_program_.get(), uploaded, indices)) {
            mesh.reset();
            continue;
          }
        }
        const QVector3D color = linear_color(phase == 0 ? QColor("#00f5ff") : QColor("#b026ff"));
        QMatrix4x4 model = scene_model;
        model.translate(coordinate_origin_offset);
        if (!reference) model *= coordinate_transform_;
        model.translate(geometry_.atoms.at(orbital.atom - 1).position - coordinate_origin_);
        model.scale(orbital.scale);
        // At the exactly singular midpoint of an interpolation the surface has
        // zero volume; omit that frame instead of using an undefined normal matrix.
        if (reference || std::abs(coordinate_transform_.determinant()) > 1.0e-6f) {
          orbital_draws.push_back({mesh.get(), model, color, reference ? 0.18f : 0.5f,
                                   orbital.orbital == QStringLiteral("2s") && phase == 1,
                                   (view * model).map(mesh->orbital_centroid).z()});
        }
      }
    }
  };
  if (reference_geometry_visible_) draw_orbitals(true);
  draw_orbitals(false);
  std::stable_sort(orbital_draws.begin(), orbital_draws.end(),
                   [](const auto& a, const auto& b) { return a.depth < b.depth; });
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
  glDepthMask(GL_FALSE);
  for (const auto& item : orbital_draws) {
    draw_mesh(*item.mesh, item.model, item.color, view, projection, item.opacity, false,
              item.cutaway, true);
  }
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  if (symmetry_element_ == SymmetryElement::InversionCenter && sphere_mesh_) {
    glDisable(GL_DEPTH_TEST);
    QMatrix4x4 model = scene_model;
    model.translate(coordinate_origin_offset);
    model.scale(std::clamp(guide_span * 0.12f, 0.13f, 0.22f));
    draw_mesh(*sphere_mesh_, model, linear_color(symmetry_element_color_), view, projection, 1.0f,
              true);
    glEnable(GL_DEPTH_TEST);
  }

  if (shows_mirror_plane && disc_mesh_) {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    // Keep the QOpenGLWidget framebuffer opaque while blending the plane.
    // A reduced destination alpha is interpreted again by Qt's compositor and
    // can produce bright chroma-key-like pixels at depth intersections.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    QMatrix4x4 model = scene_model;
    model.translate(coordinate_origin_offset);
    model.rotate(QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, 1.0f), symmetry_element_axis_));
    model.scale(guide_span * 0.98f, guide_span * 0.98f, 1.0f);
    draw_mesh(*disc_mesh_, model, linear_color(symmetry_element_color_), view, projection, 0.30f,
              true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
  shader_program_->release();
}

void MoleculeWidget::draw_axis_gizmo(QPainter* painter) {
  if (!painter || width() < 76 || height() < 76) {
    return;
  }

  struct AxisMarker {
    QVector3D direction;
    QColor color;
    QString label;
  };
  QVector<AxisMarker> axes{
      {molecule_camera::camera_space_direction(
           rotation_.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f))),
       QColor(224, 70, 70), QStringLiteral("X")},
      {molecule_camera::camera_space_direction(
           rotation_.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f))),
       QColor(72, 178, 96), QStringLiteral("Y")},
      {molecule_camera::camera_space_direction(
           rotation_.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f))),
       QColor(72, 126, 224), QStringLiteral("Z")},
  };
  std::sort(axes.begin(), axes.end(), [](const AxisMarker& first, const AxisMarker& second) {
    return first.direction.z() < second.direction.z();
  });

  const QPointF origin(38.0, height() - 38.0);
  painter->setPen(Qt::NoPen);
  painter->setBrush(QColor(245, 247, 250, 210));
  painter->drawEllipse(origin, 4.0, 4.0);

  QFont label_font = painter->font();
  label_font.setPixelSize(10);
  label_font.setBold(true);
  painter->setFont(label_font);
  for (const AxisMarker& axis : axes) {
    const qreal depth_scale = 0.82 + 0.18 * (axis.direction.z() + 1.0) * 0.5;
    const QPointF endpoint = origin + QPointF(axis.direction.x() * 25.0 * depth_scale,
                                              -axis.direction.y() * 25.0 * depth_scale);
    painter->setPen(QPen(axis.color, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(origin, endpoint);
    painter->setPen(Qt::NoPen);
    painter->setBrush(axis.color);
    painter->drawEllipse(endpoint, 9.0, 9.0);
    painter->setPen(Qt::white);
    painter->drawText(QRectF(endpoint.x() - 7.0, endpoint.y() - 7.0, 14.0, 14.0), Qt::AlignCenter,
                      axis.label);
  }
}

void MoleculeWidget::draw_mesh(Mesh& mesh, const QMatrix4x4& model, const QVector3D& color,
                               const QMatrix4x4& view, const QMatrix4x4& projection, float opacity,
                               bool unlit, bool cutaway, bool neon) {
  const QMatrix4x4 model_view = view * model;
  shader_program_->setUniformValue("model_view_projection", projection * model_view);
  shader_program_->setUniformValue("model_view", model_view);
  shader_program_->setUniformValue("normal_matrix", model_view.normalMatrix());
  shader_program_->setUniformValue("base_color", color);
  shader_program_->setUniformValue("opacity", opacity);
  shader_program_->setUniformValue("unlit", unlit);
  shader_program_->setUniformValue("cutaway", cutaway);
  shader_program_->setUniformValue("neon", neon);

  QOpenGLVertexArrayObject::Binder binder(&mesh.vao);
  glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
}

void MoleculeWidget::create_toolbar() {
  toolbar_panel_ = new QFrame(this);
  toolbar_panel_->setObjectName(QStringLiteral("moleculeToolbarPanel"));
  toolbar_panel_->setAttribute(Qt::WA_StyledBackground, true);
  toolbar_panel_->setStyleSheet(QStringLiteral(
      "#moleculeToolbarPanel {"
      " background: rgba(26, 30, 36, 224);"
      " border: 1px solid rgba(255, 255, 255, 45);"
      " border-radius: 7px;"
      "}"
      "#moleculeToolbarPanel QToolButton {"
      " background: transparent; border: 0; border-radius: 5px; padding: 5px;"
      "}"
      "#moleculeToolbarPanel QToolButton:hover { background: rgba(255, 255, 255, 35); }"
      "#moleculeToolbarPanel QToolButton:checked { background: rgb(0, 140, 140); }"
      "#moleculeToolbarPanel QToolButton:disabled { background: transparent; }"));

  auto* toolbar_layout = new QHBoxLayout(toolbar_panel_);
  toolbar_layout->setContentsMargins(3, 3, 3, 3);
  toolbar_layout->setSpacing(2);

  auto make_button = [this, toolbar_layout](const QString& object_name, const QString& tooltip) {
    auto* button = new QToolButton(toolbar_panel_);
    button->setObjectName(object_name);
    button->setFixedSize(kToolbarButtonSize, kToolbarButtonSize);
    button->setIconSize(QSize(18, 18));
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    toolbar_layout->addWidget(button);
    return button;
  };

  stereo_button_ =
      make_button(QStringLiteral("moleculeStereoButton"), QStringLiteral("Red/cyan anaglyph"));
  stereo_button_->setCheckable(true);
  connect(stereo_button_, &QToolButton::toggled, this, [this](bool checked) {
    set_stereo_mode(checked ? StereoMode::RedCyanAnaglyph : StereoMode::Mono);
  });

  vibration_button_ = make_button(QStringLiteral("moleculeVibrationButton"),
                                  QStringLiteral("Play molecular vibration"));
  vibration_button_->setCheckable(true);
  connect(vibration_button_, &QToolButton::toggled, this,
          [this](bool checked) { set_vibration_playing(checked); });

  auto_rotation_button_ = make_button(QStringLiteral("moleculeAutoRotationButton"),
                                      QStringLiteral("Rotate continuously around the Z axis"));
  auto_rotation_button_->setCheckable(true);
  connect(auto_rotation_button_, &QToolButton::toggled, this,
          [this](bool checked) { set_auto_rotation_enabled(checked); });

  axes_button_ =
      make_button(QStringLiteral("moleculeAxesButton"), QStringLiteral("Show orientation axes"));
  axes_button_->setCheckable(true);
  connect(axes_button_, &QToolButton::toggled, this,
          [this](bool checked) { set_axes_visible(checked); });

  reset_button_ =
      make_button(QStringLiteral("moleculeResetButton"), QStringLiteral("Reset molecule view"));
  connect(reset_button_, &QToolButton::clicked, this, [this] {
    reset_view();
    update();
  });

  toolbar_toggle_button_ = new QToolButton(this);
  toolbar_toggle_button_->setObjectName(QStringLiteral("moleculeToolbarToggle"));
  toolbar_toggle_button_->setFixedSize(kToolbarButtonSize, kToolbarButtonSize);
  toolbar_toggle_button_->setIconSize(QSize(18, 18));
  toolbar_toggle_button_->setCheckable(true);
  toolbar_toggle_button_->setFocusPolicy(Qt::NoFocus);
  toolbar_toggle_button_->setToolTip(QStringLiteral("Show or hide molecule controls"));
  toolbar_toggle_button_->setStyleSheet(
      QStringLiteral("QToolButton {"
                     " background: rgba(26, 30, 36, 224);"
                     " border: 1px solid rgba(255, 255, 255, 45);"
                     " border-radius: 7px; padding: 5px;"
                     "}"
                     "QToolButton:hover { background: rgba(48, 54, 63, 235); }"
                     "QToolButton:checked { background: rgb(0, 140, 140); }"));
  connect(toolbar_toggle_button_, &QToolButton::toggled, this,
          [this](bool checked) { set_toolbar_expanded(checked); });

  vibration_timer_ = new QTimer(this);
  vibration_timer_->setTimerType(Qt::PreciseTimer);
  vibration_timer_->setInterval(kAnimationIntervalMs);
  connect(vibration_timer_, &QTimer::timeout, this, [this] {
    constexpr float phase_step =
        2.0f * kPi * kVibrationCyclesPerSecond * (float(kAnimationIntervalMs) / 1000.0f);
    vibration_phase_ = std::fmod(vibration_phase_ + phase_step, 2.0f * kPi);
    update();
  });

  auto_rotation_timer_ = new QTimer(this);
  auto_rotation_timer_->setObjectName(QStringLiteral("moleculeAutoRotationTimer"));
  auto_rotation_timer_->setTimerType(Qt::PreciseTimer);
  auto_rotation_timer_->setInterval(kAnimationIntervalMs);
  connect(auto_rotation_timer_, &QTimer::timeout, this, [this] {
    constexpr float rotation_step =
        kAutoRotationDegreesPerSecond * (float(kAnimationIntervalMs) / 1000.0f);
    const QQuaternion z_rotation =
        QQuaternion::fromAxisAndAngle(QVector3D(0.0f, 0.0f, 1.0f), rotation_step);
    rotation_ = (rotation_ * z_rotation).normalized();
    update();
  });

  update_toolbar_state();
  position_toolbar();
}

void MoleculeWidget::position_toolbar() {
  if (!toolbar_toggle_button_ || !toolbar_panel_) {
    return;
  }
  if (!builtin_controls_visible_) {
    toolbar_panel_->hide();
    toolbar_toggle_button_->hide();
    return;
  }
  toolbar_toggle_button_->show();
  toolbar_panel_->adjustSize();
  const int toggle_x =
      std::max(kToolbarMargin, width() - kToolbarMargin - toolbar_toggle_button_->width());
  toolbar_toggle_button_->move(toggle_x, kToolbarMargin);

  const int preferred_x = toggle_x - 5 - toolbar_panel_->width();
  if (preferred_x >= kToolbarMargin) {
    toolbar_panel_->move(preferred_x, kToolbarMargin);
  } else {
    toolbar_panel_->move(
        std::max(kToolbarMargin, width() - kToolbarMargin - toolbar_panel_->width()),
        kToolbarMargin + toolbar_toggle_button_->height() + 5);
  }
  toolbar_panel_->setVisible(toolbar_expanded_);
  toolbar_panel_->raise();
  toolbar_toggle_button_->raise();
}

void MoleculeWidget::update_toolbar_state() {
  const QColor icon_color(0xee, 0xee, 0xee);
  if (toolbar_toggle_button_) {
    const QSignalBlocker blocker(toolbar_toggle_button_);
    toolbar_toggle_button_->setChecked(toolbar_expanded_);
    toolbar_toggle_button_->setIcon(
        font_awesome::icon(font_awesome::Style::Solid,
                           toolbar_expanded_ ? QStringLiteral("xmark") : QStringLiteral("sliders"),
                           icon_color, QSize(20, 20)));
  }
  if (stereo_button_) {
    const QSignalBlocker blocker(stereo_button_);
    const bool anaglyph_enabled = stereo_mode_ == StereoMode::RedCyanAnaglyph;
    stereo_button_->setChecked(anaglyph_enabled);
    stereo_button_->setIcon(font_awesome::icon(
        font_awesome::Style::Solid, QStringLiteral("glasses"), icon_color, QSize(20, 20)));
    stereo_button_->setToolTip(anaglyph_enabled ? QStringLiteral("Disable red/cyan anaglyph")
                                                : QStringLiteral("Enable red/cyan anaglyph"));
  }
  if (vibration_button_) {
    const QSignalBlocker blocker(vibration_button_);
    const bool has_vibration = geometry_.has_vibration();
    vibration_button_->setEnabled(has_vibration);
    vibration_button_->setChecked(vibration_playing());
    vibration_button_->setIcon(
        font_awesome::icon(font_awesome::Style::Solid,
                           vibration_playing() ? QStringLiteral("pause") : QStringLiteral("play"),
                           has_vibration ? icon_color : QColor(120, 125, 132), QSize(20, 20)));
    vibration_button_->setToolTip(
        has_vibration ? (vibration_playing() ? QStringLiteral("Pause molecular vibration")
                                             : QStringLiteral("Play molecular vibration"))
                      : QStringLiteral("This molecule has no vibration vectors"));
  }
  if (auto_rotation_button_) {
    const QSignalBlocker blocker(auto_rotation_button_);
    auto_rotation_button_->setChecked(auto_rotation_enabled_);
    auto_rotation_button_->setIcon(font_awesome::icon(
        font_awesome::Style::Solid, QStringLiteral("rotate"), icon_color, QSize(20, 20)));
    auto_rotation_button_->setToolTip(
        auto_rotation_enabled_ ? QStringLiteral("Stop continuous Z-axis rotation")
                               : QStringLiteral("Rotate continuously around the Z axis"));
  }
  if (axes_button_) {
    const QSignalBlocker blocker(axes_button_);
    axes_button_->setChecked(axes_visible_);
    axes_button_->setIcon(font_awesome::icon(font_awesome::Style::Solid, QStringLiteral("compass"),
                                             icon_color, QSize(20, 20)));
  }
  if (reset_button_) {
    reset_button_->setIcon(font_awesome::icon(
        font_awesome::Style::Solid, QStringLiteral("arrows-rotate"), icon_color, QSize(20, 20)));
  }
  if (toolbar_panel_) {
    toolbar_panel_->setVisible(builtin_controls_visible_ && toolbar_expanded_);
  }
  if (toolbar_toggle_button_) toolbar_toggle_button_->setVisible(builtin_controls_visible_);
}

void MoleculeWidget::resizeEvent(QResizeEvent* event) {
  QOpenGLWidget::resizeEvent(event);
  position_toolbar();
}

void MoleculeWidget::hideEvent(QHideEvent* event) {
  set_vibration_playing(false);
  if (auto_rotation_timer_) {
    auto_rotation_timer_->stop();
  }
  QOpenGLWidget::hideEvent(event);
}

void MoleculeWidget::showEvent(QShowEvent* event) {
  QOpenGLWidget::showEvent(event);
  if (auto_rotation_enabled_ && auto_rotation_timer_) {
    auto_rotation_timer_->start();
  }
}

void MoleculeWidget::contextMenuEvent(QContextMenuEvent* event) {
  // The menu is opened from mouseReleaseEvent. Consume the subsequently
  // generated context-menu event so it cannot create a second native popup.
  event->accept();
}

void MoleculeWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    rotating_ = false;
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    rotating_ = true;
    last_mouse_position_ = event->position();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QOpenGLWidget::mousePressEvent(event);
}

void MoleculeWidget::mouseMoveEvent(QMouseEvent* event) {
  if (rotating_ && (event->buttons() & Qt::LeftButton)) {
    const QPointF delta = event->position() - last_mouse_position_;
    last_mouse_position_ = event->position();
    const QQuaternion yaw =
        QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, float(delta.x()) * 0.65f);
    const QQuaternion pitch =
        QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, float(delta.y()) * 0.65f);
    rotation_ = (yaw * pitch * rotation_).normalized();
    update();
    event->accept();
    return;
  }
  QOpenGLWidget::mouseMoveEvent(event);
}

void MoleculeWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    if (context_menu_handler_) {
      context_menu_handler_(event->globalPosition().toPoint());
    }
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && rotating_) {
    rotating_ = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
    return;
  }
  QOpenGLWidget::mouseReleaseEvent(event);
}

void MoleculeWidget::wheelEvent(QWheelEvent* event) {
  const int angle_delta_y = event->angleDelta().y();
  const int pixel_delta_y = event->pixelDelta().y();
  if (angle_delta_y != 0 || pixel_delta_y != 0) {
    camera_distance_factor_ = molecule_camera::zoomed_distance_factor(camera_distance_factor_,
                                                                      angle_delta_y, pixel_delta_y);
    update();
    event->accept();
    return;
  }
  QOpenGLWidget::wheelEvent(event);
}

void MoleculeWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    reset_view();
    update();
    event->accept();
    return;
  }
  QOpenGLWidget::mouseDoubleClickEvent(event);
}

void MoleculeWidget::reset_view() {
  rotation_ = default_rotation_;
  camera_distance_factor_ = default_camera_distance_factor_;
  rotating_ = false;
  setCursor(Qt::OpenHandCursor);
}
