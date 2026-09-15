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

// --- gtkui vtable fake (the issue-#1 tripwire) ---
int mock_gtkui_api_major;
int mock_gtkui_api_minor;
int mock_w_save_layout_called;
char mock_w_save_layout_last_key[64];
const ddb_gtkui_widget_t *mock_w_save_layout_last_val;
ddb_gtkui_widget_t *mock_gtkui_root;

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

static ddb_gtkui_widget_t *mt_w_get_rootwidget(void) {
    return mock_gtkui_root;
}

// The tree the fake create_item_tree hands back (test-settable). Ownership
// stays with the test — mt_free_item_tree is a deliberate no-op because
// update_tree_data calls it on refresh paths and the test frees the tree
// itself with mock_node_free.
static mock_node_t *g_mock_item_tree;

static ddb_medialib_item_t *mt_create_item_tree(ddb_mediasource_source_t *source,
                                                ddb_scriptable_item_t *preset,
                                                const char *filter) {
    (void)source; (void)preset; (void)filter;
    return (ddb_medialib_item_t *)g_mock_item_tree;
}

static void mt_free_item_tree(ddb_mediasource_source_t *source, ddb_medialib_item_t *list) {
    (void)source; (void)list;
}

void mock_set_item_tree(mock_node_t *root) {
    g_mock_item_tree = root;
}

static int mt_w_save_layout_to_conf_key(const char *key, ddb_gtkui_widget_t *val) {
    mock_w_save_layout_called++;
    snprintf(mock_w_save_layout_last_key, sizeof(mock_w_save_layout_last_key),
             "%s", key ? key : "(null)");
    mock_w_save_layout_last_val = val;
    if (!val) {
        // The issue-#1 crash: GTKUI's serializer dereferences val
        // unconditionally; the contract requires a non-NULL widget pointer.
        g_error("mock gtkui: w_save_layout_to_conf_key called with NULL val");
    }
    return 0;
}

static DB_functions_t g_api;
static DB_mediasource_t g_ml;
static ddb_gtkui_t g_gtkui;

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
    g_ml.create_item_tree = mt_create_item_tree;
    g_ml.free_item_tree = mt_free_item_tree;

    deadbeef_api = &g_api;
    medialib_plugin = &g_ml;

    memset(&g_gtkui, 0, sizeof(g_gtkui));
    g_gtkui.w_get_rootwidget = mt_w_get_rootwidget;
#if DDB_GTKUI_API_LEVEL >= 206
    g_gtkui.w_save_layout_to_conf_key = mt_w_save_layout_to_conf_key;
#endif
    gtkui_plugin = &g_gtkui;
    mock_gtkui_set_api_version(DDB_GTKUI_API_VERSION_MAJOR, DDB_GTKUI_API_VERSION_MINOR);
}

void mock_gtkui_set_api_version(int major, int minor) {
    mock_gtkui_api_major = major;
    mock_gtkui_api_minor = minor;
    // gtkui publishes its API level through the plugin version fields; that
    // pair is the only sound runtime channel for member presence (the struct
    // has no _size tail guard).
    gtkui_plugin->gui.plugin.version_major = major;
    gtkui_plugin->gui.plugin.version_minor = minor;
}

void mock_reset(void) {
    mock_plt_count = 0;
    mock_plt_add_called = 0;
    mock_last_plt_add_title[0] = '\0';
    mock_w_save_layout_called = 0;
    mock_w_save_layout_last_key[0] = '\0';
    mock_w_save_layout_last_val = NULL;
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
