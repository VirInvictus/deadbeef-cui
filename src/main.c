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

// --- Internal Scriptable types mirroring medialib.so ---
typedef struct scriptableKeyValue_s {
    struct scriptableKeyValue_s *next;
    char *key;
    char *value;
} scriptableKeyValue_t;

typedef struct scriptableItem_s {
    struct scriptableItem_s *next;
    uint64_t flags;
    scriptableKeyValue_t *properties;
    struct scriptableItem_s *parent;
    struct scriptableItem_s *children;
    struct scriptableItem_s *childrenTail;
    char *type;
    char *configDialog;
    void *overrides;
} scriptableItem_t;

typedef struct scriptableItem_s ddb_scriptable_item_t;

#define SCRIPTABLE_FLAG_IS_LIST (1 << 2)

// Safe creation of scriptable items without external dependencies
static scriptableItem_t *my_scriptable_alloc(void) {
    return calloc(1, sizeof(scriptableItem_t));
}

static void my_scriptable_set_prop(scriptableItem_t *item, const char *key, const char *value) {
    scriptableKeyValue_t *kv = calloc(1, sizeof(scriptableKeyValue_t));
    kv->key = strdup(key);
    kv->value = strdup(value);
    kv->next = item->properties;
    item->properties = kv;
}

static void my_scriptable_add_child(scriptableItem_t *parent, scriptableItem_t *child) {
    if (parent->childrenTail) {
        parent->childrenTail->next = child;
    } else {
        parent->children = child;
    }
    parent->childrenTail = child;
    child->parent = parent;
}

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

    ddb_medialib_item_t *cached_tree;
    const ddb_medialib_item_t *sel_genre_node;
    const ddb_medialib_item_t *sel_artist_node;
    const ddb_medialib_item_t *sel_album_node;
} cui_widget_t;

static ddb_scriptable_item_t *my_preset = NULL;

static void init_my_preset(void) {
    if (my_preset) return;
    
    scriptableItem_t *p = my_scriptable_alloc();
    p->flags = SCRIPTABLE_FLAG_IS_LIST;
    my_scriptable_set_prop(p, "name", "Facets");

    const char *tfs[] = {"%genre%", "%album artist%", "%album%", "%title%"};
    for (int i = 0; i < 4; i++) {
        scriptableItem_t *child = my_scriptable_alloc();
        my_scriptable_set_prop(child, "name", tfs[i]);
        my_scriptable_add_child(p, child);
    }
    my_preset = (ddb_scriptable_item_t *)p;
}

static void add_tracks_recursively(const ddb_medialib_item_t *node, ddb_playlist_t *plt, DB_playItem_t **after) {
    if (!node || !medialib_plugin) return;
    
    DB_playItem_t *track = medialib_plugin->tree_item_get_track(node);
    if (track) {
        DB_playItem_t *track_new = deadbeef_api->pl_item_alloc();
        deadbeef_api->pl_item_copy(track_new, track);
        *after = deadbeef_api->plt_insert_item(plt, *after, track_new);
        deadbeef_api->pl_item_unref(track_new);
    }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        add_tracks_recursively(child, plt, after);
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static void update_playlist_from_node(const ddb_medialib_item_t *node) {
    if (!node || !deadbeef_api || !medialib_plugin) return;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (!plt) return;

    deadbeef_api->pl_lock();
    deadbeef_api->plt_clear(plt);
    DB_playItem_t *after = NULL;
    add_tracks_recursively(node, plt, &after);
    deadbeef_api->plt_modified(plt);
    deadbeef_api->pl_unlock();
    deadbeef_api->plt_unref(plt);
    deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0);
}

