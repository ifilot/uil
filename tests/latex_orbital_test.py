#!/usr/bin/env python3
"""Compile the orbital API with both PDF engines and inspect real annotations.

Requires pdflatex, lualatex, and qpdf. Run from any working directory.
"""
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ATLAS = ROOT / "latex/uilorbital-atlas"


def run(command, **kwargs):
    """Run a tool, retaining output for actionable failures."""
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, **kwargs)


def pdf_annotations(path):
    """Resolve annotations and their embedded payloads with qpdf."""
    result = run(["qpdf", "--json", "--json-key=qpdf", str(path)], check=True)
    objects = json.loads(result.stdout)["qpdf"][1]
    annotations = []
    for item in objects.values():
        value = item.get("value", {})
        if not isinstance(value, dict) or value.get("/Subtype") != "/UILAtomicOrbital":
            continue
        spec = objects["obj:" + value["/UIL"]["/Asset"]]["value"]
        stream = spec["/EF"]["/F"].split()[0]
        payload = run(["qpdf", f"--show-object={stream}", "--filtered-stream-data", str(path)],
                      check=True)
        annotations.append((value, spec, json.loads(payload.stdout)))
    return annotations


def compile_case(engine, directory, name, body, expected_error=None):
    """Compile a fixture and reject unexpected errors, warnings, or layout overflow."""
    source = directory / (name + ".tex")
    source.write_text(r"""\documentclass{article}
\usepackage[margin=15mm]{geometry}
\usepackage{uilorbital}
\uilorbitalsetup{atlas path={""" + ATLAS.as_posix() + r"""}}
\begin{document}
""" + body + "\n\\end{document}\n")
    env = dict(os.environ, TEXINPUTS=f".:{ROOT / 'latex'}//:",
               TEXMFVAR=str(directory / "texmf-var"), TEXMFCACHE=str(directory / "texmf-cache"))
    result = run([engine, "-interaction=nonstopmode", "-halt-on-error", source.name],
                 cwd=directory, env=env)
    if expected_error:
        assert result.returncode != 0 and expected_error in result.stdout, result.stdout
        return None
    assert result.returncode == 0, result.stdout
    assert "Overfull" not in result.stdout, result.stdout
    assert "Package uilorbital Warning" not in result.stdout, result.stdout
    assert "Token not allowed in a PDF string" not in result.stdout, result.stdout
    return directory / (name + ".pdf")


