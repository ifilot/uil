# Interactive objects

UIL supports four presentation-object families that replace a rectangular
piece of ordinary PDF content while the audience view is active: interactive
figures, interactive molecules, atomic orbitals, and molecular symmetry
players. In every case the LaTeX document supplies a static poster, so the
slide remains useful in an ordinary PDF reader.

## Object inventory

### Interactive figures

Interactive figures use `\interactivefigure` from `latex/uilfigure.sty`. The
command embeds a version-one `.uilfig` JSON document in the PDF; consequently,
the finished PDF does not need the source JSON file beside it.

| Figure kind | Display and interaction |
|---|---|
| `sine-wave` | Animated sine curve with amplitude and frequency sliders and play/pause/reset controls. |
| `harmonic-bond-wavepacket` | Harmonic potential and synchronized probability density with phase and initial-stretch sliders, playback, optional looping, and an optional energy reference. |
| `harmonic-basis-states` | Moving coherent-state density above the real parts of six weighted number-state components, with phase and initial-stretch sliders and playback. |
| `particle-in-box-step-expansion` | Step target and its particle-in-a-box basis expansion, with a live integer basis-count slider and reproduced-fraction readout. |
| `harmonic-displaced-state-expansion` | Displaced harmonic-oscillator target and finite basis expansion, with a live integer basis-count slider and captured-norm readout. |

All figure types support a title, self-contained background SVG, plot colors,
axis bounds, and plain-text or supported LaTeX-style math labels. The complete
version-one contract is in
[`interactive-figure-v1.schema.json`](interactive-figure-v1.schema.json).

#### Minimal figure source

Create `moving-wave.uilfig`:

```json
{
  "format": "uil.interactive-figure",
  "version": 1,
  "title": "A moving sine wave",
  "background_svg": "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 500'><rect width='800' height='500' fill='#f8fafc'/></svg>",
  "plot": {
    "kind": "sine-wave",
    "color": "#2563eb",
    "x_min": -6.283,
    "x_max": 6.283,
    "y_min": -2.5,
    "y_max": 2.5,
    "x_label": "$x$",
    "y_label": "$y$"
  },
  "controls": {
    "amplitude": { "min": 0.0, "max": 2.0, "value": 1.0 },
    "frequency": { "min": 0.25, "max": 3.0, "value": 1.0 },
    "animate": true
  }
}
```

Reference it from `slides.tex`. The second argument is the ordinary PDF poster;
it can be a TikZ/PGFPlots drawing, an image, or any other LaTeX box.

```latex
\documentclass{beamer}
\usepackage{uilfigure}

\begin{document}
\begin{frame}{Interactive wave}
  \centering
  \interactivefigure[width=10cm,height=6cm]
    {\fbox{\parbox[c][6cm][c]{10cm}{\centering Static wave poster}}}
    {moving-wave.uilfig}
\end{frame}
\end{document}
```

Make the UIL LaTeX package discoverable and compile with pdfLaTeX or LuaLaTeX:

```sh
TEXINPUTS=".:/path/to/uil/latex//:" pdflatex slides.tex
```

The repository's complete five-object example can be built with:

```sh
make -C examples/interactive-figure
```

Its payloads are useful starting points:

- `examples/interactive-figure/moving-wave.uilfig`
- `examples/interactive-figure/harmonic-wavepacket.uilfig`
- `examples/interactive-figure/harmonic-basis-states.uilfig`
- `examples/interactive-figure/particle-in-box-step-expansion.uilfig`
- `examples/interactive-figure/harmonic-displaced-state-expansion.uilfig`

### Interactive molecules

Interactive molecules use `\molecule` from `latex/uilmolecule.sty`. UIL reads
standard XYZ geometry and renders a ball-and-stick model. It infers bonds from
the coordinates and covalent radii.

The viewer provides:

- left-button drag to rotate;
- mouse-wheel zoom and double-click reset;
- continuous rotation around the molecule's local Z axis;
- mono or red/cyan anaglyph rendering;
- a Blender-style X/Y/Z orientation gizmo; and
- optional normal-mode vibration playback.

The toolbar can be collapsed. Atom rows may include the optional displacement
vector `dx dy dz`; omitted vectors are treated as zero. A geometry with at least
one non-zero displacement can use vibration playback, while ordinary XYZ files
without those columns remain static.

#### Minimal molecule source

Create `water.xyz`:

```text
3
Water bending mode
O  0.0000  0.0000  0.0000   0.0000  0.0000 -0.1000
H  0.0000  0.9572  0.0000   0.0000  0.0000  0.2400
H  0.0000 -0.2390  0.9270   0.0000  0.2200 -0.0600
```

Reference it from `slides.tex`:

```latex
\documentclass{beamer}
\usepackage{uilmolecule}

\begin{document}
\begin{frame}{Interactive water molecule}
  \centering
  \molecule[width=10cm,height=6cm]
    {\fbox{\parbox[c][6cm][c]{10cm}{\centering Static molecule poster}}}
    {water.xyz}
\end{frame}
\end{document}
```

Compile with pdfLaTeX or LuaLaTeX:

```sh
TEXINPUTS=".:/path/to/uil/latex//:" lualatex slides.tex
```

Keep `water.xyz` beside `slides.pdf` when presenting it in UIL. Alternatively,
open the PDF in UIL and save it as a `.uil` package to keep the PDF, molecule
geometry, and annotations in one portable file. The static poster is retained
inside the PDF either way.

The full molecule tour uses a project-specific Beamer theme. Point its build at
that theme and run:

```sh
make -C examples/molecule CHC_THEME_DIR=/path/to/chctheme pdf
```

The example demonstrates methane, water, carbon dioxide, and benzene. To build
the copy shipped with UIL, including its XYZ sidecars, use the default `bundle`
target instead of `pdf`.

### Atomic orbitals

Atomic orbitals use `\atomicorbital` from `latex/uilorbital.sty`. The command
embeds a version-one `.uilorb` JSON document in the PDF, so the result is fully
self-contained and needs no runtime sidecar.

The renderer evaluates normalized, real hydrogen-like wavefunctions in atomic
units. It samples a three-dimensional scalar texture and constructs positive
and negative signed-wavefunction isosurfaces. An orthographic scene shows the
rotatable orbital and a translucent sampling plane on the left, with the
corresponding signed-$\psi$ contour map and colorbar on the right. All 3D
renderers use OpenGL 3.3 as their baseline.

Interaction treats the orbital, axes, and contour plane as one rigid local
coordinate system:

- drag over the left panel to rotate the orbital, x/y/z triad, and sampling
  plane together with a camera-relative virtual trackball (quaternion arcball);
- use the black x/y/z triad to read the rotated local orientation; its
  endpoint labels are billboarded 3D glyphs, and both lines and letters are
  depth-tested so the orbital correctly obscures them;
- use the mouse wheel to move the orthographic camera closer or farther away;
- choose `xy`, `xz`, or `yz`; changing planes resets it to the origin;
- move the slider to translate the selected plane within the local system;
- double-click or press **Reset** to restore rotation, zoom, and plane offset;
- the right contour panel remains screen-aligned while it updates from the
  selected local plane.

The implementation follows Managlyph's real-orbital convention and ordering
through 4f, using an independent mathematical implementation of the hydrogenic
radial and angular functions. Since Managlyph's published catalog ends at 4f,
UIL extends the same real-tesseral naming convention to 5g.

#### Orbital catalog

The `orbital` property accepts the following 55 names:

| Shell | Accepted names |
|---|---|
| 1 | `1s` |
| 2 | `2s`, `2px`, `2py`, `2pz` |
| 3 | `3s`, `3px`, `3py`, `3pz`, `3dxy`, `3dxz`, `3dyz`, `3dx2-y2`, `3dz2` |
| 4s/4p | `4s`, `4px`, `4py`, `4pz` |
| 4d | `4dxy`, `4dxz`, `4dyz`, `4dx2-y2`, `4dz2` |
| 4f | `4fy(3x2-y2)`, `4fxyz`, `4fy(5z2-r2)`, `4fz(5z2-3r2)`, `4fx(5z2-r2)`, `4fz(x2-y2)`, `4fx(x2-3y2)` |
| 5s/5p | `5s`, `5px`, `5py`, `5pz` |
| 5d | `5dxy`, `5dxz`, `5dyz`, `5dx2-y2`, `5dz2` |
| 5f | `5fy(3x2-y2)`, `5fxyz`, `5fy(5z2-r2)`, `5fz(5z2-3r2)`, `5fx(5z2-r2)`, `5fz(x2-y2)`, `5fx(x2-3y2)` |
| 5g | `5gxy(x2-y2)`, `5gyz(3x2-y2)`, `5gxy(7z2-r2)`, `5gyz(7z2-3r2)`, `5g(35z4-30z2r2+3r4)`, `5gxz(7z2-3r2)`, `5g(x2-y2)(7z2-r2)`, `5gxz(x2-3y2)`, `5g(x4-6x2y2+y4)` |

#### Minimal atomic-orbital source

Create `2pz.uilorb`:

```json
{
  "format": "uil.atomic-orbital",
  "version": 1,
  "title": "Hydrogen-like $2p_z$ orbital",
  "orbital": "2pz",
  "surface": {
    "isovalue": 0.0,
    "positive_color": "#2563eb",
    "negative_color": "#dc2626",
    "grid_size": 65
  },
  "sampling_plane": {
    "type": "xz",
    "offset": { "min": -1.0, "max": 1.0, "value": 0.0 }
  },
  "contour": {
    "colormap": "RdBu_r",
    "maximum": 0.1,
    "levels": 14
  }
}
```

An `isovalue` of zero selects an automatic signed-wavefunction isovalue based
on Managlyph's 95% radial-probability cutoff. Set a positive value to hardcode
the absolute isovalue. `grid_size` must be an odd integer from 33 through 129;
65 is the default balance between responsiveness and detail. Plane offsets are
fractions of the sampled volume half-extent.

`title` supports the same compact LaTeX math notation as interactive-figure
labels. Delimit math with `$...$`; subscripts, superscripts, Greek symbols, and
`\mathrm{...}` are rendered as formatted text rather than literal markup.

Contour magnitudes are mapped on a fixed absolute logarithmic scale. The lower
cutoff is always `1e-8` in wavefunction units; values below it use the neutral
center color. Set `contour.maximum` to choose the symmetric positive and
negative color limits. It defaults to `1e-2` and is snapped to the nearest
power of ten, so the scale remains stable and its scientific-notation limits
stay easy to read while the plane moves. Values beyond either limit saturate.

Available Matplotlib-inspired divergent maps are `coolwarm`, `seismic`, `bwr`,
`RdBu`, `PuOr`, `BrBG`, `PiYG`, `PRGn`, and `RdGy`; append `_r` to reverse any
map. The CHC-inspired `garnet_teal` and `garnet_slate` maps (also available
with `_r`) coordinate with burgundy slide themes. See the
[color scheme gallery](../examples/atomic-orbital/color-schemes/README.md)
for rendered examples and matching surface colors.

For an automatically styled poster and embedded definition, use the atlas API:

```latex
\usepackage{uilorbital}
\uilorbitalsetup{palette=garnet_slate}
% Inside a frame:
\atomicorbital[orbital=2pz,layout=surface-contour,width=\linewidth]
```

The shipped atlas covers every supported orbital, in Garnet–Slate and
Garnet–Teal. It also provides a `surface` layout, plus `static=true` for an
ordinary poster without an annotation. Optional `height` constrains the card
without stretching it. See the [package guide](../latex/README.md) for setup
and every option.

For a custom definition, reference the payload from `slides.tex`:

```latex
\documentclass{beamer}
\usepackage{uilorbital}

\begin{document}
\begin{frame}{Interactive atomic orbital}
  \centering
  \atomicorbital[width=11cm,height=6cm]
    {\fbox{\parbox[c][6cm][c]{11cm}{\centering Static orbital poster}}}
    {2pz.uilorb}
\end{frame}
\end{document}
```

Make the UIL LaTeX package discoverable and compile with pdfLaTeX or LuaLaTeX:

```sh
TEXINPUTS=".:/path/to/uil/latex//:" pdflatex slides.tex
```

The full version-one contract is in
[`atomic-orbital-v1.schema.json`](atomic-orbital-v1.schema.json). The bundled
five-slide presentation starts with a side-by-side `1s`/`2s` comparison, then
demonstrates `2s`, `2pz`, `3dz2`, and `4fz(5z2-3r2)` (displayed as `4f_z^3`).
Use multiple `\atomicorbital` commands on the same frame, for example inside
Beamer columns. Each plot has independent rotation, sampling-plane controls,
and reset; UIL displays every valid orbital annotation on the current slide.

```sh
make -C examples/atomic-orbital
```

The example uses publication-style posters generated by
`atomic_orbital_preview_generator --atlas`: no controls, fitted camera angles,
and labeled cutaways for s orbitals with radial nodes. LaTeX adds sharp labels,
phase keys, node counts, and the **INTERACTIVE** badge. The live widget uses the
same orbital definitions but opens its normal dual-panel interactive view.
The make target also builds the 14-page Garnet–Slate catalog with all 55 orbitals.

### Molecular symmetry

Molecular symmetry players use `\molecularsymmetry` from
`latex/uilsymmetry.sty`. A self-contained `.uilsym` JSON payload combines XYZ
coordinates, a point-group label, and the complete operation list. UIL renders
the molecule beside clickable buttons; selecting a button animates the proper
rotation, reflection, inversion, or improper rotation and then restores the
starting geometry. The selected symmetry element remains in the 3D scene:
proper rotations show their axis, reflections show a translucent mirror plane,
inversion shows its center, and improper rotations show both axis and plane.
Symmetry operations and the molecule-fixed Cartesian axes use `(0,0,0)` from
the embedded XYZ coordinates as their common origin. Place the central atom at
the origin when that atom is the point-group center.
Buttons and rendered symmetry elements are color-coded by operation class. The
optional `operation_colors` object accepts `#RRGGBB` values for `identity`,
`rotation`, `reflection`, `inversion`, and `improper_rotation`; omitted entries
use UIL's default palette.

