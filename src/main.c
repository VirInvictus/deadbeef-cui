#include <deadbeef/deadbeef.h>
#include <gtk/gtk.h>
#include <deadbeef/gtkui_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static DB_functions_t *deadbeef_api;
static ddb_gtkui_t *gtkui_plugin;
static DB_mediasource_t *medialib_plugin;
static ddb_mediasource_source_t *ml_source;
static int shutting_down = 0;

// --- Medialib Scriptable API Function Pointers ---
static ddb_scriptable_item_t* (*scriptableItemAlloc)(void) = NULL;
static void (*scriptableItemFree)(ddb_scriptable_item_t *item) = NULL;
static void (*scriptableItemSetPropertyValueForKey)(ddb_scriptable_item_t *item, const char *key, const char *value) = NULL;
static void (*scriptableItemAddSubItem)(ddb_scriptable_item_t *parent, ddb_scriptable_item_t *child) = NULL;
static void (*scriptableItemFlagsAdd)(ddb_scriptable_item_t *item, uint64_t flags) = NULL;

#define SCRIPTABLE_FLAG_IS_LIST (1 << 2)

#define ALL_GENRES "[ All Genres ]"
#define ALL_ARTISTS "[ All Artists ]"
#define ALL_ALBUMS "[ All Albums ]"

// --- Widget structures ---
typedef struct {
    ddb_gtkui_widget_t base;
    GtkListStore *store_genre;
    GtkListStore *store_artist;
    GtkListStore *store_album;
    
    GtkWidget *tree_genre;
    GtkWidget *tree_artist;
    GtkWidget *tree_album;
    int listener_id;
    guint idle_id;

    ddb_medialib_item_t *cached_tree;
    const ddb_medialib_item_t *sel_genre_node;
    const ddb_medialib_item_t *sel_artist_node;
    const ddb_medialib_item_t *sel_album_node;

    char *sel_genre_text;
    char *sel_artist_text;
    char *sel_album_text;
} cui_widget_t;

static ddb_scriptable_item_t *my_preset = NULL;

static void resolve_medialib_api(void) {
    if (scriptableItemAlloc) return;

    // We try to find the symbols in the current process space first
    void *handle = RTLD_DEFAULT;
    
    scriptableItemAlloc = dlsym(handle, "scriptableItemAlloc");
    if (!scriptableItemAlloc) {
        // If not found, try to explicitly load medialib.so
        void *ml_handle = dlopen("medialib.so", RTLD_NOW | RTLD_GLOBAL);
        if (!ml_handle) {
            ml_handle = dlopen("/usr/lib64/deadbeef/medialib.so", RTLD_NOW | RTLD_GLOBAL);
        }
        if (ml_handle) {
            handle = ml_handle;
            scriptableItemAlloc = dlsym(handle, "scriptableItemAlloc");
        }
    }

    if (scriptableItemAlloc) {
        scriptableItemFree = dlsym(handle, "scriptableItemFree");
        scriptableItemSetPropertyValueForKey = dlsym(handle, "scriptableItemSetPropertyValueForKey");
        scriptableItemAddSubItem = dlsym(handle, "scriptableItemAddSubItem");
        scriptableItemFlagsAdd = dlsym(handle, "scriptableItemFlagsAdd");
        fprintf(stderr, "deadbeef-cui: [DEBUG] ScriptableItem API resolved.\n");
    } else {
        fprintf(stderr, "deadbeef-cui: [ERROR] Could not resolve ScriptableItem API.\n");
    }
}

static void init_my_preset(void) {
    if (my_preset) return;
    
    resolve_medialib_api();
    if (!scriptableItemAlloc) return;
    
    my_preset = scriptableItemAlloc();
    if (!my_preset) return;

    if (scriptableItemFlagsAdd) scriptableItemFlagsAdd(my_preset, SCRIPTABLE_FLAG_IS_LIST);
    if (scriptableItemSetPropertyValueForKey) scriptableItemSetPropertyValueForKey(my_preset, "name", "Facets");

    const char *tfs[] = {"%genre%", "%album artist%", "%album%", "%title%"};
    for (int i = 0; i < 4; i++) {
        ddb_scriptable_item_t *child = scriptableItemAlloc();
        if (child) {
            if (scriptableItemSetPropertyValueForKey) scriptableItemSetPropertyValueForKey(child, "name", tfs[i]);
            if (scriptableItemAddSubItem) scriptableItemAddSubItem(my_preset, child);
        }
    }
}

