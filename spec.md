# deadbeef-cui — Application Specification

**Version:** 0.1.2-alpha  
**Language:** C/C++  
**Dependencies:** DeaDBeeF Plugin API, GTK2/GTK3  
**License:** MIT

---

## 1. Mission Statement

deadbeef-cui is a faceted library browser plugin for the DeaDBeeF music player on Linux. It brings a Columns UI / Facets style triple-filter layout (Genre → Album Artist → Album) to DeaDBeeF, which is traditionally playlist-focused. This plugin is designed for users who navigate large music collections via metadata filtering rather than manual playlist management.

---

## 2. Architecture

### 2.1 DeaDBeeF Plugin Integration
Implemented as a standard DeaDBeeF GUI plugin, hooking into the main UI or docking system to present a dedicated library view.

### 2.2 Triple Filter Layout
Three list views (facets) that filter hierarchically:
- **Genre:** Select one or more genres to filter artists.
- **Album Artist:** Select one or more artists to filter albums.
- **Album:** Select an album to display its tracks in the active DeaDBeeF playlist.

### 2.3 Database Querying
Efficiently queries the internal DeaDBeeF media library database using the `DB_mediasource_t` API. By generating hierarchical tree views and extracting the underlying `DB_playItem_t` tracks, it pumps the results directly into DeaDBeeF's existing playlist view, avoiding the need for a custom track list viewer.

---

## 3. What deadbeef-cui Is Not

- **Not a standalone player:** It is strictly a plugin for DeaDBeeF.
- **Not a tagger:** It reads and filters by existing tags; it does not edit them.
- **Not a custom playlist viewer:** It relies on DeaDBeeF's native playlist widget for displaying and playing the actual tracks.
