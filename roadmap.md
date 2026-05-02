# deadbeef-cui — Roadmap

What's done, what's next. Sequenced for feature-parity with foobar2000's Columns UI. Updated as of v1.2.0.

---

## Phase 0: Foundation & Core Logic
*The underlying engine connecting to DeaDBeeF's internal database.*

- [x] **Project Setup:** Scaffold C/C++ build system (CMake) with GTK3 and DeaDBeeF headers.
- [x] **Plugin Skeleton:** Compile a basic plugin that DeaDBeeF recognizes.
- [x] **Database Binding:** Read library data using DeaDBeeF's internal `medialib` API.
- [x] **Stability Fixes:** Correct pointer handling and official API usage to prevent segfaults.
- [x] **Memory Management:** Resolved memory leaks in widget destruction and plugin shutdown.
- [x] **Performance:** Eliminated 2-4 second startup delay by sharing the main UI's media library database.

## Phase 1: The Triple-Pane Layout
*Constructing the primary faceted browsing interface.*

- [x] **UI Layout:** Construct the triple-pane GTK widget layout (Genre, Album Artist, Album).
- [x] **Hierarchical Filtering:** Genre selection updates Artist list; Artist selection updates Album list.
- [x] **Aggregate Views:** Columns show all items by default when no parent filter is active.
- [x] **Robust Traversal:** Replaced hardcoded depth traversal with a recursive aggregation system.
- [x] **"Various Artists" Fix:** Implemented string-based aggregation for multi-genre artists.
- [x] **"All" Rows:** Added "All Genres/Artists/Albums" options for broad filtering.
- [x] **UI Polish:** Fixed empty whitespace horizontal scrolling in filter boxes.

## Phase 2: Playback & Basic Integration
*Connecting the browser to the player's core actions.*

- [x] **Playlist Driving:** Dynamically populate a dedicated "Library Viewer" playlist based on facet selection, protecting user playlists.
- [x] **Playback Integration:** Automatically trigger playback on selection double-click.

## Phase 3: Sorting & Interaction
*Refining the behavior to match professional library managers.*

- [x] **Alphabetical Ordering:** Fix the "jumping" behavior by ensuring all facet lists are strictly sorted alphabetically.
- [x] **Header Sort Buttons:** Implement toggleable sort modes (Alphabetical vs. Item Count) accessible via column headers.
- [x] **Selection Count:** Display the total number of items in each category next to the label in a dedicated "Count" column.
- [x] **Multi-Selection:** Support selecting multiple entries in a column (Ctrl/Shift-click) to aggregate filters across multiple genres/artists.
- [x] **Track Count Caching:** Pre-calculate track counts during initial library load to ensure instantaneous filtering for large libraries (50k+ tracks).
- [x] **Fix Scroll bar / Count being blended into each other:** Might look bad on some themes.

## Phase 4: Customization & Formatting
*Extending the flexibility of the column metadata.*

- [x] **Custom Search Formatting:** Full support for DeaDBeeF title formatting syntax (e.g., `%album% [%year%]`) for column patterns.
- [x] **User-Configurable Columns:** UI for adding, removing, and reordering facets (e.g., Year → Genre → Artist).
- [x] **Album Artist Logic:** Standardize tag priority handling (TPE2 vs. `ALBUM ARTIST`) across different filetypes (Opus, MP3, FLAC).
- [x] **Prefix Handling:** Option to ignore leading articles ("The", "A", "An") during alphabetical sorting.
- [x] **Show List Number:** Display the total number of items in each column's "All" row (e.g., [All (55 Genres)]) for a clear overview of selection size.

## Phase 5: Advanced CUI Features
*Deep integration with the DeaDBeeF ecosystem.*

