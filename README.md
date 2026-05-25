# uil

`uil` is a Windows-focused Qt PDF presentation prototype for Beamer-style slide decks.

Current version: `v0.1.0`.

## Build

From an MSYS2 UCRT64 shell:

```bash
cmake -S . -B build-windows -G Ninja
cmake --build build-windows
./build-windows/uil.exe
```

## Windows installer

GitHub Actions builds the Windows installer with MSYS2 UCRT64, Qt 6, FFmpeg, and Inno Setup. The workflow uploads the installer on every run and creates a GitHub Release when a tag is pushed.

To run the deployment locally from an MSYS2 UCRT64 shell after building:

```bash
scripts/windows/deploy-msys2.sh --build-dir build-windows --stage-dir dist/uil-windows-x64
scripts/windows/build-inno-installer.sh --stage-dir dist/uil-windows-x64 --output-dir dist
```

The deployment script runs `windeployqt`, then recursively bundles non-system MSYS2 runtime DLLs discovered through `ldd` and writes dependency audit files into the staging directory.

For MP4 first-frame extraction from Beamer `multimedia` annotations, install FFmpeg libraries:

```bash
pacman -S mingw-w64-ucrt-x86_64-ffmpeg
```

The prototype currently provides a Qt Widgets presenter window, an OpenGL audience window, PDF opening through QtPdf, keyboard navigation, manual audience fullscreen with `F11`, a 512 MB LRU image cache, predictive background rendering for nearby slides, a small GPU texture cache for audience output, an embedded SVG application icon, and a Help > About dialog.
