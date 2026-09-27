# Atom-centered orbitals in symmetry slides

The side panel has **Symmetry operations** and **Settings** tabs. In Settings,
select an atom in the visible list (one-based XYZ indices) and check its orbitals.
Settings stay open when clicking elsewhere; switching tabs preserves selections.
The interactive selector presents the nine symmetry-distinct shapes s, px, py, pz, dxy, dxz,
dyz, dx2-y2, and dz2. Principal quantum numbers are intentionally omitted in the UI because
they do not change how these shapes transform under molecular symmetry operations. Existing
files may continue to use the compatible 1s/2s, 2p, and 3d identifiers.
Every shape is available for every atom. The baked meshes are normalized to unit radius,
and the single **Orbital size** control scales all selected shapes on the current atom.
Slide-provided defaults still use their authored sizes.
Cyan means positive wavefunction and neon purple means negative wavefunction,
not electric charge or density. Orbital surfaces use 50% opacity with emissive-style
rim lighting; atoms and bonds retain their original materials. Near-facing shells
are rendered in a depth-sorted translucent pass; the reference orbitals remain fainter.
The size control changes the display radius of the selected atom's orbitals.
Atom spheres shrink where orbitals are displayed to avoid hiding the lobes.

Slide authors can set starting selections in the `.uilsym` JSON:

```json
"orbitals": [
  {"atom": 1, "orbital": "2px", "scale": 0.85},
  {"atom": 2, "orbital": "1s", "scale": 0.5}
]
```

`scale` is an illustrative outer radius in the molecule's angstrom coordinates,
not a prediction of atomic size or an effective nuclear charge. When omitted it
defaults to 0.45 for H/He and 0.85 for other elements, bounded to 0.1–3.0.
Explicit authored sizes are preserved; the size control allows manual adjustment.
There may be up to 64 unique atom/orbital pairs.
Invalid atom indices, unsupported names, and duplicate selections are rejected.
Omitting `orbitals` preserves the original molecule-only presentation.
Live changes are presentation controls; they do not rewrite the embedded JSON.
**Restore slide defaults** restores the authored choices.

With orbitals selected, the operation's final pose remains visible until another
operation is played, **Reset operation** is clicked, or the slide is hidden.
Each replay starts from the original configuration, rather than composing with
the previous operation. Reset preserves the orbital selection and viewing angle.
The original orbitals appear faintly during playback and comparison. Coincident
transformed lobes cover their reference lobes; reset/replay compares their colors.

For an operation matrix G and initial atom center A, every surface point A + u
is transformed to G(A + u) about the symmetry origin. The cyan/purple label travels
with the surface. Thus inversion reverses p phase but preserves s/d parity; a
mirror perpendicular to px exchanges its signed lobes even on a stationary atom.
Equivalently the resulting field is psi'(r) = psi(G^-1 r). The intermediate
reflection/inversion animation is a visual interpolation, not physical dynamics;
the exactly singular midpoint has zero volume and its surface is skipped for that
frame. Camera dragging and continuous Z rotation act on the whole scene afterward.

## Hydrogen-like basis

Use atomic units, nuclear charge Z = 1, r = sqrt(x² + y² + z²), and psi = R(r) Y.
All real basis functions below are normalized. The global sign convention places
the positive px/py lobe on the positive coordinate axis.
The angular normalization follows [NIST DLMF §14.30](https://dlmf.nist.gov/14.30),
using normalized real combinations of the complex harmonics.

| Radial function | Expression |
|---|---|
| R10 | 2 exp(-r) |
| R20 | (2-r) exp(-r/2) / (2 sqrt(2)) |
| R21 | r exp(-r/2) / (2 sqrt(6)) |
| R32 | 4 r² exp(-r/3) / (81 sqrt(30)) |

| Angular function | Expression |
|---|---|
| s | 1 / sqrt(4 pi) |
| px, py, pz | sqrt(3/(4 pi)) × (x, y, z)/r |
| dxy, dxz, dyz | sqrt(15/(4 pi)) × (xy, xz, yz)/r² |
| dx2-y2 | sqrt(15/(16 pi)) × (x²-y²)/r² |
| dz2 | sqrt(5/(16 pi)) × (3z²-r²)/r² |

The baker uses the existing Laguerre/Legendre evaluator and removes its
Condon–Shortley global sign for odd |m|. The real m != 0 harmonics include the
sqrt(2) normalization factor. Multiplying radial and angular functions resolves
the apparent coordinate singularities at r = 0 by continuity.

## Embedded surfaces and regeneration

`symmetry_orbital_baker` is an offline build tool, not part of playback. It samples
an 81³ grid, extracts the signed surfaces with the existing marching-tetrahedra
implementation, and writes `src/orbital/symmetry_orbitals_data.inc`.
The contour is the evaluator's automatic amplitude threshold derived from the
95% radial-probability radius; it is not itself a 95% enclosed-probability surface.
The generated comments record the threshold and unscaled outer radius in Bohr.

Positions are normalized to unit outer radius. Positions and normals are
quantized to signed 16-bit values, deduplicated, indexed, compressed, and embedded
as base64 C++ strings. This is generated data, not hand-authored geometry. The
maximum coordinate quantization error is about 1/65534 of the display radius.
On first use each basis is decoded once, then uploaded once per OpenGL context.
All atoms using a basis reuse its GPU mesh. Runtime selection and animation do
not sample wavefunctions or reconstruct isosurfaces.

To regenerate in a shell with the Qt runtime available:

```sh
cmake --build build-windows --target symmetry_orbital_baker
build-windows/symmetry_orbital_baker.exe src/orbital/symmetry_orbitals_data.inc
```

Ordinary builds consume the committed data and do not run the baker.
