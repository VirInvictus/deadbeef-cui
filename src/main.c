#include <deadbeef/deadbeef.h>
#include <gtk/gtk.h>
#include <deadbeef/gtkui_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <dlfcn.h>

#if GTK_MAJOR_VERSION >= 4
#define gtk_widget_show_all(w) gtk_widget_set_visible(w, TRUE)
#define gtk_widget_hide(w) gtk_widget_set_visible(w, FALSE)
#define gtk_widget_set_no_show_all(w, no_show) 
#define gtk_box_pack_start(box, child, expand, fill, padding) gtk_box_append(GTK_BOX(box), child)
#define gtk_scrolled_window_new(h, v) gtk_scrolled_window_new()
#define gtk_container_add(container, widget) \
    do { \
        if (GTK_IS_SCROLLED_WINDOW(container)) { \
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(container), widget); \
        } else if (GTK_IS_BOX(container)) { \
            gtk_box_append(GTK_BOX(container), widget); \
        } else if (GTK_IS_PANED(container)) { \
            gtk_paned_set_start_child(GTK_PANED(container), widget); \
        } else { \
        } \
    } while(0)
#define gtk_paned_pack1(paned, child, resize, shrink) gtk_paned_set_start_child(GTK_PANED(paned), child)
#define gtk_paned_pack2(paned, child, resize, shrink) gtk_paned_set_end_child(GTK_PANED(paned), child)
#define gtk_entry_get_text(e) gtk_editable_get_text(GTK_EDITABLE(e))
#define GdkEventKey GdkEvent
#endif

#define MAX_COLUMNS 5
#define CUI_SOURCE_PATH "cui"

#define CUI_DEBUG(...) do { \
    if (getenv("DEADBEEF_CUI_DEBUG")) { \
        fprintf(stderr, "[deadbeef-cui debug] " __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while(0)

static DB_functions_t *deadbeef_api;
static ddb_gtkui_t *gtkui_plugin;
static DB_mediasource_t *medialib_plugin;
static ddb_mediasource_source_t *ml_source;
static int shutting_down = 0;
static int owns_ml_source = 0;
static int ml_modification_idx = 1;



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

// --- Widget structures ---
typedef struct {
    ddb_gtkui_widget_t base;
    ddb_gtkui_widget_extended_api_t exapi;
    int num_columns;
    GtkListStore *stores[MAX_COLUMNS];
    GtkWidget *trees[MAX_COLUMNS];
    GHashTable *sel_texts[MAX_COLUMNS];
    char *titles[MAX_COLUMNS];
    char *formats[MAX_COLUMNS];
    int ignore_prefix;
    int split_tags;
    char *autoplaylist_name;
    ddb_scriptable_item_t *my_preset;

    int listener_id;
    ddb_medialib_item_t *cached_tree;
    GHashTable *track_counts_cache;

    int last_ml_modification_idx;
    guint changed_timeout_id;
    guint lib_update_timeout_id;
    int changed_col_idx;

    GtkWidget *search_entry;
    char *search_text;
    char *last_search_text;
} cui_widget_t;

static GList *all_cui_widgets = NULL;

static void rebuild_columns(cui_widget_t *cw);
static void cui_init(ddb_gtkui_widget_t *w);
static const char **cui_serialize_to_keyvalues(ddb_gtkui_widget_t *w);
static void cui_deserialize_from_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues);
static void cui_free_serialized_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues);
static void cui_initmenu(ddb_gtkui_widget_t *w, GtkWidget *menu);


static void init_my_preset(cui_widget_t *cw);
static gboolean deferred_lib_update_cb(gpointer data);

static void init_my_preset(cui_widget_t *cw) {
    CUI_DEBUG("init_my_preset called");
    if (cw->my_preset) {
        my_scriptable_free((scriptableItem_t *)cw->my_preset);
        cw->my_preset = NULL;
    }

    scriptableItem_t *p = my_scriptable_alloc();
    p->flags = SCRIPTABLE_FLAG_IS_LIST;
    my_scriptable_set_prop(p, "name", "Facets");

    cw->num_columns = 0;

    for (int i = 0; i < MAX_COLUMNS; i++) {
        if (!cw->formats[i] || !cw->titles[i] || !cw->formats[i][0] || !cw->titles[i][0]) continue;

        scriptableItem_t *child = my_scriptable_alloc();
        my_scriptable_set_prop(child, "name", cw->formats[i]);

        if (cw->split_tags) {
            my_scriptable_set_prop(child, "split", "; ");
        }

        my_scriptable_add_child(p, child);
        cw->num_columns++;
    }

    // Fallback if user cleared everything
    if (cw->num_columns == 0) {
        scriptableItem_t *child = my_scriptable_alloc();
        my_scriptable_set_prop(child, "name", "$if2(%album artist%,%artist%)");
        my_scriptable_add_child(p, child);
        if (cw->titles[0]) g_free(cw->titles[0]);
        cw->titles[0] = g_strdup("Album Artist");
        cw->num_columns = 1;
    }

    scriptableItem_t *track_child = my_scriptable_alloc();
    my_scriptable_set_prop(track_child, "name", "%title%");
    my_scriptable_add_child(p, track_child);

    cw->my_preset = (ddb_scriptable_item_t *)p;
}

static void sync_source_config(void) {    char paths[4096];
    deadbeef_api->conf_get_str("medialib.deadbeef.paths", "", paths, sizeof(paths));
    if (paths[0]) {
        deadbeef_api->conf_set_str("medialib." CUI_SOURCE_PATH ".paths", paths);
    }
    deadbeef_api->conf_set_str("medialib." CUI_SOURCE_PATH ".enabled", "1");
}

