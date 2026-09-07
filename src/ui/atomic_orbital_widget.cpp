#include "ui/atomic_orbital_widget.hpp"
#include "ui/math_text.hpp"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace {
constexpr qint64 kGpuCacheBudget = 32 * 1024 * 1024;

/** @brief Compares every input to orbital geometry, excluding presentation settings. */
bool same_geometry(const AtomicOrbitalDefinition& a, const AtomicOrbitalDefinition& b) {
  return a.n == b.n && a.l == b.l && a.m == b.m && a.grid_size == b.grid_size &&
         a.isovalue == b.isovalue;
}

constexpr int kControlHeight = 78;
constexpr int kHeaderHeight = 54;
constexpr int kOuterMargin = 12;
constexpr int kPanelGap = 14;
constexpr int kSliderSteps = 1000;
constexpr int kTitlePixelSize = 28;
constexpr int kPanelLabelPixelSize = 20;
constexpr int kLegendPixelSize = 17;

QVector3D linear_color(const QColor& color) {
    const auto linear_channel = [](float channel) {
        return channel <= 0.04045f
            ? channel / 12.92f
            : std::pow((channel + 0.055f) / 1.055f, 2.4f);
    };
    return QVector3D(
        linear_channel(float(color.redF())),
        linear_channel(float(color.greenF())),
        linear_channel(float(color.blueF())));
}

QString plane_name(AtomicOrbitalDefinition::Plane plane) {
    switch (plane) {
    case AtomicOrbitalDefinition::Plane::XZ: return QStringLiteral("xz");
    case AtomicOrbitalDefinition::Plane::YZ: return QStringLiteral("yz");
    case AtomicOrbitalDefinition::Plane::XY: return QStringLiteral("xy");
    }
    return QStringLiteral("xy");
}

QString orbital_math_label(const QString& name) {
    AtomicOrbitalCatalogEntry entry;
    if (resolve_atomic_orbital(name, &entry)) {
        return QStringLiteral("$%1$").arg(entry.label);
    }
    return QStringLiteral("$%1$").arg(name);
}

QString signed_order_label(double value, bool positive) {
    const int exponent = int(std::lround(std::log10(value)));
    return QStringLiteral("$%1\\,1\\times10^{%2}$")
        .arg(positive ? QStringLiteral("+") : QStringLiteral("-"))
        .arg(exponent);
}

int plane_index(AtomicOrbitalDefinition::Plane plane) {
    switch (plane) {
    case AtomicOrbitalDefinition::Plane::XY: return 0;
    case AtomicOrbitalDefinition::Plane::XZ: return 1;
    case AtomicOrbitalDefinition::Plane::YZ: return 2;
    }
    return 0;
}
}  // namespace

struct AtomicOrbitalWidget::Mesh {
    QOpenGLBuffer vertex_buffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao;
    int vertex_count = 0;

    bool create(
        QOpenGLShaderProgram* program,
        const QVector<AtomicOrbitalVertex>& vertices) {
        if (!program || vertices.isEmpty() || !vao.create() || !vertex_buffer.create()) {
            return false;
        }
        QOpenGLVertexArrayObject::Binder binder(&vao);
        if (!vertex_buffer.bind()) return false;
        vertex_buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        vertex_buffer.allocate(
            vertices.constData(), int(vertices.size() * sizeof(AtomicOrbitalVertex)));
        const int position = program->attributeLocation("vertex_position");
        const int normal = program->attributeLocation("vertex_normal");
        program->enableAttributeArray(position);
        program->setAttributeBuffer(
            position, GL_FLOAT, int(offsetof(AtomicOrbitalVertex, position)), 3,
            int(sizeof(AtomicOrbitalVertex)));
        program->enableAttributeArray(normal);
        program->setAttributeBuffer(
            normal, GL_FLOAT, int(offsetof(AtomicOrbitalVertex, normal)), 3,
            int(sizeof(AtomicOrbitalVertex)));
        vertex_buffer.release();
        vertex_count = vertices.size();
        return true;
    }

    void destroy() {
        vao.destroy();
        vertex_buffer.destroy();
        vertex_count = 0;
    }
};

struct AtomicOrbitalWidget::CachedGpuVolume {
  AtomicOrbitalDefinition definition;
  std::unique_ptr<Mesh> positive;
  std::unique_ptr<Mesh> negative;
  GLuint texture = 0;
  qint64 bytes = 0;
};

quint64 AtomicOrbitalWidget::geometry_upload_count() const { return geometry_upload_count_; }
qint64 AtomicOrbitalWidget::gpu_cache_bytes() const { return gpu_cache_bytes_; }

void AtomicOrbitalWidget::clear_gpu_cache() {
  for (auto& entry : gpu_cache_) {
    glDeleteTextures(1, &entry->texture);
    entry->positive->destroy();
    entry->negative->destroy();
  }
  gpu_cache_.clear();
  gpu_cache_bytes_ = 0;
}

AtomicOrbitalWidget::AtomicOrbitalWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setObjectName(QStringLiteral("atomicOrbitalWidget"));
    QSurfaceFormat surface_format = format();
    surface_format.setVersion(3, 3);
    surface_format.setProfile(QSurfaceFormat::CoreProfile);
    surface_format.setDepthBufferSize(24);
    surface_format.setSamples(4);
    setFormat(surface_format);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(160, 100);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    create_controls();
    reset_view();
}

AtomicOrbitalWidget::~AtomicOrbitalWidget() {
  disconnect(context_cleanup_connection_);
  if (context() && context()->isValid()) {
    makeCurrent();
    destroy_renderer();
    doneCurrent();
  }
}

void AtomicOrbitalWidget::set_definition(const AtomicOrbitalDefinition& definition) {
    if (!definition.is_valid()) return;
    if (definition_ == definition && volume_.is_valid()) return;
    QString error;
    const auto volume = build_atomic_orbital_volume(definition, &error);
    set_prepared_definition(definition, volume, error);
}

