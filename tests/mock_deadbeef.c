#include "cui_globals.h"
#include "mock_deadbeef.h"
#include <string.h>
#include <stdlib.h>

// Globals normally defined in main.c. The cui translation units reference these
// as externs (see cui_globals.h); the test binary links cui_data.c / cui_widget.c
// / cui_scriptable.c without main.c, so we own the definitions here.
DB_functions_t *deadbeef_api;
ddb_gtkui_t *gtkui_plugin;
DB_mediasource_t *medialib_plugin;
ddb_mediasource_source_t *ml_source;
int shutting_down;
int owns_ml_source;
int ml_modification_idx = 1;
GList *all_cui_widgets;
int config_change_pending;

int  mock_plt_count;
char mock_last_plt_add_title[256];
int  mock_plt_add_called;

// One static sentinel handed back wherever a non-NULL ddb_playlist_t* is needed.
static int g_plt_sentinel;

static void mt_pl_lock(void) {}
static void mt_pl_unlock(void) {}

static const char *mt_pl_find_meta_raw(DB_playItem_t *it, const char *key) {
    mock_track_t *t = (mock_track_t *)it;
    if (!t) return NULL;
    if (strcmp(key, "title") == 0) return t->title;
    if (strcmp(key, "artist") == 0) return t->artist;
    return NULL;
}

static const char *mt_tree_item_get_text(const ddb_medialib_item_t *item) {
    return ((const mock_node_t *)item)->text;
}
static ddb_playItem_t *mt_tree_item_get_track(const ddb_medialib_item_t *item) {
    return (ddb_playItem_t *)((const mock_node_t *)item)->track;
}
static const ddb_medialib_item_t *mt_tree_item_get_next(const ddb_medialib_item_t *item) {
    return (const ddb_medialib_item_t *)((const mock_node_t *)item)->next;
}
static const ddb_medialib_item_t *mt_tree_item_get_children(const ddb_medialib_item_t *item) {
    return (const ddb_medialib_item_t *)((const mock_node_t *)item)->children;
}

static int mt_plt_get_count(void) { return mock_plt_count; }

static int mt_plt_add(int before, const char *title) {
    (void)before;
    mock_plt_add_called++;
    strncpy(mock_last_plt_add_title, title ? title : "", sizeof(mock_last_plt_add_title) - 1);
    mock_last_plt_add_title[sizeof(mock_last_plt_add_title) - 1] = '\0';
    return 0;
}

static ddb_playlist_t *mt_plt_get_for_idx(int idx) {
    (void)idx;
    return (ddb_playlist_t *)&g_plt_sentinel;
}
static int mt_plt_get_title(ddb_playlist_t *plt, char *buffer, int bufsize) {
    (void)plt;
    if (bufsize > 0) buffer[0] = '\0';
    return 0;
}
static void mt_plt_unref(ddb_playlist_t *plt) { (void)plt; }

static DB_functions_t g_api;
static DB_mediasource_t g_ml;

void mock_deadbeef_install(void) {
    memset(&g_api, 0, sizeof(g_api));
    memset(&g_ml, 0, sizeof(g_ml));

    g_api.pl_lock = mt_pl_lock;
    g_api.pl_unlock = mt_pl_unlock;
    g_api.pl_find_meta_raw = mt_pl_find_meta_raw;
    g_api.plt_get_count = mt_plt_get_count;
    g_api.plt_add = mt_plt_add;
    g_api.plt_get_for_idx = mt_plt_get_for_idx;
    g_api.plt_get_title = mt_plt_get_title;
    g_api.plt_unref = mt_plt_unref;

    g_ml.tree_item_get_text = mt_tree_item_get_text;
    g_ml.tree_item_get_track = mt_tree_item_get_track;
    g_ml.tree_item_get_next = mt_tree_item_get_next;
    g_ml.tree_item_get_children = mt_tree_item_get_children;

    deadbeef_api = &g_api;
    medialib_plugin = &g_ml;
}

void mock_reset(void) {
    mock_plt_count = 0;
    mock_plt_add_called = 0;
    mock_last_plt_add_title[0] = '\0';
}

mock_node_t *mock_group(const char *text, mock_node_t *children, mock_node_t *next) {
    mock_node_t *n = calloc(1, sizeof(mock_node_t));
    n->text = text;
    n->children = children;
    n->next = next;
    return n;
}

mock_node_t *mock_leaf(const char *title, const char *artist, mock_node_t *next) {
    mock_node_t *n = calloc(1, sizeof(mock_node_t));
    n->text = title;
    n->track = calloc(1, sizeof(mock_track_t));
    n->track->title = title;
    n->track->artist = artist;
    n->next = next;
    return n;
}

void mock_node_free(mock_node_t *n) {
    while (n) {
        mock_node_t *next = n->next;
        mock_node_free(n->children);
        free(n->track);
        free(n);
        n = next;
    }
}
