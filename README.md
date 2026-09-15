<p align="center">
  <img src="https://img.shields.io/badge/Version-1.3.4-blue" alt="Version: 1.3.4">
  <img src="https://img.shields.io/badge/Language-C-blue" alt="Language: C">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

---

# deadbeef-cui

A faceted library browser plugin for the [DeaDBeeF](https://deadbeef.sourceforge.io/) music player on Linux. It brings a Columns UI / Facets style multi-filter layout to DeaDBeeF, optimized for users who navigate large collections via metadata rather than static playlists.

> **Note:** This is considered completed software. It is effectively feature complete; bug fixes will be addressed as they come, but no new features are planned. It is developed and tested on the primary development environment: **Fedora Linux 44 (Workstation Edition)**, kernel `7.0.10-201.fc44.x86_64`, running **DeaDBeeF 1.10.3** with **GTK 3.24.52**. The plugin is **v1.3.4**, written in C11 against DeaDBeeF Plugin API level 17 or newer (DeaDBeeF 1.9.6+; the installed `deadbeef-devel` here provides level 19), and built with GCC 16 via CMake 4.3. A prebuilt `compiled/ddb_misc_cui_GTK3.so` is kept in sync with the source for users who do not want to build. GTK4 forward-compat shims exist, but GTK3 is the only build target that has ever been compiled; see the GTK4 note under Hard limitations.

<p align="center">
  <img src="https://github.com/user-attachments/assets/0ceaa853-cc2d-4cf8-9dc6-243d0dddfe9d" alt="DeaDBeeF CUI Plugin Screenshot" style="max-width: 100%; border-radius: 8px;">
</p>

## Why this exists

DeaDBeeF is playlist-focused by design. Once a library grows past ten thousand tracks, manual playlists and simple searches stop scaling. `deadbeef-cui` fills that hole with a dynamic, multi-pane filter layout modeled on foobar2000's Columns UI.

The plugin drives DeaDBeeF's ordinary playlist view. To keep your manual playlists safe, it targets a dedicated "Library Viewer" playlist that it creates itself: selecting items in the facets populates that playlist with the matching tracks, so browsing and playback stay inside the player you already have.

## Features

| Component | Description |
|-----------|-------------|
| **Dynamic Columns** | Configure 1 to 5 interactive list views filtering hierarchically. |
| **Custom Formatting** | Full support for DeaDBeeF title formatting syntax. |
| **Integrated Search** | Filter facets dynamically via a `CTRL-SHIFT-F` search bar. |
| **Multi-Selection** | Aggregate filters across multiple genres/artists via Ctrl/Shift-click. |
| **Native Integration** | Built as a native C GTK3 plugin using the `DB_mediasource_t` API. |

## Compatibility

The verified DeaDBeeF floor is **1.9.6** (plugin API level 17): every plugin-API member the widget calls (the `DB_mediasource_t` tree API, `plt_select_all`, and the GTKUI widget API with extended per-widget serialization) ships in 1.9.6, and since v1.3.4 nothing newer is required. Versions between 1.9.6 and 1.10.0 meet the floor but are not regularly exercised; the 1.10.x line is what is actually tested. The project is effectively feature-complete; I review and fix issues as the community reports them but am not actively adding features.

### Pre-built binary (`compiled/ddb_misc_cui_GTK3.so`)

The committed binary is provided as a convenience for users who don't want to compile.

| Constraint | Requirement |
|---|---|
| Architecture | **x86_64 only** (no i686/32-bit, no ARM/aarch64) |
| Linux glibc | **2.34 or newer** |
| Operating system | Linux only (the binary is an ELF `.so`) |
| GTK | DeaDBeeF's GTK3 GUI plugin (`ddb_gui_GTK3.so`) must be the active GUI |
| DeaDBeeF | 1.9.6 or newer (verified API floor, plugin API level 17); tested on 1.10.x |

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
| Ubuntu 20.04 LTS | 2020 | 2.31 | ❌ (compile from source) |
| Debian 11 (Bullseye) | 2021 | 2.31 | ❌ (compile from source) |
| RHEL / Rocky / Alma 8 | 2019 | 2.28 | ❌ (compile from source) |
| openSUSE Leap 15.5 | 2023 | 2.31 | ❌ (compile from source) |

### Source build (recommended for unsupported distros)

If your system can't run the pre-built binary (**or if you're on any architecture other than x86_64, or any OS where DeaDBeeF runs with the GTK3 GUI**), compile from source. The result will be linked against your system's libraries and will work on that system regardless of how old its glibc is. See the [Development & Build](#development--build) section below.

### Hard limitations (apply to both pre-built and from-source builds)

- **Linux only.** DeaDBeeF itself runs on Linux, macOS, and Windows, but those platforms use different GUI plugins. This widget is specifically a GTK3 plugin and won't load under macOS Cocoa or Windows native UIs.
- **GTK3 only.** The codebase carries forward-compatibility shims for GTK4 (see `cui_globals.h`), but DeaDBeeF currently ships only a GTK3 GUI. The shims are partial: button events, context menus, dialogs, container iteration, and drag-out are still written against GTK3 APIs, and a GTK4 build of this plugin has never been made, so a future GTK4 port is a real porting effort rather than a recompile.
- **Requires the medialib plugin.** Without `medialib.so` enabled, the widget shows a transient status line explaining what is missing instead of a silently blank layout. The medialib plugin ships with DeaDBeeF; no extra step needed unless you've explicitly disabled it.
- **No cross-compilation.** The `CMakeLists.txt` uses `pkg-config` to discover GTK3, which assumes a native build environment. Cross-compiling from x86_64 to i686 or aarch64 is plausible but untested.

## Development & Build

### Requirements
- GTK+ 3.0 development headers (`gtk3-devel` on Fedora/RHEL, `libgtk-3-dev` on Debian/Ubuntu)
- DeaDBeeF development headers (`deadbeef-devel` on Fedora/RHEL; on Debian/Ubuntu you may need to grab them from DeaDBeeF's source release if not packaged); they should land at `/usr/include/deadbeef/`
- CMake 3.10+ & a C11-capable compiler (GCC 4.8+ / Clang 3.3+)

### Build Pipeline
```bash
cmake -S . -B build
cmake --build build
```

### Installation
```bash
mkdir -p ~/.local/lib/deadbeef
cp build/ddb_misc_cui_GTK3.so ~/.local/lib/deadbeef/ddb_misc_cui_GTK3.so
```
Enter **Design Mode** in DeaDBeeF to add the **Facet Browser (CUI)** widget to your layout.

### Verifying your build

Launch DeaDBeeF from a terminal with `deadbeef --gui GTK3 -d 2>&1 | grep cui`: you should see `deadbeef-cui: Facet Browser v1.3.4 registered successfully.` on startup. If the line is missing, the plugin failed to load (check the rest of the log for unresolved symbols or missing libraries).

## Acknowledgments

Inspired by **[foobar2000](https://www.foobar2000.org/)** and its **[Columns UI](https://yuo.be/columns-ui)** and **[Facets](https://www.foobar2000.org/components/view/foo_facets)** components.

## Support

If deadbeef-cui is useful to you and you'd like to chip in:

- liberapay · [liberapay.com/bdkl](https://liberapay.com/bdkl/)
- bitcoin
  ```
  bc1qkge6zr45tzqfwfmvma2ylumt6mg7wlwmhr05yv
  ```
