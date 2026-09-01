#include "ui/molecule_widget.hpp"

#include "molecule/molecule_camera.hpp"

#include <QColor>
#include <QDebug>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QSurfaceFormat>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kBondRadius = 0.065f;
constexpr int kSphereStacks = 24;
constexpr int kSphereSlices = 32;
constexpr int kCylinderSlices = 24;

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
        return channel <= 0.04045f
            ? channel / 12.92f
            : std::pow((channel + 0.055f) / 1.055f, 2.4f);
    };
    return QVector3D(
        linear_channel(float(color.redF())),
        linear_channel(float(color.greenF())),
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
            const QVector3D normal(
                ring_radius * std::cos(theta),
                y,
                ring_radius * std::sin(theta));
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
        append_vertex(
            vertices,
            QVector3D(std::cos(theta), std::sin(theta), 0.0f),
            QVector3D(0.0f, 0.0f, -1.0f));
    }

    const quint32 top_center = quint32(vertices->size());
    append_vertex(vertices, QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 0.0f, 1.0f));
    const quint32 top_ring = quint32(vertices->size());
    for (int slice = 0; slice <= kCylinderSlices; ++slice) {
        const float theta = 2.0f * kPi * float(slice) / float(kCylinderSlices);
        append_vertex(
            vertices,
            QVector3D(std::cos(theta), std::sin(theta), 1.0f),
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
}  // namespace

struct MoleculeWidget::Mesh {
    QOpenGLBuffer vertex_buffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer index_buffer{QOpenGLBuffer::IndexBuffer};
    int index_count = 0;

    bool create(const QVector<MeshVertex>& vertices, const QVector<quint32>& indices) {
        if (!vertex_buffer.create() || !index_buffer.create() || !vertex_buffer.bind()) {
            return false;
        }
        vertex_buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        vertex_buffer.allocate(vertices.constData(), int(vertices.size() * sizeof(MeshVertex)));
        vertex_buffer.release();

        if (!index_buffer.bind()) {
            return false;
        }
        index_buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        index_buffer.allocate(indices.constData(), int(indices.size() * sizeof(quint32)));
        index_buffer.release();
        index_count = indices.size();
        return index_count > 0;
    }

    void destroy() {
        vertex_buffer.destroy();
        index_buffer.destroy();
        index_count = 0;
    }
};

MoleculeWidget::MoleculeWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    QSurfaceFormat surface_format = format();
    surface_format.setDepthBufferSize(24);
    surface_format.setSamples(4);
    setFormat(surface_format);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(48, 48);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    reset_view();
}

MoleculeWidget::~MoleculeWidget() {
    if (context() && context()->isValid()) {
        makeCurrent();
        destroy_renderer();
        doneCurrent();
    }
}

void MoleculeWidget::set_geometry(const MoleculeGeometry& geometry) {
    geometry_ = geometry;
    center_ = {};
    if (!geometry_.atoms.isEmpty()) {
        for (const MoleculeAtom& atom : geometry_.atoms) {
            center_ += atom.position;
        }
        center_ /= float(geometry_.atoms.size());
    }

    bounding_radius_ = 1.0f;
    for (const MoleculeAtom& atom : geometry_.atoms) {
        bounding_radius_ = std::max(
            bounding_radius_,
            (atom.position - center_).length() + element_display_radius(atom.element));
    }
    reset_view();
    update();
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

    static constexpr char kVertexShader[] = R"(
        attribute vec3 vertex_position;
        attribute vec3 vertex_normal;
        uniform mat4 model_view_projection;
        uniform mat4 model_view;
        uniform mat3 normal_matrix;
        varying vec3 view_position;
        varying vec3 view_normal;
        void main() {
            vec4 position = model_view * vec4(vertex_position, 1.0);
            view_position = position.xyz;
            view_normal = normalize(normal_matrix * vertex_normal);
            gl_Position = model_view_projection * vec4(vertex_position, 1.0);
        }
    )";
    static constexpr char kFragmentShader[] = R"(
        uniform vec3 base_color;
        varying vec3 view_position;
        varying vec3 view_normal;
        void main() {
            vec3 normal = normalize(view_normal);
            vec3 light = normalize(vec3(-0.45, 0.65, 1.0));
            vec3 view_direction = normalize(-view_position);
            vec3 half_direction = normalize(light + view_direction);
            float diffuse = max(dot(normal, light), 0.0);
            float specular = pow(max(dot(normal, half_direction), 0.0), 42.0);
            vec3 linear_rgb = base_color * (0.25 + 0.72 * diffuse) + vec3(0.45 * specular);
            vec3 display_rgb = pow(clamp(linear_rgb, 0.0, 1.0), vec3(1.0 / 2.2));
            gl_FragColor = vec4(display_rgb, 1.0);
        }
    )";

    shader_program_ = std::make_unique<QOpenGLShaderProgram>();
    if (!shader_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_program_->link()) {
        renderer_error_ = shader_program_->log();
        qWarning().noquote() << "Molecule shader initialization failed:" << renderer_error_;
        shader_program_.reset();
        return false;
    }

    QVector<MeshVertex> sphere_vertices;
    QVector<quint32> sphere_indices;
    create_sphere_geometry(&sphere_vertices, &sphere_indices);
    sphere_mesh_ = std::make_unique<Mesh>();
    if (!sphere_mesh_->create(sphere_vertices, sphere_indices)) {
        renderer_error_ = QStringLiteral("Could not create sphere mesh buffers");
        destroy_renderer();
        return false;
    }

    QVector<MeshVertex> cylinder_vertices;
    QVector<quint32> cylinder_indices;
    create_cylinder_geometry(&cylinder_vertices, &cylinder_indices);
    cylinder_mesh_ = std::make_unique<Mesh>();
    if (!cylinder_mesh_->create(cylinder_vertices, cylinder_indices)) {
        renderer_error_ = QStringLiteral("Could not create cylinder mesh buffers");
        destroy_renderer();
        return false;
    }
    return true;
}