static void populate_list(GtkListStore *store, const ddb_medialib_item_t *parent_node) {
    gtk_list_store_clear(store);
    if (!parent_node || !medialib_plugin) return;
    
    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(parent_node);
    GtkTreeIter iter;
    while (child) {
        const char *text = medialib_plugin->tree_item_get_text(child);
        if (text) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, text, -1);
        }
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static void populate_list_aggregated(GtkListStore *store, const ddb_medialib_item_t *parent_node) {
    gtk_list_store_clear(store);
    if (!parent_node || !medialib_plugin) return;

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const ddb_medialib_item_t *lvl1 = medialib_plugin->tree_item_get_children(parent_node);
    while (lvl1) {
        const ddb_medialib_item_t *lvl2 = medialib_plugin->tree_item_get_children(lvl1);
        while (lvl2) {
            const char *text = medialib_plugin->tree_item_get_text(lvl2);
            if (text && !g_hash_table_contains(seen, text)) {
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter, 0, text, -1);
                g_hash_table_add(seen, g_strdup(text));
            }
            lvl2 = medialib_plugin->tree_item_get_next(lvl2);
        }
        lvl1 = medialib_plugin->tree_item_get_next(lvl1);
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

static void update_tree_data(cui_widget_t *cw) {
    if (!medialib_plugin || !ml_source) return;
    
    if (cw->cached_tree) {
        medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
        cw->cached_tree = NULL;
    }
    cw->sel_genre_node = NULL;
    cw->sel_artist_node = NULL;
    cw->sel_album_node = NULL;

    init_my_preset();
    cw->cached_tree = medialib_plugin->create_item_tree(ml_source, my_preset, NULL);
    populate_list(cw->store_genre, cw->cached_tree);
    populate_list_aggregated(cw->store_artist, cw->cached_tree);
    populate_list_aggregated(cw->store_album, cw->cached_tree);
}

static gboolean repopulate_ui_idle(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    if (medialib_plugin && ml_source && medialib_plugin->scanner_state(ml_source) == DDB_MEDIASOURCE_STATE_IDLE) {
        update_tree_data(cw);
    }
    return G_SOURCE_REMOVE;
}

static void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data) {
    if (event == DDB_MEDIASOURCE_EVENT_STATE_DID_CHANGE || event == DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE) {
        g_idle_add(repopulate_ui_idle, user_data);
    }
}

static void on_genre_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    cw->sel_genre_node = NULL;
    cw->sel_artist_node = NULL;
    cw->sel_album_node = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *genre;
        gtk_tree_model_get(model, &iter, 0, &genre, -1);
        if (genre) {
            cw->sel_genre_node = find_node_by_text(cw->cached_tree, genre);
            g_free(genre);
        }
    }
    
    if (cw->sel_genre_node) {
        populate_list(cw->store_artist, cw->sel_genre_node);
        populate_list_aggregated(cw->store_album, cw->sel_genre_node);
        update_playlist_from_node(cw->sel_genre_node);
    } else {
        populate_list_aggregated(cw->store_artist, cw->cached_tree);
        populate_list_aggregated(cw->store_album, cw->cached_tree);
    }
}

static void on_artist_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    cw->sel_artist_node = NULL;
    cw->sel_album_node = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *artist;
        gtk_tree_model_get(model, &iter, 0, &artist, -1);
        if (artist) {
            if (cw->sel_genre_node) {
                cw->sel_artist_node = find_node_by_text(cw->sel_genre_node, artist);
            } else {
                const ddb_medialib_item_t *g = medialib_plugin->tree_item_get_children(cw->cached_tree);
                while (g) {
                    cw->sel_artist_node = find_node_by_text(g, artist);
                    if (cw->sel_artist_node) break;
                    g = medialib_plugin->tree_item_get_next(g);
                }
            }
            g_free(artist);
        }
    }
    
    if (cw->sel_artist_node) {
        populate_list(cw->store_album, cw->sel_artist_node);
        update_playlist_from_node(cw->sel_artist_node);
    } else {
        if (cw->sel_genre_node) populate_list_aggregated(cw->store_album, cw->sel_genre_node);
        else populate_list_aggregated(cw->store_album, cw->cached_tree);
    }
}

static void on_album_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    cw->sel_album_node = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *album;
        gtk_tree_model_get(model, &iter, 0, &album, -1);
        if (album) {
            if (cw->sel_artist_node) {
                cw->sel_album_node = find_node_by_text(cw->sel_artist_node, album);
            } else if (cw->sel_genre_node) {
                const ddb_medialib_item_t *art = medialib_plugin->tree_item_get_children(cw->sel_genre_node);
                while (art) {
                    cw->sel_album_node = find_node_by_text(art, album);
                    if (cw->sel_album_node) break;
                    art = medialib_plugin->tree_item_get_next(art);
                }
            } else {
                const ddb_medialib_item_t *g = medialib_plugin->tree_item_get_children(cw->cached_tree);
                while (g) {
                    const ddb_medialib_item_t *art = medialib_plugin->tree_item_get_children(g);
                    while (art) {
                        cw->sel_album_node = find_node_by_text(art, album);
                        if (cw->sel_album_node) break;
                        art = medialib_plugin->tree_item_get_next(art);
                    }
                    if (cw->sel_album_node) break;
                    g = medialib_plugin->tree_item_get_next(g);
                }
            }
            g_free(album);
            if (cw->sel_album_node) {
                update_playlist_from_node(cw->sel_album_node);
            }
        }
    }
}

