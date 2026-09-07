#include "ui/molecular_symmetry_widget.hpp"

#include "ui/molecule_widget.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int kAnimationFrameMs = 16;
constexpr int kFinalPoseHoldMs = 180;

QString operation_style_name(MolecularSymmetryOperationType type) {
  switch (type) {
  case MolecularSymmetryOperationType::Identity: return QStringLiteral("identity");
  case MolecularSymmetryOperationType::ProperRotation: return QStringLiteral("rotation");
  case MolecularSymmetryOperationType::Reflection: return QStringLiteral("reflection");
  case MolecularSymmetryOperationType::Inversion: return QStringLiteral("inversion");
  case MolecularSymmetryOperationType::ImproperRotation: return QStringLiteral("improper");
  }
  return {};
}

QColor mix_with_white(const QColor& color, int color_percent) {
  const int white_percent = 100 - color_percent;
  return QColor((color.red() * color_percent + 255 * white_percent) / 100,
                (color.green() * color_percent + 255 * white_percent) / 100,
                (color.blue() * color_percent + 255 * white_percent) / 100);
}

QString operation_button_style(const QColor& color) {
  const QColor background = mix_with_white(color, 10);
  const QColor hover = mix_with_white(color, 18);
  const QColor border = mix_with_white(color, 55);
  const double luminance = 0.2126 * color.redF() + 0.7152 * color.greenF()
      + 0.0722 * color.blueF();
  const QString checked_text = luminance > 0.56 ? QStringLiteral("#20252b")
                                                : QStringLiteral("#ffffff");
  return QStringLiteral(
      "QPushButton { color: %1; border: 1px solid %2; border-radius: 6px;"
      " background: %3; font-size: 16px; font-weight: 600; min-height: 36px;"
      " padding: 3px 8px; }"
      "QPushButton:hover { color: %1; background: %4; border-color: %1; }"
      "QPushButton:checked { color: %5; background: %1; border-color: %1; }")
      .arg(color.name(), border.name(), background.name(), hover.name(), checked_text);
}

QString point_group_html(const QString& point_group) {
  const QString escaped = point_group.toHtmlEscaped();
  if (escaped.size() < 2 || !escaped.at(0).isLetter()) return escaped;
  return escaped.left(1) + QStringLiteral("<sub>") + escaped.mid(1)
      + QStringLiteral("</sub>");
}
}  // namespace