// --- Track counting ---

static int track_matches_search(DB_playItem_t *track, const char *search_text) {
    if (!search_text || !search_text[0]) return 1;

    deadbeef_api->pl_lock();
    const char *title = deadbeef_api->pl_find_meta_raw(track, "title");
    const char *artist = deadbeef_api->pl_find_meta_raw(track, "artist");

    int match = 0;
    if (title || artist) {
        if (title && strcasestr(title, search_text)) match = 1;
        else if (artist && strcasestr(artist, search_text)) match = 1;
    }
    deadbeef_api->pl_unlock();

    return match;
}

static int count_tracks_recursive(const ddb_medialib_item_t *node, cui_widget_t *cw) {
    GHashTable *cache = cw->search_text ? NULL : cw->track_counts_cache;
    if (cache) {
        gpointer cached = g_hash_table_lookup(cache, node);
        if (cached) {
            return GPOINTER_TO_INT(cached) - 1;
        }
    }

    int count = 0;
    DB_playItem_t *track = medialib_plugin->tree_item_get_track(node);
    if (track) {
        if (track_matches_search(track, cw->search_text)) {
            count = 1;
        }
    }
    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        count += count_tracks_recursive(child, cw);
        child = medialib_plugin->tree_item_get_next(child);
    }

    if (cache) {
        g_hash_table_insert(cache, (gpointer)node, GINT_TO_POINTER(count + 1));
    }
    return count;
}

static const char *skip_prefix(const char *str, int ignore) {
    if (!ignore || !str) return str;
    if (strncasecmp(str, "The ", 4) == 0) return str + 4;
    if (strncasecmp(str, "A ", 2) == 0) return str + 2;
    if (strncasecmp(str, "An ", 3) == 0) return str + 3;
    return str;
}

static int sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    cui_widget_t *cw = (cui_widget_t *)user_data;
    gint sort_col;
    GtkSortType order;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model), &sort_col, &order);

    gchar *name_a, *name_b;
    int count_a, count_b;
    gboolean is_all_a, is_all_b;
    gtk_tree_model_get(model, a, 0, &name_a, 1, &count_a, 2, &is_all_a, -1);
    gtk_tree_model_get(model, b, 0, &name_b, 1, &count_b, 2, &is_all_b, -1);

    int result = 0;
    if (name_a && name_b) {
        if (is_all_a && !is_all_b) {
            result = (order == GTK_SORT_ASCENDING) ? -1 : 1;
        } else if (!is_all_a && is_all_b) {
            result = (order == GTK_SORT_ASCENDING) ? 1 : -1;
        } else {
            if (sort_col == 1) { // Count
                result = count_b - count_a;
                if (result == 0) result = g_utf8_collate(skip_prefix(name_a, cw->ignore_prefix), skip_prefix(name_b, cw->ignore_prefix));
            } else { // Name
                result = g_utf8_collate(skip_prefix(name_a, cw->ignore_prefix), skip_prefix(name_b, cw->ignore_prefix));
            }
        }
    }

    g_free(name_a);
    g_free(name_b);
    return result;
}

static void add_tracks_recursive_multi(const ddb_medialib_item_t *node, int current_level,
                                        cui_widget_t *cw, ddb_playlist_t *plt, DB_playItem_t **after) {
    if (current_level >= 1 && current_level <= cw->num_columns) {
        if (cw->sel_texts[current_level - 1]) {
            const char *text = medialib_plugin->tree_item_get_text(node);
            if (!text || !g_hash_table_contains(cw->sel_texts[current_level - 1], text)) return;
        }
    }

    DB_playItem_t *track = medialib_plugin->tree_item_get_track(node);
    if (track) {
        if (track_matches_search(track, cw->search_text)) {
            DB_playItem_t *track_new = deadbeef_api->pl_item_alloc();
            deadbeef_api->pl_item_copy(track_new, track);
            DB_playItem_t *inserted = deadbeef_api->plt_insert_item(plt, *after, track_new);
            if (*after) {
                deadbeef_api->pl_item_unref(*after);
            }
            *after = inserted;
            deadbeef_api->pl_item_ref(*after);
            deadbeef_api->pl_item_unref(track_new);
        }
    }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        add_tracks_recursive_multi(child, current_level + 1, cw, plt, after);
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static ddb_playlist_t *get_or_create_viewer_playlist(void) {
    deadbeef_api->conf_lock();
    const char *ap_name = deadbeef_api->conf_get_str_fast("cui.autoplaylist_name", "Library Viewer");
    char target_name[256];
    strncpy(target_name, ap_name, sizeof(target_name)-1);
    target_name[sizeof(target_name)-1] = '\0';
    deadbeef_api->conf_unlock();

    int count = deadbeef_api->plt_get_count();
    for (int i = 0; i < count; i++) {
        ddb_playlist_t *plt = deadbeef_api->plt_get_for_idx(i);
        if (plt) {
            char title[256];
            deadbeef_api->plt_get_title(plt, title, sizeof(title));
            if (strcmp(title, target_name) == 0) {
                return plt;
            }
            deadbeef_api->plt_unref(plt);
        }
    }
    int new_idx = deadbeef_api->plt_add(count, target_name);
    if (new_idx >= 0) {
        return deadbeef_api->plt_get_for_idx(new_idx);
    }
    return NULL;
}

static void populate_playlist_from_cui(cui_widget_t *cw, ddb_playlist_t *plt, int clear_first) {
    if (!cw->cached_tree || !deadbeef_api || !medialib_plugin) return;

    deadbeef_api->pl_lock();
    if (clear_first) {
        deadbeef_api->plt_clear(plt);
    }
    
    DB_playItem_t *after = NULL;
    if (!clear_first) {
        after = deadbeef_api->plt_get_last(plt, PL_MAIN);
    }

    const ddb_medialib_item_t *root_node = cw->cached_tree;
    int root_level = 0;

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(root_node);
    while (child) {
        add_tracks_recursive_multi(child, root_level + 1, cw, plt, &after);
        child = medialib_plugin->tree_item_get_next(child);
    }

    if (after) {
        deadbeef_api->pl_item_unref(after);
    }

    deadbeef_api->plt_modified(plt);
    deadbeef_api->pl_unlock();
    deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0);
}