void AtomicOrbitalWidget::set_prepared_definition(const AtomicOrbitalDefinition& definition,
                                                  const AtomicOrbitalVolume& volume,
                                                  const QString& error) {
  if (!definition.is_valid()) return;
  if (definition_ == definition && volume_.is_valid()) return;
  const bool reuse_geometry = renderer_ready_ && !volume_dirty_ && volume_.is_valid() &&
                              uploaded_bytes_ > 0 &&
                              same_geometry(uploaded_definition_, definition);
  definition_ = definition;
  update_plane_buttons();
  volume_ = volume;
  renderer_error_ = error;
  volume_dirty_ = volume_.is_valid() && !reuse_geometry;
  offset_slider_->setValue(
      int(std::lround((definition_.offset_initial - definition_.offset_min) /
                      (definition_.offset_max - definition_.offset_min) * kSliderSteps)));
  update_offset_label();
  reset_view();
  if (context() && volume_.is_valid()) {
    makeCurrent();
    if (volume_dirty_)
      upload_volume();
    else {
      upload_colormap();
      update_plane_buffer();
    }
    doneCurrent();
  }
  update();
}

const AtomicOrbitalDefinition& AtomicOrbitalWidget::definition() const {
    return definition_;
}

void AtomicOrbitalWidget::set_context_menu_handler(
    std::function<void(const QPoint&)> handler) {
    context_menu_handler_ = std::move(handler);
}

double AtomicOrbitalWidget::plane_offset() const {
    if (!offset_slider_) return definition_.offset_initial;
    const double amount = double(offset_slider_->value()) / double(kSliderSteps);
    return definition_.offset_min
        + amount * (definition_.offset_max - definition_.offset_min);
}

bool AtomicOrbitalWidget::renderer_available() const {
    return renderer_ready_ && volume_.is_valid() && !volume_dirty_;
}

QString AtomicOrbitalWidget::renderer_error() const {
    return renderer_error_;
}

QImage AtomicOrbitalWidget::capture_frame() {
    QImage image = grabFramebuffer();
    if (image.isNull() || !controls_panel_ || !controls_panel_->isVisible()) {
        return image;
    }
    const QImage controls = controls_panel_->grab().toImage();
    if (controls.isNull()) return image;

    const qreal scale_x = qreal(image.width()) / qreal(std::max(1, width()));
    const qreal scale_y = qreal(image.height()) / qreal(std::max(1, height()));
    const QRect geometry = controls_panel_->geometry();
    const QRectF target(
        geometry.x() * scale_x, geometry.y() * scale_y,
        geometry.width() * scale_x, geometry.height() * scale_y);
    QPainter painter(&image);
    painter.drawImage(target, controls);
    return image;
}

QImage AtomicOrbitalWidget::capture_poster(bool surface_only) {
  const QQuaternion saved_rotation = rotation_;
  const bool controls_visible = !controls_panel_->isHidden();
  poster_mode_ = true;
  poster_surface_only_ = surface_only;
  controls_panel_->hide();
  // Rotate tesseral families away from edge-on nodal planes. Spherical shells
  // keep their object axes aligned with the cutaway and the camera.
  rotation_ = definition_.l == 0 ? QQuaternion()
                                 : QQuaternion::fromEulerAngles(-12.0f - 4.0f * definition_.l,
                                                                9.0f * definition_.m, 22.0f)
                                       .normalized();
  const QImage image = grabFramebuffer();
  rotation_ = saved_rotation;
  poster_mode_ = false;
  poster_surface_only_ = false;
  controls_panel_->setVisible(controls_visible);
  update();
  return image;
}

void AtomicOrbitalWidget::reset_view() {
    rotation_ = QQuaternion::fromEulerAngles(-12.0f, 0.0f, 22.0f).normalized();
    zoom_factor_ = 1.0f;
    rotating_ = false;
    setCursor(Qt::OpenHandCursor);
    if (offset_slider_) {
        offset_slider_->setValue(int(std::lround(
            (definition_.offset_initial - definition_.offset_min)
            / (definition_.offset_max - definition_.offset_min) * kSliderSteps)));
    }
    update_offset_label();
}

void AtomicOrbitalWidget::initializeGL() {
    initializeOpenGLFunctions();
    disconnect(context_cleanup_connection_);
    context_cleanup_connection_ = connect(
        context(), &QOpenGLContext::aboutToBeDestroyed, this,
        [this] {
          makeCurrent();
          destroy_renderer();
          doneCurrent();
        },
        Qt::DirectConnection);
    renderer_ready_ = create_renderer();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    if (renderer_ready_ && volume_dirty_) upload_volume();
}

bool AtomicOrbitalWidget::create_renderer() {
    destroy_renderer();
    static constexpr char surface_vertex[] = R"glsl(#version 330 core
layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_normal;
uniform mat4 model_view_projection;
uniform mat4 model_view;
uniform mat3 normal_matrix;
out vec3 view_position;
out vec3 view_normal;
out vec3 object_position;
void main() {
    object_position = vertex_position;
    vec4 p = model_view * vec4(vertex_position, 1.0);
    view_position = p.xyz;
    view_normal = normalize(normal_matrix * vertex_normal);
    gl_Position = model_view_projection * vec4(vertex_position, 1.0);
})glsl";
    static constexpr char surface_fragment[] = R"glsl(#version 330 core
in vec3 view_position;
in vec3 view_normal;
in vec3 object_position;
uniform bool cutaway;
uniform vec3 base_color;
out vec4 fragment_color;
void main() {
    if (cutaway && object_position.x > 0.0 && object_position.y < 0.0) discard;
    vec3 n = normalize(view_normal);
    if (cutaway && !gl_FrontFacing) n = -n;
    vec3 light = normalize(vec3(-0.45, 0.65, 1.0));
    vec3 view_direction = normalize(-view_position);
    vec3 halfway = normalize(light + view_direction);
    float diffuse = max(dot(n, light), 0.0);
    float specular = pow(max(dot(n, halfway), 0.0), 38.0);
    vec3 linear_rgb = base_color * (0.24 + 0.74 * diffuse) + vec3(0.4 * specular);
    fragment_color = vec4(pow(clamp(linear_rgb, 0.0, 1.0), vec3(1.0 / 2.2)), 0.96);
})glsl";
    static constexpr char plane_vertex[] = R"glsl(#version 330 core
