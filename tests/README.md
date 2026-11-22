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
- `qt_pdf_backend_test`: opening the getting-started and pointer-and-annotations example decks,
  page counts and aspect ratios, rendering dimensions, mixed page sizes, invalid requests,
  missing, malformed, or password-protected document errors, and in-memory loading that is
  unaffected by later rewrites of the file and yields premultiplied pages.
- `molecule_geometry_test`: XYZ parsing, bond inference, and normal-mode displacement playback.
- `molecule_camera_test`: default and isometric views, fit distance, perspective scaling, stereo
  eye separation, and wheel and trackpad zoom limits.
- `molecule_widget_test`: molecule toolbar state, visualizer mode controls, precise animation
  timers, and rendering after reparenting to another window (OpenGL context recreation).
- `molecular_symmetry_test`: operation-set and orbital-selection parsing, fractional transforms,
  rejection of invalid colors and non-symmetry operations, baked orbital phases, and the bundled
  example payloads.
- `molecular_symmetry_widget_test`: exposing and playing every symmetry operation (when OpenGL is
  available) and filtering of supported ground-state subshells.
- `atomic_orbital_test`: catalog order through 5g, hydrogenic nodes and orientation, signed
  isosurfaces, phase colormaps, payload options, and rejection of invalid orbitals and colormaps.
- `atomic_orbital_widget_test`: rendering and resampling when OpenGL is available, and GPU
  geometry-cache reuse and eviction.
- `harmonic_wavepacket_test`: Gaussian normalization, coherent-state phases and Poisson weights,
  basis-function normalization, relative phases, one-period playback, displaced-state
  coefficients and convergence, and interactive-figure payload parsing.
- `particle_in_box_basis_test`: step-function sine coefficients and captured fractions against
  the slide values, box boundary conditions, monotonic convergence through 25 terms, and
  interactive-figure payload parsing.
- `atomic_orbital_cache_test`: geometry-only keys, deduplication, shared array storage, LRU
  eviction, oversized-volume delivery, replacement of speculative work, safe worker shutdown,
  and cold-build versus warm-cache timing. The controller tests also exercise rapid jumps
  through the four-orbital atlas and document changes with geometry still in flight.
- `slide_cache_test`: hit/miss statistics, replacement accounting, LRU refresh and eviction,
  oversized entries, null images, reset behavior, adaptive-budget bounds, and per-page removal.
- `spotlight_detector_test`: unavailable detection, initial receiver presence, connect/disconnect
  transitions, and suppression of duplicate presence notifications through an injected HID probe.
- `deck_overview_visibility_test`: complete visible-row detection, scrolled content coordinates,
  and selected-page fallback while layout geometry is unavailable.
- `audience_window_test`: navigation and tool shortcuts, shortcut tooltips, persistent pointer-size
  bounds, PowerPoint-compatible pointer hiding, annotation mapping, letterboxing, erasing, and
  throttled mirroring of in-progress strokes to the presenter.
- `app_controller_test`: empty-state errors, real PDF opening and rendering, navigation boundaries,
  failed-open state preservation, annotated package round trips, annotated PDF export, unsaved
  annotations surviving reloads, rendering from the opened snapshot while the file is rewritten,
  export refusing to overwrite the open deck and never leaving partial files, and immediate saves
  still bundling assets found by the background media scan.
- `render_scheduler_test`: deterministic priority promotion, generation cancellation, active-job
  deduplication, failure delivery, invalid requests, safe destruction during active rendering, and
  pruning of pending requests.
- `uil_package_test`: PDF/media/overlay round trips, hidden-overlay filtering, slash and backslash
  traversal, case-folded duplicates, CRC corruption, extraction limits, out-of-range overlay page
  indices, missing inputs, malformed archives, and API preconditions.
- `pdf_media_detector_test`: annotation helpers, empty results, page-tree ordering, linked and
  orphan annotations, rectangles, summaries, containment of raw and package-relative media
  paths, and bounded time and memory on hostile input (100k-level and cyclic page trees,
  unterminated objects, decompression bombs, huge shared annotation arrays, long digit runs).
- `video_frame_buffer_test`: timestamp ordering, duration accounting, open/decode failures,
  end-of-stream, stop/clear behavior, decoder replacement on restart, prompt interruption, and
  dropping of signals from stopped runs using a fake source; with FFmpeg, real decoding of a
  generated odd-width clip (colours, opaque RGB32 output including the right edge, end of stream).
- `performance_log_test`: session-log creation, process-start offsets, structured events, timed
  spans and checkpoints, field serialization, ordinary Qt diagnostic messages, and (on Windows)
  the native launcher's named readiness-event handshake.
- `native_launcher_test` (Windows): end-to-end launcher process creation, quoted argument
  forwarding, viewer discovery, progress-message delivery, and readiness-driven launcher shutdown
  using a system-only probe.

## Next priorities

1. Extend `QtPdfBackend` integration tests with crop boxes and rotated pages.
2. Inject PDF, render, and media backends into `AppController` to cover deterministic stale-render
   delivery, cache reuse, screen selection, and playback state without real worker threads.
3. Extract package ZIP parsing behind a byte-oriented interface so malformed central-directory,
   CRC, compression, truncation, and hostile-entry cases can be fuzzed directly.
4. Extend Qt GUI coverage with blank-screen painting, deck-overview mouse/wheel behavior,
   presenter-menu actions, high-DPI coordinates, and genuine multi-screen transitions.
5. Add a staged Windows deployment smoke test that opens all six **File > Examples** decks.
6. Add coverage-guided fuzz jobs for the ZIP and PDF media parsers after their byte-oriented test
   harnesses are available.

## Commands

Tests are built by default (`BUILD_TESTING` is on through `include(CTest)`). From an MSYS2 UCRT64
shell on Windows:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run only the fast unit layer with `ctest --test-dir build -L unit --output-on-failure`.

### Linux

The widget tests (`audience_window_test`, `app_controller_test`, `atomic_orbital_widget_test`,
`molecule_widget_test`, `molecular_symmetry_widget_test`) need a real X11 platform plugin. Run the
suite under Xvfb with the `xcb` platform, as the sanitizer CI job does:

```bash
QT_QPA_PLATFORM=xcb xvfb-run --auto-servernum ctest --test-dir build --output-on-failure
```

Do not use `QT_QPA_PLATFORM=offscreen` on Linux: with Qt 6.4 the offscreen platform crashes inside
`rhiFlush` when a `QOpenGLWidget` cannot obtain an OpenGL context. Where hardware OpenGL is
unavailable, add `LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software`. The widget tests share the
`uil_qt_opengl` CTest resource lock, so `--parallel` is safe.

To reproduce the sanitizer job, configure a separate `Debug` build with
`-DUIL_ENABLE_SANITIZERS=ON` and run it the same way with `ASAN_OPTIONS=detect_leaks=1` and
`LSAN_OPTIONS=suppressions=tests/sanitizers/lsan.supp`.

### LaTeX orbital package

Run `python3 tests/latex_orbital_test.py` with pdfLaTeX, LuaLaTeX, and qpdf
installed. This verifies every atlas identifier, embedded payloads, multiple
annotations per page, exact custom-poster dimensions, local options, and
expected error messages. XeLaTeX static-mode checks run when available.

The orbital widget regression also checks GPU-cache hits without new texture uploads, exact
frame restoration, palette changes, LRU eviction within the 32 MiB per-widget payload budget,
and reparenting across OpenGL contexts. Atlas navigation asserts that a warm four-orbital
revisit performs zero geometry uploads.