static void update_playlist_from_cui(cui_widget_t *cw) {
    CUI_DEBUG("update_playlist_from_cui called");
    ddb_playlist_t *plt = get_or_create_viewer_playlist();
    if (!plt) return;
    deadbeef_api->plt_set_curr(plt);

    populate_playlist_from_cui(cw, plt, 1);

    deadbeef_api->plt_unref(plt);
}

// --- Aggregation / population ---

static void aggregate_recursive_multi(const ddb_medialib_item_t *node,
                                       int current_level, int target_level,
                                       cui_widget_t *cw, GHashTable *seen) {
    if (current_level == target_level) {
        const char *text = medialib_plugin->tree_item_get_text(node);
        if (text) {
            int tracks = count_tracks_recursive(node, cw);
            if (tracks > 0) {
                int *count_ptr = g_hash_table_lookup(seen, text);
                if (count_ptr) {
                    *count_ptr += tracks;
                } else {
                    count_ptr = g_new(int, 1);
                    *count_ptr = tracks;
                    g_hash_table_insert(seen, g_strdup(text), count_ptr);
                }
            }
        }
        return;
    }

    if (current_level >= 1 && current_level <= cw->num_columns) {
        if (cw->sel_texts[current_level - 1]) {
            const char *text = medialib_plugin->tree_item_get_text(node);
            if (!text || !g_hash_table_contains(cw->sel_texts[current_level - 1], text)) return;
        }
    }

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(node);
    while (child) {
        aggregate_recursive_multi(child, current_level + 1, target_level, cw, seen);
        child = medialib_plugin->tree_item_get_next(child);
    }
}

static void populate_list_multi(GtkListStore *store, int target_level, cui_widget_t *cw, int col_idx) {
    double old_vscroll = 0;
    GtkWidget *scroll = gtk_widget_get_parent(cw->trees[col_idx]);
    if (GTK_IS_SCROLLED_WINDOW(scroll)) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
        old_vscroll = gtk_adjustment_get_value(adj);
    }

    gtk_list_store_clear(store);
    if (!cw->cached_tree || !medialib_plugin) return;

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    const ddb_medialib_item_t *root_node = cw->cached_tree;
    int root_level = 0;

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(root_node);
    while (child) {
        aggregate_recursive_multi(child, root_level + 1, target_level, cw, seen);
        child = medialib_plugin->tree_item_get_next(child);
    }

    int total_tracks = 0;
    int total_items = g_hash_table_size(seen);
    GList *keys = g_hash_table_get_keys(seen);
    for (GList *l = keys; l; l = l->next) {
        char *text = (char *)l->data;
        int *count_ptr = g_hash_table_lookup(seen, text);
        GtkTreeIter iter;
        gtk_list_store_insert_with_values(store, &iter, -1, 0, text, 1, *count_ptr, 2, FALSE, -1);
        total_tracks += *count_ptr;
    }
    g_list_free(keys);

    char all_text[256];
    const char *title = cw->titles[col_idx];
    const char *base_title = title;
    
    // Special case: simplify "Album Artist" to "Artist" for the aggregate label
    if (strcasecmp(title, "Album Artist") == 0) {
        base_title = "Artist";
    }

    char *plural_title;
    if (g_str_has_suffix(base_title, "s") || g_str_has_suffix(base_title, "S")) {
        plural_title = g_strdup(base_title);
    } else {
        plural_title = g_strdup_printf("%ss", base_title);
    }
    
    snprintf(all_text, sizeof(all_text), "[All (%d %s)]", total_items, plural_title);
    
    GtkTreeIter iter;
    gtk_list_store_insert_with_values(store, &iter, 0, 0, all_text, 1, total_tracks, 2, TRUE, -1);

    g_free(plural_title);
    g_hash_table_destroy(seen);

    if (GTK_IS_SCROLLED_WINDOW(scroll)) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
        gtk_adjustment_set_value(adj, old_vscroll);
    }
}

// --- Selection handlers ---

static void on_column_changed(GtkTreeSelection *selection, gpointer data);

static void update_selection_hash(GtkTreeSelection *selection, GHashTable **hash_ptr) {
    if (*hash_ptr) {
        g_hash_table_destroy(*hash_ptr);
        *hash_ptr = NULL;
    }
    
    GtkTreeModel *model;
    GList *paths = gtk_tree_selection_get_selected_rows(selection, &model);
    if (paths) {
        *hash_ptr = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        for (GList *l = paths; l != NULL; l = l->next) {
            GtkTreePath *path = (GtkTreePath *)l->data;
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(model, &iter, path)) {
                gchar *text;
                gboolean is_all = FALSE;
                gtk_tree_model_get(model, &iter, 0, &text, 2, &is_all, -1);
                if (text) {
                    if (is_all) {
                        g_hash_table_destroy(*hash_ptr);
                        *hash_ptr = NULL;
                        g_free(text);
                        break;
                    }
                    g_hash_table_insert(*hash_ptr, text, NULL);
                }
            }
        }
        g_list_free_full(paths, (GDestroyNotify)gtk_tree_path_free);
    }
}