static void add_tracks_recursive_multi(const ddb_medialib_item_t *node, int current_level, cui_widget_t *cw, ddb_playlist_t *plt, DB_playItem_t **after) {
    if (cw->sel_genre_text && current_level == 1) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (!text || strcmp(text, cw->sel_genre_text)) return;
    }
    if (cw->sel_artist_text && current_level == 2) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (!text || strcmp(text, cw->sel_artist_text)) return;
    }
    if (cw->sel_album_text && current_level == 3) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (!text || strcmp(text, cw->sel_album_text)) return;
    }

    DB_playItem_t *track = medialib_plugin->tree_item_get_track(node);
    if (track) {
        DB_playItem_t *track_new = deadbeef_api->pl_item_alloc();
        deadbeef_api->pl_item_copy(track_new, track);
        *after = deadbeef_api->plt_insert_item(plt, *after, track_new);
        deadbeef_api->pl_item_unref(track_new);
    }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        add_tracks_recursive_multi(child, current_level + 1, cw, plt, after);
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static void update_playlist_from_cui(cui_widget_t *cw) {
    if (!cw->cached_tree || !deadbeef_api || !medialib_plugin) return;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (!plt) return;

    deadbeef_api->pl_lock();
    deadbeef_api->plt_clear(plt);
    DB_playItem_t *after = NULL;
    
    const ddb_medialib_item_t *root_node = cw->cached_tree;
    int root_level = 0;

    if (cw->sel_album_node) { root_node = cw->sel_album_node; root_level = 3; }
    else if (cw->sel_artist_node) { root_node = cw->sel_artist_node; root_level = 2; }
    else if (cw->sel_genre_node) { root_node = cw->sel_genre_node; root_level = 1; }

    if (root_level == 3) {
        add_tracks_recursive_multi(root_node, 3, cw, plt, &after);
    } else {
        const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(root_node);
        while (child) {
            add_tracks_recursive_multi(child, root_level + 1, cw, plt, &after);
            child = medialib_plugin->tree_item_get_next(child);
        }
    }

    deadbeef_api->plt_modified(plt);
    deadbeef_api->pl_unlock();
    deadbeef_api->plt_unref(plt);
    deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0);
}

static void aggregate_recursive_multi(GtkListStore *store, const ddb_medialib_item_t *node, int current_level, int target_level, cui_widget_t *cw, GHashTable *seen) {
    if (current_level == target_level) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (text && !g_hash_table_contains(seen, text)) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, text, -1);
            g_hash_table_add(seen, g_strdup(text));
        }
        return;
    }

    if (cw->sel_genre_text && current_level == 1) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (!text || strcmp(text, cw->sel_genre_text)) return;
    }
    if (cw->sel_artist_text && current_level == 2) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (!text || strcmp(text, cw->sel_artist_text)) return;
    }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        aggregate_recursive_multi(store, child, current_level + 1, target_level, cw, seen);
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static void populate_list_multi(GtkListStore *store, int target_level, cui_widget_t *cw, const char *all_text) {
    gtk_list_store_clear(store);
    if (!cw->cached_tree || !medialib_plugin) return;

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (all_text) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, all_text, -1);
        g_hash_table_add(seen, g_strdup(all_text));
    }
    
    const ddb_medialib_item_t *root_node = cw->cached_tree;
    int root_level = 0;

    if (target_level > 3 && cw->sel_album_node) { root_node = cw->sel_album_node; root_level = 3; }
    else if (target_level > 2 && cw->sel_artist_node) { root_node = cw->sel_artist_node; root_level = 2; }
    else if (target_level > 1 && cw->sel_genre_node) { root_node = cw->sel_genre_node; root_level = 1; }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(root_node);
    while (child) {
        aggregate_recursive_multi(store, child, root_level + 1, target_level, cw, seen);
        child = medialib_plugin->tree_item_get_next(child);
    }
    g_hash_table_destroy(seen);
}

static const ddb_medialib_item_t *find_node_by_text(const ddb_medialib_item_t *parent_node, const char *search_text) {
    if (!parent_node || !medialib_plugin || !search_text) return NULL;
    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(parent_node);
    while (child) {
        const char *text = medialib_plugin->tree_item_get_text(child);
        if (text && !strcmp(text, search_text)) {
            return child;
        }
        child = medialib_plugin->tree_item_get_next(child);
    }
    return NULL;
}