- [x] **Integrated Search Bar:** Real-time filtering search box that narrows the facets as you type (activated by CTRL-SHIFT-F - should be hidden from sight otherwise but typing in Eminem would filter Genre/Artist/Album to only show any items that have a song with the word eminem in it (should be case-insensitive).
- [x] **Search bar fixes:** Currently, the search bar blends into the facet headers. A little more space would be ideal.
- [x] **Context Menus:** Right-click interaction for selection (Add to current playlist, Send to new playlist, Queue next).
- [x] **Multivalue Tag Support:** Correctly split and aggregate tags with multiple values (e.g., `Genre: Rock; Progressive`).
- [x] **Autoplaylist Persistence:** Option to link a specific playlist to the facet browser so it stays in sync.

## Phase 6: Visuals & Layout (The 1.0 Milestone)
*Polishing the aesthetic and reaching feature-parity.*

- [x] **Deep Bug Fixing** - Refactor anything worth refactoring. Clean up code use in testing. Make sure everything is as tight as it can be.
- [x] **Assure GTK4-compiance without breaking GTK3** - I gotta assume Deadbeef won't be GTK3 forever.
- [x] **Design Mode Integration:** Support for DeaDBeeF's Design Mode for seamless layout embedding.
- [x] **1.0.0 Stable Release:** Final documentation, icon assets, and feature-parity verification with foobar2000 Columns UI.

---

## Phase 7: Optimization & Technical Debt
*Hardening the architecture and improving performance for large libraries.*

- [x] **Selection Persistence:** Restore previously selected items after a list refresh.
- [x] **Efficient Playlist Lookup:** Replace manual playlist iteration with `plt_find_by_name`.
- [x] **Search Allocation Storm:** Optimize `track_matches_search` by removing redundant `g_utf8_strdown` heap allocations.
- [x] **Thread-Safe Tree Teardown:** Fix the race condition in `cui_destroy` by ensuring `ml_source` remains valid until all widgets are destroyed.
- [x] **Instance-Specific Settings:** Move from global `cui.*` config keys to proper `ddb_gtkui_widget_extended_api_t` serialization to support multiple independent browser instances.
- [x] **Library Event Debouncing:** Implement a timer for `ml_listener_cb` to prevent redundant UI refreshes during batch metadata edits.
- [x] **Memory Management:** 
    - [x] Fix `GtkMenu` leak in right-click context menus.
    - [x] Properly disconnect main window signal handlers in `cui_stop`.
- [x] **Real-time Library Sync:** Ensure private media source remains synchronized with background library updates via `ml_listener_cb`.
- [x] **Standardized Shortcuts:** Unify shortcut keys (`CTRL-SHIFT-F`) and ensure they don't conflict with DeaDBeeF core.
- [x] **The [All] items don't play a song on double click** - Bug that should be fixed
- [x] **If focus is on the facet and a letter is pressed, the selection should jump to that letter. This should work for as many characters as the user types**

## Phase 8: Advanced Performance Refinement
*Pushing the limits of the faceted browsing engine.*

- [x] **Memoization Refresh:** Re-enable the `track_counts_cache` when search filters are active. Validity is guaranteed by the existing `last_ml_modification_idx = -1` reset on search change, which forces a full cache rebuild. (v1.2.5)

### Dropped from Phase 8

- ~~**Incremental Playlist Updates** via `DDB_PLAYLIST_CHANGE_CONTENT`~~ — investigated and dropped in v1.2.5. The flag value is `0`, which is what we already pass to `sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0)`. The playlist widget treats that event as a full rebuild signal regardless, so a diff-based incremental update would require reimplementing the rebuild path with per-track add/remove tracking against the current playlist contents — a complex change for ~50–100 ms savings on selection switches that nobody has flagged as sluggish. The v1.2.4 fix that stopped auto-populating the playlist on first init already addressed the only observed pain point.
- [x] **Modular Refactoring:** Break up the monolithic `main.c` into domain-specific modules for better maintainability (v1.2.3).
- [x] **Standardized Shortcuts:** Unify shortcut keys (`CTRL-SHIFT-F`) and ensure they don't conflict with DeaDBeeF core.

## Phase 9: Startup Latency & Theme Conformance
*Closing the gap between widget creation and a populated, theme-correct view.*

Measured baseline (6,367-track library, fresh launch with cui in layout but no GTKUI medialib widget): `ml playlist load time` 0.36 s, `scan time` 0.64 s, `tree build time` 0.10 s, plus a hardcoded 1000 ms debounce in `ml_event_idle_cb` between the first `CONTENT_DID_CHANGE` and our rebuild. Total observable empty-→-populated gap ≈ 1.5–2.0 s. Tree build itself is fast; the wins are in the wait state.

### Startup latency
- [x] **Skip the first-fire debounce.** `ml_event_idle_cb` always queues a 1000 ms `g_timeout_add` before `update_tree_data` runs. The debounce exists to coalesce batch tag-edit events; on the first content-did-change after widget creation it is pure overhead. Track an `initial_sync_done` flag on `cui_widget_t` and dispatch the first rebuild immediately, then resume the 1000 ms debounce for subsequent events. (v1.2.4)
- [x] **Defer the synchronous `update_tree_data` in `cui_init` to `g_idle_add`.** GTKUI's layout loader calls our `init` directly, so any work we do there blocks the rest of the layout from rendering. Pushing the initial rebuild to the next idle tick lets the empty columns paint instantly and runs the heavy work after the window is visible. (v1.2.4)
- [x] **Don't pre-populate the Library Viewer playlist on first init.** `update_tree_data` ends with an unconditional `on_column_changed` call that arms `deferred_column_changed_cb` → `update_playlist_from_cui`, which copies every track in the library into the viewer playlist while holding `pl_lock`. With no saved selection, this is a full-library copy the user hasn't asked for. (v1.2.4)

### Theme conformance
- [x] **Inherit `gtkui.font.listview_*` for row cells.** Currently our `GtkCellRendererText` instances use the default GTK theme font. When `gtkui.override_listview_colors=1` is set, the playlist widget reads `gtkui.font.listview_text` (e.g. `Söhne 12`) and our cells fall out of visual sync. Read `gtkui.font.listview_text` and apply it to the row renderer's `font` property; if override is off, leave the property unset so the GTK theme applies. (v1.2.4)
- [x] **Inherit `gtkui.font.listview_column_text` for headers.** Column header labels are separate widgets. Use `gtk_tree_view_column_set_widget` with a `GtkLabel` whose Pango font description comes from `gtkui.font.listview_column_text` (e.g. `Söhne Semi-Bold 14`). Same override-flag gating as row cells. (v1.2.4)
- [x] **Re-read fonts on `DB_EV_CONFIGCHANGED`.** When the user changes the playlist font in DeaDBeeF preferences, our cells refresh automatically. The message handler dispatches an idle that walks `all_cui_widgets`, compares cached font state against current `gtkui.font.listview_*` keys, and rebuilds only the widgets that actually changed (skips unrelated CONFIGCHANGED events like volume changes). (v1.2.5)

---

## Phase 10: Add to DeaDBeeF Plugin List
*Packaging and submitting for official inclusion in the DeaDBeeF ecosystem. (Was Phase 9.)*

- [x] **Consolidated Build System:** Removed the legacy `Makefile` in favor of a single, robust CMake-driven build process.
- [x] **Manifest Authoring:** `manifest.json` lives in the repo root. It tracks the example template (git source, cmake build at root, GTK3 env vars from the builder, output `cui.so`). Re-verify against the current `deadbeef-plugin-builder` schema when opening the submission PR.
- [x] **Static Linking Audit:** Audited via `ldd` on the built `cui.so`. The plugin links only against the system GTK3 / glib / cairo / pango stack and `libdl` — all libraries DeaDBeeF itself depends on. Static-linking these would conflict with DeaDBeeF's own GTK and is incorrect for the plugin model. No non-core deps to address.
- [x] **Repository Readiness:** Repo is clean — README, spec, roadmap, patchnotes, CLAUDE.md, LICENSE, manifest.json, CMakeLists.txt, src/, compiled/ all present. No stale build artifacts checked in beyond the intentional `compiled/ddb_misc_cui_GTK3.so` for non-builders.

### Requires Brandon (external systems / decisions)
- [ ] **Cross-Platform Verification:** Run the `deadbeef-plugin-builder` Docker environment locally to verify the plugin builds for x86_64 and i686. Manifest is in place; this is a `docker run` away when ready.
- [ ] **Submission PR:** Open a PR against `DeaDBeeF-Player/deadbeef-plugin-builder` adding the manifest. Requires GitHub credentials and your own description.
- [ ] **v2.0.0 Tagging:** A v2.0 release implies a major-feature milestone; v1.2.5 is the current state. Defer until a feature warrants it (or rebrand "stable + plugin-list ready" as v2.0 if you prefer that framing).