static gboolean deferred_column_changed_cb(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    cw->changed_timeout_id = 0;

    int start_col = cw->changed_col_idx;
    cw->changed_col_idx = -1;

    if (start_col == -1 || shutting_down) return G_SOURCE_REMOVE;

    for (int col_idx = start_col; col_idx < cw->num_columns; col_idx++) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[col_idx]));

        update_selection_hash(selection, &cw->sel_texts[col_idx]);

        if (col_idx + 1 < cw->num_columns) {
            GtkTreeSelection *next_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[col_idx + 1]));
            g_signal_handlers_block_by_func(next_sel, (gpointer)on_column_changed, cw);
            populate_list_multi(cw->stores[col_idx + 1], col_idx + 2, cw, col_idx + 1);
            g_signal_handlers_unblock_by_func(next_sel, (gpointer)on_column_changed, cw);
        }
    }

    update_playlist_from_cui(cw);
    return G_SOURCE_REMOVE;
}

static void on_column_changed(GtkTreeSelection *selection, gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;

    int col_idx = -1;
    for (int i = 0; i < cw->num_columns; i++) {
        if (gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[i])) == selection) {
            col_idx = i;
            break;
        }
    }
    if (col_idx == -1) return;

    if (cw->changed_col_idx == -1 || col_idx < cw->changed_col_idx) {
        cw->changed_col_idx = col_idx;
    }

    if (cw->changed_timeout_id == 0) {
        cw->changed_timeout_id = g_timeout_add(10, deferred_column_changed_cb, cw);
    }
}

// --- Tree data refresh ---

typedef struct {
    cui_widget_t *cw;
    double values[MAX_COLUMNS];
} scroll_restore_t;

static gboolean restore_vscroll_idle(gpointer data) {
    scroll_restore_t *sr = (scroll_restore_t *)data;
    if (g_list_find(all_cui_widgets, sr->cw)) {
        for (int i = 0; i < sr->cw->num_columns; i++) {
            GtkWidget *scroll = gtk_widget_get_parent(sr->cw->trees[i]);
            if (GTK_IS_SCROLLED_WINDOW(scroll)) {
                GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
                gtk_adjustment_set_value(adj, sr->values[i]);
            }
        }
    }
    free(sr);
    return G_SOURCE_REMOVE;
}

static void update_tree_data(cui_widget_t *cw) {
    if (shutting_down || !medialib_plugin || !ml_source) return;

    int search_changed = 0;
    if (cw->search_text && (!cw->last_search_text || strcmp(cw->search_text, cw->last_search_text) != 0)) {
        search_changed = 1;
    } else if (!cw->search_text && cw->last_search_text) {
        search_changed = 1;
    }

    if (search_changed) {
        g_free(cw->last_search_text);
        cw->last_search_text = cw->search_text ? g_strdup(cw->search_text) : NULL;
        cw->last_ml_modification_idx = -1;
    }

    int current_idx = g_atomic_int_get(&ml_modification_idx);
    CUI_DEBUG("update_tree_data called (ml_idx=%d, cw_idx=%d)", current_idx, cw->last_ml_modification_idx);
    if (cw->last_ml_modification_idx == current_idx && cw->cached_tree) {
        return;
    }
    scroll_restore_t *sr = calloc(1, sizeof(scroll_restore_t));
    sr->cw = cw;

    for (int i = 0; i < cw->num_columns; i++) {
        GtkWidget *scroll = gtk_widget_get_parent(cw->trees[i]);
        if (GTK_IS_SCROLLED_WINDOW(scroll)) {
            GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
            sr->values[i] = gtk_adjustment_get_value(adj);
        }
    }

    if (cw->cached_tree) {
        medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
        cw->cached_tree = NULL;
    }
    if (cw->track_counts_cache) {
        g_hash_table_destroy(cw->track_counts_cache);
        cw->track_counts_cache = NULL;
    }
    
    GHashTable *saved_sels[MAX_COLUMNS] = {NULL};
    for (int i = 0; i < cw->num_columns; i++) {
        saved_sels[i] = cw->sel_texts[i];
        cw->sel_texts[i] = NULL;
    }

    if (!cw->my_preset) init_my_preset(cw);

    cw->cached_tree = medialib_plugin->create_item_tree(ml_source, cw->my_preset, NULL);
    if (!cw->cached_tree) {
        for (int i = 0; i < cw->num_columns; i++) {
            if (saved_sels[i]) g_hash_table_destroy(saved_sels[i]);
        }
        free(sr);
        return;
    }
    
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    populate_list_multi(cw->stores[0], 1, cw, 0);

    for (int i = 0; i < cw->num_columns; i++) {
        if (saved_sels[i]) {
            GtkTreeModel *model = GTK_TREE_MODEL(cw->stores[i]);
            GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[i]));
            
            GtkTreeIter iter;
            gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
            gboolean found_any = FALSE;
            
            g_signal_handlers_block_by_func(sel, (gpointer)on_column_changed, cw);
            
            while (valid) {
                gchar *text;
                gtk_tree_model_get(model, &iter, 0, &text, -1);
                if (text && g_hash_table_contains(saved_sels[i], text)) {
                    gtk_tree_selection_select_iter(sel, &iter);
                    found_any = TRUE;
                }
                g_free(text);
                valid = gtk_tree_model_iter_next(model, &iter);
            }
            
            g_signal_handlers_unblock_by_func(sel, (gpointer)on_column_changed, cw);
            
            if (found_any) {
                update_selection_hash(sel, &cw->sel_texts[i]);
                if (i + 1 < cw->num_columns) {
                    populate_list_multi(cw->stores[i + 1], i + 2, cw, i + 1);
                }
            } else {
                break;
            }
        }
    }

    g_idle_add(restore_vscroll_idle, sr);

    for (int i = 0; i < cw->num_columns; i++) {
        if (saved_sels[i]) {
            g_hash_table_destroy(saved_sels[i]);
        }
    }

    if (!cw->sel_texts[0]) {
        GtkTreeSelection *first_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[0]));
        on_column_changed(first_sel, cw);
    }

    cw->last_ml_modification_idx = current_idx;
}

