#pragma once

#include "molecule/molecule_geometry.hpp"

#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPointF>
#include <QQuaternion>
#include <QString>

#include <memory>

class QMouseEvent;
class QOpenGLShaderProgram;
class QWheelEvent;

/** @brief Displays an interactive ball-and-stick molecular geometry. */
class MoleculeWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    /** @brief Constructs a molecular rendering surface. */
    explicit MoleculeWidget(QWidget* parent = nullptr);
    ~MoleculeWidget() override;

    /** @brief Replaces the displayed molecular geometry and resets the view. */
    void set_geometry(const MoleculeGeometry& geometry);

protected:
    /** @brief Initializes the OpenGL-backed widget. */
    void initializeGL() override;
    /** @brief Paints the current molecular geometry. */
    void paintGL() override;
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
    void draw_mesh(
        Mesh& mesh,
        const QMatrix4x4& model,
        const QVector3D& color,
        const QMatrix4x4& view,
        const QMatrix4x4& projection);

    MoleculeGeometry geometry_;
    QVector3D center_;
    QQuaternion rotation_;
    QPointF last_mouse_position_;
    float bounding_radius_ = 1.0f;
    float camera_distance_factor_ = 1.0f;
    bool rotating_ = false;
    bool renderer_ready_ = false;
    QString renderer_error_;
    std::unique_ptr<QOpenGLShaderProgram> shader_program_;
    std::unique_ptr<Mesh> sphere_mesh_;
    std::unique_ptr<Mesh> cylinder_mesh_;
};
