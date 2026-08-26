#include "ui/deck_overview_visibility.hpp"

#include <QList>
#include <QTest>
#include <QWidget>

#include <memory>
#include <vector>

class DeckOverviewVisibilityTest final : public QObject {
    Q_OBJECT

private slots:
    /** @brief Detects every row intersecting an unscrolled viewport. */
    void finds_all_visible_rows();
    /** @brief Accounts for the content widget's scroll offset. */
    void accounts_for_scrolled_content();
    /** @brief Uses the selected page while layout geometry is unavailable. */
    void falls_back_to_selected_page();
};

namespace {

/** @brief Creates a two-column set of fixed-size thumbnail widgets. */
QList<QWidget*> create_thumbnails(
    QWidget* content,
    int page_count,
    std::vector<std::unique_ptr<QWidget>>* storage) {
    QList<QWidget*> thumbnails;
    for (int page = 0; page < page_count; ++page) {
        auto thumbnail = std::make_unique<QWidget>(content);
        thumbnail->setGeometry(
            8 + (page % 2) * 188,
            8 + (page / 2) * 136,
            180,
            128);
        thumbnails.append(thumbnail.get());
        storage->push_back(std::move(thumbnail));
    }
    return thumbnails;
}

}  // namespace

void DeckOverviewVisibilityTest::finds_all_visible_rows() {
    QWidget viewport;
    viewport.resize(400, 410);
    QWidget content(&viewport);
    content.setGeometry(0, 0, 400, 1'000);
    std::vector<std::unique_ptr<QWidget>> storage;
    const QList<QWidget*> thumbnails = create_thumbnails(&content, 12, &storage);

    const QPair<int, int> expected_range(0, 5);
    QCOMPARE(
        deck_overview::visible_page_range(thumbnails, &viewport, 0),
        expected_range);
}

void DeckOverviewVisibilityTest::accounts_for_scrolled_content() {
    QWidget viewport;
    viewport.resize(400, 250);
    QWidget content(&viewport);
    content.setGeometry(0, -145, 400, 1'000);
    std::vector<std::unique_ptr<QWidget>> storage;
    const QList<QWidget*> thumbnails = create_thumbnails(&content, 12, &storage);

    const QPair<int, int> expected_range(2, 5);
    QCOMPARE(
        deck_overview::visible_page_range(thumbnails, &viewport, 0),
        expected_range);
}

void DeckOverviewVisibilityTest::falls_back_to_selected_page() {
    QWidget viewport;
    viewport.resize(400, 250);
    QWidget content(&viewport);
    content.setGeometry(0, 500, 400, 1'000);
    std::vector<std::unique_ptr<QWidget>> storage;
    const QList<QWidget*> thumbnails = create_thumbnails(&content, 12, &storage);

    const QPair<int, int> expected_range(7, 7);
    QCOMPARE(
        deck_overview::visible_page_range(thumbnails, &viewport, 7),
        expected_range);
}

QTEST_MAIN(DeckOverviewVisibilityTest)

#include "deck_overview_visibility_test.moc"
