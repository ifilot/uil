# Testing strategy

The unit-test layer uses Qt Test and is registered with CTest. Tests use temporary directories and
synthetic fixtures, so they do not depend on developer-specific files or network access.

## Current unit coverage

- `image_util_test`: aspect-ratio fitting, rounding, invalid dimensions, and centering in offset
  bounds.
- `document_hash_test`: fingerprint stability, path normalization, invalidation after a size
  change, and deterministic handling of missing paths.
- `example_presentations_test`: installed-example discovery, stable menu ordering, absolute-path
  resolution, and rejection of missing, unknown, or non-file entries.
- `qt_pdf_backend_test`: opening both bundled PDF decks, page counts and aspect ratios, rendering
  dimensions, invalid requests, and missing, malformed, or password-protected document errors.
- `molecule_geometry_test`: XYZ parsing, bond inference, and normal-mode displacement playback.
- `molecule_widget_test`: molecule toolbar state and visualizer mode controls.
- `slide_cache_test`: hit/miss statistics, replacement accounting, LRU refresh and eviction,
  oversized entries, null images, reset behavior, and adaptive-budget bounds.
- `spotlight_detector_test`: unavailable detection, initial receiver presence, connect/disconnect
  transitions, and suppression of duplicate presence notifications through an injected HID probe.
- `deck_overview_visibility_test`: complete visible-row detection, scrolled content coordinates,
  and selected-page fallback while layout geometry is unavailable.
- `audience_window_test`: navigation and tool shortcuts, shortcut tooltips, persistent pointer-size
  bounds, PowerPoint-compatible pointer hiding, annotation mapping, letterboxing, and erasing.
- `app_controller_test`: empty-state errors, real PDF opening and rendering, navigation boundaries,
  failed-open state preservation, annotated package round trips, and annotated PDF export.
- `render_scheduler_test`: deterministic priority promotion, generation cancellation, active-job
  deduplication, failure delivery, invalid requests, and safe destruction during active rendering.
- `uil_package_test`: PDF/media/overlay round trips, hidden-overlay filtering, slash and backslash
  traversal, case-folded duplicates, CRC corruption, extraction limits, missing inputs, malformed
  archives, and API preconditions.
- `pdf_media_detector_test`: annotation helpers, empty results, page-tree ordering, linked and
  orphan annotations, rectangles, summaries, and containment of raw and package-relative media
  paths.
- `video_frame_buffer_test`: timestamp ordering, duration accounting, open/decode failures,
  end-of-stream, stop/clear behavior, and decoder replacement on restart using a fake source.
- `performance_log_test`: session-log creation, process-start offsets, structured events, timed
  spans and checkpoints, field serialization, ordinary Qt diagnostic messages, and the native
  launcher's named readiness-event handshake.
- `native_launcher_test` (Windows): end-to-end launcher process creation, quoted argument
  forwarding, viewer discovery, progress-message delivery, and readiness-driven launcher shutdown
  using a system-only probe.

## Next priorities

1. Extend `QtPdfBackend` integration tests with mixed page sizes, crop boxes, and rotated pages.
2. Inject PDF, render, and media backends into `AppController` to cover deterministic stale-render
   delivery, cache reuse, screen selection, and playback state without real worker threads.
3. Extract package ZIP parsing behind a byte-oriented interface so malformed central-directory,
   CRC, compression, truncation, and hostile-entry cases can be fuzzed directly.
4. Extend Qt GUI coverage with blank-screen painting, deck-overview mouse/wheel behavior,
   presenter-menu actions, high-DPI coordinates, and genuine multi-screen transitions.
5. Add a staged Windows deployment smoke test that opens both **File > Examples** entries.
6. Add coverage-guided fuzz jobs for the ZIP and PDF media parsers after their byte-oriented test
   harnesses are available.

## Commands

From an MSYS2 UCRT64 shell:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run only the fast unit layer with `ctest --test-dir build -L unit --output-on-failure`.
