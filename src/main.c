#include <deadbeef/deadbeef.h>
#include <gtk/gtk.h>
#include <deadbeef/gtkui_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DB_functions_t *deadbeef_api;
static ddb_gtkui_t *gtkui_plugin;
static DB_mediasource_t *medialib_plugin;
static ddb_mediasource_source_t *ml_source;
static int shutting_down = 0;

typedef struct scriptableKeyValue_s { struct scriptableKeyValue_s *next; char *key, *value; } scriptableKeyValue_t;
typedef struct scriptableItem_s { struct scriptableItem_s *next; uint64_t flags; scriptableKeyValue_t *properties; struct scriptableItem_s *parent, *children, *childrenTail; char *type, *configDialog; void *overrides; } scriptableItem_t;
#define SCRIPTABLE_FLAG_IS_LIST (1 << 2)

static scriptableItem_t *my_scriptable_alloc(void) { return calloc(1, sizeof(scriptableItem_t)); }
static void my_scriptable_free(scriptableItem_t *item) {
    if (!item) return; scriptableItem_t *c = item->children; while (c) { scriptableItem_t *n = c->next; my_scriptable_free(c); c = n; }
    scriptableKeyValue_t *kv = item->properties; while (kv) { scriptableKeyValue_t *n = kv->next; free(kv->key); free(kv->value); free(kv); kv = n; }
    free(item->type); free(item->configDialog); free(item);
}
static void my_scriptable_set_prop(scriptableItem_t *item, const char *k, const char *v) {
    scriptableKeyValue_t *kv = calloc(1, sizeof(scriptableKeyValue_t)); kv->key = strdup(k); kv->value = strdup(v); kv->next = item->properties; item->properties = kv;
}
static void my_scriptable_add_child(scriptableItem_t *p, scriptableItem_t *c) {
    if (p->childrenTail) p->childrenTail->next = c; else p->children = c; p->childrenTail = c; c->parent = p;
}

#define ALL_GENRES "[ All Genres ]"
#define ALL_ARTISTS "[ All Artists ]"
#define ALL_ALBUMS "[ All Albums ]"

typedef struct {
    ddb_gtkui_widget_t base; GtkListStore *store_genre, *store_artist, *store_album;
    GtkWidget *tree_genre, *tree_artist, *tree_album; int listener_id; guint idle_id;
    ddb_medialib_item_t *cached_tree; const ddb_medialib_item_t *sel_genre_node, *sel_artist_node, *sel_album_node;
    char *sel_genre_text, *sel_artist_text, *sel_album_text;
} cui_widget_t;

static ddb_scriptable_item_t *my_preset = NULL;
static void init_my_preset(void) {
    if (my_preset) return; scriptableItem_t *p = my_scriptable_alloc(); p->flags = SCRIPTABLE_FLAG_IS_LIST; p->type = strdup("deadbeef.medialib.tfquery");
    my_scriptable_set_prop(p, "name", "Facets"); const char *tfs[] = {"%genre%", "%album artist%", "%album%", "%title%"};
    for (int i = 0; i < 4; i++) {
        scriptableItem_t *c = my_scriptable_alloc(); c->type = strdup("deadbeef.medialib.tfstring");
        my_scriptable_set_prop(c, "name", tfs[i]); my_scriptable_add_child(p, c);
    }
    my_preset = (ddb_scriptable_item_t *)p;
}

static void add_tracks_recursive_multi(const ddb_medialib_item_t *node, int cl, cui_widget_t *cw, ddb_playlist_t *plt, DB_playItem_t **after) {
    if (cw->sel_genre_text && cl == 1) { const char *t = medialib_plugin->tree_item_get_text(node); if (!t || strcmp(t, cw->sel_genre_text)) return; }
    if (cw->sel_artist_text && cl == 2) { const char *t = medialib_plugin->tree_item_get_text(node); if (!t || strcmp(t, cw->sel_artist_text)) return; }
    if (cw->sel_album_text && cl == 3) { const char *t = medialib_plugin->tree_item_get_text(node); if (!t || strcmp(t, cw->sel_album_text)) return; }
    DB_playItem_t *tr = medialib_plugin->tree_item_get_track(node);
    if (tr) { DB_playItem_t *tn = deadbeef_api->pl_item_alloc(); deadbeef_api->pl_item_copy(tn, tr); *after = deadbeef_api->plt_insert_item(plt, *after, tn); deadbeef_api->pl_item_unref(tn); }
    const ddb_medialib_item_t *c = medialib_plugin->tree_item_get_children(node);
    while (c) { add_tracks_recursive_multi(c, cl + 1, cw, plt, after); c = medialib_plugin->tree_item_get_next(c); }
}

