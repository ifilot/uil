#include "ui/audience_window.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

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
    for (QWidget* widget : QApplication::topLevelWidgets()) {
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

    QWidget* panel = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("featureMenuPanel")) {
            panel = widget;
            break;
        }
    }
    QVERIFY(panel);

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

QTEST_MAIN(AudienceWindowTest)

#include "audience_window_test.moc"
