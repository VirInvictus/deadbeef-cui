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

// --- get_or_create_viewer_playlist capture hooks ---
// plt_get_count returns this (default 0 = "no existing playlists", so the lookup
// falls straight through to plt_add and we can capture the requested name).
extern int  mock_plt_count;
// Last title handed to plt_add — this is what fix #1 must drive from the
// per-instance autoplaylist_name rather than the dead global conf key.
extern char mock_last_plt_add_title[256];
extern int  mock_plt_add_called;

// --- tree builder helpers (test-owned; freed with mock_node_free) ---
mock_node_t *mock_group(const char *text, mock_node_t *children, mock_node_t *next);
mock_node_t *mock_leaf(const char *title, const char *artist, mock_node_t *next);
void mock_node_free(mock_node_t *n);

#endif // MOCK_DEADBEEF_H