void MoleculeWidget::destroy_renderer() {
    if (sphere_mesh_) {
        sphere_mesh_->destroy();
        sphere_mesh_.reset();
    }
    if (cylinder_mesh_) {
        cylinder_mesh_->destroy();
        cylinder_mesh_.reset();
    }
    shader_program_.reset();
    renderer_ready_ = false;
}

void MoleculeWidget::paintGL() {
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

    const float aspect = float(width()) / float(height());
    const float camera_distance = molecule_camera::distance(
        bounding_radius_,
        aspect,
        camera_distance_factor_);
    const float near_plane = std::max(0.01f, camera_distance - bounding_radius_ * 1.25f);
    const float far_plane = camera_distance + bounding_radius_ * 1.75f;

    QMatrix4x4 projection;
    projection.perspective(
        molecule_camera::kVerticalFieldOfViewDegrees,
        aspect,
        near_plane,
        far_plane);
    QMatrix4x4 view;
    view.lookAt(
        QVector3D(0.0f, 0.0f, camera_distance),
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 1.0f, 0.0f));
    QMatrix4x4 scene_model;
    scene_model.rotate(rotation_);

    shader_program_->bind();
    for (const MoleculeBond& bond : geometry_.bonds) {
        if (bond.first_atom < 0 || bond.second_atom < 0
            || bond.first_atom >= geometry_.atoms.size()
            || bond.second_atom >= geometry_.atoms.size()) {
            continue;
        }

        const QVector3D first = geometry_.atoms.at(bond.first_atom).position - center_;
        const QVector3D second = geometry_.atoms.at(bond.second_atom).position - center_;
        const QVector3D midpoint = (first + second) * 0.5f;
        const QVector3D segment_starts[] = {first, midpoint};
        const QVector3D segment_ends[] = {midpoint, second};
        const int atom_indices[] = {bond.first_atom, bond.second_atom};
        for (int segment = 0; segment < 2; ++segment) {
            const QVector3D direction = segment_ends[segment] - segment_starts[segment];
            const float length = direction.length();
            if (length <= 0.0001f) {
                continue;
            }
            QMatrix4x4 model = scene_model;
            model.translate(segment_starts[segment]);
            model.rotate(QQuaternion::rotationTo(
                QVector3D(0.0f, 0.0f, 1.0f),
                direction / length));
            model.scale(kBondRadius, kBondRadius, length);
            draw_mesh(
                *cylinder_mesh_,
                model,
                linear_color(element_color(geometry_.atoms.at(atom_indices[segment]).element)),
                view,
                projection);
        }
    }

    for (const MoleculeAtom& atom : geometry_.atoms) {
        QMatrix4x4 model = scene_model;
        model.translate(atom.position - center_);
        const float radius = element_display_radius(atom.element);
        model.scale(radius, radius, radius);
        draw_mesh(
            *sphere_mesh_,
            model,
            linear_color(element_color(atom.element)),
            view,
            projection);
    }
    shader_program_->release();
}

void MoleculeWidget::draw_mesh(
    Mesh& mesh,
    const QMatrix4x4& model,
    const QVector3D& color,
    const QMatrix4x4& view,
    const QMatrix4x4& projection) {
    const QMatrix4x4 model_view = view * model;
    shader_program_->setUniformValue("model_view_projection", projection * model_view);
    shader_program_->setUniformValue("model_view", model_view);
    shader_program_->setUniformValue("normal_matrix", model_view.normalMatrix());
    shader_program_->setUniformValue("base_color", color);

    mesh.vertex_buffer.bind();
    mesh.index_buffer.bind();
    const int position_location = shader_program_->attributeLocation("vertex_position");
    const int normal_location = shader_program_->attributeLocation("vertex_normal");
    shader_program_->enableAttributeArray(position_location);
    shader_program_->enableAttributeArray(normal_location);
    shader_program_->setAttributeBuffer(
        position_location,
        GL_FLOAT,
        int(offsetof(MeshVertex, position)),
        3,
        sizeof(MeshVertex));
    shader_program_->setAttributeBuffer(
        normal_location,
        GL_FLOAT,
        int(offsetof(MeshVertex, normal)),
        3,
        sizeof(MeshVertex));
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    shader_program_->disableAttributeArray(position_location);
    shader_program_->disableAttributeArray(normal_location);
    mesh.index_buffer.release();
    mesh.vertex_buffer.release();
}

void MoleculeWidget::mousePressEvent(QMouseEvent* event) {
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
            QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, float(delta.x()) * 0.65f);
        const QQuaternion pitch =
            QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, float(delta.y()) * 0.65f);
        rotation_ = (yaw * pitch * rotation_).normalized();
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void MoleculeWidget::mouseReleaseEvent(QMouseEvent* event) {
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
        camera_distance_factor_ = molecule_camera::zoomed_distance_factor(
            camera_distance_factor_,
            angle_delta_y,
            pixel_delta_y);
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
    rotation_ = QQuaternion::fromEulerAngles(0.0f, 0.0f, 18.0f).normalized();
    camera_distance_factor_ = 1.0f;
    rotating_ = false;
    setCursor(Qt::OpenHandCursor);
}
