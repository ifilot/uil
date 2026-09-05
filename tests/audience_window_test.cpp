#include "ui/audience_window.hpp"
#include "ui/interactive_figure_widget.hpp"
#include "ui/molecule_widget.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QSlider>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <algorithm>

namespace {
void send_mouse_move(AudienceWindow* window, const QPointF& position) {
    QMouseEvent event(
        QEvent::MouseMove,
        position,
        window->mapToGlobal(position.toPoint()),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(window, &event);
}
}  // namespace

class AudienceWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void navigation_and_tool_shortcuts();
    void shortcut_tooltips_are_exposed();
    void pointer_size_defaults_clamps_and_persists();
    void pointer_hides_after_powerpoint_interval();
    void pointer_motion_restarts_timeout();
    void annotation_coordinates_respect_letterboxing();
    void blank_screen_shortcuts_change_painted_output();
    void molecule_right_click_suspends_surface_while_menu_is_open();
    void molecule_tool_switch_restores_interaction_without_extra_click();
    void interactive_figure_controls_and_tool_switching();
    void harmonic_wavepacket_controls_update_status();
    void harmonic_basis_controls_update_phase_status();
    void particle_in_box_basis_slider_updates_fit_status();
    void harmonic_displaced_basis_slider_updates_fit_status();

private:
    QTemporaryDir settings_directory_;
};

void AudienceWindowTest::initTestCase() {
    QVERIFY(settings_directory_.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("uil-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("audience-window-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        settings_directory_.path());
}

void AudienceWindowTest::cleanup() {
    QSettings settings;
    settings.clear();
    settings.sync();
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget->objectName() == QStringLiteral("featureMenuPanel")) {
            widget->close();
        }
    }
    QCoreApplication::processEvents();
}

void AudienceWindowTest::navigation_and_tool_shortcuts() {
    AudienceWindow window;
    window.set_document_overview(8, 0);
    QSignalSpy next_spy(&window, &AudienceWindow::next_requested);
    QSignalSpy previous_spy(&window, &AudienceWindow::previous_requested);
    QSignalSpy first_spy(&window, &AudienceWindow::first_requested);
    QSignalSpy last_spy(&window, &AudienceWindow::last_requested);
    QSignalSpy media_spy(&window, &AudienceWindow::play_pause_requested);

    QTest::keyClick(&window, Qt::Key_Right);
    QTest::keyClick(&window, Qt::Key_Left);
    QTest::keyClick(&window, Qt::Key_Home);
    QTest::keyClick(&window, Qt::Key_End);
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(next_spy.size(), 1);
    QCOMPARE(previous_spy.size(), 1);
    QCOMPARE(first_spy.size(), 1);
    QCOMPARE(last_spy.size(), 1);
    QCOMPARE(media_spy.size(), 1);

    QTest::keyClick(&window, Qt::Key_G);
    QVERIFY(window.is_deck_overview_visible());
    QTest::keyClick(&window, Qt::Key_G);
    QVERIFY(!window.is_deck_overview_visible());

    QTest::keyClick(&window, Qt::Key_G);
    QTest::keyClick(&window, Qt::Key_L);
    QVERIFY(!window.is_deck_overview_visible());
    QVERIFY(window.is_pointer_tool_selected());
}

void AudienceWindowTest::shortcut_tooltips_are_exposed() {
    AudienceWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QPoint local_position(100, 100);
    QContextMenuEvent event(
        QContextMenuEvent::Mouse,
        local_position,
        window.mapToGlobal(local_position));
    QApplication::sendEvent(&window, &event);
    QCoreApplication::processEvents();

    QWidget* panel = window.findChild<QWidget*>(QStringLiteral("featureMenuPanel"));
    QVERIFY(panel);
    QVERIFY(!panel->isWindow());

    QStringList tooltips;
    for (QToolButton* button : panel->findChildren<QToolButton*>()) {
        if (!button->toolTip().isEmpty()) {
            tooltips.push_back(button->toolTip());
        }
    }
    QVERIFY(tooltips.contains(QStringLiteral("Laser pointer (L)")));
    QVERIFY(tooltips.contains(QStringLiteral("Show slide grid (G)")));
}

