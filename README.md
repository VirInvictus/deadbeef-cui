<p align="center">
  <img src="https://img.shields.io/badge/Version-1.2.3-blue" alt="Version: 1.2.3">
  <img src="https://img.shields.io/badge/Language-C/C++-blue" alt="Language: C/C++">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

---

**Compatibility Note:** This plugin has been developed, tested, and verified to be stable and fully functional specifically on **Fedora Linux 43 (Workstation Edition)** with Kernel **6.19.12-200.fc43.x86_64** running **DeaDBeeF 1.10.2**. The project is now considered effectively complete. I will review and provide fixes as needed or reported from the community, but active feature development is finalized.

# deadbeef-cui

A faceted library browser plugin for the [DeaDBeeF](https://deadbeef.sourceforge.io/) music player on Linux. It brings a Columns UI / Facets style multi-filter layout to DeaDBeeF, optimized for users who navigate large collections via metadata rather than static playlists.

<p align="center">
  <img src="https://github.com/user-attachments/assets/0ceaa853-cc2d-4cf8-9dc6-243d0dddfe9d" alt="DeaDBeeF CUI Plugin Screenshot" style="max-width: 100%; border-radius: 8px;">
</p>

## Why this exists

DeaDBeeF is inherently playlist-focused. For users with libraries exceeding 10,000 tracks, navigating via manual playlists or simple searches is insufficient. `deadbeef-cui` bridges this gap by implementing a dynamic, multi-pane filter layout mirroring foobar2000's Columns UI.

The plugin drives DeaDBeeF's existing playlist view seamlessly. To prevent accidental deletion of your manual playlists, the plugin dynamically creates and targets a dedicated "Library Viewer" playlist. Selecting items in the facets automatically populates this playlist with the corresponding tracks, combining the power of faceted browsing with the player's lightweight core.

## Features

| Component | Description |
|-----------|-------------|
| **Dynamic Columns** | Configure 1 to 5 interactive list views filtering hierarchically. |
| **Custom Formatting** | Full support for DeaDBeeF title formatting syntax. |
| **Integrated Search** | Filter facets dynamically via a `CTRL-SHIFT-F` search bar. |
| **Multi-Selection** | Aggregate filters across multiple genres/artists via Ctrl/Shift-click. |
| **Native Integration** | Built as a native C++ GTK plugin using the `DB_mediasource_t` API. |

## Development & Build

### Requirements
- GTK+ 3.0 development headers
- DeaDBeeF development headers (`/usr/include/deadbeef`)
- CMake (3.10+) & GCC

### Build Pipeline
```bash
cmake -S . -B build
cmake --build build
```

### Installation
```bash
mkdir -p ~/.local/lib/deadbeef
cp build/cui.so ~/.local/lib/deadbeef/ddb_misc_cui_GTK3.so
```
Enter **Design Mode** in DeaDBeeF to add the **Facet Browser (CUI)** widget to your layout.

## Acknowledgments

Inspired by the gold standard of library management: **[foobar2000](https://www.foobar2000.org/)** and its legendary **[Columns UI](https://yuo.be/columns-ui)** and **[Facets](https://www.foobar2000.org/components/view/foo_facets)** components.
