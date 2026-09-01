#include "ui/molecule_widget.hpp"

#include <QColor>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace {
struct ProjectedAtom {
    int atom_index = -1;
    QPointF center;
    float depth = 0.0f;
    float radius = 1.0f;
};

/** @brief Returns the conventional display color for a chemical element. */
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

/** @brief Returns a visual ball radius in molecular coordinate units. */
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

/** @brief Produces a lighter version of a color without changing its alpha. */
QColor highlighted(QColor color, int amount) {
    const int alpha = color.alpha();
    color = color.lighter(amount);
    color.setAlpha(alpha);
    return color;
}
}  // namespace

MoleculeWidget::MoleculeWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(48, 48);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    reset_view();
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

void MoleculeWidget::initializeGL() {}

void MoleculeWidget::paintGL() {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient background(0, 0, 0, height());
    background.setColorAt(0.0, QColor(246, 248, 251));
    background.setColorAt(1.0, QColor(222, 226, 233));
    painter.fillRect(rect(), background);

    if (!geometry_.is_valid() || width() <= 0 || height() <= 0) {
        painter.setPen(QColor(80, 85, 95));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Molecule unavailable"));
        return;
    }

    const float scale = 0.43f * float(std::min(width(), height())) * zoom_ / bounding_radius_;
    float largest_display_radius = 0.0f;
    for (const MoleculeAtom& atom : geometry_.atoms) {
        largest_display_radius =
            std::max(largest_display_radius, element_display_radius(atom.element));
    }
    const float maximum_screen_radius =
        std::clamp(0.065f * float(std::min(width(), height())), 8.0f, 64.0f);
    const float atom_radius_scale = largest_display_radius > 0.0f
        ? std::min(scale, maximum_screen_radius / largest_display_radius)
        : scale;
    const QPointF viewport_center(width() * 0.5, height() * 0.5);
    QVector<ProjectedAtom> projected;
    projected.reserve(geometry_.atoms.size());
    for (int atom_index = 0; atom_index < geometry_.atoms.size(); ++atom_index) {
        const MoleculeAtom& atom = geometry_.atoms.at(atom_index);
        const QVector3D position = rotation_.rotatedVector(atom.position - center_);
        projected.push_back(ProjectedAtom{
            atom_index,
            viewport_center + QPointF(position.x() * scale, -position.y() * scale),
            position.z(),
            std::max(element_display_radius(atom.element) * atom_radius_scale, 4.0f),
        });
    }

    QVector<int> atom_order(projected.size());
    std::iota(atom_order.begin(), atom_order.end(), 0);
    std::sort(atom_order.begin(), atom_order.end(), [&projected](int lhs, int rhs) {
        return projected.at(lhs).depth < projected.at(rhs).depth;
    });

    QVector<int> bond_order(geometry_.bonds.size());
    std::iota(bond_order.begin(), bond_order.end(), 0);
    std::sort(bond_order.begin(), bond_order.end(), [this, &projected](int lhs, int rhs) {
        const MoleculeBond& left = geometry_.bonds.at(lhs);
        const MoleculeBond& right = geometry_.bonds.at(rhs);
        const float left_depth =
            projected.at(left.first_atom).depth + projected.at(left.second_atom).depth;
        const float right_depth =
            projected.at(right.first_atom).depth + projected.at(right.second_atom).depth;
        return left_depth < right_depth;
    });

    const qreal bond_width = std::clamp<qreal>(scale * 0.13, 2.0, 16.0);
    for (int bond_index : std::as_const(bond_order)) {
        const MoleculeBond& bond = geometry_.bonds.at(bond_index);
        if (bond.first_atom < 0 || bond.second_atom < 0
            || bond.first_atom >= projected.size() || bond.second_atom >= projected.size()) {
            continue;
        }

        const ProjectedAtom& first = projected.at(bond.first_atom);
        const ProjectedAtom& second = projected.at(bond.second_atom);
        const QPointF midpoint = (first.center + second.center) * 0.5;
        QPen first_pen(element_color(geometry_.atoms.at(bond.first_atom).element), bond_width,
                       Qt::SolidLine, Qt::RoundCap);
        QPen second_pen(element_color(geometry_.atoms.at(bond.second_atom).element), bond_width,
                        Qt::SolidLine, Qt::RoundCap);
        painter.setPen(QPen(QColor(55, 58, 64, 100), bond_width + 2.0, Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(first.center, second.center);
        painter.setPen(first_pen);
        painter.drawLine(first.center, midpoint);
        painter.setPen(second_pen);
        painter.drawLine(midpoint, second.center);
    }

    painter.setPen(Qt::NoPen);
    for (int projected_index : std::as_const(atom_order)) {
        const ProjectedAtom& atom_projection = projected.at(projected_index);
        const QColor base_color = element_color(
            geometry_.atoms.at(atom_projection.atom_index).element);
        const QRectF atom_rect(
            atom_projection.center.x() - atom_projection.radius,
            atom_projection.center.y() - atom_projection.radius,
            atom_projection.radius * 2.0,
            atom_projection.radius * 2.0);
        QRadialGradient gradient(
            atom_projection.center
                + QPointF(-atom_projection.radius * 0.30, -atom_projection.radius * 0.35),
            atom_projection.radius * 1.25);
        gradient.setColorAt(0.0, highlighted(base_color, 165));
        gradient.setColorAt(0.48, base_color);
        gradient.setColorAt(1.0, base_color.darker(175));
        painter.setBrush(gradient);
        painter.drawEllipse(atom_rect);
    }

    painter.setPen(QPen(QColor(55, 60, 70, 120), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
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
        rotation_ = yaw * pitch * rotation_;
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
    const int delta = event->angleDelta().y();
    if (delta != 0) {
        zoom_ = std::clamp(zoom_ * std::pow(1.0015f, float(delta)), 0.35f, 4.0f);
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
    rotation_ = QQuaternion::fromEulerAngles(0.0f, 0.0f, 18.0f);
    zoom_ = 1.0f;
    rotating_ = false;
    setCursor(Qt::OpenHandCursor);
}
