# UIL LaTeX packages

`uilorbital.sty` provides self-contained interactive atomic orbitals and a
pre-rendered atlas. `uilfigure.sty` embeds interactive numerical figures;
`uilmolecule.sty` provides molecule annotations.

## Atomic orbitals: quick start

Keep `uilorbital.sty` and the entire `uilorbital-atlas` directory together in
your project's LaTeX search path. Both are included in the Windows distribution
under `latex/`. With this repository, compile using:

```sh
TEXINPUTS=".:/path/to/uil/latex//:" pdflatex slides.tex
```

LuaLaTeX is also supported. No shell escape, OpenGL, or external renderer is
needed while compiling LaTeX: the atlas images are already generated.

```latex
\documentclass[aspectratio=169]{beamer}
\usepackage{uilorbital}
\uilorbitalsetup{palette=garnet_slate}
\begin{document}
\begin{frame}{Compare radial structure}
  \begin{columns}[c,onlytextwidth]
    \begin{column}{.49\textwidth}
      \atomicorbital[orbital=1s,width=\linewidth]
    \end{column}
    \begin{column}{.49\textwidth}
      \atomicorbital[orbital=2s,width=\linewidth]
    \end{column}
  \end{columns}
\end{frame}
\end{document}
```

The atlas contains **55 real hydrogen-like orbitals through 5g**, in
**Garnet–Slate** (the default) and **Garnet–Teal**. Each combination includes a
matching `.uilorb` definition and two high-resolution posters: `surface` and
`surface-contour`. Math labels, phase keys, node counts, and the interaction
badge are typeset by LaTeX, so they stay sharp in the PDF.

| Atlas key | Default | Meaning |
| --- | --- | --- |
| `orbital` | `1s` | Exact catalog identifier, e.g. `2pz`, `3dx2-y2`, `4fz(5z2-3r2)`. |
| `palette` | `garnet_slate` | `garnet_slate` or `garnet_teal`. |
| `layout` | `surface-contour` | `surface` shows just the orbital; `surface-contour` adds a signed wavefunction slice. |
| `width` | `\linewidth` | Outer card width, including frame and padding. |
| `height` | unset | Maximum total card height; reduces the entire card proportionally when necessary. |
| `atlas path` | `uilorbital-atlas` | Directory containing `catalog.tex` and the palette subdirectories. |
| `static` | `false` | `true` draws the poster without embedding a definition or adding an annotation. |

`\uilorbitalsetup{...}` sets defaults in the current TeX scope. Per-command
options are local and do not affect subsequent plots. For a relocated atlas:

```latex
\uilorbitalsetup{atlas path={assets/orbital atlas}}
\atomicorbital[orbital=3dz2,layout=surface,width=6cm,height=5cm]
```

Widths of roughly 5–7 cm suit side-by-side slide comparisons. Very small cards
remain valid but make their legends difficult to read. `height` is an upper
bound, not a request to stretch an image. Reserve the desired alignment space
with a surrounding `minipage` or Beamer column.

See `uilorbital-atlas/catalog.tex` for every accepted identifier and
`uilorbital-atlas/manifest.json` for the generated inventory. The bundled
`examples/orbital-atlas.pdf` provides a browsable Garnet–Slate catalog;
`examples/atomic-orbitals.pdf` demonstrates two plots on a slide. In UIL, both
are available through **File > Examples**.

## Custom orbital definitions and posters

The original three-part syntax remains supported:

```latex
\atomicorbital[width=7cm]
  {\includegraphics[width=\linewidth]{my-orbital-poster.png}}
  {my-orbital.uilorb}
```

Here the optional keys are `width`, `height`, `depth`, `borderwidth`, and
`static`. One size dimension scales proportionally. Supplying **both** width
and height deliberately requests an exact box and can distort the poster;
this legacy behavior is retained for existing documents. A specified `depth`
sets the final baseline depth. `borderwidth` is a nonnegative number in PDF
points, defaulting to zero.

The annotation is measured after sizing. Its rectangle covers the same outer
box as the poster, including its depth. Every call receives a unique embedded
asset reference, so multiple plots on a page remain independent. The generated
PDF contains the orbital definition; source JSON and PNG files are not needed
to view or present that PDF.