MolecularSymmetryWidget::MolecularSymmetryWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("molecularSymmetryWidget"));
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet(QStringLiteral(
      "#molecularSymmetryWidget { background: transparent; border: 0; }"
      "#molecularSymmetryHeaderPanel, #molecularSymmetryOperations { background: #ffffff;"
      " border: 1px solid #d5d8dd; border-radius: 8px; }"
      "#molecularSymmetryScene { background: #e7e9ec; border: 1px solid #d5d8dd;"
      " border-radius: 8px; }"
      "#molecularSymmetryWidget QLabel { background: transparent; border: 0; }"
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
  molecule_widget_->set_default_view_rotation(
      QQuaternion::fromEulerAngles(-12.0f, 18.0f, 18.0f));
  molecule_widget_->set_default_camera_distance_factor(2.0f);
  molecule_widget_->set_context_menu_handler(
      [this](const QPoint& position) {
        if (context_menu_handler_) context_menu_handler_(position);
      });
  scene_layout->addWidget(molecule_widget_, 1);
  body_layout->addWidget(scene_panel, 2);

  auto* operations_panel = new QFrame(this);
  operations_panel->setObjectName(QStringLiteral("molecularSymmetryOperations"));
  operations_panel->setMinimumWidth(285);
  operations_panel->setMaximumWidth(475);
  auto* operations_panel_layout = new QVBoxLayout(operations_panel);
  operations_panel_layout->setContentsMargins(12, 11, 12, 11);
  operations_panel_layout->setSpacing(7);
  auto* operations_title = new QLabel(QStringLiteral("Symmetry operations"), operations_panel);
  operations_title->setObjectName(QStringLiteral("molecularSymmetryOperationsTitle"));
  operations_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  auto* operations_hint = new QLabel(
      QStringLiteral("Select an operation to show its element and play it."), operations_panel);
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
  operations_layout_->setVerticalSpacing(7);
  operations_layout_->setAlignment(Qt::AlignTop);
  operations_layout_->setColumnStretch(0, 1);
  operations_layout_->setColumnStretch(1, 1);
  scroll_area->setWidget(operations_content_);
  operations_panel_layout->addWidget(operations_title);
  operations_panel_layout->addWidget(operations_hint);
  operations_panel_layout->addWidget(status_label_);
  operations_panel_layout->addWidget(scroll_area, 1);
  body_layout->addWidget(operations_panel, 1);
  root->addLayout(body_layout, 1);

  operation_buttons_ = new QButtonGroup(this);
  operation_buttons_->setExclusive(true);
  connect(operation_buttons_, &QButtonGroup::idClicked,
          this, &MolecularSymmetryWidget::play_operation);

  animation_timer_ = new QTimer(this);
  animation_timer_->setObjectName(QStringLiteral("molecularSymmetryAnimationTimer"));
  animation_timer_->setTimerType(Qt::PreciseTimer);
  animation_timer_->setInterval(kAnimationFrameMs);
  connect(animation_timer_, &QTimer::timeout,
          this, &MolecularSymmetryWidget::advance_animation);
  update_status();
}

void MolecularSymmetryWidget::set_definition(
    const MolecularSymmetryDefinition& definition) {
  if (!definition.is_valid()) return;
  stop_animation(true);
  definition_ = definition;
  molecule_widget_->set_geometry(definition_.geometry);
  molecule_widget_->set_coordinate_origin(QVector3D());
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

int MolecularSymmetryWidget::active_operation_index() const {
  return active_operation_index_;
}

int MolecularSymmetryWidget::selected_operation_index() const {
  return selected_operation_index_;
}

QImage MolecularSymmetryWidget::capture_frame() const {
  return const_cast<MolecularSymmetryWidget*>(this)->grab().toImage();
}

void MolecularSymmetryWidget::set_context_menu_handler(
    std::function<void(const QPoint&)> handler) {
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
  while (QLayoutItem* item = operations_layout_->takeAt(0)) delete item;

  constexpr int columns = 2;
  for (int index = 0; index < definition_.operations.size(); ++index) {
    const MolecularSymmetryOperation& operation = definition_.operations.at(index);
    auto* button = new QPushButton(operation.label, operations_content_);
    button->setObjectName(QStringLiteral("molecularSymmetryOperationButton"));
    button->setProperty("operationType", operation_style_name(operation.type));
    button->setProperty("operationIndex", index);
    button->setStyleSheet(operation_button_style(
        definition_.operation_colors.for_type(operation.type)));
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setToolTip(
        QStringLiteral("Play %1: %2")
            .arg(operation.label, molecular_symmetry_operation_type_name(operation.type)));
    operation_buttons_->addButton(button, index);
    operations_layout_->addWidget(button, index / columns, index % columns);
  }
}

void MolecularSymmetryWidget::advance_animation() {
  if (active_operation_index_ < 0
      || active_operation_index_ >= definition_.operations.size()) {
    stop_animation(false);
    return;
  }
  const qint64 elapsed = animation_elapsed_.elapsed();
  const int duration = definition_.animation_duration_ms;
  if (elapsed >= duration + kFinalPoseHoldMs) {
    stop_animation(false);
    return;
  }
  const double progress = std::min(1.0, double(elapsed) / double(duration));
  molecule_widget_->set_coordinate_transform(
      definition_.operations.at(active_operation_index_).matrix_at(progress));
}

void MolecularSymmetryWidget::stop_animation(bool clear_selection) {
  if (animation_timer_) animation_timer_->stop();
  if (molecule_widget_) molecule_widget_->clear_coordinate_transform();
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

void MolecularSymmetryWidget::show_symmetry_element(
    const MolecularSymmetryOperation& operation) {
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
  molecule_widget_->set_symmetry_element(
      element, operation.axis, definition_.operation_colors.for_type(operation.type));
}

void MolecularSymmetryWidget::update_status() {
  if (!status_label_) return;
  if (active_operation_index_ >= 0
      && active_operation_index_ < definition_.operations.size()) {
    const auto& operation = definition_.operations.at(active_operation_index_);
    status_label_->setText(
        QStringLiteral("Playing %1 · %2")
            .arg(operation.label, molecular_symmetry_operation_type_name(operation.type)));
  } else if (selected_operation_index_ >= 0
             && selected_operation_index_ < definition_.operations.size()) {
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
        QStringLiteral("%1 operations · select one to play")
            .arg(definition_.operations.size()));
  } else {
    status_label_->setText(QStringLiteral("Select an operation to play"));
  }
}
