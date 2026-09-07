#pragma once

#include "symmetry/molecular_symmetry.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QContextMenuEvent;
class QGridLayout;
class QHideEvent;
class QLabel;
class MoleculeWidget;
class QTimer;

/** @brief Interactive molecule renderer with clickable point-group operations. */
class MolecularSymmetryWidget final : public QWidget {
  Q_OBJECT

 public:
  explicit MolecularSymmetryWidget(QWidget* parent = nullptr);

  /** @brief Replaces the molecule and its complete operation set. */
  void set_definition(const MolecularSymmetryDefinition& definition);
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
  void rebuild_operation_buttons();
  void advance_animation();
  void stop_animation(bool clear_selection = false);
  void show_symmetry_element(const MolecularSymmetryOperation& operation);
  void update_status();

  MolecularSymmetryDefinition definition_;
  MoleculeWidget* molecule_widget_ = nullptr;
  QLabel* title_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QWidget* operations_content_ = nullptr;
  QGridLayout* operations_layout_ = nullptr;
  QButtonGroup* operation_buttons_ = nullptr;
  QTimer* animation_timer_ = nullptr;
  QElapsedTimer animation_elapsed_;
  int active_operation_index_ = -1;
  int selected_operation_index_ = -1;
  std::function<void(const QPoint&)> context_menu_handler_;
};
