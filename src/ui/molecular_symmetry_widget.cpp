#include "ui/molecular_symmetry_widget.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "ui/molecule_widget.hpp"
#include "molecule/molecule_camera.hpp"

namespace {
constexpr int kAnimationFrameMs = 16;
constexpr int kFinalPoseHoldMs = 180;
constexpr float kReferencePoseOpacity = 0.5f;

QString operation_style_name(MolecularSymmetryOperationType type) {
  switch (type) {
    case MolecularSymmetryOperationType::Identity:
      return QStringLiteral("identity");
    case MolecularSymmetryOperationType::ProperRotation:
      return QStringLiteral("rotation");
    case MolecularSymmetryOperationType::Reflection:
      return QStringLiteral("reflection");
    case MolecularSymmetryOperationType::Inversion:
      return QStringLiteral("inversion");
    case MolecularSymmetryOperationType::ImproperRotation:
      return QStringLiteral("improper");
  }
  return {};
}

QColor mix_with_white(const QColor& color, int color_percent) {
  const int white_percent = 100 - color_percent;
  return QColor((color.red() * color_percent + 255 * white_percent) / 100,
                (color.green() * color_percent + 255 * white_percent) / 100,
                (color.blue() * color_percent + 255 * white_percent) / 100);
}

QString operation_button_style(const QColor& color, bool compact) {
  const QColor background = mix_with_white(color, 10);
  const QColor hover = mix_with_white(color, 18);
  const QColor border = mix_with_white(color, 55);
  const double luminance = 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
  const QString checked_text =
      luminance > 0.56 ? QStringLiteral("#20252b") : QStringLiteral("#ffffff");
  return QStringLiteral(
             "QPushButton { font-family: 'Segoe UI'; color: %1; border: 1px solid %2; "
             "border-radius: 8px;"
             " background: %3; font-size: %6px; font-weight: 600; min-height: %7px;"
             " padding: %8px 6px; }"
             "QPushButton:hover { color: %1; background: %4; border-color: %1; }"
             "QPushButton:checked { color: %5; background: %1; border-color: %1; }"
             "QPushButton:pressed { background: %2; }")
      .arg(color.name(), border.name(), background.name(), hover.name(), checked_text)
      .arg(compact ? 13 : 15)
      .arg(compact ? 22 : 32)
      .arg(compact ? 2 : 5);
}

QString point_group_html(const QString& point_group) {
  const QString escaped = point_group.toHtmlEscaped();
  if (escaped.size() < 2 || !escaped.at(0).isLetter()) return escaped;
  return escaped.left(1) + QStringLiteral("<sub>") + escaped.mid(1) + QStringLiteral("</sub>");
}
}  // namespace

