# deadbeef-cui — Roadmap

What's done, what's next. Sequenced for feature-parity with foobar2000's Columns UI. Updated as of v0.7.1-alpha.

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

- [x] **Playlist Driving:** Dynamically populate the active playlist based on facet selection.
- [x] **Playback Integration:** Automatically trigger playback on selection double-click.

## Phase 3: Sorting & Interaction
*Refining the behavior to match professional library managers.*

- [x] **Alphabetical Ordering:** Fix the "jumping" behavior by ensuring all facet lists are strictly sorted alphabetically.
- [x] **Header Sort Buttons:** Implement toggleable sort modes (Alphabetical vs. Item Count) accessible via column headers.
- [x] **Selection Count:** Display the total number of items in each category next to the label in a dedicated "Count" column.
- [ ] **Multi-Selection:** Support selecting multiple entries in a column (Ctrl/Shift-click) to aggregate filters across multiple genres/artists.
- [ ] **Track Count Caching:** Pre-calculate track counts during initial library load to ensure instantaneous filtering for large libraries (50k+ tracks).
- [ ] **Fix Scroll bar / Count being blended into each other:** Might look bad on some themes.

## Phase 4: Customization & Formatting
*Extending the flexibility of the column metadata.*

- [ ] **Custom Search Formatting:** Full support for DeaDBeeF title formatting syntax (e.g., `%album% [%year%]`) for column patterns.
- [ ] **User-Configurable Columns:** UI for adding, removing, and reordering facets (e.g., Year → Genre → Artist).
- [ ] **Album Artist Logic:** Standardize tag priority handling (TPE2 vs. `ALBUM ARTIST`) across different filetypes (Opus, MP3, FLAC).
- [ ] **Prefix Handling:** Option to ignore leading articles ("The", "A", "An") during alphabetical sorting.

## Phase 5: Advanced CUI Features
*Deep integration with the DeaDBeeF ecosystem.*

- [ ] **Integrated Search Bar:** Real-time filtering search box that narrows the facets as you type.
- [ ] **Context Menus:** Right-click interaction for selection (Add to current playlist, Send to new playlist, Queue next).
- [ ] **Multivalue Tag Support:** Correctly split and aggregate tags with multiple values (e.g., `Genre: Rock; Progressive`).
- [ ] **Autoplaylist Persistence:** Option to link a specific playlist to the facet browser so it stays in sync.

## Phase 6: Visuals & Layout (The 1.0 Milestone)
*Polishing the aesthetic and reaching feature-parity.*

- [ ] **Album Art Grid (separate plugin in repo ):** Optional grid view for the Album facet, showing covers instead of text. Also controllable by Facets. But a separate plugin altogether. In the same repo - plugin name: deadbeef-cui-gridview
- [ ] **Art Caching:** High-performance background thumbnailer for cover art. For deadbeef-cui-gridview.
- [ ] **Design Mode Integration:** Support for DeaDBeeF's Design Mode for seamless layout embedding.
- [ ] **1.0.0 Stable Release:** Final documentation, icon assets, and feature-parity verification with foobar2000 Columns UI.

---

## Deferred (v2.0+)
- [ ] Folder-based browsing mode.
- [ ] Custom CSS styling for facet rows.
- [ ] Advanced statistics (total playtime, bitrate breakdown).
