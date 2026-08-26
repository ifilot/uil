# uil

Pronounced `/œyl/`.

[![Version](https://img.shields.io/badge/version-v0.1.0-blue.svg)](https://github.com/ifilot/uil/releases)
[![License: LGPL v3](https://img.shields.io/badge/license-LGPL--3.0--only-blue.svg)](LICENSE)

`uil` is a presentation app for Windows. It displays PDF slide decks, supports
embedded video in Beamer presentations, and lets you annotate slides while
presenting. Presentations and annotations can be saved together as a `.uil`
file or exported to PDF.

## Install

1. [Download the latest Windows installer](https://github.com/ifilot/uil/releases/latest/download/uil-windows-x64-setup.exe).
2. Run the installer and launch **uil** from the Start menu.

Windows 10 or newer is required.

## Present

Open a `.pdf` or `.uil` file, select the audience display, and press `F5`.
Right-click the audience view to choose the pointer, pencil, or eraser; change
tool settings; browse all slides; or close the presentation.

| Action | Key |
|---|---|
| Next slide | `Right`, `Page Down`, or `Space` |
| Previous slide | `Left`, `Page Up`, or `Backspace` |
| First / last slide | `Home` / `End` |
| Play or pause video | `Enter` |
| Black / white screen | `B` / `W` |
| Toggle full screen | `F11` |
| Leave full screen | `Esc` |

From the presenter window, press `O` for the slide overview or `J` to jump to a
page. Use **File > Save** to preserve the presentation and annotations as a
`.uil` file, or **File > Export as PDF** to create an annotated PDF.

## Troubleshooting

The current session log is listed under **Help > About uil**. Logs can contain
local file paths, so review them before sharing.

## License

`uil` is available under the [GNU LGPL v3.0](LICENSE).
