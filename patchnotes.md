# deadbeef-cui — Patch Notes

## v0.3.0-alpha (Current)

---

### Enhancements

**Recursive Filter Engine.** Replaced the legacy hardcoded depth traversal with a robust recursive aggregation system. This ensures that hierarchical filtering works correctly at all levels, even on initial load.
**Universal "All" Selections.** Added "All Genres", "All Artists", and "All Albums" options to their respective columns. These rows allow for broad filtering and easy navigation to aggregate views.
**String-Based Multi-Aggregation.** Fixed a major flaw where artists appearing in multiple genres (like "Various Artists") were only partially represented. The plugin now aggregates child albums from *all* matching nodes across the entire library tree when no genre is selected.
**Dynamic Playlist Synchronization.** Refined the playlist driving logic to ensure that selecting "All" or switching between categories immediately and accurately updates the active DeaDBeeF playlist.

### Bug Fixes

**Critical Memory Leak.** Fixed a bug where `cui_widget_t` was never freed during layout changes or widget destruction.
**Resource Teardown.** Implemented a recursive cleanup for the custom `scriptableItem_t` tree to ensure a clean exit during plugin shutdown.
**Idle State Re-population.** Improved the GTK idle-loop handling to ensure the UI only repopulates when the media library scanner is completely idle.

## v0.1.2-alpha

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

**Official API Allocation.** Fixed a major segfault by switching from manual struct allocation to the official `scriptableItemAlloc` and related APIs exported by `medialib.so`.
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