static gboolean deferred_lib_update_cb(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    cw->lib_update_timeout_id = 0;
    
    if (shutting_down) return G_SOURCE_REMOVE;
    
    if (g_list_find(all_cui_widgets, cw)) {
        if (medialib_plugin && ml_source) {
            update_tree_data(cw);
        }
    }
    return G_SOURCE_REMOVE;
}

static gboolean ml_event_idle_cb(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    if (shutting_down) return G_SOURCE_REMOVE;
    if (!g_list_find(all_cui_widgets, cw)) return G_SOURCE_REMOVE;
    if (!medialib_plugin || !ml_source) return G_SOURCE_REMOVE;

    if (medialib_plugin->scanner_state(ml_source) != DDB_MEDIASOURCE_STATE_IDLE) {
        return G_SOURCE_REMOVE;
    }

    if (cw->lib_update_timeout_id == 0) {
        cw->lib_update_timeout_id = g_timeout_add(1000, deferred_lib_update_cb, cw);
    }
    return G_SOURCE_REMOVE;
}

static void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data) {
    if (shutting_down) return;
    if (event != DDB_MEDIASOURCE_EVENT_STATE_DID_CHANGE &&
        event != DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE) {
        return;
    }
    if (event == DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE) {
        g_atomic_int_inc(&ml_modification_idx);
    }
    g_idle_add(ml_event_idle_cb, user_data);
}

static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    (void)tree_view;
    (void)path;
    (void)column;
    (void)data;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (plt) {
        int order = deadbeef_api->conf_get_int("playback.order", 0);
        if (order == 1 || order == 2 || order == 3) {
            deadbeef_api->sendmessage(DB_EV_PLAY_RANDOM, 0, 0, 0);
        } else {
            deadbeef_api->plt_set_cursor(plt, 0, PL_MAIN);
            deadbeef_api->sendmessage(DB_EV_PLAY_NUM, 0, 0, 0);
        }
        deadbeef_api->plt_unref(plt);
    }
}

static void on_menu_add_to_current(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (plt) {
        populate_playlist_from_cui(cw, plt, 0);
        deadbeef_api->plt_unref(plt);
    }
}

static void on_menu_send_to_new(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    int count = deadbeef_api->plt_get_count();
    int new_idx = deadbeef_api->plt_add(count, "New Playlist");
    if (new_idx >= 0) {
        ddb_playlist_t *plt = deadbeef_api->plt_get_for_idx(new_idx);
        if (plt) {
            populate_playlist_from_cui(cw, plt, 1);
            deadbeef_api->plt_set_curr(plt);
            deadbeef_api->plt_unref(plt);
        }
    }
}

static void on_menu_sync_library(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    if (!medialib_plugin || !ml_source) return;
    // Async: medialib scanner fires CONTENT_DID_CHANGE when done, which routes
    // through ml_listener_cb → deferred_lib_update_cb and rebuilds the trees.
    medialib_plugin->refresh(ml_source);
}

static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTreeView *tv = GTK_TREE_VIEW(widget);
        GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);

        GtkTreePath *path = NULL;
        if (gtk_tree_view_get_path_at_pos(tv, (int)event->x, (int)event->y,
                                           &path, NULL, NULL, NULL) && path) {
            if (!gtk_tree_selection_path_is_selected(sel, path)) {
                gtk_tree_selection_unselect_all(sel);
                gtk_tree_selection_select_path(sel, path);
            }
            gtk_tree_path_free(path);
        }

        GtkWidget *menu = gtk_menu_new();

        GtkWidget *item_add = gtk_menu_item_new_with_label("Add selection to current playlist");
        g_signal_connect(item_add, "activate", G_CALLBACK(on_menu_add_to_current), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_add);

        GtkWidget *item_new = gtk_menu_item_new_with_label("Send selection to new playlist");
        g_signal_connect(item_new, "activate", G_CALLBACK(on_menu_send_to_new), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new);

        GtkWidget *sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

        GtkWidget *item_sync = gtk_menu_item_new_with_label("Sync library");
        g_signal_connect(item_sync, "activate", G_CALLBACK(on_menu_sync_library), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_sync);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

// --- Widget lifecycle ---

static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;

    if (cw->changed_timeout_id) {
        g_source_remove(cw->changed_timeout_id);
        cw->changed_timeout_id = 0;
    }
    if (cw->lib_update_timeout_id) {
        g_source_remove(cw->lib_update_timeout_id);
        cw->lib_update_timeout_id = 0;
    }

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
    }
    
    // If we're the last widget and we own the source, it'll be freed in cui_stop.
    // However, if we're just one of many, we keep it alive.
    if (all_cui_widgets == NULL && owns_ml_source) {
        // Source will be cleaned up in cui_stop or when last widget is gone if needed
    }

    if (cw->track_counts_cache) {
        g_hash_table_destroy(cw->track_counts_cache);
        cw->track_counts_cache = NULL;
    }

    for (int i = 0; i < cw->num_columns; i++) {
        if (cw->sel_texts[i]) {
            g_hash_table_destroy(cw->sel_texts[i]);
            cw->sel_texts[i] = NULL;
        }
        g_free(cw->titles[i]);
    }
    
    g_free(cw->search_text);
    cw->search_text = NULL;
    g_free(cw->last_search_text);
    cw->last_search_text = NULL;
}