MolecularSymmetryWidget::MolecularSymmetryWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("molecularSymmetryWidget"));
  setFont(QFont(QStringLiteral("Segoe UI"), 10));
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet(QStringLiteral(
      "#molecularSymmetryWidget { background: white; border: 0; }"
      "#molecularSymmetryHeaderPanel, #molecularSymmetryOperations { background: #ffffff;"
      " border: 1px solid #d5d8dd; border-radius: 8px; }"
      "#molecularSymmetryScene { background: #e7e9ec; border: 1px solid #d5d8dd;"
      " border-radius: 8px; }"
      "#molecularSymmetryWidget QLabel { font-family: 'Segoe UI'; background: transparent; border: "
      "0; }"
      "#molecularSymmetryHeader { color: #242a32; font-size: 24px; font-weight: 700;"
      " padding: 2px 5px; }"
      "#molecularSymmetryOperationsTitle { color: #242a32; font-size: 19px;"
      " font-weight: 700; }"
      "#molecularSymmetryOperationsHint { color: #737b86; font-size: 14px; }"
      "#molecularSymmetryStatus { color: #59626e; font-size: 15px; padding: 5px 7px; }"
      "#molecularSymmetryOperationScrollArea, #molecularSymmetryOperationsContent {"
      " background: transparent; border: 0; }"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(9);

  auto* header_panel = new QFrame(this);
  header_panel->setObjectName(QStringLiteral("molecularSymmetryHeaderPanel"));
  auto* header_layout = new QHBoxLayout(header_panel);
  header_layout->setContentsMargins(13, 7, 13, 7);
  title_label_ = new QLabel(header_panel);
  title_label_->setObjectName(QStringLiteral("molecularSymmetryHeader"));
  title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  title_label_->setTextFormat(Qt::RichText);
  title_label_->setFocusPolicy(Qt::NoFocus);
  header_layout->addWidget(title_label_, 1);
  root->addWidget(header_panel);

  auto* body_layout = new QHBoxLayout;
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(9);

  auto* scene_panel = new QFrame(this);
  scene_panel->setObjectName(QStringLiteral("molecularSymmetryScene"));
  auto* scene_layout = new QVBoxLayout(scene_panel);
  scene_layout->setContentsMargins(1, 1, 1, 1);
  scene_layout->setSpacing(0);
  molecule_widget_ = new MoleculeWidget(scene_panel);
  molecule_widget_->setObjectName(QStringLiteral("symmetryMoleculeOpenGLWidget"));
  molecule_widget_->set_builtin_controls_visible(false);
  molecule_widget_->set_axes_visible(false);
  molecule_widget_->set_world_axes_visible(true);
  molecule_widget_->set_default_view_rotation(molecule_camera::isometric_rotation());
  molecule_widget_->set_default_camera_distance_factor(2.0f);
  molecule_widget_->set_reference_geometry_opacity(kReferencePoseOpacity);
  molecule_widget_->set_context_menu_handler([this](const QPoint& position) {
    if (context_menu_handler_) context_menu_handler_(position);
  });
  auto* spin_button = new QPushButton(QStringLiteral("Z rotation: off"), header_panel);
  spin_button->setObjectName(QStringLiteral("molecularSymmetrySpinButton"));
  spin_button->setCheckable(true);
  spin_button->setFocusPolicy(Qt::NoFocus);
  spin_button->setCursor(Qt::PointingHandCursor);
  spin_button->setMinimumWidth(150);
  spin_button->setStyleSheet(operation_button_style(QColor(39, 125, 131), true));
  spin_button->setToolTip(
      QStringLiteral("Rotate continuously around the molecule's Z axis. Drag to change the view; "
                     "symmetry operations continue independently."));
  connect(spin_button, &QPushButton::toggled, this, [this, spin_button](bool enabled) {
    molecule_widget_->set_auto_rotation_enabled(enabled);
    spin_button->setText(enabled ? QStringLiteral("Z rotation: on")
                                 : QStringLiteral("Z rotation: off"));
  });
  header_layout->addWidget(spin_button);
  auto* tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("symmetryControlTabs"));
  tabs->setMinimumWidth(340);
  tabs->setMaximumWidth(500);
  tabs->setStyleSheet(QStringLiteral(
      "QTabWidget, QStackedWidget, QTabBar { background: white; }"
      "QTabWidget::pane { border: 1px solid #d5d8dd; background: white; border-radius: 7px; }"
      "QScrollArea > QWidget > QWidget, QScrollArea > QWidget { background: white; }"
      "QScrollBar:vertical { background: #edf1f4; width: 10px; margin: 0; }"
      "QScrollBar::handle:vertical { background: #a2b7bc; min-height: 24px; border-radius: 4px; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: #edf1f4; }"
      "QTabBar::tab { background: #edf1f4; color: #59626e; padding: 10px 12px; border: 0; }"
      "QTabBar::tab:selected { background: #277d83; color: white; }"
      "QTabBar::tab:hover { background: #b7d4d6; color: #242a32; }"));
  auto* menu = new QScrollArea(tabs);
  menu->setObjectName(QStringLiteral("symmetryOrbitalPanel"));
  menu->setWidgetResizable(true);
  menu->setFrameShape(QFrame::NoFrame);
  menu->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  menu->setStyleSheet(QStringLiteral(
      "QListWidget { background: white; color: #242a32; }"
      "QListWidget::item { padding: 6px; }"
      "QListWidget::item:selected { background: #277d83; color: white; }"
      "QScrollBar:vertical { background: #edf1f4; width: 10px; }"
      "QScrollBar::handle:vertical { background: #a2b7bc; min-height: 24px; }"

      "QScrollArea, QFrame { background: #ffffff; color: #242a32; }"
      "QLabel, QCheckBox { background: white; color: #242a32; font-family: 'Segoe UI'; font-size: "
      "14px; }"
      "QCheckBox::indicator { width: 14px; height: 14px; background: white; border: 1px solid "
      "#8b9aa5; border-radius: 3px; }"
      "QCheckBox::indicator:checked { background: #277d83; border: 2px solid #a8d7d5; }"
      "QListWidget, QDoubleSpinBox { background: white; color: #242a32; padding: 5px;"
      "border: 1px solid #c8cfd8; border-radius: 4px; }"
      "QComboBox::drop-down { background: #edf5f5; border-left: 1px solid #c8cfd8; }"
      "QComboBox QAbstractItemView { background: white; color: #242a32; "
      "selection-background-color: #277d83; selection-color: white; }"
      "QPushButton { color: #277d83; background: #edf5f5; border: 1px solid #b7d4d6;"
      "border-radius: 5px; padding: 6px; }"));
  auto* panel = new QFrame(menu);
  panel->setMinimumWidth(300);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(15, 12, 15, 12);
  layout->setSpacing(10);
  layout->addWidget(new QLabel(QStringLiteral("Atomic orbitals"), panel));
  layout->addWidget(new QLabel(QStringLiteral("1. Select an atom"), panel));
  orbital_atom_ = new QListWidget(panel);
  orbital_atom_->setObjectName(QStringLiteral("symmetryOrbitalAtom"));
  orbital_atom_->setMinimumHeight(100);
  orbital_atom_->setMaximumHeight(130);
  layout->addWidget(orbital_atom_);
  orbital_summary_ = new QLabel(panel);
  orbital_summary_->setWordWrap(true);
  layout->addWidget(orbital_summary_);
  layout->addWidget(new QLabel(QStringLiteral("2. Select orbitals on this atom"), panel));
  orbital_advanced_ = new QCheckBox(QStringLiteral("Advanced: all basis functions"), panel);
  orbital_advanced_->setObjectName(QStringLiteral("symmetryOrbitalAdvanced"));
  orbital_advanced_->setToolTip(QStringLiteral(
      "Include unoccupied / polarization functions. These are not ground-state occupancies."));
  layout->addWidget(orbital_advanced_);
  connect(orbital_advanced_, &QCheckBox::toggled, this, [this] { update_orbital_controls(); });
  auto* grid = new QGridLayout;
  const auto names = symmetry_orbital_names();
  for (int index = 0; index < names.size(); ++index) {
    const QString name = names.at(index);
    auto* check = new QCheckBox(name, panel);
    check->setObjectName(QStringLiteral("symmetryOrbital_") + name);
    orbital_checks_.push_back(check);
    grid->addWidget(check, index / 2, index % 2);
    connect(check, &QCheckBox::toggled, this, [this, name](bool checked) {
      auto selected = molecule_widget_->orbitals();
      const int atom = orbital_atom_->currentRow() + 1;
      selected.erase(std::remove_if(selected.begin(), selected.end(),
                                    [&](const auto& entry) {
                                      return entry.atom == atom && entry.orbital == name;
                                    }),
                     selected.end());
      if (checked) selected.push_back({atom, name, float(orbital_scale_->value())});
      molecule_widget_->set_orbitals(selected);
      update_orbital_controls();
    });
  }
  layout->addLayout(grid);
  orbital_scale_ = new QDoubleSpinBox(panel);
  orbital_scale_->setObjectName(QStringLiteral("symmetryOrbitalRadius"));
  orbital_scale_->setRange(0.1, 3.0);
  orbital_scale_->setSingleStep(0.1);
  orbital_scale_->setValue(0.85);
  orbital_scale_->setPrefix(QStringLiteral("Display radius: "));
  orbital_scale_->setSuffix(QStringLiteral(" Å"));
  layout->addWidget(orbital_scale_);
  auto* auto_size = new QPushButton(QStringLiteral("Use atom-based size"), panel);
  auto_size->setToolTip(QStringLiteral("Restore the illustrative default: H/He 0.45 Å; other atoms 0.85 Å."));
  layout->addWidget(auto_size);
  connect(auto_size, &QPushButton::clicked, this, [this] {
    const int atom = orbital_atom_->currentRow();
    if (atom >= 0 && atom < definition_.geometry.atoms.size())
      orbital_scale_->setValue(default_symmetry_orbital_radius(definition_.geometry.atoms.at(atom).element));
  });
  auto* legend = new QLabel(
      QStringLiteral("<span style='color:#008e99'>Cyan: +ψ</span> · "
                     "<span style='color:#b026ff'>Purple: −ψ</span><br>"
                     "50% transparent neon surfaces.<br>"
                     "Axes follow the molecule. 2s uses a cutaway.<br>Up to 64 selected orbitals."),
      panel);
  layout->addWidget(legend);
  auto* clear = new QPushButton(QStringLiteral("Clear all orbitals"), panel);
  auto* restore = new QPushButton(QStringLiteral("Restore slide defaults"), panel);
  layout->addWidget(clear);
  layout->addWidget(restore);
  connect(clear, &QPushButton::clicked, this, [this] {
    molecule_widget_->set_orbitals({});
    update_orbital_controls();
  });
  connect(restore, &QPushButton::clicked, this, [this] {
    molecule_widget_->set_orbitals(definition_.orbitals);
    update_orbital_controls();
  });
  connect(orbital_atom_, &QListWidget::currentRowChanged, this,
          [this] { update_orbital_controls(); });
  connect(orbital_scale_, &QDoubleSpinBox::valueChanged, this, [this](double scale) {
    auto selected = molecule_widget_->orbitals();
    for (auto& orbital : selected) {
      if (orbital.atom == orbital_atom_->currentRow() + 1) orbital.scale = float(scale);
    }
    molecule_widget_->set_orbitals(selected);
  });
  layout->addStretch();
  menu->setWidget(panel);
  auto* reset_camera = new QPushButton(QStringLiteral("Reset camera"), header_panel);
  reset_camera->setObjectName(QStringLiteral("symmetryResetCamera"));
  reset_camera->setFocusPolicy(Qt::NoFocus);
  reset_camera->setCursor(Qt::PointingHandCursor);
  reset_camera->setToolTip(QStringLiteral("Restore the default isometric view and zoom. Orbital selections and symmetry playback are unchanged."));
  reset_camera->setStyleSheet(operation_button_style(QColor(95, 107, 120), true));
  connect(reset_camera, &QPushButton::clicked, molecule_widget_, &MoleculeWidget::reset_camera);
  header_layout->addWidget(reset_camera);
  auto* reset = new QPushButton(QStringLiteral("Reset operation"), header_panel);
  reset->setObjectName(QStringLiteral("symmetryResetOperation"));
  reset->setFocusPolicy(Qt::NoFocus);
  reset->setStyleSheet(operation_button_style(QColor(95, 107, 120), true));
  connect(reset, &QPushButton::clicked, this, [this] { stop_animation(true); });
  header_layout->addWidget(reset);
  update_orbital_controls();
  scene_layout->addWidget(molecule_widget_, 1);
  body_layout->addWidget(scene_panel, 2);

  auto* operations_panel = new QFrame(this);
  operations_panel->setObjectName(QStringLiteral("molecularSymmetryOperations"));
  operations_panel->setMinimumWidth(285);
  auto* operations_panel_layout = new QVBoxLayout(operations_panel);
  operations_panel_layout->setContentsMargins(12, 11, 12, 11);
  operations_panel_layout->setSpacing(7);
  auto* operations_title = new QLabel(QStringLiteral("Symmetry operations"), operations_panel);
  operations_title->setObjectName(QStringLiteral("molecularSymmetryOperationsTitle"));
  operations_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  auto* operations_hint =
      new QLabel(QStringLiteral("Choose an operation · click again to replay"), operations_panel);
  operations_hint->setObjectName(QStringLiteral("molecularSymmetryOperationsHint"));
  operations_hint->setWordWrap(true);
  status_label_ = new QLabel(operations_panel);
  status_label_->setObjectName(QStringLiteral("molecularSymmetryStatus"));
  status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  status_label_->setWordWrap(true);

  auto* scroll_area = new QScrollArea(operations_panel);
  scroll_area->setObjectName(QStringLiteral("molecularSymmetryOperationScrollArea"));
  scroll_area->setWidgetResizable(true);
  scroll_area->setFrameShape(QFrame::NoFrame);
  scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  operations_content_ = new QWidget(scroll_area);
  operations_content_->setObjectName(QStringLiteral("molecularSymmetryOperationsContent"));
  operations_layout_ = new QGridLayout(operations_content_);
  operations_layout_->setContentsMargins(1, 1, 1, 1);
  operations_layout_->setHorizontalSpacing(7);
  operations_layout_->setVerticalSpacing(5);
  operations_layout_->setAlignment(Qt::AlignTop);
  operations_layout_->setColumnStretch(0, 1);
  operations_layout_->setColumnStretch(1, 1);
  scroll_area->setWidget(operations_content_);
  operations_panel_layout->addWidget(operations_title);
  operations_panel_layout->addWidget(operations_hint);
  operations_panel_layout->addWidget(status_label_);
  operations_panel_layout->addWidget(scroll_area, 1);
  tabs->addTab(operations_panel, QStringLiteral("Symmetry operations"));
  tabs->addTab(menu, QStringLiteral("Settings"));
  body_layout->addWidget(tabs, 1);
  root->addLayout(body_layout, 1);

  operation_buttons_ = new QButtonGroup(this);
  operation_buttons_->setExclusive(true);
  connect(operation_buttons_, &QButtonGroup::idClicked, this,
          &MolecularSymmetryWidget::play_operation);

  animation_timer_ = new QTimer(this);
  animation_timer_->setObjectName(QStringLiteral("molecularSymmetryAnimationTimer"));
  animation_timer_->setTimerType(Qt::PreciseTimer);
  animation_timer_->setInterval(kAnimationFrameMs);
  connect(animation_timer_, &QTimer::timeout, this, &MolecularSymmetryWidget::advance_animation);
  update_status();
}