static void update_tree_data(cui_widget_t *cw);

static gboolean repopulate_ui_idle(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    cw->idle_id = 0;
    if (!shutting_down) {
        update_tree_data(cw);
    }
    return G_SOURCE_REMOVE;
}

static void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data) {
    cui_widget_t *cw = (cui_widget_t *)user_data;
    if (!shutting_down && (event == DDB_MEDIASOURCE_EVENT_STATE_DID_CHANGE || event == DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE)) {
        if (cw->idle_id) g_source_remove(cw->idle_id);
        cw->idle_id = g_idle_add(repopulate_ui_idle, user_data);
    }
}

static void update_tree_data(cui_widget_t *cw) {
    if (shutting_down) return;
    
    if (!medialib_plugin) {
        medialib_plugin = (DB_mediasource_t *)deadbeef_api->plug_get_for_id("medialib");
    }
    if (!medialib_plugin) return;

    if (!ml_source) {
        ml_source = medialib_plugin->create_source("deadbeef");
        if (ml_source) {
            medialib_plugin->refresh(ml_source);
            if (!cw->listener_id) {
                cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw);
            }
        }
    }
    if (!ml_source) return;

    init_my_preset();
    if (!my_preset) return;

    if (cw->cached_tree) {
        medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
        cw->cached_tree = NULL;
    }
    cw->sel_genre_node = NULL;
    cw->sel_artist_node = NULL;
    cw->sel_album_node = NULL;

    cw->cached_tree = medialib_plugin->create_item_tree(ml_source, my_preset, NULL);
    if (!cw->cached_tree) return;
    
    populate_list_multi(cw->store_genre, 1, cw, ALL_GENRES);
    populate_list_multi(cw->store_artist, 2, cw, ALL_ARTISTS);
    populate_list_multi(cw->store_album, 3, cw, ALL_ALBUMS);

    update_playlist_from_cui(cw);
}

static void on_artist_changed(GtkTreeSelection *selection, gpointer data);
static void on_album_changed(GtkTreeSelection *selection, gpointer data);

static void on_genre_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    cw->sel_genre_node = NULL;
    g_free(cw->sel_genre_text);
    cw->sel_genre_text = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *genre;
        gtk_tree_model_get(model, &iter, 0, &genre, -1);
        if (genre && strcmp(genre, ALL_GENRES)) {
            cw->sel_genre_text = g_strdup(genre);
            cw->sel_genre_node = find_node_by_text(cw->cached_tree, genre);
        }
        g_free(genre);
    }
    
    populate_list_multi(cw->store_artist, 2, cw, ALL_ARTISTS);
    on_artist_changed(gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_artist)), cw);
}

static void on_artist_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    cw->sel_artist_node = NULL;
    g_free(cw->sel_artist_text);
    cw->sel_artist_text = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *artist;
        gtk_tree_model_get(model, &iter, 0, &artist, -1);
        if (artist && strcmp(artist, ALL_ARTISTS)) {
            cw->sel_artist_text = g_strdup(artist);
            if (cw->sel_genre_node) {
                cw->sel_artist_node = find_node_by_text(cw->sel_genre_node, artist);
            }
        }
        g_free(artist);
    }

    populate_list_multi(cw->store_album, 3, cw, ALL_ALBUMS);
    on_album_changed(gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_album)), cw);
}

static void on_album_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    cw->sel_album_node = NULL;
    g_free(cw->sel_album_text);
    cw->sel_album_text = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *album;
        gtk_tree_model_get(model, &iter, 0, &album, -1);
        if (album && strcmp(album, ALL_ALBUMS)) {
            cw->sel_album_text = g_strdup(album);
            if (cw->sel_artist_node) {
                cw->sel_album_node = find_node_by_text(cw->sel_artist_node, album);
            }
        }
        g_free(album);
    }

    update_playlist_from_cui(cw);
}

static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (plt) {
        deadbeef_api->plt_set_cursor(plt, 0, PL_MAIN);
        deadbeef_api->sendmessage(DB_EV_PLAY_NUM, 0, 0, 0);
        deadbeef_api->plt_unref(plt);
    }
}