static GtkWidget *create_column(const char *title, GtkListStore **out_store, GtkWidget **out_tree, cui_widget_t *cw) {
    GtkListStore *store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 0, sort_func, cw, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 1, sort_func, cw, NULL);
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
    g_object_set(count_renderer, "xalign", 1.0, "xpad", 16, NULL);
    GtkTreeViewColumn *count_column = gtk_tree_view_column_new_with_attributes("Count", count_renderer, "text", 1, NULL);
    gtk_tree_view_column_set_sizing(count_column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_column_set_sort_column_id(count_column, 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), count_column);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    if (out_store) *out_store = store;
    if (out_tree) *out_tree = tree;

    return scroll;
}

static void update_tree_data(cui_widget_t *cw);

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    cui_widget_t *cw = (cui_widget_t *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    if (cw->search_text) {
        g_free(cw->search_text);
        cw->search_text = NULL;
    }
    if (text && text[0]) {
        cw->search_text = g_utf8_strdown(text, -1);
    }
    update_tree_data(cw);
}


static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    
    if (event->keyval == GDK_KEY_Escape) {
        if (gtk_widget_get_visible(cw->search_entry)) {
            gtk_entry_set_text(GTK_ENTRY(cw->search_entry), "");
            gtk_widget_hide(cw->search_entry);
            
            // Return focus to the first column
            if (cw->num_columns > 0 && cw->trees[0]) {
                gtk_widget_grab_focus(cw->trees[0]);
            }
            return TRUE;
        }
    }
    
    return FALSE;
}


static void rebuild_columns(cui_widget_t *cw) {
    if (cw->num_columns <= 0) return;
    
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(cw->base.widget));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    GtkWidget *col_widgets[MAX_COLUMNS] = {NULL};
    for (int i = 0; i < cw->num_columns; i++) {
        col_widgets[i] = create_column(cw->titles[i], &cw->stores[i], &cw->trees[i], cw);

        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[i]));
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
        g_signal_connect(sel, "changed", G_CALLBACK(on_column_changed), cw);
        g_signal_connect(cw->trees[i], "row-activated", G_CALLBACK(on_row_activated), cw);
#if GTK_MAJOR_VERSION < 4
        g_signal_connect(cw->trees[i], "button-press-event", G_CALLBACK(on_tree_button_press), cw);
#endif
        g_signal_connect(cw->trees[i], "key-press-event", G_CALLBACK(on_key_press), cw);
        gtk_tree_view_set_enable_search(GTK_TREE_VIEW(cw->trees[i]), TRUE);
        gtk_tree_view_set_search_column(GTK_TREE_VIEW(cw->trees[i]), 0);
    }

    GtkWidget *top_widget = col_widgets[cw->num_columns - 1];
    for (int i = cw->num_columns - 2; i >= 0; i--) {
        GtkWidget *pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(pane), col_widgets[i], TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(pane), top_widget, TRUE, FALSE);
        top_widget = pane;
    }

    cw->search_entry = gtk_search_entry_new();
    g_signal_connect(cw->search_entry, "changed", G_CALLBACK(on_search_changed), cw);
    g_signal_connect(cw->search_entry, "key-press-event", G_CALLBACK(on_key_press), cw);
    gtk_widget_set_no_show_all(cw->search_entry, TRUE);
    gtk_widget_hide(cw->search_entry);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(cw->search_entry, 4);
    gtk_widget_set_margin_end(cw->search_entry, 4);
    gtk_widget_set_margin_top(cw->search_entry, 4);
    gtk_widget_set_margin_bottom(cw->search_entry, 4);
    gtk_box_pack_start(GTK_BOX(vbox), cw->search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), top_widget, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(cw->base.widget), vbox);
    gtk_widget_show_all(cw->base.widget);
}

static void cui_init(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;
    if (!cw->my_preset) init_my_preset(cw);
    
    rebuild_columns(cw);

    if (gtkui_plugin && gtkui_plugin->w_override_signals) {
        gtkui_plugin->w_override_signals(w->widget, w);
    }

    if (!ml_source && medialib_plugin) {
        void *gtkui_handle = dlopen("ddb_gui_GTK3.so", RTLD_LAZY | RTLD_NOLOAD);
        if (gtkui_handle) {
            ddb_mediasource_source_t * (*get_shared_source)(void) =
                (ddb_mediasource_source_t * (*)(void))dlsym(gtkui_handle, "gtkui_medialib_get_source");
            if (get_shared_source) {
                ml_source = get_shared_source();
                CUI_DEBUG("Using shared medialib source from GTKUI");
            }
            dlclose(gtkui_handle);
        }

        if (!ml_source && medialib_plugin->create_source) {
            sync_source_config();
            ml_source = medialib_plugin->create_source(CUI_SOURCE_PATH);
            if (ml_source) {
                owns_ml_source = 1;
                medialib_plugin->refresh(ml_source);
            }
        }
    }

    if (medialib_plugin && ml_source) {
        cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw);
    }

    if (!g_list_find(all_cui_widgets, cw)) {
        all_cui_widgets = g_list_append(all_cui_widgets, cw);
    }
    update_tree_data(cw);
}

