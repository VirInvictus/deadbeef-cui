<p align="center">
  <img src="https://img.shields.io/badge/Version-1.2.5-blue" alt="Version: 1.2.5">
  <img src="https://img.shields.io/badge/Language-C-blue" alt="Language: C">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

---

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
| **Native Integration** | Built as a native C GTK3 plugin using the `DB_mediasource_t` API. |

## Compatibility

This plugin is developed and tested on **Fedora Linux** (currently Fedora 44, x86_64) running **DeaDBeeF 1.10.2** with the GTK3 GUI plugin. The project is effectively feature-complete; I review and fix issues as the community reports them but am not actively adding features.

### Pre-built binary (`compiled/ddb_misc_cui_GTK3.so`)

The committed binary is provided as a convenience for users who don't want to compile.

| Constraint | Requirement |
|---|---|
| Architecture | **x86_64 only** (no i686/32-bit, no ARM/aarch64) |
| Linux glibc | **2.34 or newer** |
| Operating system | Linux only (the binary is an ELF `.so`) |
| GTK | DeaDBeeF's GTK3 GUI plugin (`ddb_gui_GTK3.so`) must be the active GUI |
| DeaDBeeF | 1.10.x (uses `DB_mediasource_t` API, level 18+) |

The glibc 2.34 floor exists because that release moved `dlopen`/`dlsym`/`dlclose` from `libdl.so.2` into `libc.so.6` and rebound them to `GLIBC_2.34`. The binary calls those three functions to share the GTKUI plugin's media library source.

#### Distro support matrix

| Distro | Released | glibc | Pre-built binary works? |
|---|---|---|---|
| Fedora 35+ | 2021+ | 2.34+ | ✅ |
| Ubuntu 22.04 LTS | 2022 | 2.35 | ✅ |
| Ubuntu 24.04 LTS | 2024 | 2.39 | ✅ |
| Debian 12 (Bookworm) | 2023 | 2.36 | ✅ |
| RHEL / Rocky / Alma 9 | 2022 | 2.34 | ✅ |
| openSUSE Leap 15.6 | 2024 | 2.38 | ✅ |
| Arch / Tumbleweed | rolling | latest | ✅ |
| Ubuntu 20.04 LTS | 2020 | 2.31 | ❌ — compile from source |
| Debian 11 (Bullseye) | 2021 | 2.31 | ❌ — compile from source |
| RHEL / Rocky / Alma 8 | 2019 | 2.28 | ❌ — compile from source |
| openSUSE Leap 15.5 | 2023 | 2.31 | ❌ — compile from source |

### Source build (recommended for unsupported distros)

If your system can't run the pre-built binary — **or if you're on any architecture other than x86_64, or any OS where DeaDBeeF runs with the GTK3 GUI** — compile from source. The result will be linked against your system's libraries and will work on that system regardless of how old its glibc is. See the [Development & Build](#development--build) section below.

### Hard limitations (apply to both pre-built and from-source builds)

- **Linux only.** DeaDBeeF itself runs on Linux, macOS, and Windows, but those platforms use different GUI plugins. This widget is specifically a GTK3 plugin and won't load under macOS Cocoa or Windows native UIs.
- **GTK3 only.** The codebase carries forward-compatibility shims for GTK4 (see `cui_globals.h`), but DeaDBeeF currently ships only a GTK3 GUI. When DeaDBeeF gets a GTK4 GUI plugin, this widget should compile against it with minimal changes.
- **Requires the medialib plugin.** Without `medialib.so` enabled, the widget renders an empty layout. The medialib plugin ships with DeaDBeeF — no extra step needed unless you've explicitly disabled it.
- **No cross-compilation.** The `CMakeLists.txt` uses `pkg-config` to discover GTK3, which assumes a native build environment. Cross-compiling from x86_64 to i686 or aarch64 is plausible but untested.

## Development & Build

### Requirements
- GTK+ 3.0 development headers (`gtk3-devel` on Fedora/RHEL, `libgtk-3-dev` on Debian/Ubuntu)
- DeaDBeeF development headers (`deadbeef-devel` on Fedora/RHEL; on Debian/Ubuntu you may need to grab them from DeaDBeeF's source release if not packaged) — should land at `/usr/include/deadbeef/`
- CMake 3.10+ & a C11-capable compiler (GCC 4.8+ / Clang 3.3+)

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

### Verifying your build

Launch DeaDBeeF from a terminal with `deadbeef --gui GTK3 -d 2>&1 | grep cui` — you should see `deadbeef-cui: Facet Browser v1.2.5 registered successfully.` on startup. If the line is missing, the plugin failed to load (check the rest of the log for unresolved symbols or missing libraries).

## Acknowledgments

Inspired by the gold standard of library management: **[foobar2000](https://www.foobar2000.org/)** and its legendary **[Columns UI](https://yuo.be/columns-ui)** and **[Facets](https://www.foobar2000.org/components/view/foo_facets)** components.

## Support

If deadbeef-cui is useful to you and you'd like to chip in:

```
bc1qkge6zr45tzqfwfmvma2ylumt6mg7wlwmhr05yv
```
