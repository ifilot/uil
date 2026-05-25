# uil

`uil` is a Windows-focused Qt PDF presentation prototype for Beamer-style slide decks.

## Build

From an MSYS2 UCRT64 shell:

```bash
cmake -S . -B build-windows -G Ninja
cmake --build build-windows
./build-windows/uil.exe
```

For MP4 first-frame extraction from Beamer `multimedia` annotations, install FFmpeg libraries:

```bash
pacman -S mingw-w64-ucrt-x86_64-ffmpeg
```

The prototype currently provides a Qt Widgets presenter window, an OpenGL audience window, PDF opening through QtPdf, keyboard navigation, manual audience fullscreen with `F11`, a 512 MB LRU image cache, predictive background rendering for nearby slides, and a small GPU texture cache for audience output.
