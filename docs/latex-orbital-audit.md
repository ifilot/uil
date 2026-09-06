# Orbital LaTeX package audit

Reviewed and implemented on 2026-09-06. Scope: `latex/uilorbital.sty`, its
atlas generator, generated assets, and the path from LaTeX through embedded
PDF annotations to UIL's orbital widgets.

The original package was a useful embedding prototype. It required authors
to prepare every poster manually and lacked enough isolation, validation,
and integration tests to call its authoring API polished. The revised package
is suitable for routine slide authoring, with a small atlas API and a retained
custom-poster API for advanced configurations.

## Findings and resolutions

| Finding | Resolution |
| --- | --- |
| Every orbital required a manually managed preview and JSON file. | A catalog-derived atlas supplies all 55 orbitals, two palettes, and two layouts. A single keyed command selects matching assets. |
| Static screenshots included interactive controls and rasterized labels. | Poster capture hides controls, fits the mesh, chooses family-dependent camera angles, and labels s-shell cutaways. LaTeX owns labels, legends, framing, and badges. |
| Settings and scratch dimensions were not explicitly scoped per call. | Both authoring paths are grouped; per-call palette and sizing changes cannot leak into later plots. Dedicated boxes replace shared LaTeX scratch boxes. |
| Width and height could silently stretch a poster. | Atlas height is a proportional upper bound. The custom API retains exact-box sizing for compatibility, documented explicitly. |
| Explicit depth could be lost during resizing. | The custom API rebases the finished box to its requested depth; PDF rectangles use the measured final width, height, and depth. |
| Engine checks did not distinguish non-PDF output, and loading `embedfile` prevented a clean static fallback on XeTeX. | Embedding is loaded only for supported PDF engines. Static mode is available on XeLaTeX; attempted interactive use gives a targeted error. |
| Missing assets and unsupported atlas options had no author-facing recovery path. | Errors identify the missing file or invalid option and explain how to correct it. No silent orbital substitution. |
| Underscores in attachment filenames produced hyperref PDF-string warnings. | Expanded filenames are sanitized before writing file-specification strings; tests also cover spaces. |
| Catalog loading could emit whitespace in horizontal boxes. | The registry consumes inter-entry whitespace; measured atlas widths stay exact. |
| File embedding emitted unwanted horizontal glue in a full-width paragraph. | Embedding runs in a discarded local box, preserving its PDF objects without changing poster layout. |
| No automated coverage linked the LaTeX command to actual embedded PDF objects. | Both PDF engines compile the entire catalog; qpdf resolves each annotation's asset and decodes its JSON. Runtime tests scan the bundled PDFs using UIL itself. |

## Verification

- pdfLaTeX and LuaLaTeX: every one of the 55 identifiers, including complex f/g
  names; both authoring APIs; independent annotations on a single page; matching
  embedded definitions; palette scoping; exact custom dimensions; bounding
  dimensions; filenames containing spaces and underscores; static output; and
  invalid orbital, palette, layout, dimensions, engine, and missing-file errors.
- XeLaTeX: static atlas output, and the explicit error for interactive output.
- Generated inventory: 110 definitions and 220 images, covering both palettes
  and both layouts for every orbital.
- OpenGL regression: poster capture produces a clean image and restores the
  live frame, controls, and sampling offset afterward.
- PDF detector regression: all 55 atlas annotations resolve to the expected
  catalog entries and palette, with non-overlapping rectangles on each page.
- Visual review: the comparison deck, s-shell cutaways, and the atlas's p/d/f/g
  families; compilation checked for overfull boxes and package warnings.

Run `python3 tests/latex_orbital_test.py` for the TeX/PDF checks and the standard
CMake/CTest suite for renderer and application checks. Build the examples with
`make -C examples/atomic-orbital`.

## Intentional boundaries

- The atlas is a finite set of preset definitions, not a LaTeX orbital solver.
  Custom planes, isovalues, palettes, and scales use the custom-poster API and
  require a matching regenerated image.
- Camera fitting is per orbital. Cards are not a common spatial scale, and
  mathematical node counts do not imply that every node is visible from a
  particular camera angle. The underlying fields and surfaces are sampled.
- Cutaways, fitted framing, and the surface-only layout are poster composition
  choices. UIL still opens its ordinary interactive view when replacing the
  poster; the atlas does not add persistent camera or cutaway fields to the
  version-one JSON format.
- Renders are PNG assets, while labels and framing remain PDF text/vector
  content. Regeneration needs the Qt/OpenGL build, but LaTeX compilation does
  not need OpenGL or shell escape.
- Existing `uilfigure.sty` and `uilmolecule.sty` are separate packages. Their APIs
  were not silently changed as part of this orbital audit. Consolidating their
  shared annotation machinery would be a separate compatibility-sensitive task.
