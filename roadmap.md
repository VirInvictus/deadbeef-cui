# deadbeef-cui — Roadmap

What's done, what's next. Updated as of v0.6.0-alpha.

---

## Done

- [x] **Project Setup:** Scaffold C/C++ build system (CMake) with GTK3 and DeaDBeeF headers.
- [x] **Plugin Skeleton:** Compile a basic plugin that DeaDBeeF recognizes.
- [x] **UI Layout:** Construct the triple-pane GTK widget layout (Genre, Album Artist, Album).
- [x] **Database Binding:** Read library data using DeaDBeeF's internal `medialib` API.
- [x] **Hierarchical Filtering:** Genre selection updates Artist list; Artist selection updates Album list.
- [x] **Aggregate Views:** Columns show all items by default when no parent filter is active.
- [x] **Playlist Driving:** Dynamically populate the active playlist based on facet selection.
- [x] **Playback Integration:** Automatically trigger playback on selection double-click.
- [x] **Stability Fixes:** Correct pointer handling and official API usage to prevent segfaults.
- [x] **Memory Management:** Resolved memory leaks in widget destruction and plugin shutdown.
- [x] **Robust Traversal:** Replaced hardcoded depth traversal with a recursive aggregation system.
- [x] **"Various Artists" Fix:** Implemented string-based aggregation for multi-genre artists.
- [x] **"All" Rows:** Added "All Genres/Artists/Albums" options for broad filtering.
- [x] **Performance:** Eliminated 2-4 second startup delay by sharing the main UI's media library database.
- [x] **UI Polish:** Fixed empty whitespace horizontal scrolling in filter boxes.

---

## Future

- [ ] **Configuration Settings:** User-configurable columns (e.g., modifying the triple-filter order).
- [ ] **Custom Search Formatting:** Allow users to define their own Title Formatting strings for columns.
- [ ] **Album Artist set per filetype:** Handle tagging differences between Opus and MP3 (TPE2 vs ALbum Artist).
- [ ] **Design Mode Polish:** Ensure widget properties are correctly serialized during layout saving.
- [ ] **Multi-Selection:** Support selecting multiple entries in a column to aggregate filters.
