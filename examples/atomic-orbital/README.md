# Atomic-orbital example

This four-slide Beamer presentation embeds hydrogen-like `2s`, `2pz`, `3dz2`,
and `4fz(5z2-3r2)` orbital definitions directly in its PDF. Build and copy the
presentation to `examples/bundled` with:

```sh
make
```

The static posters are captured from the same OpenGL widget and are visible in
every PDF reader. UIL replaces each poster with the live OpenGL 3.3 orbital and
signed-wavefunction contour renderer. Regenerate the images after renderer
changes with the `atomic_orbital_preview_generator` development target.

Each `contour.maximum` selects a stable symmetric absolute color limit. UIL
snaps it to the nearest power of ten; the lower cutoff is fixed at `1e-8`.