static void update_playlist_from_cui(cui_widget_t *cw) {
    if (!cw->cached_tree || !deadbeef_api || !medialib_plugin) return;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr(); if (!plt) return;
    deadbeef_api->pl_lock(); deadbeef_api->plt_clear(plt); DB_playItem_t *after = NULL;
    const ddb_medialib_item_t *root = cw->cached_tree; int rl = 0;
    if (cw->sel_album_node) { root = cw->sel_album_node; rl = 3; }
    else if (cw->sel_artist_node) { root = cw->sel_artist_node; rl = 2; }
    else if (cw->sel_genre_node) { root = cw->sel_genre_node; rl = 1; }
    if (rl == 3) add_tracks_recursive_multi(root, 3, cw, plt, &after);
    else { const ddb_medialib_item_t *c = medialib_plugin->tree_item_get_children(root); while (c) { add_tracks_recursive_multi(c, rl + 1, cw, plt, &after); c = medialib_plugin->tree_item_get_next(c); } }
    deadbeef_api->plt_modified(plt); deadbeef_api->pl_unlock(); deadbeef_api->plt_unref(plt); deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0);
}

static void aggregate_recursive_multi(GtkListStore *store, const ddb_medialib_item_t *node, int cl, int tl, cui_widget_t *cw, GHashTable *seen) {
    if (cl == tl) {
        const char *t = medialib_plugin->tree_item_get_text(node);
        if (t && !g_hash_table_contains(seen, t)) { GtkTreeIter iter; gtk_list_store_append(store, &iter); gtk_list_store_set(store, &iter, 0, t, -1); g_hash_table_add(seen, g_strdup(t)); }
        return;
    }
    if (cw->sel_genre_text && cl == 1) { const char *t = medialib_plugin->tree_item_get_text(node); if (!t || strcmp(t, cw->sel_genre_text)) return; }
    if (cw->sel_artist_text && cl == 2) { const char *t = medialib_plugin->tree_item_get_text(node); if (!t || strcmp(t, cw->sel_artist_text)) return; }
    const ddb_medialib_item_t *c = medialib_plugin->tree_item_get_children(node);
    while (c) { aggregate_recursive_multi(store, c, cl + 1, tl, cw, seen); c = medialib_plugin->tree_item_get_next(c); }
}

static void populate_list_multi(GtkListStore *store, int tl, cui_widget_t *cw, const char *all_t) {
    gtk_list_store_clear(store); if (!cw->cached_tree || !medialib_plugin) return;
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (all_t) { GtkTreeIter iter; gtk_list_store_append(store, &iter); gtk_list_store_set(store, &iter, 0, all_t, -1); g_hash_table_add(seen, g_strdup(all_t)); }
    const ddb_medialib_item_t *root = cw->cached_tree; int rl = 0;
    if (tl > 3 && cw->sel_album_node) { root = cw->sel_album_node; rl = 3; }
    else if (tl > 2 && cw->sel_artist_node) { root = cw->sel_artist_node; rl = 2; }
    else if (tl > 1 && cw->sel_genre_node) { root = cw->sel_genre_node; rl = 1; }
    const ddb_medialib_item_t *c = medialib_plugin->tree_item_get_children(root);
    while (c) { aggregate_recursive_multi(store, c, rl + 1, tl, cw, seen); c = medialib_plugin->tree_item_get_next(c); }
    g_hash_table_destroy(seen);
}

static const ddb_medialib_item_t *find_node_by_text(const ddb_medialib_item_t *p, const char *st) {
    if (!p || !medialib_plugin || !st) return NULL;
    const ddb_medialib_item_t *c = medialib_plugin->tree_item_get_children(p);
    while (c) { const char *t = medialib_plugin->tree_item_get_text(c); if (t && !strcmp(t, st)) return c; c = medialib_plugin->tree_item_get_next(c); }
    return NULL;
}

static void update_tree_data(cui_widget_t *cw);
static gboolean repopulate_ui_idle(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data; cw->idle_id = 0; if (!shutting_down) update_tree_data(cw); return G_SOURCE_REMOVE;
}
static void ml_listener_cb(ddb_mediasource_event_type_t ev, void *ud) {
    cui_widget_t *cw = (cui_widget_t *)ud;
    if (!shutting_down && (ev == DDB_MEDIASOURCE_EVENT_STATE_DID_CHANGE || ev == DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE)) {
        if (cw->idle_id) g_source_remove(cw->idle_id); cw->idle_id = g_idle_add(repopulate_ui_idle, ud);
    }
}

