# deadbeef-cui

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.7.1--alpha-blue" alt="Version: 0.7.1-alpha">
  <img src="https://img.shields.io/badge/Language-C/C++-blue" alt="Language: C/C++">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

A faceted library browser plugin for the [DeaDBeeF](https://deadbeef.sourceforge.io/) music player on Linux. It brings a Columns UI / Facets style triple-filter layout (Genre → Album Artist → Album) to DeaDBeeF.

<p align="center">
  <img src="https://github.com/user-attachments/assets/4ed93103-5148-4cdc-9fba-3d63bb9ce97b" alt="DeaDBeeF CUI Plugin Screenshot" style="max-width: 100%; border-radius: 8px;">
</p>

## Why this exists

Right now, DeaDBeeF is inherently playlist-focused. For users with large libraries who navigate via metadata filtering rather than manual playlists, a faceted search is the only thing missing to make DeaDBeeF the perfect Linux music player. 

This plugin implements a triple-filter view of the media library (set in settings), mirroring foobar2000's Columns UI or Facets component:
1. **Genre**
2. **Album Artist**
3. **Album**

*Note: Instead of implementing a custom 4th pane for the track list, this plugin seamlessly drives DeaDBeeF's existing playlist view. Selecting items in the facets will dynamically populate the active playlist.*

## Features

| Component | Description |
|-----------|-------------|
| **Faceted Browsing** | Three interactive list views filtering hierarchically. |
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
   Right-click on any UI element, enter **Design Mode**, and add the **Facet Browser (CUI) v0.7** widget to your layout.