def main():
    """Check full catalog assets, both APIs, sizing, scope, and expected errors."""
    for tool in ("pdflatex", "lualatex", "qpdf"):
        if not shutil.which(tool):
            raise SystemExit(f"Required test tool not found: {tool}")
    manifest = json.loads((ATLAS / "manifest.json").read_text())
    assert len(manifest["entries"]) == 110
    assert len({entry["orbital"] for entry in manifest["entries"]}) == 55
    for entry in manifest["entries"]:
        stem = ATLAS / entry["palette"] / entry["orbital"]
        payload = json.loads(stem.with_suffix(".uilorb").read_text())
        assert payload["orbital"] == entry["orbital"]
        assert payload["contour"]["colormap"] == entry["palette"]
        for layout in manifest["layouts"]:
            assert Path(str(stem) + "-" + layout + ".png").stat().st_size > 1000
    with tempfile.TemporaryDirectory(prefix="uil orbital tests ") as temporary:
        directory = Path(temporary)
        # Exercise underscores and spaces in embedded filenames with both engines.
        payload_path = directory / "custom orbital_test.uilorb"
        shutil.copyfile(ATLAS / "garnet_slate/2s.uilorb", payload_path)
        incomplete = directory / "incomplete-atlas"
        incomplete.mkdir()
        shutil.copyfile(ATLAS / "catalog.tex", incomplete / "catalog.tex")
        for engine in ("pdflatex", "lualatex"):
            pdf = compile_case(engine, directory, "api", r"""
\noindent
\begin{minipage}{.48\linewidth}
  \atomicorbital[orbital=1s,width=\linewidth]
\end{minipage}\hfill
\begin{minipage}{.48\linewidth}
  \atomicorbital[orbital=2s,palette=garnet_teal,width=\linewidth]
\end{minipage}
\par\bigskip
\atomicorbital[orbital=2pz,layout=surface,width=6cm,height=4cm]
\atomicorbital[orbital=3dz2,static=true,width=6cm,height=4cm]
\par\bigskip
\atomicorbital[width=100pt,height=40pt,depth=5pt]{\fbox{Custom poster}}{custom orbital_test.uilorb}
\atomicorbital[static=true]{Static custom poster}{missing.uilorb}
\newsavebox{\naturalcard}\newsavebox{\limitedcard}\newsavebox{\customcard}
\sbox{\naturalcard}{\atomicorbital[width=200pt,layout=surface,static=true]}
\sbox{\limitedcard}{\atomicorbital[width=200pt,height=80pt,layout=surface,static=true]}
\sbox{\customcard}{\atomicorbital[width=100pt,height=40pt,depth=5pt,static=true]{Custom}{missing.uilorb}}
\typeout{NATURAL:\the\wd\naturalcard:\the\ht\naturalcard:\the\dp\naturalcard}
\typeout{LIMITED:\the\wd\limitedcard:\the\ht\limitedcard:\the\dp\limitedcard}
\typeout{CUSTOM:\the\wd\customcard:\the\ht\customcard:\the\dp\customcard}
""")
            log = pdf.with_suffix(".log").read_text(errors="replace")
            dimensions = {}
            for key in ("NATURAL", "LIMITED", "CUSTOM"):
                match = re.search(key + r":([0-9.]+)pt:([0-9.]+)pt:([0-9.]+)pt", log)
                assert match, log
                dimensions[key] = tuple(float(value) for value in match.groups())
            nw, nh, nd = dimensions["NATURAL"]
            lw, lh, ld = dimensions["LIMITED"]
            assert abs(nw - 200) < .01, dimensions
            assert abs(lh + ld - 80) < .01, dimensions
            assert abs(lw / nw - (lh + ld) / (nh + nd)) < .001, dimensions
            # graphicx stores scale factors at finite decimal precision.
            assert all(abs(a - b) < .05 for a, b in zip(dimensions["CUSTOM"], (100, 40, 5))), dimensions
            annotations = pdf_annotations(pdf)
            assert len(annotations) == 4, annotations
            assert [a[2]["orbital"] for a in annotations] == ["1s", "2s", "2pz", "2s"]
            assert [a[2]["contour"]["colormap"] for a in annotations[:3]] == [
                "garnet_slate", "garnet_teal", "garnet_slate"]
            assert len({a[0]["/UIL"]["/Asset"] for a in annotations}) == 4
            first, second = (a[0]["/Rect"] for a in annotations[:2])
            assert first[2] <= second[0], (first, second)
            rect = annotations[2][0]["/Rect"]
            assert rect[2] - rect[0] <= 6 * 72 / 2.54 + .02
            assert rect[3] - rect[1] <= 4 * 72 / 2.54 + .02
            rect = annotations[3][0]["/Rect"]
            assert abs(rect[2] - rect[0] - 100 * 72 / 72.27) < .02, rect
            assert abs(rect[3] - rect[1] - 45 * 72 / 72.27) < .02, rect
            assert annotations[3][1]["/UF"].endswith("custom orbital_test.uilorb")
            # Every supported identifier, including complex g names, compiles.
            names = [e["orbital"] for e in manifest["entries"] if e["palette"] == "garnet_slate"]
            body = "\n".join(r"\atomicorbital[orbital=" + name +
                             r",layout=surface,width=7cm,height=6cm]\par\newpage" for name in names)
            all_pdf = compile_case(engine, directory, "all-orbitals", body)
            all_annotations = pdf_annotations(all_pdf)
            assert len(all_annotations) == 55
            assert [a[2]["orbital"] for a in all_annotations] == names
            for body, message in [
                (r"\atomicorbital[orbital=6h]", "Unknown atlas orbital"),
                (r"\atomicorbital[palette=unknown]", "Unknown atlas palette"),
                (r"\atomicorbital[layout=unknown]", "Unknown atlas layout"),
                (r"\atomicorbital[atlas path=missing-atlas]", "Orbital atlas not found"),
                (r"\atomicorbital[atlas path=incomplete-atlas]", "Atlas image"),
                (r"\atomicorbital[width=0pt]", "Atlas width is too small"),
                (r"\atomicorbital[height=-1cm]", "Invalid atlas height"),
                (r"\atomicorbital[static=maybe]", "Invalid static value"),
                (r"\atomicorbital{poster}", "Custom poster requires"),
                (r"\atomicorbital{poster}{missing.uilorb}", "not found"),
                (r"\atomicorbital[width=-1cm]{poster}{missing.uilorb}", "Invalid poster dimensions"),
                (r"\atomicorbital[static=maybe]{poster}{missing.uilorb}", "Invalid static value"),
            ]:
                compile_case(engine, directory, "error", body, message)
            print(f"PASS: {engine}: 55 orbitals, both APIs, payloads, sizing, scoping, and errors", flush=True)

        if shutil.which("xelatex"):
            pdf = compile_case("xelatex", directory, "static-xetex",
                               r"\atomicorbital[orbital=2s,static=true,width=7cm]")
            assert not pdf_annotations(pdf)
            compile_case("xelatex", directory, "interactive-xetex",
                         r"\atomicorbital[orbital=2s,width=7cm]",
                         "Interactive orbitals require PDF output")
            print("PASS: xelatex: static output and explicit interactive-engine error", flush=True)


if __name__ == "__main__":
    main()
