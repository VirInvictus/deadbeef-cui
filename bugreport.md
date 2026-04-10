# deadbeef-cui: Bug Report & Feature Plan

## 1. Memory Management & Teardown
**Status:** Fixed
* **The Leak:** `cui_create_widget` allocates `cui_widget_t` via `malloc`. The `cui_destroy` callback tears down the DeaDBeeF listeners and cached tree, but didn't initially call `free(cw)`. We previously added `free(cw)` but it caused a double-free because GTKUI manages widget destruction.
* **The Dirty Exit:** `my_preset` is allocated as a custom scriptable linked list in `init_my_preset`. It is never freed in `cui_stop()`. While the OS reclaims this on application exit, it's terrible hygiene for a C plugin.
* **Fix:** Removed `free(cw);` from `cui_destroy` (relying on GTKUI lifecycle to handle the base widget). Wrote a recursive teardown function for `ddb_scriptable_item_t` and invoke it in `cui_stop`. Removed `gtkui_plugin->w_unreg_widget("cui");` from `cui_stop` to prevent a segfault when closing the application.

## 2. Tree Traversal & Hardcoded Depths
**Status:** Fixed
* **The Flaw:** `populate_list_aggregated` hardcodes a 2-level deep traversal (`lvl1` -> `lvl2`). When a Genre is selected, this correctly maps to Artists -> Albums. However, on startup (when Root is passed), it maps to Genres -> Artists. You are currently populating the Album column with Artists on the initial load.
* **Fix:** Refactored aggregation into a recursive multi-filter system. Population now correctly identifies depth targets and correctly populates the initial Album list from the Root tree.

## 3. The "Various Artists" Collision (String Matching)
**Status:** Fixed
* **The Flaw:** `find_node_by_text` grabs the *first* node that matches a string. If a user clicks "Various Artists" in the Artist column (without a Genre selected), the loop finds the "Various Artists" node under the first alphabetical Genre (e.g., "Electronic") and breaks. It silently ignores "Various Artists" under "Rock", "Jazz", etc. 
* **Fix:** Switched to string-based aggregation for ambiguous selections. When no parent genre is selected, clicking an Artist now aggregates child albums from *all* artist nodes matching that string across the entire tree. 

## 4. Feature Implementation: "All" Rows
**Status:** Implemented
* **Requirement:** Add "All Genres", "All Artists", and "All Albums" to the top of their respective lists.
* **Implementation:**
    1.  **UI Population:** Modified `populate_list_multi` to unconditionally prepend relevant "All [Category]" strings.
    2.  **Selection Handling:** Selection handlers now check for these strings and treat them as `NULL` filters, triggering hierarchical aggregation from the appropriate depth.
    3.  **Cascading State:** Selections correctly cascade through the UI, ensuring all columns and the playlist stay synchronized even when broad categories are selected.

#5 When nothing is selected, the all of each filter should be default. (Fixed)

## 6. Startup Delay (Duplicate Media Library Scan)
**Status:** Fixed
* **The Flaw:** The plugin originally created its own `"cui"` media library source, causing DeaDBeeF to scan the user's entire music folder twice on startup (once for the main UI, once for the CUI plugin). This caused massive disk I/O contention and a 2-4 second delay before the UI populated.
* **Fix:** Implemented a robust workaround using `dlopen` and `dlsym` to dynamically acquire the `gtkui_medialib_get_source` function from the loaded GTK3 plugin. The Facet Browser now safely piggybacks on the main interface's media library, completely eliminating the redundant background scan and startup delay.

## 7. Empty Whitespace Side-Scrolling
**Status:** Fixed
* **The Flaw:** The filter columns (Genre, Artist, Album) allowed the user to scroll horizontally into empty whitespace. This happened because GTK tree view columns default to `GTK_TREE_VIEW_COLUMN_GROW_ONLY`. If a column grew to fit long text, it never shrank when the list was updated with shorter text, triggering a wide horizontal scrollbar for empty space.
* **Fix:** Set the column sizing policy to `GTK_TREE_VIEW_COLUMN_AUTOSIZE` during `create_column`. The column now automatically resizes to match the exact width of the current list content, ensuring horizontal scrollbars only appear when there is actual text extending beyond the pane.