layout(location = 0) in vec3 vertex_position;
uniform mat4 model_view_projection;
out vec3 object_position;
void main() {
    object_position = vertex_position;
    gl_Position = model_view_projection * vec4(vertex_position, 1.0);
})glsl";
    static constexpr char sampling_functions[] = R"glsl(
vec3 plane_world(vec2 uv, int plane, float offset, float extent) {
    vec2 p = (uv * 2.0 - 1.0) * extent;
    if (plane == 0) return vec3(p.x, p.y, offset * extent);
    if (plane == 1) return vec3(p.x, offset * extent, p.y);
    return vec3(offset * extent, p.x, p.y);
}
vec4 sampled_color(vec3 object_position, sampler3D volume_texture,
                   sampler1D color_texture, float extent, float minimum_value,
                   float maximum_value, int contour_levels) {
    vec3 tc = object_position / (2.0 * extent) + vec3(0.5);
    if (any(lessThan(tc, vec3(0.0))) || any(greaterThan(tc, vec3(1.0)))) {
        return vec4(0.97, 0.97, 0.98, 1.0);
    }
    float value = texture(volume_texture, tc).r;
    float absolute_value = abs(value);
    float magnitude = absolute_value <= minimum_value ? 0.0
        : clamp(log(absolute_value / minimum_value)
                / log(maximum_value / minimum_value), 0.0, 1.0);
    float signed_value = sign(value) * magnitude;
    vec3 color = texture(color_texture, 0.5 + 0.5 * signed_value).rgb;
    if (absolute_value > minimum_value && absolute_value < maximum_value) {
        float log_position = log(absolute_value / minimum_value)
            / log(maximum_value / minimum_value);
        float line_distance = abs(fract(log_position * float(contour_levels)) - 0.5);
        if (line_distance > 0.46) color *= 0.58;
    }
    return vec4(color, 1.0);
}
)glsl";
    const QByteArray plane_fragment = QByteArrayLiteral("#version 330 core\n")
        + QByteArray(sampling_functions)
        + QByteArrayLiteral(R"glsl(
in vec3 object_position;
uniform sampler3D volume_texture;
uniform sampler1D color_texture;
uniform float extent;
uniform float minimum_value;
uniform float maximum_value;
uniform int contour_levels;
out vec4 fragment_color;
void main() {
    vec4 color = sampled_color(object_position, volume_texture, color_texture,
        extent, minimum_value, maximum_value, contour_levels);
    fragment_color = vec4(color.rgb, 0.58);
})glsl");
    static constexpr char contour_vertex[] = R"glsl(#version 330 core
out vec2 uv;
void main() {
    vec2 positions[4] = vec2[4](vec2(-1.0,-1.0), vec2(1.0,-1.0),
                                vec2(-1.0,1.0), vec2(1.0,1.0));
    uv = positions[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
})glsl";
    static constexpr char axis_vertex[] = R"glsl(#version 330 core
layout(location = 0) in vec3 vertex_position;
uniform mat4 model_view_projection;
void main() {
    gl_Position = model_view_projection * vec4(vertex_position, 1.0);
})glsl";
    static constexpr char axis_fragment[] = R"glsl(#version 330 core
out vec4 fragment_color;
void main() {
    fragment_color = vec4(0.025, 0.025, 0.025, 1.0);
})glsl";
    const QByteArray contour_fragment = QByteArrayLiteral("#version 330 core\n")
        + QByteArray(sampling_functions)
        + QByteArrayLiteral(R"glsl(
in vec2 uv;
uniform sampler3D volume_texture;
uniform sampler1D color_texture;
uniform float extent;
uniform float plane_offset;
uniform float minimum_value;
uniform float maximum_value;
uniform int contour_levels;
uniform int plane;
out vec4 fragment_color;
void main() {
    vec3 world = plane_world(uv, plane, plane_offset, extent);
    fragment_color = sampled_color(world, volume_texture, color_texture,
        extent, minimum_value, maximum_value, contour_levels);
})glsl");

    const auto make_program = [this](const QByteArray& vertex, const QByteArray& fragment) {
        auto program = std::make_unique<QOpenGLShaderProgram>();
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            || !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
            || !program->link()) {
            renderer_error_ = program->log();
            return std::unique_ptr<QOpenGLShaderProgram>();
        }
        return program;
    };
    surface_program_ = make_program(surface_vertex, surface_fragment);
    plane_program_ = make_program(plane_vertex, plane_fragment);
    contour_program_ = make_program(contour_vertex, contour_fragment);
    axis_program_ = make_program(axis_vertex, axis_fragment);
    if (!surface_program_ || !plane_program_ || !contour_program_ || !axis_program_) {
        qWarning().noquote() << "Atomic-orbital shader initialization failed:" << renderer_error_;
        destroy_renderer();
        return false;
    }
    if (!plane_vao_.create() || !plane_buffer_.create() || !quad_vao_.create()
        || !axis_vao_.create() || !axis_buffer_.create()) {
        renderer_error_ = QStringLiteral("Could not create atomic-orbital vertex arrays");
        destroy_renderer();
        return false;
    }
    {
        QOpenGLVertexArrayObject::Binder binder(&plane_vao_);
        plane_buffer_.bind();
        plane_buffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        plane_buffer_.allocate(6 * 3 * int(sizeof(float)));
        plane_program_->enableAttributeArray(0);
        plane_program_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * int(sizeof(float)));
        plane_buffer_.release();
    }
    {
        QOpenGLVertexArrayObject::Binder binder(&axis_vao_);
        axis_buffer_.bind();
        axis_buffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        axis_buffer_.allocate(32 * 3 * int(sizeof(float)));
        axis_program_->enableAttributeArray(0);
        axis_program_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * int(sizeof(float)));
        axis_buffer_.release();
    }
    return true;
}

