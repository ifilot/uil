# Changelog

Notable user-facing changes to `uil` are documented here.

## 0.2.1 - Unreleased

### Added

- Added one-click red/cyan anaglyph molecule rendering.
- Added sinusoidal normal-mode playback from optional XYZ displacement vectors.
- Added a Blender-style X/Y/Z orientation gizmo to molecule views.
- Added a compact, collapsible molecule toolbar for stereo, vibration, axes, and view reset.
- Added a bundled interactive molecule presentation that demonstrates every visualizer control.
- Added rendered molecule stills as fallbacks when the example deck is opened in a PDF reader.
- Added a subtle gray outline that distinguishes a live interactive molecule from its PDF poster.

### Fixed

- Forward molecule right-clicks immediately to an audience-window overlay and suspend the OpenGL
  surface while it is visible, preventing delayed menus and black fullscreen compositor frames.
- Preserve the molecule's last rendered frame beneath the audience controls and while pointer or
  annotation tools are active instead of reverting to its static PDF poster.
- Close the audience controls immediately when returning to the classic cursor, allowing the next
  click-and-drag to rotate the molecule without an extra activation click.

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