static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (plt) {
        deadbeef_api->plt_set_cursor(plt, 0, PL_MAIN);
        deadbeef_api->sendmessage(DB_EV_PLAY_NUM, 0, 0, 0);
        deadbeef_api->plt_unref(plt);
    }
}

// Widget destruction callback
static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;
    if (medialib_plugin && ml_source && cw->listener_id) {
        medialib_plugin->remove_listener(ml_source, cw->listener_id);
        cw->listener_id = 0;
    }
    if (medialib_plugin && ml_source && cw->cached_tree) {
        medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
        cw->cached_tree = NULL;
    }
}

// Helper to create a single list column
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

// Widget creation function
static ddb_gtkui_widget_t *cui_create_widget(void) {
    cui_widget_t *cw = malloc(sizeof(cui_widget_t));
    memset(cw, 0, sizeof(cui_widget_t));

    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "cui";
    
    // Create the triple-pane facet view
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

    // This is required for design mode
    if (gtkui_plugin && gtkui_plugin->w_override_signals) {
        gtkui_plugin->w_override_signals(w->widget, w);
    }

    if (!ml_source && medialib_plugin && medialib_plugin->create_source) {
        ml_source = medialib_plugin->create_source("deadbeef");
        if (ml_source) {
            medialib_plugin->refresh(ml_source);
        }
    }

    if (medialib_plugin && ml_source) {
        cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw);
    }

    // Attach signals
    GtkTreeSelection *sel_genre = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_genre));
    g_signal_connect(sel_genre, "changed", G_CALLBACK(on_genre_changed), cw);

    GtkTreeSelection *sel_artist = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_artist));
    g_signal_connect(sel_artist, "changed", G_CALLBACK(on_artist_changed), cw);

    GtkTreeSelection *sel_album = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->tree_album));
    g_signal_connect(sel_album, "changed", G_CALLBACK(on_album_changed), cw);

    g_signal_connect(cw->tree_genre, "row-activated", G_CALLBACK(on_row_activated), cw);
    g_signal_connect(cw->tree_artist, "row-activated", G_CALLBACK(on_row_activated), cw);
    g_signal_connect(cw->tree_album, "row-activated", G_CALLBACK(on_row_activated), cw);

    // Populate initial data
    if (medialib_plugin && ml_source && medialib_plugin->scanner_state(ml_source) == DDB_MEDIASOURCE_STATE_IDLE) {
        update_tree_data(cw);
    }

    return w;
}

static int cui_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
    return 0;
}

int cui_start(void) {
    gtkui_plugin = (ddb_gtkui_t *)deadbeef_api->plug_get_for_id(DDB_GTKUI_PLUGIN_ID);
    if (!gtkui_plugin) {
        fprintf(stderr, "deadbeef-cui: GTK UI plugin not found!\n");
        return -1;
    }

    medialib_plugin = (DB_mediasource_t *)deadbeef_api->plug_get_for_id("medialib");
    if (!medialib_plugin) {
        fprintf(stderr, "deadbeef-cui: medialib plugin not found or unsupported!\n");
    }

    gtkui_plugin->w_reg_widget("Facet Browser (CUI)", 0, cui_create_widget, "cui", NULL);
    fprintf(stderr, "deadbeef-cui: Facet Browser registered successfully.\n");

    return 0;
}

int cui_stop(void) {
    if (gtkui_plugin) {
        gtkui_plugin->w_unreg_widget("cui");
    }
    if (medialib_plugin && ml_source) {
        medialib_plugin->free_source(ml_source);
        ml_source = NULL;
    }
    return 0;
}

static DB_misc_t plugin = {
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 0,
    .plugin.version_minor = 2,
    .plugin.id = "cui",
    .plugin.name = "Columns UI for DeaDBeeF",
    .plugin.descr = "A faceted library browser for DeaDBeeF.",
    .plugin.copyright = "MIT License",
    .plugin.website = "https://github.com/bdkl/deadbeef-cui",
    .plugin.start = cui_start,
    .plugin.stop = cui_stop,
    .plugin.message = cui_message,
};

DB_plugin_t * ddb_misc_cui_GTK3_load(DB_functions_t *api) {
    deadbeef_api = api;
    return (DB_plugin_t *)&plugin;
}
