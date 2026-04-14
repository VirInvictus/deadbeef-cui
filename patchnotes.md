# deadbeef-cui — Patch Notes

## v0.8.2-alpha (Current)

---

### Major Features

**Dedicated Library Viewer Playlist.** To prevent accidental deletion of user playlists, the plugin now dynamically creates and targets a dedicated "Library Viewer" playlist for all facet selections.
**Smart Random Playback.** Double-clicking any facet row now respects your current playback order. If Shuffle or Random modes are active, the plugin will immediately trigger a random track from the current selection.

## v0.8.1-alpha

---

### Bug Fixes

**Aggregate Row Collision.** Fixed a sorting bug where albums or metadata starting with a bracket (e.g., `[A->B] Life` or `[Led Zeppelin IV]`) were mistakenly treated as the generated aggregate `[ All ]` row. The sort function now relies on a hidden boolean flag in the GTK ListStore rather than string-matching, making the sorting bulletproof against any user metadata.

## v0.8.0-alpha

---

### Major Features

**Dynamic User-Configurable Columns.** The hardcoded triple-pane layout has been completely rewritten into a dynamic, recursive engine. Users can now define anywhere from 1 to 5 columns in the plugin settings, customizing both the column title and the underlying metadata query.
**Custom Search Formatting.** Each column now fully supports DeaDBeeF's title formatting syntax. You can use simple tags like `%genre%` or complex logic like `$if2(%album artist%,%artist%)` to power your facet queries.
**Album Artist Standardization.** By utilizing title formatting for columns, "Album Artist" now properly evaluates priority logic (`$if2(%album artist%,%artist%)` by default) resolving fragmentation across different file types (FLAC, MP3, Opus).
**Prefix Handling (Article Ignoral).** Added a new configuration toggle to automatically ignore leading articles ("The ", "A ", "An ") when sorting rows alphabetically.

### Code Quality & Debugging

**Debug Logging.** Added a `DEADBEEF_CUI_DEBUG` environment variable toggle. When launched with `DEADBEEF_CUI_DEBUG=1 deadbeef`, the plugin will emit verbose lifecycle and state evaluation logs to `stderr` to aid in deeper bug fixing and issue reporting.

**Optimized Multi-Selection Performance.** Added a robust debouncing mechanism to column selection updates. This resolves an exponential recursion issue where GTK's native "changed" signal would fire simultaneously for every single item added to a multi-selection block, drastically reducing redundant computations and CPU spikes.

## v0.7.2-alpha

---

### Enhancements

**Multi-Selection Support.** All facet columns now support GTK multi-selection (Ctrl-click / Shift-click). You can now select multiple genres, artists, or albums simultaneously to aggregate the results in the subsequent columns and the playlist view.

**Track Count Caching.** Implemented a `GHashTable` based memoization system for recursive track counting. This drastically speeds up the calculation of track counts for each facet entry, reducing CPU load when navigating the media library, especially for huge collections (50k+ tracks).

### UI Polish

**Count Column Padding.** Added `xpad` to the GTK cell renderer for the "Count" column to prevent the count numbers from visually bleeding into the scrollbar on some desktop themes.

## v0.7.1-alpha

---

### Bug Fixes

**Pinned [All] Row Sorting.** Fixed a bug where the "[ All ]" rows would lose their top position when sorting by Count or descending Alphabetical order. The sort function now explicitly checks the sort direction and ensures the aggregate "All" rows remain fixed at the top regardless of user-selected sorting criteria.

## v0.7.0-alpha

---

### Enhancements

**Alphabetical Ordering.** Fixed the "jumping" behavior by ensuring all facet lists are strictly sorted alphabetically. A custom sort function keeps "[ All ]" rows pinned to the top while ordering all other metadata strings using UTF-8 collation.
**Track Counting (Selection Count).** Each facet row now displays the total number of tracks it contains in a dedicated "Count" column. This is calculated through a recursive traversal of the media library tree for every unique metadata entry.
**Header Sort Buttons.** Columns are now interactive. Users can toggle between Alphabetical (Name) and Item Count (Descending) sorting by clicking the respective column headers.

### UI Polish

**Fixed Count Column Alignment.** The "Count" column is now pinned to the right edge of each facet. Metadata strings (Genre, Artist, Album) will now automatically ellipsize with "..." if they exceed the available width, ensuring the item counts and headers remain visible at all times without requiring horizontal scrolling.

### Bug Fixes

