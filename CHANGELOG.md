# Changelog

Notable user-facing changes to `uil` are documented here.

## 1.0.1 - 2026-11-22

### Fixed

- Windows: the fullscreen audience display no longer flashes black when a tooltip or
  dialog appears above it, for example while picking the pen and its ink colour before
  drawing on a slide.

## 1.0.0 - 2026-11-22

First stable release.

### Changed

- Smoother drawing: pen and eraser strokes repaint only the area they touch, and the
  presenter preview follows along ten times per second instead of copying the whole
  annotation layer on every mouse movement.
- Smoother laser pointer and video: only the pointer and the video area are repainted.
- Faster slide changes: a cached slide is shown on the next event-loop turn, rendered
  slides are painted 1:1 without rescaling, and holding an arrow key no longer queues
  renders for every slide passed.
- The presenter's thumbnail strip no longer delays rendering of the next slides, and
  the presenter and audience slide grids no longer evict each other's thumbnails.
- PDF pages render on a single background worker from an in-memory copy of the
  deck, which uses less memory and keeps the current slide from waiting behind
  speculative work.
- Smoother video playback: decoding uses multiple CPU cores, frames are drawn without
  per-pixel blending, and frames are timed against a steady clock, dropping late ones.
- Smoother interactive figures and orbital views: math labels are laid out once and
  reused, the atomic-orbital colour bar and axes are no longer rebuilt every frame, and
  animations run at a constant speed regardless of timer jitter.
- Simplified molecular-symmetry orbital selection to the `s`, `p`, and `d`
  symmetry types, with normalized render sizes and properly formatted notation.
- The Windows startup splash now disappears after 60 seconds if the viewer has not
  finished loading, and installs in folders with paths longer than 260 characters start
  correctly.
- The LaTeX packages `uilfigure`, `uilmolecule`, `uilorbital`, and `uilsymmetry` are
  versioned 1.0, and the browsable orbital atlas (`orbital-atlas.pdf`) is now installed
  in the `examples` folder.

### Fixed

- Unsaved drawings are no longer lost: **F5** (reload and present) keeps them, and
  closing uil or opening another presentation asks whether to save them first.
- **Export as PDF** writes the file atomically, refuses to overwrite the open
  presentation, and suggests the folder of the `.uil` package instead of a temporary
  folder.
- Saving a `.uil` package right after opening a PDF now includes its videos and
  molecule files.
- Rebuilding the PDF while it is open no longer mixes old and new slides; the new
  version is picked up on reload.
- Video poster frames now appear on slides that were rendered before the media scan
  finished.
- Fixed a thin transparent or garbage strip at the right edge of videos whose width is
  not a multiple of eight.
- HD videos now show accurate colours: the colour space and range recorded in the file are
  honoured, and untagged HD video uses the BT.709 standard instead of the SD one.
- A playing molecule vibration now resumes after the view was hidden, for example by
  switching to the pointer and back, just like automatic rotation.
- Changing slides during video playback stops the video promptly, and a single corrupt
  video frame no longer ends playback.
- Malformed or hostile PDFs (deeply nested or cyclic page trees, unterminated objects,
  decompression bombs, huge annotation arrays) can no longer crash, hang, or exhaust
  memory in the background media scan.
- `.uil` manifests with absurd overlay page numbers are ignored safely.
- Fixed molecule views releasing OpenGL resources under the wrong context when moved
  to another window.
- Preserve molecular-symmetry render settings independently while switching slides.
- Present OpenGL content immediately after entering fullscreen so starting a deck
  with F5 no longer leaves the audience display black until pointer input.
- The cursor now hides again during video playback on slides with a molecular-symmetry
  player.

## 0.3.0 - 2026-09-27

### Added

- Added self-contained atomic-orbital objects covering all real hydrogen-like
  orbitals from 1s through 5g, with signed isosurfaces and configurable phase colors.
- Added a movable world-fixed sampling plane, logarithmic signed-wavefunction
  contours, divergent color maps, and a colorbar beside the rotatable orbital.
- Added a world-fixed, depth-tested black XYZ axis triad with billboarded 3D
  endpoint labels to the orbital scene.
- Added the `uilorbital` LaTeX package, version-one JSON schema, authoring guide,
  and bundled 2s, 2pz, 3dz2, and 4fz3 example presentation.
- Added one-click red/cyan anaglyph molecule rendering.
- Added sinusoidal normal-mode playback from optional XYZ displacement vectors.
- Added a Blender-style X/Y/Z orientation gizmo to molecule views.
- Added a compact, collapsible molecule toolbar for stereo, vibration, axes, and view reset.
- Added a bundled interactive molecule presentation that demonstrates every visualizer control.
- Added rendered molecule stills as fallbacks when the example deck is opened in a PDF reader.
- Added a subtle gray outline that distinguishes a live interactive molecule from its PDF poster.
- Added a toolbar toggle for continuous rotation around the molecule's local Z axis.
- Changed the default molecule camera to look along the negative X axis with positive Z upward.
- Placed the example water molecule in the YZ plane and added benzene to the bundled tour.

### Fixed

- Render atomic-orbital titles and panel notation as math text, enlarge their
  controls, and replace fixed-axis mouse rotation with a quaternion arcball.
- Use a JSON-configurable, power-of-ten absolute maximum for orbital contour
  colors, with a fixed `1e-8` cutoff and scientific-notation colorbar limits.
- Generate atomic-orbital PDF posters from the live renderer and label them as
  interactive, keeping fallback previews consistent with the live object.

- Forward molecule right-clicks immediately to an audience-window overlay and suspend the OpenGL
  surface while it is visible, preventing delayed menus and black fullscreen compositor frames.
- Preserve the molecule's last rendered frame beneath the audience controls and while pointer or
  annotation tools are active instead of reverting to its static PDF poster.
- Close the audience controls immediately when returning to the classic cursor, allowing the next
  click-and-drag to rotate the molecule without an extra activation click.

### Changed

- Use OpenGL 3.3 core-profile rendering as the baseline for molecule and atomic-orbital views.

## 0.2.0 - 2026-09-02

### Added

- Added a presenter start button, Logitech Spotlight presence status, and build identifier.
- Added persistent slider and numeric controls for laser-pointer size.
- Added `G` and `L` presentation shortcuts for the slide grid and laser pointer.
- Added two bundled sample decks under **File > Examples**.

### Changed

- Updated the presentation controls and pointer-size input styling.
- Hide the laser pointer after three seconds of inactivity, matching PowerPoint's default.
- Expanded automated coverage for controller workflows, audience shortcuts and annotations,
  pointer settings, Spotlight detection, render scheduling, PDF errors, and video buffering.

## 0.1.1 - 2026-08-27

### Changed

- Refreshed the application branding and splash screen.
- Added classic Bluecurve-style icons to menus and buttons.
- Improved Windows license-inventory generation performance.

## 0.1.0 - 2026-08-26

Initial release.

### Added

- Windows presentation viewer for PDF and `.uil` files.
- Separate presenter and audience views with selectable display output.
- Full-screen presenting, slide overview, page jumping, and keyboard navigation.
- Embedded MP4 playback for supported Beamer media annotations.
- Pointer, pencil, and eraser tools for live presentations.
- Saving presentations and annotations as `.uil` packages.
- Exporting annotated presentations to PDF or individual slide images.
- Black and white screen modes.
- Slide caching and background rendering for responsive navigation.
- Windows installer distributed through GitHub Releases.