void AtomicOrbitalWidget::destroy_renderer() {
  clear_gpu_cache();
  uploaded_bytes_ = 0;
  volume_dirty_ = volume_.is_valid();
  if (positive_mesh_) positive_mesh_->destroy();
  if (negative_mesh_) negative_mesh_->destroy();
  positive_mesh_.reset();
  negative_mesh_.reset();
  plane_vao_.destroy();
  quad_vao_.destroy();
  plane_buffer_.destroy();
  axis_vao_.destroy();
  axis_buffer_.destroy();
  if (volume_texture_) glDeleteTextures(1, &volume_texture_);
  if (colormap_texture_) glDeleteTextures(1, &colormap_texture_);
  volume_texture_ = 0;
  colormap_texture_ = 0;
  surface_program_.reset();
  plane_program_.reset();
  contour_program_.reset();
  axis_program_.reset();
  renderer_ready_ = false;
}

bool AtomicOrbitalWidget::upload_volume() {
    if (!volume_.is_valid() || !surface_program_) return false;
    // Remove the requested entry before eviction, so admitting the outgoing volume
    // cannot evict the very geometry we are about to display.
    std::unique_ptr<CachedGpuVolume> hit;
    for (auto it = gpu_cache_.begin(); it != gpu_cache_.end(); ++it) {
      if (same_geometry((*it)->definition, definition_)) {
        hit = std::move(*it);
        gpu_cache_bytes_ -= hit->bytes;
        gpu_cache_.erase(it);
        break;
      }
    }
    if (uploaded_bytes_ > 0 && uploaded_bytes_ <= kGpuCacheBudget) {
      while (gpu_cache_bytes_ + uploaded_bytes_ > kGpuCacheBudget && !gpu_cache_.empty()) {
        auto& oldest = gpu_cache_.front();
        glDeleteTextures(1, &oldest->texture);
        oldest->positive->destroy();
        oldest->negative->destroy();
        gpu_cache_bytes_ -= oldest->bytes;
        gpu_cache_.erase(gpu_cache_.begin());
      }
      auto outgoing = std::make_unique<CachedGpuVolume>();
      outgoing->definition = uploaded_definition_;
      outgoing->positive = std::move(positive_mesh_);
      outgoing->negative = std::move(negative_mesh_);
      outgoing->texture = std::exchange(volume_texture_, 0);
      outgoing->bytes = uploaded_bytes_;
      gpu_cache_bytes_ += outgoing->bytes;
      gpu_cache_.push_back(std::move(outgoing));
    }
    uploaded_bytes_ = 0;
    if (hit) {
      if (positive_mesh_) positive_mesh_->destroy();
      if (negative_mesh_) negative_mesh_->destroy();
      if (volume_texture_) glDeleteTextures(1, &volume_texture_);
      positive_mesh_ = std::move(hit->positive);
      negative_mesh_ = std::move(hit->negative);
      volume_texture_ = hit->texture;
      uploaded_bytes_ = hit->bytes;
      uploaded_definition_ = definition_;
      upload_colormap();
      volume_dirty_ = false;
      renderer_error_.clear();
      update_plane_buffer();
      return true;
    }

    if (positive_mesh_) positive_mesh_->destroy();
    if (negative_mesh_) negative_mesh_->destroy();
    positive_mesh_ = std::make_unique<Mesh>();
    negative_mesh_ = std::make_unique<Mesh>();
    const bool positive_ok = volume_.positive_vertices.isEmpty()
        || positive_mesh_->create(surface_program_.get(), volume_.positive_vertices);
    const bool negative_ok = volume_.negative_vertices.isEmpty()
        || negative_mesh_->create(surface_program_.get(), volume_.negative_vertices);
    if (!positive_ok || !negative_ok) {
        renderer_error_ = QStringLiteral("Could not upload atomic-orbital surface buffers");
        return false;
    }

    if (!volume_texture_) glGenTextures(1, &volume_texture_);
    glBindTexture(GL_TEXTURE_3D, volume_texture_);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    const float border[] = {0, 0, 0, 0};
    glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, border);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ++geometry_upload_count_;
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F,
                 volume_.grid_size, volume_.grid_size, volume_.grid_size,
                 0, GL_RED, GL_FLOAT, volume_.values.constData());

    uploaded_definition_ = definition_;
    uploaded_bytes_ = qint64(volume_.values.size()) * sizeof(float) +
                      qint64(volume_.positive_vertices.size() + volume_.negative_vertices.size()) *
                          sizeof(AtomicOrbitalVertex);
    glBindTexture(GL_TEXTURE_3D, 0);
    upload_colormap();
    volume_dirty_ = false;
    renderer_error_.clear();
    update_plane_buffer();
    return true;
}

void AtomicOrbitalWidget::upload_colormap() {
  const QVector<QColor> colors = atomic_orbital_colormap(definition_.colormap);
  QVector<quint8> rgba;
  rgba.reserve(colors.size() * 4);
  for (const QColor& color : colors) {
    rgba.append({quint8(color.red()), quint8(color.green()), quint8(color.blue()), quint8(255)});
  }
  if (!colormap_texture_) glGenTextures(1, &colormap_texture_);
  glBindTexture(GL_TEXTURE_1D, colormap_texture_);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, colors.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba.constData());
  glBindTexture(GL_TEXTURE_1D, 0);
}

void AtomicOrbitalWidget::update_plane_buffer() {
    if (!plane_buffer_.isCreated() || !volume_.is_valid()) return;
    const float e = volume_.half_extent;
    const float o = float(plane_offset()) * e;
    QVector3D a, b, c, d;
    if (definition_.plane == AtomicOrbitalDefinition::Plane::XY) {
        a = {-e, -e, o}; b = {e, -e, o}; c = {-e, e, o}; d = {e, e, o};
    } else if (definition_.plane == AtomicOrbitalDefinition::Plane::XZ) {
        a = {-e, o, -e}; b = {e, o, -e}; c = {-e, o, e}; d = {e, o, e};
    } else {
        a = {o, -e, -e}; b = {o, e, -e}; c = {o, -e, e}; d = {o, e, e};
    }
    const std::array<QVector3D, 6> vertices{{a, b, c, c, b, d}};
    plane_buffer_.bind();
    plane_buffer_.allocate(vertices.data(), int(sizeof(vertices)));
    plane_buffer_.release();
}

