#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QQuaternion>
#include <QRect>
#include <QString>
#include <QVector>
#include <array>
#include <functional>
#include <memory>

#include "molecule/molecule_geometry.hpp"
#include "orbital/symmetry_orbitals.hpp"

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

/**
 * @brief Displays an interactive ball-and-stick molecular geometry.
 *
 * Renders with an OpenGL 3.3 core context owned by QOpenGLWidget. All methods must be called on
 * the GUI thread. GL resources are created in initializeGL() and released when the context is
 * about to be destroyed (for example when the widget moves to another top-level window), so the
 * widget can be reparented safely. Animations advance by measured elapsed time, not per tick.
 */
class MoleculeWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    /** @brief Stereo output modes supported by the embedded renderer. */
    enum class StereoMode { Mono, RedCyanAnaglyph };

    /** @brief Symmetry element drawn with the molecule in its local coordinates. */
    enum class SymmetryElement {
        None,
        RotationAxis,
        MirrorPlane,
        InversionCenter,
        ImproperAxisAndPlane,
    };

    /** @brief Constructs a molecular rendering surface. */
    explicit MoleculeWidget(QWidget* parent = nullptr);
    /** @brief Stops animation timers and releases GL resources under this widget's context. */
    ~MoleculeWidget() override;

    /** @brief Replaces the displayed molecular geometry and resets the view. */
    void set_geometry(const MoleculeGeometry& geometry);
    /** @brief Selects atom-centered baked orbitals; invalid selections are ignored. */
    void set_orbitals(const QVector<SymmetryOrbital>& orbitals);
    /** @brief Returns the selected atom-centered orbitals. */
    const QVector<SymmetryOrbital>& orbitals() const;
    /** @brief Selects mono or red/cyan anaglyph output. */
    void set_stereo_mode(StereoMode mode);
    /** @brief Returns the active stereo output mode. */
    StereoMode stereo_mode() const;
    /** @brief Shows or hides the orientation-axis gizmo. */
    void set_axes_visible(bool visible);
    /** @brief Returns whether the orientation-axis gizmo is visible. */
    bool axes_visible() const;
    /** @brief Shows Cartesian axes through the molecular origin in scene coordinates. */
    void set_world_axes_visible(bool visible);
    /** @brief Returns whether molecule-fixed Cartesian axes are visible. */
    bool world_axes_visible() const;
    /**
     * @brief Starts or pauses normal-mode animation when displacement data exists.
     *
     * Requests to play a geometry without vibration vectors are ignored. Hiding the widget pauses
     * playback; it is not resumed automatically.
     */
    void set_vibration_playing(bool playing);
    /** @brief Returns whether normal-mode animation is running. */
    bool vibration_playing() const;
    /**
     * @brief Enables or disables continuous rotation around the molecule's Z axis.
     *
     * The setting persists while hidden: the timer stops on hide and resumes on show.
     */
    void set_auto_rotation_enabled(bool enabled);
    /** @brief Returns whether continuous Z-axis rotation is enabled. */
    bool auto_rotation_enabled() const;
    /** @brief Expands or collapses the floating molecule toolbar. */
    void set_toolbar_expanded(bool expanded);
    /** @brief Returns whether the floating molecule toolbar is expanded. */
    bool toolbar_expanded() const;
    /** @brief Sets the molecule-coordinate origin used by operations and world axes. */
    void set_coordinate_origin(const QVector3D& origin);
    /** @brief Returns the configured molecule-coordinate origin. */
    QVector3D coordinate_origin() const;
    /** @brief Applies a temporary origin-centered transform to atom coordinates. */
    void set_coordinate_transform(const QMatrix4x4& transform);
    /** @brief Restores untransformed atom coordinates. */
    void clear_coordinate_transform();
    /** @brief Squashes atom surfaces along a mirror normal without moving their centers. */
    void set_atom_reflection_shape(const QVector3D& normal, double progress);
    /** @brief Shows the untransformed molecule as a translucent reference pose. */
    void set_reference_geometry_visible(bool visible);
    /** @brief Returns whether the translucent reference pose is enabled. */
    bool reference_geometry_visible() const;
    /** @brief Sets the opacity of the untransformed reference pose. */
    void set_reference_geometry_opacity(float opacity);
    /** @brief Returns the opacity of the untransformed reference pose. */
    float reference_geometry_opacity() const;
    /** @brief Shows or hides the built-in molecule toolbar and toggle. */
    void set_builtin_controls_visible(bool visible);
    /** @brief Sets and applies the orientation restored by reset interactions. */
    void set_default_view_rotation(const QQuaternion& rotation);
    /** @brief Sets and applies the camera-distance factor restored by reset interactions. */
    void set_default_camera_distance_factor(float factor);
    /** @brief Returns the current camera-distance factor. */
    float camera_distance_factor() const;
    /** @brief Returns the current molecule orientation. */
    QQuaternion view_rotation() const;
    /** @brief Restores a previously captured orientation and zoom. */
    void set_camera_view(const QQuaternion& rotation, float distance_factor);
    /** @brief Restores the default view and zoom without changing molecular state. */
    void reset_camera();
    /** @brief Shows a symmetry element whose axis is expressed in molecule coordinates. */
    void set_symmetry_element(SymmetryElement element, QVector3D axis = {},
                              QColor color = QColor(145, 43, 67));
    /** @brief Returns the currently displayed symmetry element. */
    SymmetryElement symmetry_element() const;
    /** @brief Returns the normalized rotation axis or mirror-plane normal. */
    QVector3D symmetry_element_axis() const;
    /** @brief Returns the configured symmetry-element color. */
    QColor symmetry_element_color() const;
    /** @brief Delegates right-click menu requests to the presentation window. */
    void set_context_menu_handler(std::function<void(const QPoint&)> handler);