Use the custom API when changing sampling planes, contour limits, isovalues,
or colors beyond the two atlas palettes. Regenerate a matching preview when
changing the definition; merely relabeling an atlas image would misrepresent
its content. The `.uilorb` format is documented in the repository's
`docs/interactive-objects.md` and `docs/atomic-orbital-v1.schema.json`.

## Static output and scientific presentation

`\atomicorbital[orbital=2s,static=true]` produces an ordinary image card, with
an **ORBITAL** label in place of **INTERACTIVE**. XeLaTeX supports this static
mode; interactive annotations require pdfLaTeX or LuaLaTeX in PDF mode.
Missing atlas images or invalid options produce package errors with recovery
instructions; the package never silently substitutes a different orbital.

Posters use fitted, family-dependent viewing angles. S orbitals with radial
nodes have a labeled quadrant cutaway to expose nested phase surfaces. These
are sampled isosurfaces, not drawings of hard electron boundaries. Positive
and negative colors represent the sign of the real wavefunction, not charge.
The contour uses a signed logarithmic scale with absolute cutoffs from
`1e-8` to `1e-1` in Bohr wavefunction units; values outside that range use the
neutral or saturated color. All catalog cards use the same color limits.

Each orbital is fitted independently to its poster: **apparent sizes are not
a common spatial scale**. The posters are composed for publication. On
activation, UIL opens its normal dual-panel interactive view and controls;
the poster-only cutaway and fitted camera are not persistent live settings.

## Regenerating the atlas

From the repository root, using the Windows build's Qt/OpenGL environment:

```sh
cmake --build build-windows --target atomic_orbital_preview_generator
build-windows/atomic_orbital_preview_generator.exe --atlas latex/uilorbital-atlas
make -C examples/atomic-orbital
```

The generator derives all identifiers, math labels, and node counts from the
same orbital catalog used by UIL. It renders 220 PNGs and writes 110 embedded
definitions plus a TeX catalog and JSON manifest. Run it after changes to the
renderer, atlas palettes, or orbital calculations. Ordinary document builds
reuse these assets and never regenerate them. Existing custom previews can
still be generated with `OUTPUT_DIR INPUT.uilorb [...]`.

The LaTeX integration tests compile real PDFs and inspect their annotations:

```sh
python3 tests/latex_orbital_test.py
```

They require pdfLaTeX, LuaLaTeX, and qpdf; XeLaTeX static-mode checks run when
XeLaTeX is installed. Renderer tests are included in the CMake test suite.

## Molecule annotations

`uilmolecule.sty` adds a `\molecule` command that places a UIL-specific PDF
annotation over ordinary LaTeX poster content:

```latex
\usepackage{uilmolecule}

\molecule[width=7cm,height=5cm]
  {\fbox{\parbox[c][5cm][c]{7cm}{\centering Interactive water molecule}}}
  {water.xyz}
```

When `width`, `height`, or `depth` is supplied, the package resizes the poster
box to the requested outer dimensions before creating the annotation. This
keeps frames and other poster ink aligned with the interactive surface.

The geometry uses the standard XYZ layout: atom count, one description line,
then one element symbol and three Cartesian coordinates per atom. Coordinates
are interpreted as angstrom because UIL uses angstrom covalent radii to infer
bonds. Keep the XYZ file beside the generated PDF, or preserve both by saving
the presentation as a `.uil` package. pdfLaTeX and LuaLaTeX are supported by
this first version.

Geometry can live in the `.tex` source by creating XYZ assets with
`filecontents*`; see the three-slide `examples/molecule/molecule-example.tex`.
That test deck uses the `chctheme` package from the adjacent
`lecture-slides-chemical-bonding` project; add its `chctheme` directory to
`TEXINPUTS` when compiling the example.

The poster remains ordinary PDF page content, so PDF viewers that do not know
about UIL display the fallback while UIL replaces its rectangle with the live
molecular view.

This experimental version shows the first molecule annotation on a slide in the
audience window. Select the classic cursor tool to interact with it; the widget
is temporarily hidden for the laser pointer, pencil, and eraser so those slide
tools continue to receive mouse input. Presenter previews, exported PDFs, and
other PDF viewers show the poster instead of the live view.
