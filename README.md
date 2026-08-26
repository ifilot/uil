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

On Windows, `uil.exe` is a small native launcher that displays the loading
window before any Qt DLL is loaded. It starts the internal `uil-viewer.exe`,
forwards command-line arguments, and closes the loading window after the
presenter reports its first paint. Users and shortcuts should always start
`uil.exe`; `uil-viewer.exe` remains an implementation detail.

The loading window combines a continuous native animation with real viewer
milestones sent over a lightweight Windows message protocol. Its labels and
progress targets reflect completed initialization stages; only the smooth
visual interpolation between those targets is cosmetic.

Run the unit tests from the configured build directory:

```bash
ctest --test-dir build-windows --output-on-failure
```

Set `-DBUILD_TESTING=OFF` during configuration when a build should omit the test targets.
See [`tests/README.md`](tests/README.md) for the current coverage and testing roadmap.

To create a deployable Windows staging directory after building:

```bash
scripts/windows/deploy-msys2.sh
```

Normal deployment copies the application's license files but skips the slow,
exhaustive MSYS2 package-license crawl. Add `--third-party-notices` when a
release audit is explicitly required. That opt-in writes
`THIRD_PARTY_NOTICES.txt` and `third-party/` with package ownership, versions,
license metadata, installed notice files, and GPL/LGPL-family review items.

To build the installer, install Inno Setup 6 for Windows, make sure `ISCC.exe`
is available on `PATH` or set `ISCC=/path/to/ISCC.exe`, then run:

```bash
scripts/windows/build-inno-installer.sh
```

## Performance logs

The application creates a timestamped performance log for every run. The exact
path is shown under **Help > About uil**. On Windows, logs are stored below the
Qt application-data directory, normally under `%LOCALAPPDATA%`, in the `logs`
subdirectory.

Each line contains a wall-clock timestamp, time since application startup,
severity, Qt category, thread identifier, and message. Performance messages use
a compact JSON payload so they remain both readable and easy to process. The
current instrumentation measures:

- native-launcher creation, loading-window visibility, Qt viewer creation and
  entry into `main()`, presenter-window startup, and the first show, paint, and
  event-loop milestones;
- confirmed launcher progress stages reported by the Qt viewer;
- PDF/UIL opening and notification stages;
- PDF media parsing and MP4 first-frame extraction;
- visible deck-thumbnail construction and debounced render scheduling;
- render queue wait, per-worker PDF backend reuse, and page rasterization; and
- end-to-end time until the first slide is displayed.

Media discovery runs on a dedicated background worker. Slide rendering uses a
bounded four-thread pool, retains one PDF backend per worker, prioritizes the
current slide, and renders only visible overview pages plus a small prefetch
margin. Cache debug messages are disabled by default so profiling does not
materially interfere with UI scheduling. FFmpeg is resolved on first media use
instead of being imported by the executable, keeping its large codec DLL tree
out of ordinary PDF startup while retaining video support in packaged builds.

For a useful trace, start the application, wait for the presenter window to
settle, open one representative PDF, wait until its first slide and overview
thumbnails appear, then close the application and retain that session's log.
Logs can contain local document paths emitted by Qt diagnostics, so review them
before sharing publicly.

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
