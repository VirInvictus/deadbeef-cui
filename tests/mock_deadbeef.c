#include "cui_globals.h"
#include "cui_data.h"
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

// Twin of main.c's plugin definition: cui_log (cui_widget.c) references
// &cui_plugin.plugin; the mock's vlog_detailed is NULL so it is never read.
DB_misc_t cui_plugin;

char mock_last_plt_add_title[256];
int  mock_plt_add_called;

// --- playlist table (titles + the hidden viewer marker + clear capture) ---
// plt_get_for_idx returns pointers into this table, so tests can compare
// handles for identity and assert per-playlist effects.
#define MOCK_MAX_PLTS 16
#define MOCK_MAX_ITEMS 64

// A copied play item. The first two fields mirror mock_track_t's layout so
// pl_item_copy can read from either a tree leaf or another copy.
typedef struct mock_playitem {
    const char *title;
    const char *artist;
    int refc;
    char *own_title;   // heap copies owned by this item (NULL until copied)
    char *own_artist;
} mock_playitem_t;

typedef struct {
    char title[256];
    char marker[128];   // value of the CUI_VIEWER_MARKER meta ('' = unset)
    int cleared;        // set by mt_plt_clear
    // inserted play items (the chunked fill's output); owned here
    mock_playitem_t *items[MOCK_MAX_ITEMS];
    int item_count;
} mock_playlist_t;
static mock_playlist_t g_plts[MOCK_MAX_PLTS];
static int g_plts_count = 0;

int mock_plt_clear_called = 0;

int mock_plt_was_cleared(int idx) {
    if (idx < 0 || idx >= g_plts_count) return 0;
    return g_plts[idx].cleared;
}

// --- gtkui vtable fake (the issue-#1 tripwire) ---
int mock_gtkui_api_major;
int mock_gtkui_api_minor;
int mock_w_save_layout_called;
char mock_w_save_layout_last_key[64];
const ddb_gtkui_widget_t *mock_w_save_layout_last_val;
ddb_gtkui_widget_t *mock_gtkui_root;

static void mt_pl_lock(void) {}
static void mt_pl_unlock(void) {}

static const char *mt_pl_find_meta_raw(DB_playItem_t *it, const char *key) {
    mock_track_t *t = (mock_track_t *)it;
    if (!t) return NULL;
    if (strcmp(key, "title") == 0) return t->title;
    if (strcmp(key, "artist") == 0) return t->artist;
    return NULL;
}

int mock_scanner_state = 0; // DDB_MEDIASOURCE_STATE_IDLE

