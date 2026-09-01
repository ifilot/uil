#pragma once

#include "molecule/molecule_geometry.hpp"

#include <QOpenGLWidget>
#include <QPointF>
#include <QQuaternion>

class QMouseEvent;
class QWheelEvent;

/** @brief Displays an interactive ball-and-stick molecular geometry. */
class MoleculeWidget final : public QOpenGLWidget {
public:
    /** @brief Constructs a molecular rendering surface. */
    explicit MoleculeWidget(QWidget* parent = nullptr);

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
    /** @brief Restores the default orientation and zoom. */
    void reset_view();

    MoleculeGeometry geometry_;
    QVector3D center_;
    QQuaternion rotation_;
    QPointF last_mouse_position_;
    float bounding_radius_ = 1.0f;
    float zoom_ = 1.0f;
    bool rotating_ = false;
};