static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;
    if (cw->idle_id) {
        g_source_remove(cw->idle_id);
        cw->idle_id = 0;
    }

    if (!shutting_down && medialib_plugin && ml_source) {
        if (cw->listener_id) {
            medialib_plugin->remove_listener(ml_source, cw->listener_id);
            cw->listener_id = 0;
        }
        if (cw->cached_tree) {
            medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
            cw->cached_tree = NULL;
        }
    }
    
    g_free(cw->sel_genre_text);
    g_free(cw->sel_artist_text);
    g_free(cw->sel_album_text);
    free(cw);
}

static GtkWidget *create_column(const char *title, GtkListStore **out_store, GtkWidget **out_tree) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    if (out_store) *out_store = store;
    if (out_tree) *out_tree = tree;
    return scroll;
}

static gboolean initial_populate_idle(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    update_tree_data(cw);
    return G_SOURCE_REMOVE;
}

static ddb_gtkui_widget_t *cui_create_widget(void) {
    cui_widget_t *cw = malloc(sizeof(cui_widget_t));
    memset(cw, 0, sizeof(cui_widget_t));
    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "deadbeef_cui";
    GtkWidget *pane1 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *pane2 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *col_genre = create_column("Genre", &cw->store_genre, &cw->tree_genre);
    GtkWidget *col_artist = create_column("Album Artist", &cw->store_artist, &cw->tree_artist);
    GtkWidget *col_album = create_column("Album", &cw->store_album, &cw->tree_album);
    gtk_paned_pack1(GTK_PANED(pane2), col_artist, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(pane2), col_album, TRUE, FALSE);
    gtk_paned_pack1(GTK_PANED(pane1), col_genre, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(pane1), pane2, TRUE, FALSE);
    gtk_widget_show_all(pane1);
    w->widget = pane1;
    w->destroy = cui_destroy;
    if (gtkui_plugin && gtkui_plugin->w_override_signals) {
        gtkui_plugin->w_override_signals(w->widget, w);
    }
    GtkTreeSelection *sel_genre = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_genre));
    g_signal_connect(sel_genre, "changed", G_CALLBACK(on_genre_changed), cw);
    GtkTreeSelection *sel_artist = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_artist));
    g_signal_connect(sel_artist, "changed", G_CALLBACK(on_artist_changed), cw);
    GtkTreeSelection *sel_album = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_album));
    g_signal_connect(sel_album, "changed", G_CALLBACK(on_album_changed), cw);
    g_signal_connect(cw->tree_genre, "row-activated", G_CALLBACK(on_row_activated), cw);
    g_signal_connect(cw->tree_artist, "row-activated", G_CALLBACK(on_row_activated), cw);
    g_signal_connect(cw->tree_album, "row-activated", G_CALLBACK(on_row_activated), cw);
    
    // Defer initial population to ensure other plugins are ready
    g_idle_add(initial_populate_idle, cw);
    return w;
}

int cui_start(void) {
    shutting_down = 0;
    gtkui_plugin = (ddb_gtkui_t *)deadbeef_api->plug_get_for_id(DDB_GTKUI_PLUGIN_ID);
    if (!gtkui_plugin) return -1;
    medialib_plugin = (DB_mediasource_t *)deadbeef_api->plug_get_for_id("medialib");
    gtkui_plugin->w_reg_widget("CUI Facets v0.3.6", 0, cui_create_widget, "deadbeef_cui", NULL);
    return 0;
}

int cui_stop(void) {
    shutting_down = 1;
    if (gtkui_plugin) {
        gtkui_plugin->w_unreg_widget("deadbeef_cui");
    }
    ml_source = NULL;
    my_preset = NULL;
    return 0;
}

static DB_misc_t plugin = {
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 0,
    .plugin.version_minor = 3,
    .plugin.id = "deadbeef_cui",
    .plugin.name = "Columns UI for DeaDBeeF",
    .plugin.descr = "A faceted library browser for DeaDBeeF.",
    .plugin.copyright = "MIT License",
    .plugin.website = "https://github.com/bdkl/deadbeef-cui",
    .plugin.start = cui_start,
    .plugin.stop = cui_stop,
};

DB_plugin_t * ddb_misc_cui_GTK3_load(DB_functions_t *api) {
    deadbeef_api = api;
    return (DB_plugin_t *)&plugin;
}