void AtomicOrbitalWidget::paintGL() {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glViewport(0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    if (poster_mode_)
      glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    else
      glClearColor(0.965f, 0.97f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!renderer_ready_ || !volume_.is_valid()) {
        draw_labels_and_colorbar();
        return;
    }
    if (volume_dirty_ && !upload_volume()) {
        draw_labels_and_colorbar();
        return;
    }

    const QRect left_viewport = to_gl_viewport(left_logical_viewport());
    glViewport(left_viewport.x(), left_viewport.y(), left_viewport.width(), left_viewport.height());
    glEnable(GL_DEPTH_TEST);
    const float aspect = left_viewport.height() > 0
        ? float(left_viewport.width()) / float(left_viewport.height()) : 1.0f;
    const float scale = volume_.half_extent * 1.28f * zoom_factor_;
    QMatrix4x4 projection;
    projection.ortho(-scale * aspect, scale * aspect, -scale, scale,
                     -volume_.half_extent * 10.0f, volume_.half_extent * 10.0f);
    QMatrix4x4 view;
    if (poster_mode_) {
      // Face the principal lobe plane, with a small oblique angle for depth.
      // Equatorial tesseral families need a view from above; xz/yz families
      // need a view from the perpendicular axis, not down one of their lobes.
      QVector3D camera(2.8f, -3.2f, 2.35f);
      if (definition_.l >= 2 && std::abs(definition_.m) == definition_.l) {
        camera = QVector3D(1.0f, -1.0f, 4.0f);
      } else if (definition_.m == -1) {
        camera = QVector3D(4.0f, -0.8f, 1.8f);
      } else if (definition_.m == 1) {
        camera = QVector3D(0.8f, -4.0f, 1.8f);
      } else if (definition_.l > 0 && definition_.m == 0) {
        camera = QVector3D(3.0f, -4.0f, 1.5f);
      }
      view.lookAt(rotation_.rotatedVector(camera) * volume_.half_extent, QVector3D(),
                  rotation_.rotatedVector(QVector3D(0, 0, 1)));
    } else {
      view.lookAt(QVector3D(2.8f, -3.2f, 2.35f) * volume_.half_extent, QVector3D(),
                  QVector3D(0, 0, 1));
    }
    QMatrix4x4 model;
    model.rotate(rotation_);
    if (poster_mode_) {
      float horizontal = 0.0f;
      float vertical = 0.0f;
      const QMatrix4x4 model_view = view * model;
      for (const auto* vertices : {&volume_.positive_vertices, &volume_.negative_vertices}) {
        for (const auto& vertex : *vertices) {
          const QVector3D point = model_view.map(vertex.position);
          horizontal = std::max(horizontal, std::abs(point.x()));
          vertical = std::max(vertical, std::abs(point.y()));
        }
      }
      const float fitted = std::max(vertical, horizontal / aspect) * 1.16f;
      projection.setToIdentity();
      projection.ortho(-fitted * aspect, fitted * aspect, -fitted, fitted,
                       -volume_.half_extent * 10.0f, volume_.half_extent * 10.0f);
    }
    if (positive_mesh_ && positive_mesh_->vertex_count > 0) {
        draw_surface(*positive_mesh_, definition_.positive_color, model, view, projection);
    }
    if (negative_mesh_ && negative_mesh_->vertex_count > 0) {
        draw_surface(*negative_mesh_, definition_.negative_color, model, view, projection);
    }
    if (!poster_mode_) {
      draw_sampling_plane(model, view, projection);
      draw_world_axes(model, view, projection);
    }

    const QRect right_viewport = to_gl_viewport(right_logical_viewport());
    if (!poster_mode_ || !poster_surface_only_) draw_contour(right_viewport);
    glViewport(0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    // Volume uploads use byte-aligned scalar rows. Restore Qt's expected
    // defaults before it uploads and samples the font glyph atlas.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    draw_labels_and_colorbar();
}

void AtomicOrbitalWidget::draw_surface(
    const Mesh& mesh,
    const QColor& color,
    const QMatrix4x4& model,
    const QMatrix4x4& view,
    const QMatrix4x4& projection) {
    surface_program_->bind();
    const QMatrix4x4 model_view = view * model;
    surface_program_->setUniformValue("model_view_projection", projection * model_view);
    surface_program_->setUniformValue("model_view", model_view);
    surface_program_->setUniformValue("normal_matrix", model_view.normalMatrix());
    surface_program_->setUniformValue("base_color", linear_color(color));
    surface_program_->setUniformValue("cutaway",
                                      poster_mode_ && definition_.l == 0 && definition_.n > 1);
    QOpenGLVertexArrayObject::Binder binder(const_cast<QOpenGLVertexArrayObject*>(&mesh.vao));
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertex_count);
    surface_program_->release();
}

void AtomicOrbitalWidget::draw_sampling_plane(
    const QMatrix4x4& model,
    const QMatrix4x4& view,
    const QMatrix4x4& projection) {
    update_plane_buffer();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    plane_program_->bind();
    plane_program_->setUniformValue(
        "model_view_projection", projection * view * model);
    plane_program_->setUniformValue("extent", volume_.half_extent);
    plane_program_->setUniformValue(
        "minimum_value", float(kAtomicOrbitalContourMinimum));
    plane_program_->setUniformValue("maximum_value", float(definition_.contour_maximum));
    plane_program_->setUniformValue("contour_levels", definition_.contour_levels);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, volume_texture_);
    plane_program_->setUniformValue("volume_texture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, colormap_texture_);
    plane_program_->setUniformValue("color_texture", 1);
    QOpenGLVertexArrayObject::Binder binder(&plane_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    plane_program_->release();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void AtomicOrbitalWidget::draw_contour(const QRect& pixel_viewport) {
    glViewport(pixel_viewport.x(), pixel_viewport.y(),
               pixel_viewport.width(), pixel_viewport.height());
    glDisable(GL_DEPTH_TEST);
    contour_program_->bind();
    contour_program_->setUniformValue("extent", volume_.half_extent);
    contour_program_->setUniformValue("plane_offset", float(plane_offset()));
    contour_program_->setUniformValue(
        "minimum_value", float(kAtomicOrbitalContourMinimum));
    contour_program_->setUniformValue("maximum_value", float(definition_.contour_maximum));
    contour_program_->setUniformValue("contour_levels", definition_.contour_levels);
    contour_program_->setUniformValue("plane", plane_index(definition_.plane));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, volume_texture_);
    contour_program_->setUniformValue("volume_texture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, colormap_texture_);
    contour_program_->setUniformValue("color_texture", 1);
    QOpenGLVertexArrayObject::Binder binder(&quad_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    contour_program_->release();
    glEnable(GL_DEPTH_TEST);
}

void AtomicOrbitalWidget::draw_world_axes(
    const QMatrix4x4& model,
    const QMatrix4x4& view,
    const QMatrix4x4& projection) {
    if (!axis_program_ || !axis_vao_.isCreated()) return;
    const float length = volume_.half_extent * 0.72f;
    const float glyph_size = volume_.half_extent * 0.052f;
    const float label_gap = volume_.half_extent * 0.045f;
    const QVector3D eye_direction = QVector3D(2.8f, -3.2f, 2.35f).normalized();
    const QVector3D forward = -eye_direction;
    const QVector3D right = QVector3D::crossProduct(
        forward, QVector3D(0, 0, 1)).normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    QVector<QVector3D> vertices;
    vertices.reserve(28);
    const auto line = [&vertices](const QVector3D& from, const QVector3D& to) {
        vertices.append(from);
        vertices.append(to);
    };
    const QVector3D origin;
    const QVector3D x_end(length, 0, 0);
    const QVector3D y_end(0, length, 0);
    const QVector3D z_end(0, 0, length);
    line(origin, x_end);
    line(origin, y_end);
    line(origin, z_end);

    const auto glyph_point = [&](const QVector3D& center, float x, float y) {
        return center + right * (x * glyph_size) + up * (y * glyph_size);
    };
    const QVector3D x_label = x_end + QVector3D(label_gap, 0, 0);
    line(glyph_point(x_label, -0.55f, -0.65f), glyph_point(x_label, 0.55f, 0.65f));
    line(glyph_point(x_label, -0.55f, 0.65f), glyph_point(x_label, 0.55f, -0.65f));

    const QVector3D y_label = y_end + QVector3D(0, label_gap, 0);
    line(glyph_point(y_label, -0.55f, 0.65f), glyph_point(y_label, 0.0f, 0.05f));
    line(glyph_point(y_label, 0.55f, 0.65f), glyph_point(y_label, 0.0f, 0.05f));
    line(glyph_point(y_label, 0.0f, 0.05f), glyph_point(y_label, 0.0f, -0.68f));

    const QVector3D z_label = z_end + QVector3D(0, 0, label_gap);
    line(glyph_point(z_label, -0.52f, 0.65f), glyph_point(z_label, 0.52f, 0.65f));
    line(glyph_point(z_label, 0.52f, 0.65f), glyph_point(z_label, -0.52f, -0.65f));
    line(glyph_point(z_label, -0.52f, -0.65f), glyph_point(z_label, 0.52f, -0.65f));

    axis_buffer_.bind();
    axis_buffer_.allocate(vertices.constData(), int(vertices.size() * sizeof(QVector3D)));
    axis_buffer_.release();

    glEnable(GL_DEPTH_TEST);
    glLineWidth(std::max(1.0f, float(devicePixelRatioF()) * 2.0f));
    axis_program_->bind();
    axis_program_->setUniformValue(
        "model_view_projection", projection * view * model);
    {
        QOpenGLVertexArrayObject::Binder binder(&axis_vao_);
        glDrawArrays(GL_LINES, 0, vertices.size());
    }
    axis_program_->release();
    glLineWidth(1.0f);
}

void AtomicOrbitalWidget::draw_labels_and_colorbar() {
  if (poster_mode_) return;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QColor(QStringLiteral("#1f2937")));
  QFont title_font = painter.font();
  title_font.setBold(true);
  title_font.setPixelSize(kTitlePixelSize);
  painter.setFont(title_font);
  ui_math_text::draw(painter, QRectF(kOuterMargin, 4, width() - 2 * kOuterMargin, 44),
                     definition_.title, Qt::AlignCenter, title_font,
                     QColor(QStringLiteral("#1f2937")));
  QFont label_font = painter.font();
  label_font.setBold(true);
  label_font.setPixelSize(kPanelLabelPixelSize);
  painter.setFont(label_font);
  ui_math_text::draw(
      painter, left_logical_viewport().adjusted(6, 2, -4, -3),
      QStringLiteral("%1 orbital + fixed $%2$ plane")
          .arg(orbital_math_label(definition_.orbital), plane_name(definition_.plane)),
      Qt::AlignLeft | Qt::AlignTop, label_font, QColor(QStringLiteral("#1f2937")));
  ui_math_text::draw(painter, right_logical_viewport().adjusted(6, 2, -4, -3),
                     QStringLiteral("Signed $\\psi$ contour ($a_0^{-3/2}$)"),
                     Qt::AlignLeft | Qt::AlignTop, label_font, QColor(QStringLiteral("#1f2937")));

  painter.setPen(QPen(QColor(75, 85, 99), 1.0));
  painter.drawRect(left_logical_viewport().adjusted(0, 0, -1, -1));
  painter.drawRect(right_logical_viewport().adjusted(0, 0, -1, -1));

  const QRect right = right_logical_viewport();
  const QRect bar(right.right() - 43, right.top() + 50, 16, std::max(10, right.height() - 72));
  const QVector<QColor> colors = atomic_orbital_colormap(definition_.colormap);
  for (int y = 0; y < bar.height(); ++y) {
    const int index = std::clamp(
        int(std::lround((1.0 - double(y) / std::max(1, bar.height() - 1)) * 255.0)), 0, 255);
    painter.setPen(colors.value(index, Qt::white));
    painter.drawLine(bar.left(), bar.top() + y, bar.right(), bar.top() + y);
  }
    painter.setPen(QColor(55, 65, 81));
    QFont legend_font = painter.font();
    legend_font.setBold(false);
    legend_font.setPixelSize(kLegendPixelSize);
    painter.setFont(legend_font);
    painter.drawRect(bar.adjusted(0, 0, -1, -1));
    const QRectF label_bounds(bar.left() - 106, 0, 98, 24);
    ui_math_text::draw(
        painter, label_bounds.translated(0, bar.top() - 10),
        signed_order_label(definition_.contour_maximum, true),
        Qt::AlignRight | Qt::AlignVCenter, legend_font, QColor(55, 65, 81));
    ui_math_text::draw(
        painter, label_bounds.translated(0, bar.center().y() - 30),
        signed_order_label(kAtomicOrbitalContourMinimum, true),
        Qt::AlignRight | Qt::AlignVCenter, legend_font, QColor(55, 65, 81));
    ui_math_text::draw(
        painter, label_bounds.translated(0, bar.center().y() + 6),
        signed_order_label(kAtomicOrbitalContourMinimum, false),
        Qt::AlignRight | Qt::AlignVCenter, legend_font, QColor(55, 65, 81));
    ui_math_text::draw(
        painter, QRectF(bar.right() + 5, bar.center().y() - 12, 22, 24),
        QStringLiteral("$0$"), Qt::AlignLeft | Qt::AlignVCenter,
        legend_font, QColor(55, 65, 81));
    ui_math_text::draw(
        painter, label_bounds.translated(0, bar.bottom() - 14),
        signed_order_label(definition_.contour_maximum, false),
        Qt::AlignRight | Qt::AlignVCenter, legend_font, QColor(55, 65, 81));
    if (!renderer_error_.isEmpty()) {
        painter.setPen(QColor(QStringLiteral("#b91c1c")));
        painter.drawText(rect().adjusted(20, 30, -20, -kControlHeight),
                         Qt::AlignCenter | Qt::TextWordWrap, renderer_error_);
    }
}

void AtomicOrbitalWidget::create_controls() {
    controls_panel_ = new QFrame(this);
    controls_panel_->setObjectName(QStringLiteral("atomicOrbitalControls"));
    controls_panel_->setStyleSheet(QStringLiteral(
        "#atomicOrbitalControls { background: #f1f5f9;"
        " border: 1px solid #cbd5e1; }"
        "#atomicOrbitalControls QLabel { color: #111827; background: transparent;"
        " border: none; padding: 0 3px; font-size: 26px; font-weight: 600; }"
        "QSlider#atomicOrbitalOffsetSlider { background: transparent; }"
        "QSlider#atomicOrbitalOffsetSlider::groove:horizontal {"
        " height: 9px; background: #cbd5e1; border-radius: 4px; }"
        "QSlider#atomicOrbitalOffsetSlider::sub-page:horizontal {"
        " background: #0891b2; border-radius: 2px; }"
        "QSlider#atomicOrbitalOffsetSlider::handle:horizontal {"
        " width: 24px; margin: -9px 0; background: #0e7490;"
        " border: 1px solid #155e75; border-radius: 12px; }"
        "#atomicOrbitalControls QPushButton { color: white; background: #0f766e;"
        " border: 1px solid #0f766e; font-size: 24px; font-weight: 600;"
        " padding: 9px 16px; min-width: 112px; }"
        "#atomicOrbitalControls QPushButton:hover { background: #0d9488;"
        " border-color: #0d9488; }"
        "#atomicOrbitalControls QPushButton[planeSelector=\"true\"] {"
        " min-width: 52px; max-width: 52px; padding: 9px 4px; }"
        "#atomicOrbitalControls QPushButton[planeSelector=\"true\"]:checked {"
        " background: #164e63; border-color: #083344; }"));
    auto* layout = new QHBoxLayout(controls_panel_);
    layout->setContentsMargins(16, 8, 16, 12);
    layout->setSpacing(14);
    offset_label_ = new QLabel(controls_panel_);
    offset_label_->setObjectName(QStringLiteral("atomicOrbitalOffsetLabel"));
    offset_label_->setMinimumWidth(270);
    offset_slider_ = new QSlider(Qt::Horizontal, controls_panel_);
    offset_slider_->setObjectName(QStringLiteral("atomicOrbitalOffsetSlider"));
    // The slider is pointer-controlled. Keeping keyboard focus on the audience
    // window lets arrow and page keys continue to navigate the presentation.
    offset_slider_->setFocusPolicy(Qt::NoFocus);
    offset_slider_->setRange(0, kSliderSteps);
    plane_button_group_ = new QButtonGroup(controls_panel_);
    plane_button_group_->setExclusive(true);
    const auto add_plane_button = [this, layout](
                                      const QString& text,
                                      const QString& object_name,
                                      AtomicOrbitalDefinition::Plane plane) {
        auto* button = new QPushButton(text, controls_panel_);
        button->setObjectName(object_name);
        button->setProperty("planeSelector", true);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        plane_button_group_->addButton(button, int(plane));
        layout->addWidget(button);
    };
    reset_button_ = new QPushButton(QStringLiteral("Reset"), controls_panel_);
    reset_button_->setObjectName(QStringLiteral("atomicOrbitalResetButton"));
    reset_button_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(offset_label_);
    layout->addWidget(offset_slider_, 1);
    add_plane_button(
        QStringLiteral("XY"), QStringLiteral("atomicOrbitalPlaneXYButton"),
        AtomicOrbitalDefinition::Plane::XY);
    add_plane_button(
        QStringLiteral("XZ"), QStringLiteral("atomicOrbitalPlaneXZButton"),
        AtomicOrbitalDefinition::Plane::XZ);
    add_plane_button(
        QStringLiteral("YZ"), QStringLiteral("atomicOrbitalPlaneYZButton"),
        AtomicOrbitalDefinition::Plane::YZ);
    layout->addWidget(reset_button_);
    update_plane_buttons();
    connect(offset_slider_, &QSlider::valueChanged, this, [this] {
        update_offset_label();
        if (context()) {
            makeCurrent();
            update_plane_buffer();
            doneCurrent();
        }
        update();
    });
    connect(plane_button_group_, &QButtonGroup::idClicked, this, [this](int id) {
        set_plane(AtomicOrbitalDefinition::Plane(id));
    });
    connect(reset_button_, &QPushButton::clicked, this, [this] {
        reset_view();
        update();
    });
}

void AtomicOrbitalWidget::set_plane(AtomicOrbitalDefinition::Plane plane) {
    if (definition_.plane == plane) {
        return;
    }

    definition_.plane = plane;
    update_plane_buttons();
    const double origin = std::clamp(
        0.0, definition_.offset_min, definition_.offset_max);
    offset_slider_->setValue(int(std::lround(
        (origin - definition_.offset_min)
        / (definition_.offset_max - definition_.offset_min) * kSliderSteps)));
    update_offset_label();
    if (context()) {
        makeCurrent();
        update_plane_buffer();
        doneCurrent();
    }
    update();
}

void AtomicOrbitalWidget::update_plane_buttons() {
    if (!plane_button_group_) {
        return;
    }
    if (QAbstractButton* button = plane_button_group_->button(int(definition_.plane))) {
        button->setChecked(true);
    }
}

void AtomicOrbitalWidget::position_controls() {
    if (!controls_panel_) return;
    controls_panel_->setGeometry(
        kOuterMargin, height() - kControlHeight - 6,
        std::max(1, width() - 2 * kOuterMargin), kControlHeight - 6);
    controls_panel_->raise();
}

void AtomicOrbitalWidget::update_offset_label() {
    if (!offset_label_) return;
    const double bohr_offset = volume_.is_valid()
        ? plane_offset() * double(volume_.half_extent) : 0.0;
    offset_label_->setText(QStringLiteral("%1 plane: %2 a₀")
        .arg(plane_name(definition_.plane))
        .arg(bohr_offset, 0, 'f', 2));
}

QRect AtomicOrbitalWidget::left_logical_viewport() const {
  if (poster_mode_) {
    const int panel_width = poster_surface_only_ ? width() : width() / 2 - 16;
    return QRect(16, 16, std::max(1, panel_width - 32), std::max(1, height() - 32));
  }
    const int available_width = std::max(2, width() - 2 * kOuterMargin - kPanelGap);
    const int left_width = available_width / 2;
    return QRect(kOuterMargin, kHeaderHeight, left_width,
                 std::max(1, height() - kHeaderHeight - kControlHeight - 8));
}

QRect AtomicOrbitalWidget::right_logical_viewport() const {
  if (poster_mode_) {
    const int side = std::max(1, std::min(width() / 2 - 48, height() - 32));
    return QRect(width() * 3 / 4 - side / 2, (height() - side) / 2, side, side);
  }
    const QRect left = left_logical_viewport();
    return QRect(left.right() + 1 + kPanelGap, left.top(),
                 std::max(1, width() - kOuterMargin - (left.right() + 1 + kPanelGap)),
                 left.height());
}

QRect AtomicOrbitalWidget::to_gl_viewport(const QRect& logical) const {
    const qreal ratio = devicePixelRatioF();
    return QRect(
        int(std::lround(logical.x() * ratio)),
        int(std::lround((height() - logical.y() - logical.height()) * ratio)),
        int(std::lround(logical.width() * ratio)),
        int(std::lround(logical.height() * ratio)));
}

void AtomicOrbitalWidget::resizeGL(int, int) {}

void AtomicOrbitalWidget::resizeEvent(QResizeEvent* event) {
    QOpenGLWidget::resizeEvent(event);
    position_controls();
}

void AtomicOrbitalWidget::contextMenuEvent(QContextMenuEvent* event) {
    event->accept();
}

void AtomicOrbitalWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        rotating_ = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton
        && left_logical_viewport().contains(event->position().toPoint())) {
        rotating_ = true;
        last_trackball_point_ = trackball_point(event->position());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void AtomicOrbitalWidget::mouseMoveEvent(QMouseEvent* event) {
    if (rotating_ && (event->buttons() & Qt::LeftButton)) {
        const QVector3D current = trackball_point(event->position());
        QVector3D axis_camera = QVector3D::crossProduct(last_trackball_point_, current);
        const float sine = axis_camera.length();
        const float cosine = std::clamp(
            QVector3D::dotProduct(last_trackball_point_, current), -1.0f, 1.0f);
        last_trackball_point_ = current;
        if (sine > 1.0e-6f) {
            axis_camera /= sine;
            const QVector3D eye_direction = QVector3D(2.8f, -3.2f, 2.35f).normalized();
            const QVector3D forward = -eye_direction;
            const QVector3D right = QVector3D::crossProduct(
                forward, QVector3D(0, 0, 1)).normalized();
            const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
            const QVector3D axis_world = (
                right * axis_camera.x() + up * axis_camera.y()
                + eye_direction * axis_camera.z()).normalized();
            const float angle = float(std::atan2(double(sine), double(cosine))
                                      * 180.0 / 3.14159265358979323846);
            rotation_ = (QQuaternion::fromAxisAndAngle(axis_world, angle)
                         * rotation_).normalized();
        }
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

QVector3D AtomicOrbitalWidget::trackball_point(const QPointF& position) const {
    const QRect viewport = left_logical_viewport();
    const QPointF center = viewport.center();
    const double radius = std::max(1.0, 0.5 * double(std::min(
        viewport.width(), viewport.height())));
    const float x = float((position.x() - center.x()) / radius);
    const float y = float((center.y() - position.y()) / radius);
    const float radius_squared = x * x + y * y;
    const float z = radius_squared <= 0.5f
        ? std::sqrt(std::max(0.0f, 1.0f - radius_squared))
        : 0.5f / std::sqrt(radius_squared);
    return QVector3D(x, y, z).normalized();
}

void AtomicOrbitalWidget::mouseReleaseEvent(QMouseEvent* event) {
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

void AtomicOrbitalWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        reset_view();
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void AtomicOrbitalWidget::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y() != 0
        ? event->angleDelta().y() : event->pixelDelta().y();
    if (delta != 0) {
        zoom_factor_ = std::clamp(
            zoom_factor_ * std::exp(-float(delta) * 0.0015f), 0.45f, 2.8f);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::wheelEvent(event);
}
