# Atomic-orbital examples

Run `make` to build and copy both PDFs to `examples/bundled`:

- `atomic-orbitals.pdf`: five Beamer slides, beginning with independently
  interactive 1s and 2s plots side by side.
- `orbital-atlas.pdf`: a 14-page Garnet–Slate catalog with all 55 supported
  orbitals embedded as independent interactive plots.

The examples use `\atomicorbital[orbital=...,width=...]` and the pre-rendered
atlas in `latex/uilorbital-atlas`. No renderer or shell escape runs during
LaTeX compilation. Garnet–Slate is the default; select
`palette=garnet_teal` to use the alternative theme.

See the [LaTeX package guide](../../latex/README.md) for setup, keys, custom
posters, static mode, and atlas regeneration. The older `.uilorb` files and
widget screenshots in this directory remain usable with the custom-poster
API. The [color scheme gallery](color-schemes/README.md) includes their color
settings and standalone examples.
