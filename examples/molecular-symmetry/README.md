# Molecular symmetry example

This five-slide deck embeds complete, self-contained molecular symmetry objects
for water (`C2v`), ammonia (`C3v`), boron trifluoride (`D3h`), ethylene
(`D2h`), and methane (`Td`). Each operation in the finite point group has its
own clickable button.

The default view looks toward the origin along the positive XYZ body diagonal:
Z points up on screen, X down-left, and Y down-right. Dragging still changes the camera.
**Reset camera** restores this view and the default zoom without changing the
selected orbitals or symmetry operation. Continuous Z rotation retains its on/off state.

The **Settings** tab supports per-atom selections of the real 1s, 2s, 2p,
and 3d basis functions. Water starts with an oxygen 2px orbital: try its yz
reflection to see the phase colors reverse while the oxygen stays fixed.
Choose an atom from the visible list and check the desired orbitals. Default
choices are filtered to the element's occupied neutral-atom subshells; advanced
mode exposes the full supported basis. Switch to **Symmetry operations** to
animate the result. Clicking elsewhere does not dismiss Settings.
The presenter preview and ordinary PDF readers show a static poster, not live controls.
See [symmetry orbital authoring and basis functions](../../docs/symmetry-orbitals.md)
for the JSON defaults, sign conventions, and offline mesh regeneration.

The header's **Z rotation** toggle spins the molecule continuously around its local
Z axis. Camera dragging and symmetry playback remain available while it spins;
the reference pose and symmetry elements rotate with the molecule. Toggle it off
to hold the current orientation. Rotation pauses while the slide is hidden.

Operations are grouped by type in the live controls. During playback a translucent
original pose remains visible. Reflections flatten and restore atom surfaces along
the mirror normal, so even atoms whose centers lie in the plane visibly map onto
themselves. This is a visual cue, not a physical deformation of the molecule.

The PNG posters are captured from the same OpenGL widget and are embedded in the
PDF, so the molecule is also visible in the presenter preview and ordinary PDF readers.
After building the `molecular_symmetry_preview_generator` target, regenerate them
in a shell with the Qt runtime available using `make previews`. Override
`PREVIEW_GENERATOR` to select a different build directory. A working OpenGL display
is required for regeneration; the checked-in PNGs suffice to build the PDF.

Build and copy the bundled PDF with:

```sh
make bundle
```

The `.uilsym` sources contain the XYZ geometry and explicit operation axes, so
the presentation has no runtime sidecars.

Use the optional `operation_colors` object in a `.uilsym` file to customize the
`identity`, `rotation`, `reflection`, `inversion`, and `improper_rotation`
class colors. Values use `#RRGGBB` notation.
