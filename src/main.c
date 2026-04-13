#include <deadbeef/deadbeef.h>
#include <gtk/gtk.h>
#include <deadbeef/gtkui_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static DB_functions_t *deadbeef_api;
static ddb_gtkui_t *gtkui_plugin;
static DB_mediasource_t *medialib_plugin;
static ddb_mediasource_source_t *ml_source;
static int shutting_down = 0;
static int owns_ml_source = 0;

// --- Internal Scriptable types mirroring medialib.so ---
// Layout verified against DeaDBeeF source (shared/scriptable/scriptable.c)
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

#define SCRIPTABLE_FLAG_IS_LIST (1 << 2)

static scriptableItem_t *my_scriptable_alloc(void) {
    return calloc(1, sizeof(scriptableItem_t));
}

static void my_scriptable_free(scriptableItem_t *item) {
    if (!item) return;

    scriptableItem_t *child = item->children;
    while (child) {
        scriptableItem_t *next = child->next;
        my_scriptable_free(child);
        child = next;
    }

    scriptableKeyValue_t *kv = item->properties;
    while (kv) {
        scriptableKeyValue_t *next = kv->next;
        free(kv->key);
        free(kv->value);
        free(kv);
        kv = next;
    }

    free(item->type);
    free(item->configDialog);
    free(item);
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

#define ALL_GENRES "[ All Genres ]"
#define ALL_ARTISTS "[ All Artists ]"
#define ALL_ALBUMS "[ All Albums ]"

#define CUI_SOURCE_PATH "cui"

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
    GHashTable *track_counts_cache;
    const ddb_medialib_item_t *sel_genre_node;
    const ddb_medialib_item_t *sel_artist_node;
    const ddb_medialib_item_t *sel_album_node;

    char *sel_genre_text;
    char *sel_artist_text;
    char *sel_album_text;
} cui_widget_t;

static ddb_scriptable_item_t *my_preset = NULL;
static GList *all_cui_widgets = NULL;

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

// Copy the main medialib folder config to our source's config namespace
static void sync_source_config(void) {
    char paths[4096];
    deadbeef_api->conf_get_str("medialib.deadbeef.paths", "", paths, sizeof(paths));
    if (paths[0]) {
        deadbeef_api->conf_set_str("medialib." CUI_SOURCE_PATH ".paths", paths);
    }
    deadbeef_api->conf_set_str("medialib." CUI_SOURCE_PATH ".enabled", "1");
}

// --- Track counting ---

static int count_tracks_recursive(const ddb_medialib_item_t *node, GHashTable *cache) {
    if (cache) {
        gpointer cached = g_hash_table_lookup(cache, node);
        if (cached) {
            return GPOINTER_TO_INT(cached) - 1;
        }
    }

    int count = 0;
    if (medialib_plugin->tree_item_get_track(node)) {
        count = 1;
    }
    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        count += count_tracks_recursive(child, cache);
        child = medialib_plugin->tree_item_get_next(child);
    }

    if (cache) {
        g_hash_table_insert(cache, (gpointer)node, GINT_TO_POINTER(count + 1));
    }
    return count;
}

static int sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    int sort_col = GPOINTER_TO_INT(user_data);
    gint current_sort_col;
    GtkSortType order;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model), &current_sort_col, &order);

    gchar *name_a, *name_b;
    int count_a, count_b;
    gtk_tree_model_get(model, a, 0, &name_a, 1, &count_a, -1);
    gtk_tree_model_get(model, b, 0, &name_b, 1, &count_b, -1);

    int result = 0;
    if (name_a && name_b) {
        int is_all_a = (name_a[0] == '[');
        int is_all_b = (name_b[0] == '[');

        if (is_all_a && !is_all_b) {
            // [All] always comes first. GTK negates results in descending mode,
            // so we return 1 in descending mode to counteract that.
            result = (order == GTK_SORT_ASCENDING) ? -1 : 1;
        } else if (!is_all_a && is_all_b) {
            result = (order == GTK_SORT_ASCENDING) ? 1 : -1;
        } else {
            if (sort_col == 1) { // Count
                result = count_b - count_a; // Descending by default for counts
                if (result == 0) result = g_utf8_collate(name_a, name_b);
            } else { // Name
                result = g_utf8_collate(name_a, name_b);
            }
        }
    }

    g_free(name_a);
    g_free(name_b);
    return result;
}

