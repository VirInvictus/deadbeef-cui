#ifndef CUI_DATA_H
#define CUI_DATA_H

#include "cui_globals.h"

// Hidden per-playlist meta marking the viewer playlists this plugin created
// (and pre-marker viewers it adopted). Value = the viewer name it was created
// for. The marker is what makes viewer matching safe: a user playlist that
// merely shares the name "Library Viewer" carries no marker, so the shutdown
// clear (marker-only matching) can never wipe it.
#define CUI_VIEWER_MARKER "_cui_viewer"

int track_matches_search(DB_playItem_t *track, const char *search_text);
int count_tracks_recursive(const ddb_medialib_item_t *node, cui_widget_t *cw);
const char *skip_prefix(const char *str, int ignore);
int sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data);
void add_tracks_recursive_multi(const ddb_medialib_item_t *node, int current_level, cui_widget_t *cw, ddb_playlist_t *plt, DB_playItem_t **after);
ddb_playlist_t *find_viewer_playlist(cui_widget_t *cw);
ddb_playlist_t *find_marked_viewer_playlist(cui_widget_t *cw);
ddb_playlist_t *get_or_create_viewer_playlist(cui_widget_t *cw);
void populate_playlist_from_cui(cui_widget_t *cw, ddb_playlist_t *plt, int clear_first);
void update_playlist_from_cui(cui_widget_t *cw, int synchronous);
void cui_fill_cancel(cui_widget_t *cw);
void aggregate_recursive_multi(const ddb_medialib_item_t *node, int current_level, int target_level, cui_widget_t *cw, GHashTable *seen);
void populate_list_multi(GtkListStore *store, int target_level, cui_widget_t *cw, int col_idx);
void update_tree_data(cui_widget_t *cw);

#endif // CUI_DATA_H