```json
"operation_colors": {
  "identity": "#5f6b78",
  "rotation": "#9b2f4f",
  "reflection": "#277d83",
  "inversion": "#7a5aa6",
  "improper_rotation": "#c06a2b"
}
```

```latex
\documentclass[aspectratio=169]{beamer}
\usepackage{uilsymmetry}
\begin{document}
\begin{frame}{Water symmetry}
  \molecularsymmetry[width=12cm,height=6cm]
    {\fbox{\parbox[c][6cm][c]{12cm}{\centering Static $C_{2v}$ poster}}}
    {water.uilsym}
\end{frame}
\end{document}
```

The payload format is documented by
[`molecular-symmetry-v1.schema.json`](molecular-symmetry-v1.schema.json). The
bundled five-slide example covers water (`C2v`), ammonia (`C3v`), boron
trifluoride (`D3h`), ethylene (`D2h`), and methane (`Td`):

```sh
make -C examples/molecular-symmetry bundle
```

## Poster and sizing arguments

All four commands have the same basic shape:

```latex
\interactivefigure[width=...,height=...,depth=...,borderwidth=...]
  {poster material}{figure.uilfig}

\molecule[width=...,height=...,depth=...,borderwidth=...]
  {poster material}{geometry.xyz}

\atomicorbital[width=...,height=...,depth=...,borderwidth=...]
  {poster material}{orbital.uilorb}

\molecularsymmetry[width=...,height=...,borderwidth=...]
  {poster material}{symmetry.uilsym}
```

If explicit dimensions are omitted, the interactive rectangle follows the
natural dimensions of the poster box. Supplying dimensions resizes that poster
before the PDF annotation is created, keeping the fallback and live object
aligned.

Live objects appear in the audience window while the classic cursor tool is
selected. UIL temporarily suspends them when presentation drawing tools or
menus need the same pointer input. Presenter previews, PDF exports, and PDF
readers that do not understand UIL annotations show the static poster.

## Orbital transition performance

Audience orbital geometry is prepared by two low-priority worker threads. A cold slide can
appear using its embedded poster while its interactive surfaces are being constructed; sampling
and meshing no longer block navigation. OpenGL uploads remain on the GUI thread.

A 128 MiB least-recently-used cache retains sampled volumes and signed meshes. The key includes
quantum numbers, grid resolution, and isovalue. Titles, palettes (including Garnet–Slate), and
sampling controls share geometry. Qt's implicitly shared arrays avoid copying large volumes
into each widget. Unchanged definitions preserve the existing view and GPU resources; presentation
changes using the same cached volume reuse the geometry buffers and refresh the palette/plane.

Each plot also retains up to 32 MiB of inactive GPU meshes and 3D textures using an LRU
cache. Revisiting a retained orbital restores those resources without uploading geometry again;
palettes and sampling-plane controls are refreshed independently. This budget counts vertex and
texture payloads, excluding the active orbital, framebuffer attachments, and driver overhead.
Cached resources are released in their owning OpenGL context, including when Qt recreates that
context. They do not retain additional copies of the CPU volume arrays.

Current-slide requests precede prefetches for the next, previous, and second-next slide.
Repeated requests share one build. Navigating replaces unstarted speculative requests; at most
two already-running builds finish before new work can start. Completions only populate matching
active overlays, and overlays wait for the corresponding document/page image before appearing.

The budget covers retained CPU cache entries; active widgets, GPU buffers, and the two in-flight
builds use additional memory. Oversized volumes are delivered without retention. This cache lasts
for the audience window's lifetime and does not write files or require changes to LaTeX decks.

The Windows release-build regression benchmark (Qt 6.10.1, grid 81) measured a 5g build at
335 ms, an asynchronous request at 0.11 ms, and shared cache lookup at 0.043 microseconds.
The atlas integration test measured three rapid cold navigation calls at 1.23 ms combined
and a cached four-orbital navigation call at 76 ms before GPU caching. With the GPU cache,
the same integration test measured 38 ms and verified zero geometry uploads on the revisit.
These are local
measurements, not frame-presentation guarantees; uncached PDF rendering and GPU work still
contribute to visible transition time. Reproduce the measurements with
`atomic_orbital_cache_test -o cache.txt,txt` and
`app_controller_test orbital_atlas_navigation -o navigation.txt,txt` from the build directory.