void MolecularSymmetryWidget::set_definition(const MolecularSymmetryDefinition& definition) {
  if (!definition.is_valid()) return;
  stop_animation(true);
  definition_ = definition;
  molecule_widget_->set_geometry(definition_.geometry);
  molecule_widget_->set_coordinate_origin(QVector3D());
  molecule_widget_->set_orbitals(definition_.orbitals);
  {
    const QSignalBlocker blocker(orbital_atom_);
    orbital_atom_->clear();
    for (int atom = 0; atom < definition_.geometry.atoms.size(); ++atom) {
      orbital_atom_->addItem(QStringLiteral("Atom %1 · %2")
                                 .arg(atom + 1)
                                 .arg(definition_.geometry.atoms.at(atom).element));
      orbital_atom_->item(atom)->setSizeHint(QSize(0, 30));
    }
    orbital_atom_->setCurrentRow(0);
  }
  orbital_advanced_->setChecked(false);
  update_orbital_controls();
  title_label_->setText(
      QStringLiteral("%1 &nbsp; <span style='font-size:16px;color:#737b86'>Point group</span> "
                     "<span style='color:#922d49'>%2</span>")
          .arg(definition_.title.toHtmlEscaped(), point_group_html(definition_.point_group)));
  rebuild_operation_buttons();
  update_status();
}

