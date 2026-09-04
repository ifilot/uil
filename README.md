# uil

Pronounced `/œyl/`.

[![Version](https://img.shields.io/badge/version-v0.2.1-blue.svg)](https://github.com/ifilot/uil/releases)
[![License: LGPL v3](https://img.shields.io/badge/license-LGPL--3.0--only-blue.svg)](LICENSE)
[![Sanitizers](https://github.com/ifilot/uil/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/ifilot/uil/actions/workflows/sanitizers.yml)

`uil` is a presentation app for Windows. It displays PDF slide decks, supports
embedded video in Beamer presentations, and lets you annotate slides while
presenting. Presentations and annotations can be saved together as a `.uil`
file or exported to PDF.

## Install

1. [Download the latest Windows installer](https://github.com/ifilot/uil/releases/latest/download/uil-windows-x64-setup.exe).
2. Run the installer and launch **uil** from the Start menu.

Windows 10 or newer is required.

## Present

Open a `.pdf` or `.uil` file, select the audience display, and press `F5`.
Right-click the audience view to choose the pointer, pencil, or eraser; change
tool settings; browse all slides; or close the presentation. The laser pointer
automatically disappears after three seconds without movement and reappears as
soon as it moves again.

Four sample decks are included with the Windows distribution. They are all
ordinary `.pdf` files, so the same files can also be opened in a regular PDF
reader. Open them from
**File > Examples** for tours of navigation, pointer and annotation features,
the interactive molecule visualizer, and the embedded interactive-figure
examples, including a displaced harmonic-bond wavepacket.

| Action | Key |
|---|---|
| Next slide | `Right`, `Page Down`, or `Space` |
| Previous slide | `Left`, `Page Up`, or `Backspace` |
| First / last slide | `Home` / `End` |
| Play or pause video | `Enter` |
| Black / white screen | `B` / `W` |
| Show slide grid | `G` |
| Select laser pointer | `L` |
| Toggle full screen | `F11` |
| Leave full screen | `Esc` |

From the presenter window, press `O` for the slide overview or `J` to jump to a
page. Use **File > Save** to preserve the presentation and annotations as a
`.uil` file, or **File > Export as PDF** to create an annotated PDF.

## Interactive molecules (experimental)

The molecule visualizer accepts XYZ geometry referenced by the dedicated
`\molecule` command in `latex/uilmolecule.sty`. UIL replaces the command's
poster rectangle with a ball-and-stick view: drag with the left mouse button to
rotate, use the mouse wheel to zoom, and double-click to reset. Its floating
toolbar can collapse to a single button and provides a one-click red/cyan
anaglyph toggle, vibration playback, continuous local Z-axis rotation, and an
orientation-axis gizmo.

Normal-mode animation uses an extended but backwards-compatible XYZ atom row:
`element x y z dx dy dz`. The final three values are displacement components;
ordinary four-column XYZ files remain static.

See `examples/molecule/molecule-example.tex` and the bundled **Interactive
Molecule Visualizer** PDF for a complete tour. pdfLaTeX and LuaLaTeX are
supported. Its small generated XYZ sidecars must remain beside the PDF for UIL
interaction; an ordinary PDF reader needs only the PDF and displays its still
images. Saving as a `.uil` package remains available when a single portable
file containing those molecule assets is preferred.

The poster argument remains visible in ordinary PDF readers. Pass a rendered
still with `\includegraphics`, as demonstrated by the example deck, and UIL
will replace that image with the interactive molecule while presenting.

## Interactive figures (prototype)

UIL can also replace a static PDF poster with a self-contained interactive
figure. Version one includes an animated sine curve, a displaced harmonic-bond
wavepacket with synchronized potential and probability-density plots, and a
six-component basis view showing a moving coherent density above the evolving
real parts of its weighted number-state components. The plots, sliders, and
playback actions use
native Qt painting and controls; no browser engine or JavaScript runtime is
involved.

The `\interactivefigure` command in `latex/uilfigure.sty` embeds the complete
`.uilfig` JSON payload into the PDF as an EmbeddedFile stream. Ordinary PDF
readers continue to show the supplied poster, while UIL detects the custom
annotation and activates the figure in the audience view. The finished PDF has
no external runtime asset dependency.

The embedded runtime document is defined by
`docs/interactive-figure-v1.schema.json`. Titles, axis labels and ranges, SVG
artwork, curve styling, controls, and initial values all come from that one JSON
document, including all figure colors and whether harmonic playback loops.
Surrounding slide content, the static PDF poster, and the annotation
rectangle are authored by LaTeX, because a conventional PDF reader does not
interpret the UIL JSON.

See `examples/interactive-figure` for the single three-slide deck containing
the sine-wave, coherent wavepacket, and basis-state examples. The set of
supported plot kinds remains deliberately narrow while the embedding and
interaction model is evaluated.

## Troubleshooting

The current session log is listed under **Help > About uil**. Logs can contain
local file paths, so review them before sharing.

## License

`uil` is available under the [GNU LGPL v3.0](LICENSE).