static ddb_gtkui_widget_t *cui_create_widget(void) {
    CUI_DEBUG("cui_create_widget called");

    cui_widget_t *cw = calloc(1, sizeof(cui_widget_t));
    cw->changed_col_idx = -1;
    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "cui";

    cw->exapi._size = sizeof(ddb_gtkui_widget_extended_api_t);
    cw->exapi.serialize_to_keyvalues = cui_serialize_to_keyvalues;
    cw->exapi.deserialize_from_keyvalues = cui_deserialize_from_keyvalues;
    cw->exapi.free_serialized_keyvalues = cui_free_serialized_keyvalues;

    w->initmenu = cui_initmenu;
    w->init = cui_init;
    w->destroy = cui_destroy;

#if GTK_MAJOR_VERSION >= 4
    w->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
    w->widget = gtk_event_box_new();
#endif
    gtk_widget_show(w->widget);

    g_signal_connect(w->widget, "key-press-event", G_CALLBACK(on_key_press), cw);

    return w;
}


static const char **cui_serialize_to_keyvalues(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;
    int num_items = MAX_COLUMNS * 2 + 3; // formats, titles, split_tags, ignore_prefix, autoplaylist
    char **keyvalues = calloc(num_items * 2 + 1, sizeof(char *));
    int idx = 0;
    
    for (int i = 0; i < MAX_COLUMNS; i++) {
        char key_title[32], key_format[32];
        snprintf(key_title, sizeof(key_title), "col%d_title", i + 1);
        snprintf(key_format, sizeof(key_format), "col%d_format", i + 1);
        
        keyvalues[idx++] = strdup(key_title);
        keyvalues[idx++] = cw->titles[i] ? strdup(cw->titles[i]) : strdup("");
        
        keyvalues[idx++] = strdup(key_format);
        keyvalues[idx++] = cw->formats[i] ? strdup(cw->formats[i]) : strdup("");
    }
    
    keyvalues[idx++] = strdup("split_tags");
    char temp[16];
    snprintf(temp, sizeof(temp), "%d", cw->split_tags);
    keyvalues[idx++] = strdup(temp);
    
    keyvalues[idx++] = strdup("ignore_prefix");
    snprintf(temp, sizeof(temp), "%d", cw->ignore_prefix);
    keyvalues[idx++] = strdup(temp);
    
    keyvalues[idx++] = strdup("autoplaylist_name");
    keyvalues[idx++] = cw->autoplaylist_name ? strdup(cw->autoplaylist_name) : strdup("Library Viewer");
    
    keyvalues[idx] = NULL;
    return (const char **)keyvalues;
}

static void cui_deserialize_from_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues) {
    cui_widget_t *cw = (cui_widget_t *)w;
    
    cw->ignore_prefix = 0;
    cw->split_tags = 1;
    cw->autoplaylist_name = g_strdup("Library Viewer");
    
    int found_any = 0;
    
    if (keyvalues) {
        for (int i = 0; keyvalues[i]; i += 2) {
            const char *k = keyvalues[i];
            const char *v = keyvalues[i+1];
            if (!v) continue;
            
            if (strncmp(k, "col", 3) == 0 && strlen(k) >= 10) {
                int col = k[3] - '1';
                if (col >= 0 && col < MAX_COLUMNS) {
                    if (strstr(k, "_title")) {
                        cw->titles[col] = g_strdup(v);
                        found_any = 1;
                    } else if (strstr(k, "_format")) {
                        cw->formats[col] = g_strdup(v);
                        found_any = 1;
                    }
                }
            } else if (strcmp(k, "split_tags") == 0) {
                cw->split_tags = atoi(v);
            } else if (strcmp(k, "ignore_prefix") == 0) {
                cw->ignore_prefix = atoi(v);
            } else if (strcmp(k, "autoplaylist_name") == 0) {
                if (cw->autoplaylist_name) g_free(cw->autoplaylist_name);
                cw->autoplaylist_name = g_strdup(v);
            }
        }
    }
    
    if (!found_any) {
        cw->titles[0] = g_strdup("Genre");
        cw->formats[0] = g_strdup("%genre%");
        cw->titles[1] = g_strdup("Album Artist");
        cw->formats[1] = g_strdup("$if2(%album artist%,%artist%)");
        cw->titles[2] = g_strdup("Album");
        cw->formats[2] = g_strdup("%album%");
    }
}

static void cui_free_serialized_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues) {
    (void)w;
    if (!keyvalues) return;
    for (int i = 0; keyvalues[i]; i++) {
        free((void *)keyvalues[i]);
    }
    free((void *)keyvalues);
}