const MolecularSymmetryDefinition& MolecularSymmetryWidget::definition() const {
  return definition_;
}

void MolecularSymmetryWidget::play_operation(int index) {
  if (index < 0 || index >= definition_.operations.size()) return;
  stop_animation(false);
  molecule_widget_->clear_coordinate_transform();
  molecule_widget_->set_reference_geometry_visible(true);
  selected_operation_index_ = index;
  active_operation_index_ = index;
  show_symmetry_element(definition_.operations.at(index));
  if (QAbstractButton* button = operation_buttons_->button(index)) button->setChecked(true);
  animation_elapsed_.restart();
  animation_timer_->start();
  update_status();
  advance_animation();
}

bool MolecularSymmetryWidget::is_animating() const {
  return animation_timer_ && animation_timer_->isActive();
}

int MolecularSymmetryWidget::active_operation_index() const { return active_operation_index_; }

int MolecularSymmetryWidget::selected_operation_index() const { return selected_operation_index_; }

QImage MolecularSymmetryWidget::capture_frame() const {
  return const_cast<MolecularSymmetryWidget*>(this)->grab().toImage();
}

void MolecularSymmetryWidget::set_context_menu_handler(std::function<void(const QPoint&)> handler) {
  context_menu_handler_ = std::move(handler);
}