void AudienceWindowTest::pointer_size_defaults_clamps_and_persists() {
    AudienceWindow first_window;
    QCOMPARE(first_window.pointer_size(), 25);
    first_window.set_pointer_size(500);
    QCOMPARE(first_window.pointer_size(), 120);

    QSettings().sync();
    AudienceWindow restored_window;
    QCOMPARE(restored_window.pointer_size(), 120);
    restored_window.set_pointer_size(-20);
    QCOMPARE(restored_window.pointer_size(), 12);
}

void AudienceWindowTest::pointer_hides_after_powerpoint_interval() {
    AudienceWindow window;
    window.set_pointer_tool();
    send_mouse_move(&window, QPointF(120, 90));
    QVERIFY(window.is_pointer_visible());

    QTest::qWait(AudienceWindow::kPointerInactivityTimeoutMs - 250);
    QVERIFY(window.is_pointer_visible());
    QTRY_VERIFY_WITH_TIMEOUT(
        !window.is_pointer_visible(),
        750);
}

void AudienceWindowTest::pointer_motion_restarts_timeout() {
    AudienceWindow window;
    window.set_pointer_tool();
    send_mouse_move(&window, QPointF(100, 100));
    QTest::qWait(1800);
    send_mouse_move(&window, QPointF(110, 110));
    QTest::qWait(1500);
    QVERIFY(window.is_pointer_visible());
    QTRY_VERIFY_WITH_TIMEOUT(
        !window.is_pointer_visible(),
        1800);
}

void AudienceWindowTest::annotation_coordinates_respect_letterboxing() {
    AudienceWindow window;
    window.resize(800, 600);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(Qt::white);
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);
    window.set_pen_tool();

    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    const QImage centered_overlay = window.current_annotation_overlay_image();
    QVERIFY(!centered_overlay.isNull());
    QVERIFY(centered_overlay.pixelColor(800, 450).alpha() > 0);

    const QImage before_outside_click = centered_overlay;
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 20));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 20));
    QCOMPARE(window.current_annotation_overlay_image(), before_outside_click);

    window.set_eraser_tool();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QCOMPARE(window.current_annotation_overlay_image().pixelColor(800, 450).alpha(), 0);
}

void AudienceWindowTest::blank_screen_shortcuts_change_painted_output() {
    AudienceWindow window;
    window.resize(800, 450);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(QColor(20, 80, 180));
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    auto render_window = [&window] {
        QImage result(window.size(), QImage::Format_RGB32);
        result.fill(Qt::magenta);
        QPainter painter(&result);
        window.render(&painter);
        return result;
    };

    QCOMPARE(render_window().pixelColor(400, 225), QColor(20, 80, 180));
    QTest::keyClick(&window, Qt::Key_B);
    QCOMPARE(render_window().pixelColor(400, 225), QColor(Qt::black));
    QTest::keyClick(&window, Qt::Key_W);
    QCOMPARE(render_window().pixelColor(400, 225), QColor(Qt::white));
    QTest::keyClick(&window, Qt::Key_W);
    QCOMPARE(render_window().pixelColor(400, 225), QColor(20, 80, 180));
}

void AudienceWindowTest::molecule_right_click_suspends_surface_while_menu_is_open() {
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        QSKIP("The offscreen Qt platform cannot safely expose QOpenGLWidget");
    }

    AudienceWindow window;
    window.resize(800, 450);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(QColor(235, 240, 245));
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    MoleculeGeometry geometry;
    geometry.atoms.push_back(MoleculeAtom{
        QStringLiteral("H"), QVector3D(), QVector3D(0.1f, 0.0f, 0.0f)});
    window.set_molecule_overlay(geometry, QRectF(0.2, 0.2, 0.6, 0.6));
    window.show();
    QTest::qWait(100);

    MoleculeWidget* molecule = dynamic_cast<MoleculeWidget*>(
        window.findChild<QWidget*>(QStringLiteral("moleculeWidget")));
    QVERIFY(molecule);
    QVERIFY(molecule->isVisible());
    molecule->set_vibration_playing(true);
    QVERIFY(molecule->vibration_playing());

    QTest::mouseClick(molecule, Qt::RightButton, Qt::NoModifier, molecule->rect().center());

    QWidget* menu = window.findChild<QWidget*>(QStringLiteral("featureMenuPanel"));
    QVERIFY(menu);
    QVERIFY(!menu->isWindow());
    QVERIFY(menu->isVisible());
    QVERIFY(molecule->isHidden());
    QVERIFY(!molecule->vibration_playing());

    QPointer<QWidget> guarded_menu = menu;
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(guarded_menu.isNull());
    QTRY_VERIFY(molecule->isVisible());
    QTRY_VERIFY(molecule->vibration_playing());
}

