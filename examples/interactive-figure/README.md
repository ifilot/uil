# Embedded interactive figure prototype

This five-slide deck demonstrates a moving sine wave, a displaced coherent
wavepacket in a harmonic bond, and a moving coherent-state density above its
first six time-dependent real basis components. The fourth slide fits a step
function with between 1 and 25 particle-in-a-box basis functions and reports
the fraction reproduced. A fifth slide fits the Week 2 stretched-bond state
with 1 through 25 harmonic-oscillator eigenfunctions. Every slide contains a normal poster and a
complete UIL interactive-figure payload. No companion file is required after
the PDF is built.

Build it with:

```sh
make
```

Open `build/interactive-figure-example.pdf` in an ordinary PDF reader to see the
five static posters. Each poster mirrors the corresponding payload's initial
state and carries an `INTERACTIVE` badge in the poster's upper-right corner.
Open the same file in UIL, start the audience view, and
use the sliders and playback controls. The sine-wave slide
offers amplitude and frequency controls; both oscillator slides offer phase and
initial-stretch controls. The step-expansion slide has a discrete, live-updating
basis-count slider from 1 through 25; the displaced-state slide uses the same
discrete interaction and reports the captured norm to two decimal places.

The version-one `.uilfig` format is intentionally a focused prototype. It
supports the sine wave, coherent wavepacket, six-component harmonic-basis, and
particle-in-a-box step-expansion and displaced harmonic-state expansion
views and proves embedded extraction, Qt SVG rendering, native controls, and
the PDF fallback behavior end to end. Plot colors and continuous-loop behavior
are authored exclusively in the JSON. The equation below the
first figure is ordinary LaTeX slide content, outside the media annotation. The
normative shape of the JSON document is recorded in
`../../docs/interactive-figure-v1.schema.json`.

Plot and axis labels accept a small, presentation-safe LaTeX math subset when
wrapped in `$...$`. Supported notation includes `_` and `^` groups,
`\frac`, `\mathrm`, `\sum`, `\pi`, `\psi`, `\Phi`, `\alpha`, `\omega`, `\tau`, `\ell`, and
`\hbar`. UIL typesets these labels itself, so a LaTeX installation is not
required on the presentation computer.