protected:
    /**
     * @brief Creates shaders and meshes for the (possibly recreated) current context.
     *
     * Also (re)connects QOpenGLContext::aboutToBeDestroyed so resources are released under the
     * context that owns them. Called by Qt with the context current.
     */
    void initializeGL() override;
    /** @brief Paints the current molecular geometry; called by Qt with the context current. */
    void paintGL() override;
    /** @brief Keeps the floating toolbar anchored to the top-right corner. */
    void resizeEvent(QResizeEvent* event) override;
    /**
     * @brief Stops animation timers while the molecule overlay is not visible.
     *
     * vibration_playing() reports false while hidden; playback that was running resumes on show.
     */
    void hideEvent(QHideEvent* event) override;
    /** @brief Resumes automatic rotation and any vibration that was playing when hidden. */
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
    /**
     * @brief Creates shader and mesh resources for the current context.
     * @return False, with renderer_error_ set, when a shader or buffer cannot be created.
     * Requires the widget's OpenGL context to be current.
     */
    bool create_renderer();
    /**
     * @brief Releases shader, mesh, and cached orbital buffers and marks the renderer unready.
     *
     * The caller must make the owning context current first; the destructor and the
     * aboutToBeDestroyed hook installed by initializeGL() both do so.
     */
    void destroy_renderer();
    /**
     * @brief Advances continuous rotation and normal-mode phase by the elapsed wall time.
     *
     * Driven by the animation timers; gaps longer than a few frames are clamped so the molecule
     * never jumps after a stall. Schedules a repaint.
     */
    void advance_animation();
    /**
     * @brief Starts @p timer, restarting the animation clock if no animation was running.
     *
     * This keeps the first frame after a pause or hide from replaying the idle interval.
     */
    void start_animation_timer(QTimer* timer);
    /** @brief Draws one transformed and colored mesh; requires a bound shader program. */
    void draw_mesh(Mesh& mesh, const QMatrix4x4& model, const QVector3D& color,
                   const QMatrix4x4& view, const QMatrix4x4& projection, float opacity = 1.0f,
                   bool unlit = false, bool cutaway = false, bool neon = false);
    /** @brief Draws one eye into the supplied framebuffer viewport. */
    void draw_eye(const QRect& pixel_viewport, float eye_offset,
                  const QVector<QVector3D>& positions);
    /** @brief Paints a Blender-style orientation gizmo over the OpenGL scene. */
    void draw_axis_gizmo(QPainter* painter);
    /** @brief Creates and connects the collapsible toolbar controls. */
    void create_toolbar();
    /** @brief Repositions the toolbar after a resize or expansion change. */
    void position_toolbar();
    /** @brief Refreshes toolbar icons, checks, labels, and enabled states. */
    void update_toolbar_state();

    MoleculeGeometry geometry_;
    QVector<SymmetryOrbital> orbitals_;
    std::array<std::unique_ptr<Mesh>, 20> orbital_meshes_;
    QVector3D center_;
    QVector3D coordinate_origin_;
    QQuaternion default_rotation_ = QQuaternion::fromEulerAngles(0.0f, 0.0f, 18.0f).normalized();
    QQuaternion rotation_;
    QMatrix4x4 coordinate_transform_;
    QMatrix4x4 atom_shape_transform_;
    SymmetryElement symmetry_element_ = SymmetryElement::None;
    QVector3D symmetry_element_axis_{0.0f, 0.0f, 1.0f};
    QColor symmetry_element_color_{145, 43, 67};
    QPointF last_mouse_position_;
    float bounding_radius_ = 1.0f;
    float camera_distance_factor_ = 1.0f;
    float default_camera_distance_factor_ = 1.0f;
    float vibration_phase_ = 0.0f;
    bool rotating_ = false;
    bool renderer_ready_ = false;
    bool axes_visible_ = true;
    bool world_axes_visible_ = false;
    bool toolbar_expanded_ = true;
    bool builtin_controls_visible_ = true;
    bool auto_rotation_enabled_ = false;
    // Set by hideEvent() when vibration was playing; cleared when a new geometry is loaded.
    bool resume_vibration_on_show_ = false;
    bool reference_geometry_visible_ = false;
    float reference_geometry_opacity_ = 0.5f;
    StereoMode stereo_mode_ = StereoMode::Mono;
    QString renderer_error_;
    std::unique_ptr<QOpenGLShaderProgram> shader_program_;
    std::unique_ptr<Mesh> sphere_mesh_;
    std::unique_ptr<Mesh> cylinder_mesh_;
    std::unique_ptr<Mesh> disc_mesh_;
    QFrame* toolbar_panel_ = nullptr;
    QToolButton* toolbar_toggle_button_ = nullptr;
    QToolButton* stereo_button_ = nullptr;
    QToolButton* vibration_button_ = nullptr;
    QToolButton* auto_rotation_button_ = nullptr;
    QToolButton* axes_button_ = nullptr;
    QToolButton* reset_button_ = nullptr;
    QTimer* vibration_timer_ = nullptr;
    QTimer* auto_rotation_timer_ = nullptr;
    QElapsedTimer animation_clock_;
    QMetaObject::Connection context_cleanup_connection_;
    std::function<void(const QPoint&)> context_menu_handler_;
};
