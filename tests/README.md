# Testing strategy

The unit-test layer uses Qt Test and is registered with CTest. Tests use temporary directories and
synthetic fixtures, so they do not depend on developer-specific files or network access.

## Current unit coverage

- `image_util_test`: aspect-ratio fitting, rounding, invalid dimensions, and centering in offset
  bounds.
- `document_hash_test`: fingerprint stability, path normalization, invalidation after a size
  change, and deterministic handling of missing paths.
- `slide_cache_test`: hit/miss statistics, replacement accounting, LRU refresh and eviction,
  oversized entries, null images, and reset behavior.
- `deck_overview_visibility_test`: complete visible-row detection, scrolled content coordinates,
  and selected-page fallback while layout geometry is unavailable.
- `uil_package_test`: PDF/media/overlay round trips, hidden-overlay filtering, path traversal,
  duplicate entries, missing inputs, malformed archives, and API preconditions.
- `pdf_media_detector_test`: annotation helpers, empty results, page-tree ordering, linked and
  orphan annotations, rectangles, summaries, and package-relative path resolution.
- `performance_log_test`: session-log creation, process-start offsets, structured events, timed
  spans and checkpoints, field serialization, ordinary Qt diagnostic messages, and the native
  launcher's named readiness-event handshake.
- `native_launcher_test` (Windows): end-to-end launcher process creation, quoted argument
  forwarding, viewer discovery, progress-message delivery, and readiness-driven launcher shutdown
  using a system-only probe.

## Next priorities

1. Add `QtPdfBackend` integration tests using small checked-in PDFs that cover multiple page sizes,
   malformed input, password protection, and rendering at several target dimensions.
2. Add `RenderScheduler` asynchronous tests for duplicate suppression, generation cancellation,
   failure delivery, and object destruction while a render is active.
3. Introduce an injectable media-decoder interface around `VideoFrameReader`, then test
   `VideoFrameBuffer` timing, end-of-stream, buffering, stop/restart, and error propagation with a
   deterministic fake decoder.
4. Extract package ZIP parsing behind a byte-oriented interface so malformed central-directory,
   CRC, compression, truncation, and hostile-entry cases can be fuzzed directly.
5. Test `AppController` against fake PDF, render, and media backends. Cover navigation boundaries,
   stale render generations, cache reuse, overlay state, screen selection, and playback state.
6. Add Qt GUI tests for presenter/audience shortcuts, annotation coordinate transforms, blanking,
   deck-overview scrolling, and multi-screen transitions. Keep these separate from the fast unit
   label because they require a GUI platform plugin.
7. Run the unit label under AddressSanitizer and UndefinedBehaviorSanitizer in a Linux CI job, and
   retain the MSYS2 UCRT64 job as the authoritative Windows build and runtime check.

## Commands

From an MSYS2 UCRT64 shell:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run only the fast unit layer with `ctest --test-dir build -L unit --output-on-failure`.
