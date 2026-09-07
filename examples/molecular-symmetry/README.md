# Molecular symmetry example

This five-slide deck embeds complete, self-contained molecular symmetry objects
for water (`C2v`), ammonia (`C3v`), boron trifluoride (`D3h`), ethylene
(`D2h`), and methane (`Td`). Each operation in the finite point group has its
own clickable button.

Build and copy the bundled PDF with:

```sh
make bundle
```

The `.uilsym` sources contain the XYZ geometry and explicit operation axes, so
the presentation has no runtime sidecars.

Use the optional `operation_colors` object in a `.uilsym` file to customize the
`identity`, `rotation`, `reflection`, `inversion`, and `improper_rotation`
class colors. Values use `#RRGGBB` notation.
