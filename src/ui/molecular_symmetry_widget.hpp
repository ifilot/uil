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

/** @brief Interactive molecule renderer with clickable point-group operations. */
class MolecularSymmetryWidget final : public QWidget {
  Q_OBJECT

 public:
  explicit MolecularSymmetryWidget(QWidget* parent = nullptr);

  /** @brief Replaces the molecule and restores controls saved for @p slide_identity. */
  void set_definition(const MolecularSymmetryDefinition& definition,
                      const QString& slide_identity = {});
  /** @brief Discards all per-slide interactive render settings. */
  void clear_saved_slide_states();
  /** @brief Returns the active definition. */
  const MolecularSymmetryDefinition& definition() const;
  /** @brief Starts the operation at @p index. */
  void play_operation(int index);
  /** @brief Returns whether an operation animation is active. */
  bool is_animating() const;
  /** @brief Returns the active operation index or -1. */
  int active_operation_index() const;
  /** @brief Returns the selected operation whose element remains visible. */
  int selected_operation_index() const;
  /** @brief Captures the composed OpenGL scene and operation controls. */
  QImage capture_frame() const;
  /** @brief Delegates right-click menu requests to the presentation window. */
  void set_context_menu_handler(std::function<void(const QPoint&)> handler);

 protected:
  void hideEvent(QHideEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  struct SlideState {
    QVector<SymmetryOrbital> orbitals;
    QQuaternion rotation;
    float camera_distance_factor = 2.0f;
    int selected_atom = 0;
    int selected_operation = -1;
    int current_tab = 0;
    bool auto_rotation = false;
  };

  SlideState capture_slide_state() const;
  void restore_slide_state(const SlideState& state);
  void rebuild_operation_buttons();
  void advance_animation();
  void stop_animation(bool clear_selection = false);
  void show_symmetry_element(const MolecularSymmetryOperation& operation);
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