static void on_config_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    cui_widget_t *cw = (cui_widget_t *)user_data;
    
    if (response_id == GTK_RESPONSE_ACCEPT) {
        // Find inputs and update cw.
        GtkWidget *content_area = gtk_dialog_get_content_area(dialog);
        GList *children = gtk_container_get_children(GTK_CONTAINER(content_area));
        if (children && GTK_IS_GRID(children->data)) {
            GtkGrid *grid = GTK_GRID(children->data);
            for (int i = 0; i < MAX_COLUMNS; i++) {
                GtkWidget *title_entry = gtk_grid_get_child_at(grid, 1, i + 1);
                GtkWidget *format_entry = gtk_grid_get_child_at(grid, 2, i + 1);
                if (title_entry && format_entry) {
                    if (cw->titles[i]) g_free(cw->titles[i]);
                    if (cw->formats[i]) g_free(cw->formats[i]);
                    cw->titles[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(title_entry)));
                    cw->formats[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(format_entry)));
                }
            }
            GtkWidget *ignore_cb = gtk_grid_get_child_at(grid, 1, MAX_COLUMNS + 1);
            if (ignore_cb) cw->ignore_prefix = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignore_cb));
            
            GtkWidget *split_cb = gtk_grid_get_child_at(grid, 1, MAX_COLUMNS + 2);
            if (split_cb) cw->split_tags = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_cb));
            
            GtkWidget *ap_entry = gtk_grid_get_child_at(grid, 1, MAX_COLUMNS + 3);
            if (ap_entry) {
                if (cw->autoplaylist_name) g_free(cw->autoplaylist_name);
                cw->autoplaylist_name = g_strdup(gtk_entry_get_text(GTK_ENTRY(ap_entry)));
            }
        }
        g_list_free(children);

        // Rebuild internal data
        if (cw->my_preset) {
            my_scriptable_free((scriptableItem_t *)cw->my_preset);
            cw->my_preset = NULL;
        }
        init_my_preset(cw);
        rebuild_columns(cw);
        update_tree_data(cw);
        
        if (gtkui_plugin && gtkui_plugin->w_save_layout_to_conf_key) {
            gtkui_plugin->w_save_layout_to_conf_key("layout", NULL); // Best effort, layout is global, might need user to save layout
        }
    }
    
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void show_config_dialog(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    
    GtkWidget *mainwin = gtkui_plugin->get_mainwin ? gtkui_plugin->get_mainwin() : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Configure Facets",
                                                    GTK_WINDOW(mainwin),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_OK", GTK_RESPONSE_ACCEPT,
                                                    NULL);
                                                    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Title"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Format"), 2, 0, 1, 1);
    
    for (int i = 0; i < MAX_COLUMNS; i++) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "Column %d:", i + 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(label_text), 0, i + 1, 1, 1);
        
        GtkWidget *title_entry = gtk_entry_new();
        if (cw->titles[i]) gtk_entry_set_text(GTK_ENTRY(title_entry), cw->titles[i]);
        gtk_grid_attach(GTK_GRID(grid), title_entry, 1, i + 1, 1, 1);
        
        GtkWidget *format_entry = gtk_entry_new();
        if (cw->formats[i]) gtk_entry_set_text(GTK_ENTRY(format_entry), cw->formats[i]);
        gtk_widget_set_hexpand(format_entry, TRUE);
        gtk_grid_attach(GTK_GRID(grid), format_entry, 2, i + 1, 1, 1);
    }
    
    GtkWidget *ignore_cb = gtk_check_button_new_with_label("Ignore Prefix (The, A, An)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ignore_cb), cw->ignore_prefix);
    gtk_grid_attach(GTK_GRID(grid), ignore_cb, 1, MAX_COLUMNS + 1, 2, 1);
    
    GtkWidget *split_cb = gtk_check_button_new_with_label("Split Multivalue Tags (; )");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_cb), cw->split_tags);
    gtk_grid_attach(GTK_GRID(grid), split_cb, 1, MAX_COLUMNS + 2, 2, 1);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Autoplaylist Name:"), 0, MAX_COLUMNS + 3, 1, 1);
    GtkWidget *ap_entry = gtk_entry_new();
    if (cw->autoplaylist_name) gtk_entry_set_text(GTK_ENTRY(ap_entry), cw->autoplaylist_name);
    gtk_grid_attach(GTK_GRID(grid), ap_entry, 1, MAX_COLUMNS + 3, 2, 1);
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    gtk_widget_show_all(dialog);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_config_dialog_response), cw);
}

static void cui_initmenu(ddb_gtkui_widget_t *w, GtkWidget *menu) {
    GtkWidget *item = gtk_menu_item_new_with_mnemonic("Configure Facets...");
    gtk_widget_show(item);
    gtk_container_add(GTK_CONTAINER(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(show_config_dialog), w);
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

    gtkui_plugin->w_reg_widget("Facet Browser (CUI) v1.2.1", DDB_WF_SUPPORTS_EXTENDED_API, cui_create_widget, "cui", NULL);
    fprintf(stderr, "deadbeef-cui: Facet Browser v1.2.1 registered successfully.\n");

    return 0;
}

int cui_stop(void) {
    shutting_down = 1;

    if (owns_ml_source && medialib_plugin && ml_source) {
        medialib_plugin->free_source(ml_source);
        ml_source = NULL;
    }

    return 0;
}

static int action_search_facets(DB_plugin_action_t *action, void *ctx) {
    (void)action;
    (void)ctx;
    if (all_cui_widgets) {
        for (GList *l = all_cui_widgets; l; l = l->next) {
            cui_widget_t *cw = (cui_widget_t *)l->data;
            if (gtk_widget_get_visible(cw->search_entry)) {
                gtk_entry_set_text(GTK_ENTRY(cw->search_entry), "");
                gtk_widget_hide(cw->search_entry);
                if (cw->num_columns > 0 && cw->trees[0]) {
                    gtk_widget_grab_focus(cw->trees[0]);
                }
            } else {
                gtk_widget_show(cw->search_entry);
                gtk_widget_grab_focus(cw->search_entry);
            }
        }
    }
    return 0;
}

static DB_plugin_action_t search_action = {
    .title = "Search Facets",
    .name = "search_facets",
    .flags = DB_ACTION_COMMON,
    .callback = action_search_facets,
    .next = NULL
};

static DB_plugin_action_t *cui_get_actions(DB_playItem_t *it) {
    (void)it;
    return &search_action;
}

static DB_misc_t plugin = {
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 1,
    .plugin.version_minor = 2,
    .plugin.id = "cui",
    .plugin.name = "Columns UI for DeaDBeeF",
    .plugin.descr = "A faceted library browser for DeaDBeeF. Version 1.2.1",
    .plugin.copyright = "MIT License",
    .plugin.website = "https://github.com/bdkl/deadbeef-cui",
    .plugin.start = cui_start,
    .plugin.stop = cui_stop,
    .plugin.message = cui_message,
    .plugin.get_actions = cui_get_actions,
};

DB_plugin_t *ddb_misc_cui_GTK3_load(DB_functions_t *api) {
    deadbeef_api = api;
    return (DB_plugin_t *)&plugin;
}


