# deadbeef-cui — Patch Notes

## v0.1.2-alpha (Current)

---

### Enhancements

**Dynamic Media Library Integration.** Successfully hooked into DeaDBeeF's `medialib` plugin using a custom 4-level hierarchical tree (`%genre%` -> `%album artist%` -> `%album%` -> `%title%`).
**Real-time Updates.** Added an event listener to the media library. The UI now automatically populates and refreshes as soon as the background library scan completes.
**Hierarchical Filtering.** Implemented instantaneous column-to-column filtering. Selecting a Genre filters Artists; selecting an Artist filters Albums.
**Improved Data Display.** Fixed the hierarchical tree nesting to ensure the Album column only shows the album title, not redundant metadata.
**Global Search / Aggregate Views.** Columns now show "Every" item by default if no parent filter is applied (e.g., Artist column shows all artists if no Genre is selected).
**Full Playlist Driving.** Selecting any item (Genre, Artist, or Album) now immediately populates the active playlist with all matching tracks.
**Global Double-Click Playback.** Double-clicking any entry in any column now populates the playlist and starts playback from the first track.

### Bug Fixes

**Official API Allocation.** Fixed a major segfault by switching from manual struct allocation to the official `scriptableItemAlloc` and related APIs exported by `medialib.so`. This ensures internal metadata state (like bytecode compilation) is correctly initialized.
**Memory Management.** Fixed a double-free crash during plugin shutdown by aligning with GTKUI's widget ownership model.
**Data Isolation.** Resolved an issue where the plugin used a separate empty database; it now correctly shares the primary `deadbeef` media source.
**Pointer Ownership.** Fixed a critical crash on album selection by correctly cloning track items instead of attempting to move internal library pointers.
**UI Formatting.** Fixed hierarchical linking in the custom tree builder to prevent redundant metadata (Artist/Disc) from bloating the Album column.

## v0.1.1-alpha

---

### Enhancements

**Build System & UI Skeleton.** Complete CMake scaffolding and basic plugin structure. Constructed the triple-pane GTK widget layout (Genre, Album Artist, Album).
**Architecture Pivot.** Decided to drive DeaDBeeF's native playlist view from the facet selections rather than building a custom 4th track list pane.

## v0.1.0-alpha (Planning Stage)

---

### Enhancements

**Initial Planning & Architecture.** Drafted the initial specifications for a faceted, columns UI-style plugin for DeaDBeeF. Outlined a roadmap focusing on a C/C++ plugin integrating with DeaDBeeF's GTK UI.
