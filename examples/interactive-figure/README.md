# Embedded interactive figure prototype

This three-slide deck demonstrates a moving sine wave, a displaced coherent
wavepacket in a harmonic bond, and a moving coherent-state density above its
first six time-dependent real basis components. Every slide contains a normal poster and a
complete UIL interactive-figure payload. No companion file is required after
the PDF is built.

Build it with:

```sh
make
```

Open `build/interactive-figure-example.pdf` in an ordinary PDF reader to see the
three static posters. Open the same file in UIL, start the audience view, and
use the sliders and playback controls. The sine-wave slide
offers amplitude and frequency controls; both oscillator slides offer phase and
initial-stretch controls.

The version-one `.uilfig` format is intentionally a focused prototype. It
supports the sine wave, coherent wavepacket, and six-component harmonic-basis
views and proves embedded extraction, Qt SVG rendering, native controls, and
the PDF fallback behavior end to end. Plot colors and continuous-loop behavior
are authored exclusively in the JSON. The equation below the
first figure is ordinary LaTeX slide content, outside the media annotation. The
normative shape of the JSON document is recorded in
`../../docs/interactive-figure-v1.schema.json`.