static void update_tree_data(cui_widget_t *cw) {
    if (shutting_down) return; if (!medialib_plugin) medialib_plugin = (DB_mediasource_t *)deadbeef_api->plug_get_for_id("medialib");
    if (!medialib_plugin) return;
    if (!ml_source) {
        ml_source = medialib_plugin->create_source("deadbeef_cui");
        if (ml_source) { medialib_plugin->refresh(ml_source); if (!cw->listener_id) cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw); }
    }
    if (!ml_source) return;
    init_my_preset(); if (!my_preset) return;
    if (cw->cached_tree) { medialib_plugin->free_item_tree(ml_source, cw->cached_tree); cw->cached_tree = NULL; }
    cw->sel_genre_node = cw->sel_artist_node = cw->sel_album_node = NULL;
    cw->cached_tree = medialib_plugin->create_item_tree(ml_source, my_preset, NULL);
    if (!cw->cached_tree) return;
    populate_list_multi(cw->store_genre, 1, cw, ALL_GENRES); populate_list_multi(cw->store_artist, 2, cw, ALL_ARTISTS); populate_list_multi(cw->store_album, 3, cw, ALL_ALBUMS);
    update_playlist_from_cui(cw);
}

static void on_artist_changed(GtkTreeSelection *s, gpointer ud);
static void on_album_changed(GtkTreeSelection *s, gpointer ud);
static void on_genre_changed(GtkTreeSelection *s, gpointer ud) {
    cui_widget_t *cw = (cui_widget_t *)ud; GtkTreeIter iter; GtkTreeModel *m;
    cw->sel_genre_node = NULL; g_free(cw->sel_genre_text); cw->sel_genre_text = NULL;
    if (gtk_tree_selection_get_selected(s, &m, &iter)) {
        gchar *g; gtk_tree_model_get(m, &iter, 0, &g, -1);
        if (g && strcmp(g, ALL_GENRES)) { cw->sel_genre_text = g_strdup(g); cw->sel_genre_node = find_node_by_text(cw->cached_tree, g); }
        g_free(g);
    }
    populate_list_multi(cw->store_artist, 2, cw, ALL_ARTISTS); on_artist_changed(gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_artist)), cw);
}
static void on_artist_changed(GtkTreeSelection *s, gpointer ud) {
    cui_widget_t *cw = (cui_widget_t *)ud; GtkTreeIter iter; GtkTreeModel *m;
    cw->sel_artist_node = NULL; g_free(cw->sel_artist_text); cw->sel_artist_text = NULL;
    if (gtk_tree_selection_get_selected(s, &m, &iter)) {
        gchar *a; gtk_tree_model_get(m, &iter, 0, &a, -1);
        if (a && strcmp(a, ALL_ARTISTS)) { cw->sel_artist_text = g_strdup(a); if (cw->sel_genre_node) cw->sel_artist_node = find_node_by_text(cw->sel_genre_node, a); }
        g_free(a);
    }
    populate_list_multi(cw->store_album, 3, cw, ALL_ALBUMS); on_album_changed(gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_album)), cw);
}
static void on_album_changed(GtkTreeSelection *s, gpointer ud) {
    cui_widget_t *cw = (cui_widget_t *)ud; GtkTreeIter iter; GtkTreeModel *m;
    cw->sel_album_node = NULL; g_free(cw->sel_album_text); cw->sel_album_text = NULL;
    if (gtk_tree_selection_get_selected(s, &m, &iter)) {
        gchar *a; gtk_tree_model_get(m, &iter, 0, &a, -1);
        if (a && strcmp(a, ALL_ALBUMS)) { cw->sel_album_text = g_strdup(a); if (cw->sel_artist_node) cw->sel_album_node = find_node_by_text(cw->sel_artist_node, a); }
        g_free(a);
    }
    update_playlist_from_cui(cw);
}
static void on_row_activated(GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer ud) {
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr(); if (plt) { deadbeef_api->plt_set_cursor(plt, 0, PL_MAIN); deadbeef_api->sendmessage(DB_EV_PLAY_NUM, 0, 0, 0); deadbeef_api->plt_unref(plt); }
}
static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w; if (cw->idle_id) { g_source_remove(cw->idle_id); cw->idle_id = 0; }
    if (!shutting_down && medialib_plugin && ml_source) {
        if (cw->listener_id) { medialib_plugin->remove_listener(ml_source, cw->listener_id); cw->listener_id = 0; }
        if (cw->cached_tree) { medialib_plugin->free_item_tree(ml_source, cw->cached_tree); cw->cached_tree = NULL; }
    }
    g_free(cw->sel_genre_text); g_free(cw->sel_artist_text); g_free(cw->sel_album_text); free(cw);
}
static GtkWidget *create_column(const char *title, GtkListStore **os, GtkWidget **ot) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING); GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)); g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE); GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", 0, NULL); gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);
    GtkWidget *s = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(s), tree); if (os) *os = store; if (ot) *ot = tree; return s;
}
static gboolean initial_populate_idle(gpointer data) { cui_widget_t *cw = (cui_widget_t *)data; update_tree_data(cw); return G_SOURCE_REMOVE; }
static ddb_gtkui_widget_t *cui_create_widget(void) {
    cui_widget_t *cw = calloc(1, sizeof(cui_widget_t)); ddb_gtkui_widget_t *w = &cw->base; w->type = "deadbeef_cui";
    GtkWidget *p1 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL), *p2 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *cg = create_column("Genre", &cw->store_genre, &cw->tree_genre), *ca = create_column("Album Artist", &cw->store_artist, &cw->tree_artist), *cb = create_column("Album", &cw->store_album, &cw->tree_album);
    gtk_paned_pack1(GTK_PANED(p2), ca, TRUE, FALSE); gtk_paned_pack2(GTK_PANED(p2), cb, TRUE, FALSE); gtk_paned_pack1(GTK_PANED(p1), cg, TRUE, FALSE); gtk_paned_pack2(GTK_PANED(p1), p2, TRUE, FALSE);
    gtk_widget_show_all(p1); w->widget = p1; w->destroy = cui_destroy;
    if (gtkui_plugin && gtkui_plugin->w_override_signals) gtkui_plugin->w_override_signals(w->widget, w);
    GtkTreeSelection *sg = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_genre)), *sa = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_artist)), *sb = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_album));
    g_signal_connect(sg, "changed", G_CALLBACK(on_genre_changed), cw); g_signal_connect(sa, "changed", G_CALLBACK(on_artist_changed), cw); g_signal_connect(sb, "changed", G_CALLBACK(on_album_changed), cw);
    g_signal_connect(cw->tree_genre, "row-activated", G_CALLBACK(on_row_activated), cw); g_signal_connect(cw->tree_artist, "row-activated", G_CALLBACK(on_row_activated), cw); g_signal_connect(cw->tree_album, "row-activated", G_CALLBACK(on_row_activated), cw);
    g_idle_add(initial_populate_idle, cw); return w;
}
int cui_start(void) {
    shutting_down = 0; gtkui_plugin = (ddb_gtkui_t *)deadbeef_api->plug_get_for_id(DDB_GTKUI_PLUGIN_ID); if (!gtkui_plugin) return -1;
    medialib_plugin = (DB_mediasource_t *)deadbeef_api->plug_get_for_id("medialib");
    gtkui_plugin->w_reg_widget("Facet Browser (CUI)", 0, cui_create_widget, "deadbeef_cui", NULL); return 0;
}
int cui_stop(void) {
    shutting_down = 1; if (gtkui_plugin) gtkui_plugin->w_unreg_widget("deadbeef_cui");
    if (my_preset) { my_scriptable_free((scriptableItem_t *)my_preset); my_preset = NULL; }
    if (medialib_plugin && ml_source) { medialib_plugin->free_source(ml_source); ml_source = NULL; }
    return 0;
}
static DB_misc_t plugin = {
    .plugin.type = DB_PLUGIN_MISC, .plugin.api_vmajor = 1, .plugin.api_vminor = 0, .plugin.version_major = 0, .plugin.version_minor = 3,
    .plugin.id = "deadbeef_cui", .plugin.name = "Columns UI for DeaDBeeF", .plugin.descr = "A faceted library browser for DeaDBeeF.",
    .plugin.copyright = "MIT License", .plugin.website = "https://github.com/bdkl/deadbeef-cui", .plugin.start = cui_start, .plugin.stop = cui_stop,
};
DB_plugin_t * ddb_misc_cui_GTK3_load(DB_functions_t *api) { deadbeef_api = api; return (DB_plugin_t *)&plugin; }