static ddb_mediasource_state_t mt_scanner_state(ddb_mediasource_source_t *source) {
    (void)source;
    return (ddb_mediasource_state_t)mock_scanner_state;
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

static int mt_plt_get_count(void) { return g_plts_count; }

static int mt_plt_add(int before, const char *title) {
    (void)before;
    if (g_plts_count >= MOCK_MAX_PLTS) return -1;
    mock_playlist_t *p = &g_plts[g_plts_count];
    snprintf(p->title, sizeof(p->title), "%s", title ? title : "");
    p->marker[0] = '\0';
    p->cleared = 0;
    mock_plt_add_called++;
    strncpy(mock_last_plt_add_title, title ? title : "", sizeof(mock_last_plt_add_title) - 1);
    mock_last_plt_add_title[sizeof(mock_last_plt_add_title) - 1] = '\0';
    return g_plts_count++;
}

static ddb_playlist_t *mt_plt_get_for_idx(int idx) {
    if (idx < 0 || idx >= g_plts_count) return NULL;
    return (ddb_playlist_t *)&g_plts[idx];
}

static int mt_plt_get_title(ddb_playlist_t *plt, char *buffer, int bufsize) {
    mock_playlist_t *p = (mock_playlist_t *)plt;
    if (bufsize > 0) snprintf(buffer, bufsize, "%s", p ? p->title : "");
    return 0;
}

static void mt_plt_clear(ddb_playlist_t *plt) {
    mock_playlist_t *p = (mock_playlist_t *)plt;
    mock_plt_clear_called++;
    p->cleared = 1;
    for (int i = 0; i < p->item_count; i++) {
        free(p->items[i]->own_title);
        free(p->items[i]->own_artist);
        free(p->items[i]);
    }
    p->item_count = 0;
}

static DB_playItem_t *mt_plt_insert_item(ddb_playlist_t *plt, DB_playItem_t *after, DB_playItem_t *it) {
    mock_playlist_t *p = (mock_playlist_t *)plt;
    int pos = p->item_count;
    if (after) {
        for (int i = 0; i < p->item_count; i++) {
            if ((DB_playItem_t *)p->items[i] == after) { pos = i + 1; break; }
        }
    }
    if (p->item_count < MOCK_MAX_ITEMS) {
        memmove(&p->items[pos + 1], &p->items[pos], (p->item_count - pos) * sizeof(mock_playitem_t *));
        p->items[pos] = (mock_playitem_t *)it;
        p->item_count++;
    }
    return it;
}

static int mt_plt_get_item_count(ddb_playlist_t *plt, int iter) {
    (void)iter;
    return ((mock_playlist_t *)plt)->item_count;
}

static void mt_plt_modified(ddb_playlist_t *plt) { (void)plt; }

static int mt_sendmessage(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
    (void)id; (void)ctx; (void)p1; (void)p2;
    return 0;
}

static DB_playItem_t *mt_plt_get_last(ddb_playlist_t *plt, int iter) {
    (void)iter;
    mock_playlist_t *p = (mock_playlist_t *)plt;
    if (p->item_count == 0) return NULL;
    return (DB_playItem_t *)p->items[p->item_count - 1];
}

// pl_item copies. src is a tree leaf (mock_track_t) or another copy — both
// share the title/artist pointer prefix.
static DB_playItem_t *mt_pl_item_alloc(void) {
    mock_playitem_t *it = calloc(1, sizeof(mock_playitem_t));
    it->refc = 1;
    return (DB_playItem_t *)it;
}

static void mt_pl_item_copy(DB_playItem_t *dst, DB_playItem_t *src) {
    mock_playitem_t *d = (mock_playitem_t *)dst;
    mock_track_t *s = (mock_track_t *)src;
    free(d->own_title);
    free(d->own_artist);
    d->own_title = strdup(s->title ? s->title : "");
    d->own_artist = strdup(s->artist ? s->artist : "");
    d->title = d->own_title;
    d->artist = d->own_artist;
}

static void mt_pl_item_ref(DB_playItem_t *it) {
    ((mock_playitem_t *)it)->refc++;
}

static void mt_pl_item_unref(DB_playItem_t *it) {
    mock_playitem_t *p = (mock_playitem_t *)it;
    p->refc--;
    // Ownership of playlist-resident items rests with the playlist array
    // (freed by mt_plt_clear / mock_reset), so this deliberately does not
    // free at zero.
}

static const char *mt_plt_find_meta(ddb_playlist_t *plt, const char *key) {
    mock_playlist_t *p = (mock_playlist_t *)plt;
    if (!p) return NULL;
    if (strcmp(key, CUI_VIEWER_MARKER) == 0) return p->marker[0] ? p->marker : NULL;
    return NULL;
}

static void mt_plt_replace_meta(ddb_playlist_t *plt, const char *key, const char *value) {
    mock_playlist_t *p = (mock_playlist_t *)plt;
    if (!p) return;
    if (strcmp(key, CUI_VIEWER_MARKER) == 0) snprintf(p->marker, sizeof(p->marker), "%s", value ? value : "");
}

static int mt_plt_set_curr_idx = -1;
int mock_plt_set_curr_count = 0;

static void mt_plt_set_curr(ddb_playlist_t *plt) {
    mock_plt_set_curr_count++;
    for (int i = 0; i < g_plts_count; i++) {
        if ((ddb_playlist_t *)&g_plts[i] == plt) { mt_plt_set_curr_idx = i; return; }
    }
    mt_plt_set_curr_idx = -1;
}

static ddb_playlist_t *mt_plt_get_curr(void) {
    if (mt_plt_set_curr_idx < 0 || mt_plt_set_curr_idx >= g_plts_count) return NULL;
    return (ddb_playlist_t *)&g_plts[mt_plt_set_curr_idx];
}

static void mt_plt_unref(ddb_playlist_t *plt) {
    // The real plt_unref derefs plt->refc with no NULL guard (upstream
    // playlist.c): a NULL here segfaults in production. This blind spot hid
    // the v1.3.5 marked-fallback unref bug from every viewer test, so the
    // mock refuses it loudly instead of swallowing it.
    if (!plt) {
        g_error("mock: plt_unref(NULL) — the real API would segfault");
    }
}

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
    g_api.plt_clear = mt_plt_clear;
    g_api.plt_find_meta = mt_plt_find_meta;
    g_api.plt_replace_meta = mt_plt_replace_meta;
    g_api.plt_insert_item = mt_plt_insert_item;
    g_api.plt_get_item_count = mt_plt_get_item_count;
    g_api.plt_modified = mt_plt_modified;
    g_api.sendmessage = mt_sendmessage;
    g_api.plt_get_last = mt_plt_get_last;
    g_api.pl_item_alloc = mt_pl_item_alloc;
    g_api.pl_item_copy = mt_pl_item_copy;
    g_api.pl_item_ref = mt_pl_item_ref;
    g_api.pl_item_unref = mt_pl_item_unref;
    g_api.plt_set_curr = mt_plt_set_curr;
    g_api.plt_get_curr = mt_plt_get_curr;
    g_api.plt_unref = mt_plt_unref;

    g_ml.tree_item_get_text = mt_tree_item_get_text;
    g_ml.tree_item_get_track = mt_tree_item_get_track;
    g_ml.tree_item_get_next = mt_tree_item_get_next;
    g_ml.tree_item_get_children = mt_tree_item_get_children;
    g_ml.scanner_state = mt_scanner_state;
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
    mock_scanner_state = 0;
    for (int i = 0; i < g_plts_count; i++) {
        mt_plt_clear((ddb_playlist_t *)&g_plts[i]);
    }
    g_plts_count = 0;
    mock_plt_clear_called = 0;
    mock_plt_add_called = 0;
    mock_last_plt_add_title[0] = '\0';
    mock_plt_set_curr_count = 0;
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