void AudienceWindowTest::molecule_tool_switch_restores_interaction_without_extra_click() {
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        QSKIP("The offscreen Qt platform cannot safely expose QOpenGLWidget");
    }

    AudienceWindow window;
    window.resize(800, 450);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(QColor(235, 240, 245));
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    MoleculeGeometry geometry;
    geometry.atoms.push_back(MoleculeAtom{QStringLiteral("H"), QVector3D(), QVector3D()});
    window.set_molecule_overlay(geometry, QRectF(0.2, 0.2, 0.6, 0.6));
    window.show();
    QTest::qWait(100);

    MoleculeWidget* molecule = dynamic_cast<MoleculeWidget*>(
        window.findChild<QWidget*>(QStringLiteral("moleculeWidget")));
    QVERIFY(molecule);
    QVERIFY(molecule->isVisible());

    QTest::mouseClick(molecule, Qt::RightButton, Qt::NoModifier, molecule->rect().center());
    QWidget* menu = window.findChild<QWidget*>(QStringLiteral("featureMenuPanel"));
    QVERIFY(menu);

    QToolButton* pointer_button = nullptr;
    for (QToolButton* button : menu->findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Laser\npointer")) {
            pointer_button = button;
            break;
        }
    }
    QVERIFY(pointer_button);
    pointer_button->click();
    QVERIFY(window.is_pointer_tool_selected());
    QVERIFY(menu->isVisible());

    QPointer<QWidget> pointer_menu = menu;
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(pointer_menu.isNull());
    QVERIFY(molecule->isHidden());

    const QPoint menu_position(100, 100);
    QContextMenuEvent context_event(
        QContextMenuEvent::Mouse, menu_position, window.mapToGlobal(menu_position));
    QApplication::sendEvent(&window, &context_event);
    menu = window.findChild<QWidget*>(QStringLiteral("featureMenuPanel"));
    QVERIFY(menu);

    QToolButton* cursor_button = nullptr;
    for (QToolButton* button : menu->findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Classic\npointer")) {
            cursor_button = button;
            break;
        }
    }
    QVERIFY(cursor_button);
    QPointer<QWidget> cursor_menu = menu;
    cursor_button->click();
    QTRY_VERIFY(cursor_menu.isNull());
    QTRY_VERIFY(molecule->isVisible());

    QTest::mousePress(molecule, Qt::LeftButton, Qt::NoModifier, molecule->rect().center());
    QCOMPARE(molecule->cursor().shape(), Qt::ClosedHandCursor);
    QTest::mouseRelease(molecule, Qt::LeftButton, Qt::NoModifier, molecule->rect().center());
    QCOMPARE(molecule->cursor().shape(), Qt::OpenHandCursor);
}

void AudienceWindowTest::interactive_figure_controls_and_tool_switching() {
    AudienceWindow window;
    window.resize(800, 450);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(QColor(235, 240, 245));
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    InteractiveFigureDefinition definition;
    definition.title = QStringLiteral("A moving sine wave");
    definition.x_label = QStringLiteral("$x\\;\\mathrm{(radians)}$");
    definition.y_label = QStringLiteral("$y$");
    definition.background_svg = QByteArrayLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 500'>"
        "<rect width='800' height='500' fill='#ffffff'/></svg>");
    window.set_interactive_figure_overlay(definition, QRectF(0.15, 0.15, 0.7, 0.7));
    window.show();
    QCoreApplication::processEvents();

    auto* figure = window.findChild<InteractiveFigureWidget*>(
        QStringLiteral("interactiveFigureWidget"));
    QVERIFY(figure);
    QVERIFY(figure->isVisible());
    QVERIFY(figure->font().pixelSize() >= 28);
    auto* amplitude = figure->findChild<QSlider*>(QStringLiteral("figureAmplitudeSlider"));
    QVERIFY(amplitude);
    QVERIFY(!figure->findChild<QWidget*>(QStringLiteral("figureColorButton")));
    const int initial_value = amplitude->value();
    amplitude->setValue(std::min(amplitude->maximum(), initial_value + 100));
    QVERIFY(amplitude->value() != initial_value);

    window.set_pen_tool();
    QVERIFY(figure->isHidden());
    window.set_cursor_tool();
    QVERIFY(figure->isVisible());
}