static void add_tracks_recursive_multi(const ddb_medialib_item_t *node, int current_level,
                                        cui_widget_t *cw, ddb_playlist_t *plt, DB_playItem_t **after) {
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

// --- Aggregation / population ---

static void aggregate_recursive_multi(const ddb_medialib_item_t *node,
                                       int current_level, int target_level,
                                       cui_widget_t *cw, GHashTable *seen) {
    if (current_level == target_level) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (text) {
            int tracks = count_tracks_recursive(node, cw->track_counts_cache);
            int *count_ptr = g_hash_table_lookup(seen, text);
            if (count_ptr) {
                *count_ptr += tracks;
            } else {
                count_ptr = g_new(int, 1);
                *count_ptr = tracks;
                g_hash_table_insert(seen, g_strdup(text), count_ptr);
            }
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
        aggregate_recursive_multi(child, current_level + 1, target_level, cw, seen);
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static void populate_list_multi(GtkListStore *store, int target_level, cui_widget_t *cw, const char *all_text) {
    gtk_list_store_clear(store);
    if (!cw->cached_tree || !medialib_plugin) return;

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    const ddb_medialib_item_t *root_node = cw->cached_tree;
    int root_level = 0;

    // Use current selection as root if possible, to limit search space
    if (target_level == 2 && cw->sel_genre_node) { root_node = cw->sel_genre_node; root_level = 1; }
    else if (target_level == 3 && cw->sel_artist_node) { root_node = cw->sel_artist_node; root_level = 2; }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(root_node);
    while (child) {
        aggregate_recursive_multi(child, root_level + 1, target_level, cw, seen);
        child = medialib_plugin->tree_item_get_next(child);
    }

    int total_count = 0;
    GList *keys = g_hash_table_get_keys(seen);
    for (GList *l = keys; l; l = l->next) {
        char *text = (char *)l->data;
        int *count_ptr = g_hash_table_lookup(seen, text);
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, text, 1, *count_ptr, -1);
        total_count += *count_ptr;
    }
    g_list_free(keys);

    if (all_text) {
        GtkTreeIter iter;
        gtk_list_store_prepend(store, &iter);
        gtk_list_store_set(store, &iter, 0, all_text, 1, total_count, -1);
    }

    g_hash_table_destroy(seen);
}

// --- Tree lookup ---

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

// --- Tree data refresh ---

static void update_tree_data(cui_widget_t *cw) {
    if (shutting_down || !medialib_plugin || !ml_source) return;

    if (cw->cached_tree) {
        medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
        cw->cached_tree = NULL;
    }
    if (cw->track_counts_cache) {
        g_hash_table_destroy(cw->track_counts_cache);
        cw->track_counts_cache = NULL;
    }
    
    cw->sel_genre_node = NULL;
    cw->sel_artist_node = NULL;
    cw->sel_album_node = NULL;

    g_free(cw->sel_genre_text); cw->sel_genre_text = NULL;
    g_free(cw->sel_artist_text); cw->sel_artist_text = NULL;
    g_free(cw->sel_album_text); cw->sel_album_text = NULL;

    init_my_preset();
    if (!my_preset) return;

    cw->cached_tree = medialib_plugin->create_item_tree(ml_source, my_preset, NULL);
    if (!cw->cached_tree) return;
    
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    populate_list_multi(cw->store_genre, 1, cw, ALL_GENRES);
    populate_list_multi(cw->store_artist, 2, cw, ALL_ARTISTS);
    populate_list_multi(cw->store_album, 3, cw, ALL_ALBUMS);

    update_playlist_from_cui(cw);
}

static gboolean repopulate_ui_idle(gpointer data) {
    if (shutting_down) return G_SOURCE_REMOVE;
    cui_widget_t *cw = (cui_widget_t *)data;
    if (g_list_find(all_cui_widgets, cw)) {
        if (medialib_plugin && ml_source) {
            update_tree_data(cw);
        }
    }
    return G_SOURCE_REMOVE;
}

static void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data) {
    if (shutting_down) return;
    if (event == DDB_MEDIASOURCE_EVENT_STATE_DID_CHANGE || event == DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE) {
        g_idle_add(repopulate_ui_idle, user_data);
    }
}

// --- Selection handlers ---

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
    (void)tree_view;
    (void)path;
    (void)column;
    (void)data;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (plt) {
        deadbeef_api->plt_set_cursor(plt, 0, PL_MAIN);
        deadbeef_api->sendmessage(DB_EV_PLAY_NUM, 0, 0, 0);
        deadbeef_api->plt_unref(plt);
    }
}

// --- Widget lifecycle ---

