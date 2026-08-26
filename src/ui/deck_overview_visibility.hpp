#pragma once

#include <QPair>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <algorithm>

namespace deck_overview {

/**
 * @brief Finds the first and last widgets intersecting a viewport.
 *
 * Widget rectangles are mapped into viewport coordinates. This avoids relying
 * on the scroll area's internal content offset, which can be transient while a
 * layout or scroll-bar range is being updated.
 *
 * @tparam WidgetRange An ordered range containing QWidget-derived pointers.
 * @param widgets Widgets ordered by their zero-based page index.
 * @param viewport Viewport against which visibility is measured.
 * @param fallback_index Page returned when no widget currently intersects.
 * @return The inclusive first and last visible page indices.
 */
template <typename WidgetRange>
QPair<int, int> visible_page_range(
    const WidgetRange& widgets,
    const QWidget* viewport,
    int fallback_index) {
    const int widget_count = int(widgets.size());
    if (widget_count <= 0 || !viewport) {
        return {-1, -1};
    }

    const QRect viewport_rect(QPoint(0, 0), viewport->size());
    int first_visible = -1;
    int last_visible = -1;
    int page_index = 0;
    for (QWidget* widget : widgets) {
        if (widget) {
            const QRect widget_rect(
                widget->mapTo(viewport, QPoint(0, 0)),
                widget->size());
            if (widget_rect.intersects(viewport_rect)) {
                if (first_visible < 0) {
                    first_visible = page_index;
                }
                last_visible = page_index;
            }
        }
        ++page_index;
    }

    if (first_visible >= 0) {
        return {first_visible, last_visible};
    }

    const int fallback = std::clamp(fallback_index, 0, widget_count - 1);
    return {fallback, fallback};
}

}  // namespace deck_overview