void AudienceWindowTest::harmonic_wavepacket_controls_update_status() {
    AudienceWindow window;
    window.resize(1100, 700);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(Qt::white);
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    InteractiveFigureDefinition definition;
    definition.kind = InteractiveFigureDefinition::Kind::HarmonicBondWavepacket;
    definition.title = QStringLiteral("A displaced harmonic-bond wavepacket");
    definition.background_svg = QByteArrayLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 600'>"
        "<rect width='800' height='600' fill='#f8fafc'/></svg>");
    definition.x_min = -5.0;
    definition.x_max = 5.0;
    definition.x_label = QStringLiteral("$x = q / \\ell$");
    definition.potential_label = QStringLiteral("$U/(\\hbar\\omega)$");
    definition.density_label = QStringLiteral("$\\ell|\\psi|^2$");
    definition.animate_initially = false;
    window.set_interactive_figure_overlay(definition, QRectF(0.08, 0.08, 0.84, 0.84));
    window.show();
    QCoreApplication::processEvents();

    auto* figure = window.findChild<InteractiveFigureWidget*>(
        QStringLiteral("interactiveFigureWidget"));
    QVERIFY(figure);
    auto* status = figure->findChild<QLabel*>(QStringLiteral("figureStatusLabel"));
    auto* phase = figure->findChild<QSlider*>(QStringLiteral("figureFrequencySlider"));
    auto* stretch = figure->findChild<QSlider*>(QStringLiteral("figureAmplitudeSlider"));
    QVERIFY(status);
    QVERIFY(phase);
    QVERIFY(stretch);
    QVERIFY(!figure->findChild<QWidget*>(QStringLiteral("figureColorButton")));
    QVERIFY(status->isVisible());
    QVERIFY(status->text().contains(QStringLiteral("maximum stretch")));

    phase->setValue(250);
    QVERIFY(status->text().contains(QStringLiteral("crossing equilibrium inward")));
    QVERIFY(status->text().contains(QStringLiteral("P = -3.00")));
    stretch->setValue(0);
    QVERIFY(status->text().contains(QStringLiteral("P = -0.50")));
}

void AudienceWindowTest::harmonic_basis_controls_update_phase_status() {
    AudienceWindow window;
    window.resize(1100, 700);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(Qt::white);
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    InteractiveFigureDefinition definition;
    definition.kind = InteractiveFigureDefinition::Kind::HarmonicBasisStates;
    definition.title = QStringLiteral("A coherent packet and its real basis components");
    definition.background_svg = QByteArrayLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 600'>"
        "<rect width='800' height='600' fill='#f8fafc'/></svg>");
    definition.x_min = -5.0;
    definition.x_max = 5.0;
    definition.x_label = QStringLiteral("$x = q / \\ell$");
    definition.phase_initial = 0.5;
    definition.animate_initially = false;
    window.set_interactive_figure_overlay(definition, QRectF(0.08, 0.08, 0.84, 0.84));
    window.show();
    QCoreApplication::processEvents();

    auto* figure = window.findChild<InteractiveFigureWidget*>(
        QStringLiteral("interactiveFigureWidget"));
    QVERIFY(figure);
    auto* status = figure->findChild<QLabel*>(QStringLiteral("figureStatusLabel"));
    auto* phase = figure->findChild<QSlider*>(QStringLiteral("figureFrequencySlider"));
    QVERIFY(status);
    QVERIFY(phase);
    QVERIFY(!figure->findChild<QWidget*>(QStringLiteral("figureColorButton")));
    QVERIFY(status->text().contains(QStringLiteral("τ = 0.500")));
    phase->setValue(750);
    QVERIFY(status->text().contains(QStringLiteral("τ = 0.750")));
}

