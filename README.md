# UIL

Pronunciation: `/ œyl /`

[![Windows Installer](https://github.com/ifilot/uil/actions/workflows/windows-installer.yml/badge.svg)](https://github.com/ifilot/uil/actions/workflows/windows-installer.yml)
[![License: LGPL v3](https://img.shields.io/badge/license-LGPL--3.0--only-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-v0.1.0-blue.svg)](CMakeLists.txt)

## Purpose

`uil` is a Windows-focused Qt presentation prototype for Beamer-style slide
decks. It can open regular PDF presentations as well as so-called `uil`
presentations: a PDF bundled together with its supporting assets. That second
form is useful when a slide deck contains movies, because the PDF alone does not
carry the media in a way that is convenient for a lightweight presenter.

Movie support is an important part of the project. `uil` uses the handles
provided by the Beamer LaTeX class to connect slide content to the corresponding
media assets, allowing presentations to include videos without giving up the
familiar PDF-based workflow. The application is also designed with effective
caching in mind, so it remains practical on low-end systems where rendering
every slide or media frame on demand would be too expensive.

The goal is not only to show slides, but to support live presentation and
teaching workflows. Slides can be annotated during a session, and those
annotations can later be saved, making `uil` useful in classroom and lecture
settings where the presentation often becomes part of the teaching material.

## Build

Windows builds are intended to be made with **MSYS2 UCRT64**. Use the
`MSYS2 UCRT64` shell, not the plain `MSYS`, `MINGW64`, or Windows `cmd.exe`
prompt, so CMake finds the same compiler, Qt libraries, and runtime DLLs that
the deployment scripts expect.

Install or update MSYS2 first, then install the build dependencies:

```bash
pacman -Syu
```

If MSYS2 asks you to close the terminal after the system update, reopen the
`MSYS2 UCRT64` shell and run `pacman -Syu` again until there is nothing left to
update.

Then install the project dependencies:

```bash
pacman -S --needed \
  git \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-pdf \
  mingw-w64-ucrt-x86_64-qt6-svg \
  mingw-w64-ucrt-x86_64-zlib \
  mingw-w64-ucrt-x86_64-ffmpeg
```

`ffmpeg` is optional at configure time, but install it for media-frame
extraction support. The `toolchain` group provides GCC, binutils, make, and
`pkgconf`; CMake uses `pkgconf` to detect the FFmpeg libraries when they are
available.

Configure and build from the repository root:

```bash
cmake -S . -B build-windows -G Ninja
cmake --build build-windows
./build-windows/uil.exe
```

To create a deployable Windows staging directory after building:

```bash
scripts/windows/deploy-msys2.sh
```

The deployment script writes a complete staged dependency audit to
`dist/uil-windows-x64/THIRD_PARTY_NOTICES.txt` and
`dist/uil-windows-x64/third-party/`. Those files include the staged file
inventory, owning MSYS2 packages, package versions, package license metadata,
copied installed license/notice files, and a generated review file for
GPL/LGPL-family attention items.

To build the installer, install Inno Setup 6 for Windows, make sure `ISCC.exe`
is available on `PATH` or set `ISCC=/path/to/ISCC.exe`, then run:

```bash
scripts/windows/build-inno-installer.sh
```

## License

`uil` is licensed under the GNU Lesser General Public License v3.0 only. The
application links Qt 6 modules that are available under Qt's commercial or
open-source licensing options, including LGPL/GPL terms depending on module and
distribution. Windows release builds also redistribute the MSYS2 runtime
dependency closure, including optional FFmpeg dependencies when FFmpeg is
available at configure time. See [LICENSE](LICENSE) for the application LGPLv3
text, [LICENSES/GPL-3.0-only.txt](LICENSES/GPL-3.0-only.txt) for the GPLv3
terms incorporated by LGPLv3, and the generated third-party notices in release
artifacts for redistributed dependency licenses.