static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;

    all_cui_widgets = g_list_remove(all_cui_widgets, cw);

    if (medialib_plugin && ml_source) {
        if (cw->listener_id) {
            medialib_plugin->remove_listener(ml_source, cw->listener_id);
            cw->listener_id = 0;
        }
        if (cw->cached_tree) {
            medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
            cw->cached_tree = NULL;
        }
        if (cw->track_counts_cache) {
            g_hash_table_destroy(cw->track_counts_cache);
            cw->track_counts_cache = NULL;
        }
    }

    g_free(cw->sel_genre_text); cw->sel_genre_text = NULL;
    g_free(cw->sel_artist_text); cw->sel_artist_text = NULL;
    g_free(cw->sel_album_text); cw->sel_album_text = NULL;
}

static GtkWidget *create_column(const char *title, GtkListStore **out_store, GtkWidget **out_tree) {
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 0, sort_func, GINT_TO_POINTER(0), NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 1, sort_func, GINT_TO_POINTER(1), NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", 0, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_min_width(column, 50);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sort_column_id(column, 0);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    GtkCellRenderer *count_renderer = gtk_cell_renderer_text_new();
    g_object_set(count_renderer, "xalign", 1.0, "xpad", 6, NULL);
    GtkTreeViewColumn *count_column = gtk_tree_view_column_new_with_attributes("Count", count_renderer, "text", 1, NULL);
    gtk_tree_view_column_set_sizing(count_column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_column_set_sort_column_id(count_column, 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), count_column);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    // Disable horizontal scrolling to force ellipsizing of the name column
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    if (out_store) *out_store = store;
    if (out_tree) *out_tree = tree;

    return scroll;
}

static ddb_gtkui_widget_t *cui_create_widget(void) {
    cui_widget_t *cw = calloc(1, sizeof(cui_widget_t));
    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "cui";

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

    if (!ml_source && medialib_plugin && medialib_plugin->create_source) {
        sync_source_config();
        ml_source = medialib_plugin->create_source(CUI_SOURCE_PATH);
        if (ml_source) {
            medialib_plugin->refresh(ml_source);
        }
    }

    if (medialib_plugin && ml_source) {
        cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw);
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

    all_cui_widgets = g_list_append(all_cui_widgets, cw);
    update_tree_data(cw);

    return w;
}

// --- Plugin entry points ---

static int cui_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
    (void)ctx;
    (void)p1;
    (void)p2;
    if (id == DB_EV_TERMINATE) {
        shutting_down = 1;
    }
    return 0;
}

int cui_start(void) {
    shutting_down = 0;

    gtkui_plugin = (ddb_gtkui_t *)deadbeef_api->plug_get_for_id(DDB_GTKUI_PLUGIN_ID);
    if (!gtkui_plugin) {
        fprintf(stderr, "deadbeef-cui: GTK UI plugin not found!\n");
        return -1;
    }

    medialib_plugin = (DB_mediasource_t *)deadbeef_api->plug_get_for_id("medialib");
    if (!medialib_plugin) {
        fprintf(stderr, "deadbeef-cui: medialib plugin not found or unsupported!\n");
    }

    gtkui_plugin->w_reg_widget("Facet Browser (CUI) v0.7.2", 0, cui_create_widget, "cui", NULL);
    fprintf(stderr, "deadbeef-cui: Facet Browser v0.7.2 registered successfully.\n");

    return 0;
}

int cui_stop(void) {
    fprintf(stderr, "cui_stop called\n");
    fflush(stderr);
    shutting_down = 1;

    if (gtkui_plugin) {
        gtkui_plugin->w_unreg_widget("cui");
    }
    if (owns_ml_source && medialib_plugin && ml_source) {
        medialib_plugin->free_source(ml_source);
        ml_source = NULL;
    }
    if (my_preset) {
        my_scriptable_free((scriptableItem_t *)my_preset);
        my_preset = NULL;
    }

    return 0;
}

static DB_misc_t plugin = {
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 0,
    .plugin.version_minor = 7,
    .plugin.id = "cui",
    .plugin.name = "Columns UI for DeaDBeeF",
    .plugin.descr = "A faceted library browser for DeaDBeeF.",
    .plugin.copyright = "MIT License",
    .plugin.website = "https://github.com/bdkl/deadbeef-cui",
    .plugin.start = cui_start,
    .plugin.stop = cui_stop,
    .plugin.message = cui_message,
};

DB_plugin_t *ddb_misc_cui_GTK3_load(DB_functions_t *api) {
    deadbeef_api = api;
    return (DB_plugin_t *)&plugin;
}