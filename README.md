# deadbeef-cui

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.8.0--alpha-blue" alt="Version: 0.8.0-alpha">
  <img src="https://img.shields.io/badge/Language-C/C++-blue" alt="Language: C/C++">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

A faceted library browser plugin for the [DeaDBeeF](https://deadbeef.sourceforge.io/) music player on Linux. It brings a Columns UI / Facets style multi-filter layout to DeaDBeeF.

<p align="center">
  <img src="https://github.com/user-attachments/assets/4ed93103-5148-4cdc-9fba-3d63bb9ce97b" alt="DeaDBeeF CUI Plugin Screenshot" style="max-width: 100%; border-radius: 8px;">
</p>

## Why this exists

Right now, DeaDBeeF is inherently playlist-focused. For users with large libraries who navigate via metadata filtering rather than manual playlists, a faceted search is the only thing missing to make DeaDBeeF the perfect Linux music player. 

This plugin implements a dynamic, user-configurable multi-pane view of the media library (set in settings), mirroring foobar2000's Columns UI or Facets component. By default it features a triple-filter view:
1. **Genre**
2. **Album Artist**
3. **Album**

However, you can configure anywhere from 1 to 5 columns with custom title formatting!

*Note: Instead of implementing a custom pane for the track list, this plugin seamlessly drives DeaDBeeF's existing playlist view. Selecting items in the facets will dynamically populate the active playlist.*

## Features

| Component | Description |
|-----------|-------------|
| **Dynamic Columns** | Configure 1 to 5 interactive list views filtering hierarchically. |
| **Custom Formatting** | Full support for DeaDBeeF title formatting syntax for column patterns. |
| **Multi-Selection** | Select multiple entries (Ctrl/Shift-click) to aggregate filters across multiple genres/artists. |
| **Aggregate Views** | Columns show "Every" item by default if no parent filter is applied. |
| **Playlist Integration** | Selecting any entry immediately populates the active DeaDBeeF playlist. |
| **Double-Click Playback** | Double-clicking any entry in any column starts playback of that selection. |
| **DeaDBeeF Native** | Built as a native C/C++ GTK plugin using the `DB_mediasource_t` API for seamless library integration. |

## Requirements

To build and run this plugin, you need:
- **GTK+ 3.0** development headers
- **DeaDBeeF** development headers (usually in `/usr/include/deadbeef`)
- **CMake** (3.10 or higher)
- **GCC** or another C11 compatible compiler
- **PkgConfig**

On Fedora, you can install these with:
```bash
sudo dnf install gtk3-devel deadbeef-devel cmake gcc pkgconf
```

## Build & Install

1. **Clone the repository:**
   ```bash
   git clone https://github.com/bdkl/deadbeef-cui.git
   cd deadbeef-cui
   ```

2. **Configure and Build:**
   ```bash
   cmake -S . -B build
   cmake --build build
   ```

3. **Install:**
   Copy the compiled plugin to your local DeaDBeeF plugin directory:
   ```bash
   mkdir -p ~/.local/lib/deadbeef
   cp build/cui.so ~/.local/lib/deadbeef/ddb_misc_cui_GTK3.so
   ```

4. **Restart DeaDBeeF:**
   Right-click on any UI element, enter **Design Mode**, and add the **Facet Browser (CUI) v0.8.1** widget to your layout.

## Acknowledgments / Thank You

This plugin would not exist without the incredible inspiration and foundation laid by:
- **[foobar2000](https://www.foobar2000.org/)**: The gold standard for library management that heavily inspired the workflow of this plugin.
- **[Columns UI (foobar2000 plugin)](https://yuo.be/columns-ui)**: The legendary UI component by *musicmusic* that introduced the power of multi-pane faceted browsing to music players.
- **[DeaDBeeF](https://deadbeef.sourceforge.io/)**: The amazing open-source, lightweight audio player by Alexey Yakovenko and contributors, which serves as the robust and extensible foundation for this project.
