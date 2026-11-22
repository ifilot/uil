#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QQuaternion>
#include <QWidget>
#include <functional>

#include "symmetry/molecular_symmetry.hpp"

class QButtonGroup;
class QCheckBox;
class QListWidget;
class QDoubleSpinBox;
class QToolButton;
class QContextMenuEvent;
class QGridLayout;
class QHideEvent;
class QLabel;
class MoleculeWidget;
class QTimer;
class QTabWidget;
class QPushButton;

/**
 * @brief Interactive molecule renderer with clickable point-group operations.
 *
 * Wraps a MoleculeWidget (which owns all OpenGL resources and their context lifetime) together
 * with operation buttons and an atom-orbital settings tab. GUI thread only. Operation playback
 * is driven by a precise 16 ms timer that runs only while an operation animates; poses are
 * computed from elapsed wall time, so playback speed does not depend on the tick rate.
 */
class MolecularSymmetryWidget final : public QWidget {
  Q_OBJECT

 public:
  /** @brief Builds the header, molecule scene, operation list, and orbital settings tab. */
  explicit MolecularSymmetryWidget(QWidget* parent = nullptr);

  /**
   * @brief Replaces the molecule and its complete operation set.
   *
   * Invalid definitions are ignored. A valid one first saves the render settings of the
   * previous slide identity, then stops playback, clears the selected operation, resets the
   * camera and restores the definition's default orbital selection. When settings were saved
   * earlier for @p slide_identity, they are restored instead of the defaults.
   * @param[in] definition Molecule, operations and default orbitals to show.
   * @param[in] slide_identity Key for per-slide settings; empty disables saving and restoring.
   */
  void set_definition(const MolecularSymmetryDefinition& definition,
                      const QString& slide_identity = {});
  /** @brief Discards all per-slide interactive render settings. */
  void clear_saved_slide_states();
  /** @brief Returns the active definition. */
  const MolecularSymmetryDefinition& definition() const;
  /** @brief Starts or replays the operation at @p index; out-of-range indices are ignored. */
  void play_operation(int index);
  /** @brief Returns whether an operation animation is active. */
  bool is_animating() const;
  /** @brief Returns the active operation index or -1. */
  int active_operation_index() const;
  /** @brief Returns the selected operation whose element remains visible. */
  int selected_operation_index() const;
  /** @brief Captures the composed OpenGL scene and operation controls (renders synchronously). */
  QImage capture_frame() const;
  /** @brief Delegates right-click menu requests to the presentation window. */
  void set_context_menu_handler(std::function<void(const QPoint&)> handler);

 protected:
  /** @brief Stops a running operation so no timer runs while the widget is hidden. */
  void hideEvent(QHideEvent* event) override;
  /** @brief Forwards right-clicks outside the scene to the presentation context menu. */
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  /** @brief Interactive render settings remembered per slide identity. */
  struct SlideState {
    QVector<SymmetryOrbital> orbitals;
    QQuaternion rotation;
    float camera_distance_factor = 2.0f;
    int selected_atom = 0;
    int selected_operation = -1;
    int current_tab = 0;
    bool auto_rotation = false;
  };

  /** @brief Captures orbitals, camera, selections, tab and auto-rotation of the current slide. */
  SlideState capture_slide_state() const;
  /** @brief Re-applies settings captured by capture_slide_state(). */
  void restore_slide_state(const SlideState& state);
  /** @brief Recreates the operation buttons, grouped by operation type, for definition_. */
  void rebuild_operation_buttons();
  /**
   * @brief Applies the active operation's pose for the elapsed playback time.
   *
   * After the final pose has been held, playback stops; with orbitals displayed the
   * transformed result stays visible until the next operation or reset.
   */
  void advance_animation();
  /**
   * @brief Stops playback and restores the untransformed molecule.
   * @param clear_selection Also hides the symmetry element and unchecks every operation button.
   */
  void stop_animation(bool clear_selection = false);
  /** @brief Shows the axis, plane, or centre belonging to @p operation in its type colour. */
  void show_symmetry_element(const MolecularSymmetryOperation& operation);
  /** @brief Refreshes the status line for the playing, held, or selected operation. */
  void update_status();
  /** @brief Synchronizes the persistent orbital menu with the selected atom. */
  void update_orbital_controls();

  MolecularSymmetryDefinition definition_;
  MoleculeWidget* molecule_widget_ = nullptr;
  QLabel* title_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QWidget* operations_content_ = nullptr;
  QGridLayout* operations_layout_ = nullptr;
  QButtonGroup* operation_buttons_ = nullptr;
  QTimer* animation_timer_ = nullptr;
  QElapsedTimer animation_elapsed_;
  QListWidget* orbital_atom_ = nullptr;
  QDoubleSpinBox* orbital_scale_ = nullptr;
  QLabel* orbital_summary_ = nullptr;
  QVector<QCheckBox*> orbital_checks_;
  QTabWidget* tabs_ = nullptr;
  QPushButton* spin_button_ = nullptr;
  QHash<QString, SlideState> slide_states_;
  QString active_slide_identity_;
  bool result_visible_ = false;
  int active_operation_index_ = -1;
  int selected_operation_index_ = -1;
  std::function<void(const QPoint&)> context_menu_handler_;
};
