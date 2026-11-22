# Interactive-figure performance analysis

## Executive summary

The PDF renderer and the interactive-figure renderer have different hot paths.
PDF pages already use a bounded LRU image cache and a priority-aware worker pool.
Interactive figures are painted on the GUI thread, so their most valuable
optimization is to move invariant mathematics and artwork out of `paintEvent()`.

The current implementation now does that for the most expensive reusable work:

- Every partial sum for the discrete `N = 1..25` fitting figures is computed
  once when the payload is loaded.
- Those sample arrays are converted to screen-space `QPainterPath` objects once
  per widget geometry and reused for every slider movement.
- The payload's SVG background is rasterized once per logical size and device
  pixel ratio instead of being rendered during every repaint.
- The six stationary harmonic-oscillator basis functions are sampled once. An
  animation frame now updates six weights and six phase factors instead of
  repeatedly evaluating Hermite polynomials and coherent-state coefficients.

For the fitting views, a slider update is consequently a cached-path selection,
a status-label update, and a repaint. It no longer recomputes the expansion.

## Existing render architecture

`AppController` asks `RenderScheduler` for the current slide and a small set of
likely neighbours. Requests carry priorities, duplicate requests are coalesced,
and obsolete document generations are discarded. Navigation drops queued
full-size renders that are no longer near the current slide, and the presenter's
thumbnail strip is queued behind the next slides. The scheduler renders from an
in-memory copy of the PDF taken when the deck was opened.

QtPdf serializes every PDFium call behind one process-wide lock, so additional
render threads add document parses and memory without adding throughput. The
scheduler therefore uses a single worker for PDF pages, which also bounds how
long a current-slide request can wait behind speculative work to one page render.
Rendered pages are converted to premultiplied ARGB on that worker so that the
GUI thread can paint them without per-paint format conversion.

Rendered PDF pages enter `SlideCache`, a memory-bounded LRU cache. The default
budget is 512 MiB on Linux and other non-Windows platforms. On Windows it is one
eighth of physical RAM, clamped to 128 MiB through 1 GiB. Cache keys include
the document, page, pixel size, and rotation, which avoids incorrect reuse.

The normal presentation look-ahead is deliberately small: current, next,
previous, and next-plus-one. The deck overview separately requests visible
thumbnails and a bounded ring around them. These policies protect the current
slide from speculative work.

## Cost and memory of the new fitting cache

At the declared maximum of 25 states and 1,001 samples, the raw partial-sum
points occupy about 0.4 MiB (`25 * 1001 * sizeof(QPointF)`). Cached painter paths
are of the same order, so an active fitting figure remains comfortably below a
few MiB. The cache is rebuilt only when a new definition is loaded; screen paths
are rebuilt only after a resize.

The SVG raster cache costs width times height times four bytes. A full-HD active
figure is about 8 MiB at device-pixel ratio 1. Only the live figure widget owns
this cache, and resizing replaces rather than accumulates images.

## Recommended idle prefetch policy

Do not eagerly render an entire deck. A 100-slide, full-HD deck can exceed the
LRU budget, evict the useful near-slide set, and occupy the non-preemptive PDF
worker just before the presenter navigates.

A safe background policy is:

1. Start an idle timer roughly 600--800 ms after the last navigation, resize, or
   overview scroll.
2. Keep at most one speculative full-size render queued at a time, so that
   current-slide, next-slide and thumbnail requests are never stuck behind a
   batch of speculative pages.
3. Walk outward from the already predicted set, biased forward: `+3, -2, +4,
   -3, ...`.
4. Submit idle work at a priority below every visible or predictive request.
   Reset the walk when navigation occurs. Pending work can then be overtaken by
   critical requests; the one active speculative raster is allowed to finish.
5. Stop when the next candidate would push useful near-slide images out of the
   memory budget. Resume only after another idle interval.

This exploits the stalled periods typical of a lecture without creating a
second workload that competes with the presenter. Interactive slider states
should not use this worker queue: their compact, deterministic local cache is
faster and avoids cross-thread GUI objects.

## Next easy improvements, in priority order

Math-label layout is already cached: `ui_math_text` keeps a small GUI-thread LRU
of laid-out documents keyed by text, font and colour.

1. Cache the static plot layer (grid, axes, ticks, legend, and unchanged labels)
   per size/DPI. Animated frames would then paint only the changing curves and
   markers over one cached image.
2. Add the one-in-flight idle PDF prefetch policy above. It should be measured
   before widening the window.
3. Consider an optional disk page cache keyed by document content hash, page,
   pixel size, device-pixel ratio, rotation, renderer version, and colour mode.
   This improves reopening large decks, but requires eviction, atomic writes,
   and privacy-aware lifecycle controls, so it is not an "easy" first change.

## Measurement plan

Add timings around interactive `paintEvent()`, cache construction, and slider
updates using the existing performance log. Track median and p95 paint time,
frames exceeding 16.7 ms, PDF queue wait, raster time, LRU hit rate, and
evictions. Test at 1080p and 4K, both device-pixel ratios 1 and 2. A background
prefetch change should ship only if current-slide queue latency does not regress
under rapid navigation.