void MolecularSymmetryWidget::hideEvent(QHideEvent* event) {
  stop_animation(false);
  QWidget::hideEvent(event);
}

void MolecularSymmetryWidget::contextMenuEvent(QContextMenuEvent* event) {
  if (context_menu_handler_) context_menu_handler_(event->globalPos());
  event->accept();
}

void MolecularSymmetryWidget::rebuild_operation_buttons() {
  for (QAbstractButton* button : operation_buttons_->buttons()) {
    operation_buttons_->removeButton(button);
    delete button;
  }
  while (QLayoutItem* item = operations_layout_->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const int columns = definition_.operations.size() > 12 ? 3 : 2;
  for (int column = 0; column < 3; ++column) {
    operations_layout_->setColumnStretch(column, column < columns ? 1 : 0);
  }
  int row = 0;
  const MolecularSymmetryOperationType types[] = {
      MolecularSymmetryOperationType::Identity, MolecularSymmetryOperationType::ProperRotation,
      MolecularSymmetryOperationType::Reflection, MolecularSymmetryOperationType::Inversion,
      MolecularSymmetryOperationType::ImproperRotation};
  for (const auto type : types) {
    int count = 0;
    for (const auto& operation : definition_.operations)
      if (operation.type == type) ++count;
    if (count == 0) continue;
    auto* heading = new QLabel(
        QStringLiteral("%1  ·  %2").arg(molecular_symmetry_operation_type_name(type)).arg(count),
        operations_content_);
    heading->setStyleSheet(QStringLiteral(
        "color: #59626e; font-size: 12px; font-weight: 600; padding: 4px 2px 1px 2px;"));
    operations_layout_->addWidget(heading, row++, 0, 1, columns);
    int column = 0;
    for (int index = 0; index < definition_.operations.size(); ++index) {
      const MolecularSymmetryOperation& operation = definition_.operations.at(index);
      if (operation.type != type) continue;
      auto* button = new QPushButton(operation.label, operations_content_);
      button->setObjectName(QStringLiteral("molecularSymmetryOperationButton"));
      button->setProperty("operationType", operation_style_name(operation.type));
      button->setProperty("operationIndex", index);
      button->setStyleSheet(operation_button_style(
          definition_.operation_colors.for_type(operation.type), columns == 3));
      button->setCheckable(true);
      button->setFocusPolicy(Qt::NoFocus);
      button->setCursor(Qt::PointingHandCursor);
      button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      button->setToolTip(
          QStringLiteral("Play %1: %2")
              .arg(operation.label, molecular_symmetry_operation_type_name(operation.type)));
      operation_buttons_->addButton(button, index);
      operations_layout_->addWidget(button, row, column, 1, count == 1 ? columns : 1);
      if (++column == columns) {
        ++row;
        column = 0;
      }
    }
    if (column != 0) ++row;
  }
}

void MolecularSymmetryWidget::advance_animation() {
  if (active_operation_index_ < 0 || active_operation_index_ >= definition_.operations.size()) {
    stop_animation(false);
    return;
  }
  const qint64 elapsed = animation_elapsed_.elapsed();
  const int duration = definition_.animation_duration_ms;
  if (elapsed >= duration + kFinalPoseHoldMs) {
    if (molecule_widget_->orbitals().isEmpty()) {
      stop_animation(false);
    } else {
      molecule_widget_->set_coordinate_transform(
          definition_.operations.at(active_operation_index_).matrix_at(1.0));
      molecule_widget_->set_atom_reflection_shape({}, 1.0);
      animation_timer_->stop();
      active_operation_index_ = -1;
      result_visible_ = true;
      update_status();
    }
    return;
  }
  const double progress = std::min(1.0, double(elapsed) / double(duration));
  const auto& operation = definition_.operations.at(active_operation_index_);
  if (operation.type == MolecularSymmetryOperationType::Reflection ||
      operation.type == MolecularSymmetryOperationType::ImproperRotation) {
    const double reflection_progress = operation.type == MolecularSymmetryOperationType::Reflection
                                           ? progress
                                           : std::max(0.0, progress * 2.0 - 1.0);
    molecule_widget_->set_atom_reflection_shape(operation.axis, reflection_progress);
  }
  molecule_widget_->set_coordinate_transform(
      definition_.operations.at(active_operation_index_).matrix_at(progress));
}

void MolecularSymmetryWidget::stop_animation(bool clear_selection) {
  result_visible_ = false;
  if (animation_timer_) animation_timer_->stop();
  if (molecule_widget_) {
    molecule_widget_->set_reference_geometry_visible(false);
    molecule_widget_->clear_coordinate_transform();
  }
  active_operation_index_ = -1;
  if (clear_selection) {
    selected_operation_index_ = -1;
    if (molecule_widget_) {
      molecule_widget_->set_symmetry_element(MoleculeWidget::SymmetryElement::None);
    }
    if (operation_buttons_) {
      operation_buttons_->setExclusive(false);
      for (QAbstractButton* button : operation_buttons_->buttons()) button->setChecked(false);
      operation_buttons_->setExclusive(true);
    }
  }
  update_status();
}

void MolecularSymmetryWidget::show_symmetry_element(const MolecularSymmetryOperation& operation) {
  MoleculeWidget::SymmetryElement element = MoleculeWidget::SymmetryElement::None;
  switch (operation.type) {
    case MolecularSymmetryOperationType::Identity:
      break;
    case MolecularSymmetryOperationType::ProperRotation:
      element = MoleculeWidget::SymmetryElement::RotationAxis;
      break;
    case MolecularSymmetryOperationType::Reflection:
      element = MoleculeWidget::SymmetryElement::MirrorPlane;
      break;
    case MolecularSymmetryOperationType::Inversion:
      element = MoleculeWidget::SymmetryElement::InversionCenter;
      break;
    case MolecularSymmetryOperationType::ImproperRotation:
      element = MoleculeWidget::SymmetryElement::ImproperAxisAndPlane;
      break;
  }
  molecule_widget_->set_symmetry_element(element, operation.axis,
                                         definition_.operation_colors.for_type(operation.type));
}

void MolecularSymmetryWidget::update_status() {
  if (!status_label_) return;
  if (result_visible_) {
    status_label_->setText(QStringLiteral("Result held · replay or reset to compare"));
    return;
  }
  if (active_operation_index_ >= 0 && active_operation_index_ < definition_.operations.size()) {
    const auto& operation = definition_.operations.at(active_operation_index_);
    status_label_->setText(
        QStringLiteral("Playing %1 · %2")
            .arg(operation.label, molecular_symmetry_operation_type_name(operation.type)));
  } else if (selected_operation_index_ >= 0 &&
             selected_operation_index_ < definition_.operations.size()) {
    const auto& operation = definition_.operations.at(selected_operation_index_);
    QString element_name;
    switch (operation.type) {
      case MolecularSymmetryOperationType::Identity:
        element_name = QStringLiteral("Identity has no geometric element");
        break;
      case MolecularSymmetryOperationType::ProperRotation:
        element_name = QStringLiteral("Rotation axis shown");
        break;
      case MolecularSymmetryOperationType::Reflection:
        element_name = QStringLiteral("Mirror plane shown");
        break;
      case MolecularSymmetryOperationType::Inversion:
        element_name = QStringLiteral("Inversion center shown");
        break;
      case MolecularSymmetryOperationType::ImproperRotation:
        element_name = QStringLiteral("Rotation axis and mirror plane shown");
        break;
    }
    status_label_->setText(
        QStringLiteral("%1 · click %2 to replay").arg(element_name, operation.label));
  } else if (!definition_.operations.isEmpty()) {
    status_label_->setText(
        QStringLiteral("%1 operations · select one to play").arg(definition_.operations.size()));
  } else {
    status_label_->setText(QStringLiteral("Select an operation to play"));
  }
}

void MolecularSymmetryWidget::update_orbital_controls() {
  const auto& selected = molecule_widget_->orbitals();

  const int atom = orbital_atom_->currentRow() + 1;
  const QString element = atom > 0 && atom <= definition_.geometry.atoms.size()
                              ? definition_.geometry.atoms.at(atom - 1).element
                              : QString();
  const auto allowed = occupied_symmetry_orbital_names(element);
  orbital_summary_->setText(QStringLiteral("%1 selected · neutral-atom defaults").arg(selected.size()));
  orbital_summary_->setToolTip(QStringLiteral("Supported occupied subshells of the neutral atom, not a molecular or ionic occupancy calculation."));
  const auto names = symmetry_orbital_names();
  for (int index = 0; index < orbital_checks_.size(); ++index) {
    auto* check = orbital_checks_.at(index);
    const QSignalBlocker blocker(check);
    const bool checked = std::any_of(selected.begin(), selected.end(), [&](const auto& entry) {
      return entry.atom == atom && entry.orbital == names.at(index);
    });
    check->setChecked(checked);
    const bool available = allowed.contains(names.at(index)) || orbital_advanced_->isChecked();
    check->setVisible(available || checked);
    check->setToolTip(
        !available && checked
            ? QStringLiteral("Selected non-ground-state basis function; uncheck to remove.")
            : QString());
    check->setEnabled(atom > 0 && (checked || (available && selected.size() < 64)));
  }
  const QSignalBlocker blocker(orbital_scale_);
  float scale = default_symmetry_orbital_radius(element);
  for (const auto& entry : selected)
    if (entry.atom == atom) {
      scale = entry.scale;
      break;
    }
  orbital_scale_->setValue(scale);
}
