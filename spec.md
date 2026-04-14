# deadbeef-cui — Application Specification

**Version:** 0.8.2-alpha  
**Language:** C/C++  
**Dependencies:** DeaDBeeF Plugin API, GTK2/GTK3  
**License:** MIT

---

## 1. Mission Statement

deadbeef-cui is a faceted library browser plugin for the DeaDBeeF music player on Linux. It brings a Columns UI / Facets style multi-filter layout to DeaDBeeF, which is traditionally playlist-focused. This plugin is designed for users who navigate large music collections via metadata filtering rather than manual playlist management. 1.0 aims for full feature-parity with the original foobar2000 Facets/Columns UI component.

---

## 2. Architecture

### 2.1 DeaDBeeF Plugin Integration
Implemented as a standard DeaDBeeF GUI plugin, hooking into the main UI or docking system to present a dedicated library view.

### 2.2 Configurable Facet Layout
A series of list views (facets) that filter hierarchically. While the default is a triple-pane (Genre → Album Artist → Album), the system is designed to support arbitrary columns and configurations:
- **Metadata Filtering:** Columns can be mapped to any tag (Genre, Artist, Album, Year, Composer, etc.).
- **Hierarchical Engine:** Selection in one pane filters all subsequent panes to the right.
- **Aggregate Selection:** "All" rows and multi-selection support for broad filtering.

### 2.3 Recursive Filter Engine
A robust recursive aggregation system that ensures hierarchical filtering works correctly at all levels, even when broad "All" categories are selected. It resolves the "Various Artists" collision by aggregating child albums from all matching nodes across the library tree.

### 2.4 Database Querying
Efficiently queries the internal DeaDBeeF media library database using the `DB_mediasource_t` API. By generating hierarchical tree views and extracting the underlying `DB_playItem_t` tracks, it pumps the results directly into DeaDBeeF's existing playlist view, avoiding the need for a custom track list viewer.

---

## 3. Features for 1.0 Parity

- **Dynamic Sorting:** Sort columns by name or item count.
- **Title Formatting:** Full DeaDBeeF title formatting syntax for custom column display.
- **Album Art View:** Grid-based cover art display for relevant facets.
- **Multivalue Tag Support:** Proper handling of `;` or `\\` separated tags.
- **Integrated Search:** Global real-time search to narrow all facets simultaneously.
- **Autoplaylists:** Dynamically updating playlists based on the current facet selection.

---

## 4. What deadbeef-cui Is Not

- **Not a standalone player:** It is strictly a plugin for DeaDBeeF.
- **Not a tagger:** It reads and filters by existing tags; it does not edit them.
- **Not a custom playlist viewer:** It relies on DeaDBeeF's native playlist widget for displaying and playing the actual tracks.
