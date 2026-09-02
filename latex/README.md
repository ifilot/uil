# UIL LaTeX molecule annotations

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
