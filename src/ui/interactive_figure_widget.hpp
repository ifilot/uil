#pragma once

#include "figure/interactive_figure.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>

#include <functional>

class QLabel;
class QFrame;
class QPushButton;
class QSlider;
class QTimer;
class QVBoxLayout;

/** @brief Displays the minimal SVG-backed interactive-figure prototype. */
class InteractiveFigureWidget final : public QWidget {
    Q_OBJECT

public:
    explicit InteractiveFigureWidget(QWidget* parent = nullptr);

    /** @brief Loads a validated figure and restores its declared initial state. */
    void set_definition(const InteractiveFigureDefinition& definition);
    /** @brief Delegates right-click menu requests to the presentation window. */
    void set_context_menu_handler(std::function<void(const QPoint&)> handler);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    class PlotCanvas;
    void reset_controls();
    void update_labels();
    void update_harmonic_status();
    void advance_animation();
    void set_harmonic_phase(double phase, bool update_slider);
    void set_animation_running(bool running);

    InteractiveFigureDefinition definition_;
    QVBoxLayout* root_layout_ = nullptr;
    QFrame* controls_frame_ = nullptr;
    QLabel* status_label_ = nullptr;
    PlotCanvas* canvas_ = nullptr;
    QLabel* amplitude_label_ = nullptr;
    QLabel* frequency_label_ = nullptr;
    QSlider* amplitude_slider_ = nullptr;
    QSlider* frequency_slider_ = nullptr;
    QPushButton* animation_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    QTimer* animation_timer_ = nullptr;
    bool animation_requested_ = true;
    double harmonic_phase_ = 0.0;
    double playback_start_phase_ = 0.0;
    QElapsedTimer playback_elapsed_;
    std::function<void(const QPoint&)> context_menu_handler_;
};
