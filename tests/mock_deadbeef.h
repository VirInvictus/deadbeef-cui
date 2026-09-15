#ifndef MOCK_DEADBEEF_H
#define MOCK_DEADBEEF_H

#include <deadbeef/deadbeef.h>

// Minimal in-process fakes for the slices of the DeaDBeeF + medialib API the cui
// engine actually calls. The plugin only ever touches a medialib node through
// medialib_plugin->tree_item_*, and a track through pl_find_meta_raw, so we get
// full control by handing the cui code our own structs cast to the opaque API
// types. Nothing here talks to a real player; it exists so the engine functions
// in cui_data.c can be exercised deterministically off-thread of DeaDBeeF.

// A fake track. We only model the two meta keys cui reads (title, artist).
typedef struct {
    const char *title;
    const char *artist;
} mock_track_t;

// A fake medialib tree node. cui treats ddb_medialib_item_t* opaquely, so the
// layout is entirely ours: leaf nodes carry a track, grouping nodes carry text.
typedef struct mock_node {
    const char *text;       // facet label (NULL for the synthetic root)
    mock_track_t *track;    // non-NULL only on leaf (track) nodes
    struct mock_node *children;
    struct mock_node *next;
} mock_node_t;

// Wire deadbeef_api + medialib_plugin (the globals cui_data.c/cui_widget.c read)
// to the fakes. Call once before any test.
void mock_deadbeef_install(void);

// Reset per-test capture state.
void mock_reset(void);

// --- viewer-playlist capture hooks ---
// plt_get_count/plt_add/plt_get_for_idx operate on an in-memory playlist
// table (plt_add appends; plt_get_title/plt_find_meta/plt_replace_meta read
// and write it; plt_clear records which entry it emptied), so tests can
// assert per-playlist effects. plt_add also captures the requested title —
// what fix #1 must drive from the per-instance autoplaylist_name rather than
// the dead global conf key.
extern char mock_last_plt_add_title[256];
extern int  mock_plt_add_called;
extern int  mock_plt_clear_called;
// Whether mt_plt_clear emptied the playlist at table index idx.
int mock_plt_was_cleared(int idx);

// --- gtkui vtable fake (the issue-#1 tripwire) ---
// gtkui_plugin is populated with a fake vtable whose published API version is
// switchable (2.6 default, 2.5 models a pre-1.10.1 runtime). The
// w_save_layout_to_conf_key stub records its arguments and hard-fails (g_error)
// on a NULL val: passing NULL there is the issue-#1 crash, so any
// reintroduction in a path the suite exercises dies immediately.
extern int  mock_gtkui_api_major;
extern int  mock_gtkui_api_minor;
extern int  mock_w_save_layout_called;
extern char mock_w_save_layout_last_key[64];
extern const ddb_gtkui_widget_t *mock_w_save_layout_last_val;
// Switchable root handed back by the fake w_get_rootwidget (NULL by default).
extern ddb_gtkui_widget_t *mock_gtkui_root;
void mock_gtkui_set_api_version(int major, int minor);

// --- tree builder helpers (test-owned; freed with mock_node_free) ---
mock_node_t *mock_group(const char *text, mock_node_t *children, mock_node_t *next);
mock_node_t *mock_leaf(const char *title, const char *artist, mock_node_t *next);
void mock_node_free(mock_node_t *n);

// Tree returned by the fake create_item_tree (what update_tree_data caches).
// The test owns it: update_tree_data's free_item_tree calls are no-ops here.
void mock_set_item_tree(mock_node_t *root);

#endif // MOCK_DEADBEEF_H