**Fixed Hierarchical Track Counts.** Corrected a logic error where track counts were only populating for the Genre column. The aggregation engine now correctly identifies the selected parent node when traversing the Artist and Album facets, ensuring accurate counts are displayed across all columns.

## v0.6.0-alpha

---

### Bug Fixes

**Fixed Shutdown Crash (Double-Free / Segfault).** Root-caused a double-free on application exit during widget destruction. `cui_destroy` incorrectly called `free(cw)` on a widget that was already managed by the GTKUI framework, leading to a crash on teardown. Also removed the `gtkui_plugin->w_unreg_widget("cui")` call during `cui_stop` to resolve a subsequent GTK segfault when DeaDBeeF was unloading the GUI plugin.
**Thread-Safe Multi-Widget Support.** Replaced a single global `active_widget` pointer with a GTK main-thread global list (`all_cui_widgets`). This fixes use-after-free conditions that could occur if a user had multiple UI tabs or splits containing the Facet Browser and one was destroyed while the background scanner was running.
**Fixed Empty Whitespace Side-Scrolling.** Changed the column sizing behavior from `GROW_ONLY` to `AUTOSIZE`. Previously, the filter panes would expand for long text but never shrink when the list updated with shorter text, creating an unnecessary horizontal scrollbar allowing users to scroll into empty whitespace. Columns now correctly resize to the data.

### Code Quality

**Compiler Warnings Resolved.** Silenced unused parameter warnings in GTK tree-view callbacks and the main DeaDBeeF message loop. Cleaned up syntax warnings from duplicated code blocks. The plugin now compiles completely warning-free.

### Performance

**Eliminated Startup Delay.** The plugin no longer creates its own duplicate media library database on startup. By using `dlopen`/`dlsym` to acquire the internal GTKUI media library source, the plugin now perfectly shares the main database. This eliminates a redundant background scan, saving CPU, memory, and resolving a 2-4 second startup delay.

## v0.5.0-alpha

---

### Bug Fixes

**Fixed Shutdown Crash (SEGV in `create_item_tree`).** Root-caused a use-after-free during DeaDBeeF shutdown. The GTK `quit_gtk_cb` destroys our widget (freeing `cw`), but a pending `g_idle_add` callback from the medialib listener still held a pointer to the freed struct. Fixed by using a global `active_widget` pointer: the idle callback reads the global (set to NULL in `cui_destroy`) instead of dereferencing the stale callback data. Since both run on the GTK main thread, there is no race.
**Own Medialib Source.** Switched from `create_source("deadbeef")` (shared with GTKUI) to `create_source("cui")` with synced folder config. Two source instances sharing the same database path caused concurrent access conflicts in `_create_item_tree_from_collection`.
**Scanner State Guard in `update_tree_data`.** Added `scanner_state == IDLE` check directly in `update_tree_data` (not just the idle callback), so the initial call from `cui_create_widget` is also protected against calling `create_item_tree` while the scanner is still running.

### Enhancements

**`DB_EV_TERMINATE` Handling.** The `cui_message` handler catches `DB_EV_TERMINATE` as defense-in-depth, setting `shutting_down` early in the shutdown sequence.
**Thread-Safe Listener Callback.** The medialib listener (called from a background thread) now passes `NULL` to `g_idle_add` instead of `cw`, avoiding any background-thread access to widget state.

## v0.4.0-alpha

---

### Bug Fixes

**Restored Shared Media Library Source.** Reverted `create_source` name from `"deadbeef_cui"` back to `"deadbeef"`, fixing a regression where the plugin created its own empty database instead of sharing the main media library.
**Restored Scanner State Check.** Re-added the `scanner_state == IDLE` guard before repopulating the UI, preventing incomplete tree data from being displayed while the scanner is still active.
**Fixed Stale Selection Filters.** Restored clearing of selection text strings (`sel_genre_text`, `sel_artist_text`, `sel_album_text`) during tree refresh, preventing stale filters from corrupting results after a library update.
**Restored Widget Type Consistency.** Changed widget type back to `"cui"` to maintain compatibility with saved layouts.
**Restored Plugin ID.** Changed plugin ID back to `"cui"` for consistency and layout compatibility.
**Restored Direct Initialization.** Moved media source creation back to widget init instead of lazy-loading in `update_tree_data`, ensuring proper listener setup on first load.

### Code Quality

**Reformatted Source.** Restored readable, properly indented formatting throughout `main.c` (was compressed to single-line statements).

## v0.3.0-alpha

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
