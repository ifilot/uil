#pragma once

#include "figure/interactive_figure.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>

#include <functional>

class QLabel;
class QFrame;
class QGridLayout;
class QPushButton;
class QSlider;
class QTimer;
class QVBoxLayout;

/**
 * @brief Interactive plot overlay for the figure kinds of InteractiveFigureDefinition.
 *
 * Supports an animated sine wave, a harmonic-bond coherent wavepacket, its six-state basis
 * decomposition, and the particle-in-a-box step and displaced-oscillator basis-expansion fits.
 * Each figure paints over an optional SVG background, with sliders, play/pause, and reset
 * controls that change per kind. Painting is QPainter-based on the GUI thread (no OpenGL).
 *
 * Animation uses a 16 ms Qt::PreciseTimer that runs only while playback is requested and the
 * widget is visible. Motion advances by measured elapsed time, so its speed does not depend on
 * timer jitter or the display's refresh rate. All methods must be called on the GUI thread.
 */
class InteractiveFigureWidget final : public QWidget {
    Q_OBJECT

public:
    /** @brief Builds the plot canvas and the shared control strip; no figure is loaded yet. */
    explicit InteractiveFigureWidget(QWidget* parent = nullptr);

    /**
     * @brief Loads a validated figure and restores its declared initial state.
     *
     * Rebuilds per-figure sample caches synchronously, rearranges the controls for the figure
     * kind, and starts playback if the definition animates initially and the widget is visible.
     */
    void set_definition(const InteractiveFigureDefinition& definition);
    /** @brief Delegates right-click menu requests to the presentation window. */
    void set_context_menu_handler(std::function<void(const QPoint&)> handler);

protected:
    /** @brief Forwards right-clicks to the presentation context-menu handler. */
    void contextMenuEvent(QContextMenuEvent* event) override;
    /** @brief Freezes playback at its current phase and stops the timer while hidden. */
    void hideEvent(QHideEvent* event) override;
    /** @brief Resumes requested playback from the phase reached when the widget was hidden. */
    void showEvent(QShowEvent* event) override;

private:
    class PlotCanvas;
    /** @brief Restores every slider, the phase, and the play state from the definition. */
    void reset_controls();
    /** @brief Refreshes the slider captions for the current figure kind. */
    void update_labels();
    /** @brief Shows mean position/momentum or captured basis weight for harmonic figures. */
    void update_harmonic_status();
    /** @brief Shows the captured fraction of the target for basis-expansion figures. */
    void update_basis_expansion_status();
    /** @brief Timer slot: advances the active animation by the elapsed wall time. */
    void advance_animation();
    /**
     * @brief Sets the harmonic playback phase (in periods), clamped to the declared range.
     * @param update_slider Also moves the phase slider without emitting its change signal.
     */
    void set_harmonic_phase(double phase, bool update_slider);
    /**
     * @brief Starts or pauses playback and updates the play button.
     *
     * The timer only runs while the widget is visible. Starting a finished non-looping
     * harmonic playback rewinds it to the beginning.
     */
    void set_animation_running(bool running);

    InteractiveFigureDefinition definition_;
    QVBoxLayout* root_layout_ = nullptr;
    QGridLayout* controls_layout_ = nullptr;
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
    QElapsedTimer sine_frame_clock_;
    std::function<void(const QPoint&)> context_menu_handler_;
};