void AudienceWindowTest::particle_in_box_basis_slider_updates_fit_status() {
    AudienceWindow window;
    window.resize(1100, 700);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(Qt::white);
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    InteractiveFigureDefinition definition;
    definition.kind = InteractiveFigureDefinition::Kind::ParticleInBoxStepExpansion;
    definition.title = QStringLiteral("Fitting a step with box eigenfunctions");
    definition.background_svg = QByteArrayLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 520'>"
        "<rect width='800' height='520' fill='#f8fafc'/></svg>");
    definition.x_min = 0.0;
    definition.x_max = 1.0;
    definition.y_min = -0.25;
    definition.y_max = 1.25;
    definition.x_label = QStringLiteral("$x$");
    definition.y_label = QStringLiteral("$f(x), S_N(x)$");
    window.set_interactive_figure_overlay(definition, QRectF(0.08, 0.08, 0.84, 0.84));
    window.show();
    QCoreApplication::processEvents();

    auto* figure = window.findChild<InteractiveFigureWidget*>(
        QStringLiteral("interactiveFigureWidget"));
    QVERIFY(figure);
    auto* status = figure->findChild<QLabel*>(QStringLiteral("figureStatusLabel"));
    auto* basis_count = figure->findChild<QSlider*>(QStringLiteral("figureAmplitudeSlider"));
    QVERIFY(status);
    QVERIFY(basis_count);
    QCOMPARE(basis_count->minimum(), 1);
    QCOMPARE(basis_count->maximum(), 25);
    QVERIFY(status->text().contains(QStringLiteral("40.5%")));

    for (int n = 1; n <= 25; ++n) {
        basis_count->setValue(n);
        QCoreApplication::processEvents();
        QVERIFY2(status->text().contains(QStringLiteral("= %1").arg(n)),
                 qPrintable(status->text()));
    }
    basis_count->setValue(5);
    QVERIFY(status->text().contains(QStringLiteral("= 5")));
    QVERIFY(status->text().contains(QStringLiteral("87.2%")));
    basis_count->setValue(25);
    QVERIFY(status->text().contains(QStringLiteral("= 25")));
    QVERIFY(status->text().contains(QStringLiteral("97.5%")));
    basis_count->setValue(16);
    QCoreApplication::processEvents();
}

void AudienceWindowTest::harmonic_displaced_basis_slider_updates_fit_status() {
    AudienceWindow window;
    window.resize(1100, 700);
    QImage slide(1600, 900, QImage::Format_RGB32);
    slide.fill(Qt::white);
    window.set_slide_image(QStringLiteral("deck:0:1600x900:0"), slide);

    InteractiveFigureDefinition definition;
    definition.kind = InteractiveFigureDefinition::Kind::HarmonicDisplacedStateExpansion;
    definition.title = QStringLiteral("Fitting a stretched harmonic-oscillator state");
    definition.background_svg = QByteArrayLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 520'>"
        "<rect width='800' height='520' fill='#f8fafc'/></svg>");
    definition.x_min = -6.0;
    definition.x_max = 6.0;
    definition.y_min = -0.2;
    definition.y_max = 0.9;
    definition.x_label = QStringLiteral("$y=x/\\alpha$");
    definition.y_label = QStringLiteral("$\\Phi(y), S_N(y)$");
    definition.displacement = 2.0;
    window.set_interactive_figure_overlay(definition, QRectF(0.08, 0.08, 0.84, 0.84));
    window.show();
    QCoreApplication::processEvents();

    auto* figure = window.findChild<InteractiveFigureWidget*>(
        QStringLiteral("interactiveFigureWidget"));
    QVERIFY(figure);
    auto* status = figure->findChild<QLabel*>(QStringLiteral("figureStatusLabel"));
    auto* basis_count = figure->findChild<QSlider*>(QStringLiteral("figureAmplitudeSlider"));
    QVERIFY(status);
    QVERIFY(basis_count);
    QCOMPARE(basis_count->minimum(), 1);
    QCOMPARE(basis_count->maximum(), 25);
    QVERIFY2(status->text().contains(QStringLiteral("13.53%")), qPrintable(status->text()));

    for (int n = 1; n <= 25; ++n) {
        basis_count->setValue(n);
        QCoreApplication::processEvents();
        QVERIFY2(status->text().contains(QStringLiteral("= %1").arg(n)),
                 qPrintable(status->text()));
    }
    basis_count->setValue(5);
    QVERIFY(status->text().contains(QStringLiteral("94.73%")));
    basis_count->setValue(25);
    QVERIFY(status->text().contains(QStringLiteral("100.00%")));
}

QTEST_MAIN(AudienceWindowTest)

#include "audience_window_test.moc"
