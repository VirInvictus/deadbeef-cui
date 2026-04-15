#include <deadbeef/deadbeef.h>
#include <gtk/gtk.h>
#include <deadbeef/gtkui_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

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

static int ignore_prefix = 0;
static char global_titles[MAX_COLUMNS][256] = {0};
static int global_num_columns = 0;

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
    int num_columns;
    GtkListStore *stores[MAX_COLUMNS];
    GtkWidget *trees[MAX_COLUMNS];
    GHashTable *sel_texts[MAX_COLUMNS];
    char *titles[MAX_COLUMNS];

    int listener_id;
    ddb_medialib_item_t *cached_tree;
    GHashTable *track_counts_cache;

    guint changed_timeout_id;
    int changed_col_idx;
} cui_widget_t;

static ddb_scriptable_item_t *my_preset = NULL;
static GList *all_cui_widgets = NULL;

static void init_my_preset(void) {
    CUI_DEBUG("init_my_preset called");
    if (my_preset) {
        my_scriptable_free((scriptableItem_t *)my_preset);
        my_preset = NULL;
    }
    
    deadbeef_api->conf_lock();
    ignore_prefix = deadbeef_api->conf_get_int("cui.ignore_prefix", 0);
    
    // Default configs if empty
    if (!deadbeef_api->conf_get_str_fast("cui.col1_format", NULL)) {
        deadbeef_api->conf_set_str("cui.col1_title", "Genre");
        deadbeef_api->conf_set_str("cui.col1_format", "%genre%");
        deadbeef_api->conf_set_str("cui.col2_title", "Album Artist");
        deadbeef_api->conf_set_str("cui.col2_format", "$if2(%album artist%,%artist%)");
        deadbeef_api->conf_set_str("cui.col3_title", "Album");
        deadbeef_api->conf_set_str("cui.col3_format", "%album%");
        deadbeef_api->conf_set_int("cui.ignore_prefix", 0);
    }
    deadbeef_api->conf_unlock();

    scriptableItem_t *p = my_scriptable_alloc();
    p->flags = SCRIPTABLE_FLAG_IS_LIST;
    my_scriptable_set_prop(p, "name", "Facets");

    global_num_columns = 0;

    for (int i = 0; i < MAX_COLUMNS; i++) {
        char key_title[32];
        char key_format[32];
        snprintf(key_title, sizeof(key_title), "cui.col%d_title", i + 1);
        snprintf(key_format, sizeof(key_format), "cui.col%d_format", i + 1);
        
        deadbeef_api->conf_lock();
        const char *title = deadbeef_api->conf_get_str_fast(key_title, "");
        const char *format = deadbeef_api->conf_get_str_fast(key_format, "");
        deadbeef_api->conf_unlock();
        
        char title_copy[256] = {0};
        char format_copy[256] = {0};
        
        if (title) strncpy(title_copy, title, sizeof(title_copy) - 1);
        if (format) strncpy(format_copy, format, sizeof(format_copy) - 1);

        if (title_copy[0] && format_copy[0]) {
            scriptableItem_t *child = my_scriptable_alloc();
            my_scriptable_set_prop(child, "name", format_copy);
            my_scriptable_add_child(p, child);
            
            strncpy(global_titles[global_num_columns], title_copy, 255);
            global_num_columns++;
        }
    }
    
    // Fallback if user cleared everything
    if (global_num_columns == 0) {
        scriptableItem_t *child = my_scriptable_alloc();
        my_scriptable_set_prop(child, "name", "$if2(%album artist%,%artist%)");
        my_scriptable_add_child(p, child);
        strcpy(global_titles[0], "Album Artist");
        global_num_columns = 1;
    }

    scriptableItem_t *track_child = my_scriptable_alloc();
    my_scriptable_set_prop(track_child, "name", "%title%");
    my_scriptable_add_child(p, track_child);
    
    my_preset = (ddb_scriptable_item_t *)p;
}

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

static const char *skip_prefix(const char *str) {
    if (!ignore_prefix || !str) return str;
    if (strncasecmp(str, "The ", 4) == 0) return str + 4;
    if (strncasecmp(str, "A ", 2) == 0) return str + 2;
    if (strncasecmp(str, "An ", 3) == 0) return str + 3;
    return str;
}

static int sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    int sort_col = GPOINTER_TO_INT(user_data);
    gint current_sort_col;
    GtkSortType order;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model), &current_sort_col, &order);

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
                if (result == 0) result = g_utf8_collate(skip_prefix(name_a), skip_prefix(name_b));
            } else { // Name
                result = g_utf8_collate(skip_prefix(name_a), skip_prefix(name_b));
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

static ddb_playlist_t *get_or_create_viewer_playlist(void) {
    int count = deadbeef_api->plt_get_count();
    for (int i = 0; i < count; i++) {
        ddb_playlist_t *plt = deadbeef_api->plt_get_for_idx(i);
        if (plt) {
            char title[256];
            deadbeef_api->plt_get_title(plt, title, sizeof(title));
            if (strcmp(title, "Library Viewer") == 0) {
                return plt;
            }
            deadbeef_api->plt_unref(plt);
        }
    }
    int new_idx = deadbeef_api->plt_add(count, "Library Viewer");
    if (new_idx >= 0) {
        return deadbeef_api->plt_get_for_idx(new_idx);
    }
    return NULL;
}

static void update_playlist_from_cui(cui_widget_t *cw) {
    CUI_DEBUG("update_playlist_from_cui called");
    if (!cw->cached_tree || !deadbeef_api || !medialib_plugin) return;
    ddb_playlist_t *plt = get_or_create_viewer_playlist();
    if (!plt) return;
    deadbeef_api->plt_set_curr(plt);

    deadbeef_api->pl_lock();
    deadbeef_api->plt_clear(plt);
    DB_playItem_t *after = NULL;

    const ddb_medialib_item_t *root_node = cw->cached_tree;
    int root_level = 0;

    const ddb_medialib_item_t *child = medialib_plugin->tree_item_get_children(root_node);
    while (child) {
        add_tracks_recursive_multi(child, root_level + 1, cw, plt, &after);
        child = medialib_plugin->tree_item_get_next(child);
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
    char *plural_title;
    if (g_str_has_suffix(title, "s") || g_str_has_suffix(title, "S")) {
        plural_title = g_strdup(title);
    } else {
        plural_title = g_strdup_printf("%ss", title);
    }
    
    snprintf(all_text, sizeof(all_text), "[All (%d %s)]", total_items, plural_title);
    
    GtkTreeIter iter;
    gtk_list_store_insert_with_values(store, &iter, 0, 0, all_text, 1, total_tracks, 2, TRUE, -1);

    g_free(plural_title);
    g_hash_table_destroy(seen);
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

static void update_tree_data(cui_widget_t *cw) {
    CUI_DEBUG("update_tree_data called");
    if (shutting_down || !medialib_plugin || !ml_source) return;

    if (cw->cached_tree) {
        medialib_plugin->free_item_tree(ml_source, cw->cached_tree);
        cw->cached_tree = NULL;
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
    }

    if (!my_preset) init_my_preset();

    cw->cached_tree = medialib_plugin->create_item_tree(ml_source, my_preset, NULL);
    if (!cw->cached_tree) return;
    
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    populate_list_multi(cw->stores[0], 1, cw, 0);

    GtkTreeSelection *first_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[0]));
    on_column_changed(first_sel, cw);
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

// --- Widget lifecycle ---

static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;

    if (cw->changed_timeout_id) {
        g_source_remove(cw->changed_timeout_id);
        cw->changed_timeout_id = 0;
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
        if (cw->track_counts_cache) {
            g_hash_table_destroy(cw->track_counts_cache);
            cw->track_counts_cache = NULL;
        }
    }

    for (int i = 0; i < cw->num_columns; i++) {
        if (cw->sel_texts[i]) {
            g_hash_table_destroy(cw->sel_texts[i]);
            cw->sel_texts[i] = NULL;
        }
        g_free(cw->titles[i]);
    }
}

static GtkWidget *create_column(const char *title, GtkListStore **out_store, GtkWidget **out_tree) {
    GtkListStore *store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
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

static ddb_gtkui_widget_t *cui_create_widget(void) {
    CUI_DEBUG("cui_create_widget called");
    if (!my_preset) init_my_preset();

    cui_widget_t *cw = calloc(1, sizeof(cui_widget_t));
    cw->changed_col_idx = -1;
    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "cui";

    cw->num_columns = global_num_columns;
    
    GtkWidget *col_widgets[MAX_COLUMNS];
    for (int i = 0; i < cw->num_columns; i++) {
        cw->titles[i] = g_strdup(global_titles[i]);
        col_widgets[i] = create_column(cw->titles[i], &cw->stores[i], &cw->trees[i]);
        
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[i]));
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
        g_signal_connect(sel, "changed", G_CALLBACK(on_column_changed), cw);
        g_signal_connect(cw->trees[i], "row-activated", G_CALLBACK(on_row_activated), cw);
    }

    GtkWidget *top_widget = col_widgets[cw->num_columns - 1];
    for (int i = cw->num_columns - 2; i >= 0; i--) {
        GtkWidget *pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(pane), col_widgets[i], TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(pane), top_widget, TRUE, FALSE);
        top_widget = pane;
    }

    gtk_widget_show_all(top_widget);

    w->widget = top_widget;
    w->destroy = cui_destroy;

    if (gtkui_plugin && gtkui_plugin->w_override_signals) {
        gtkui_plugin->w_override_signals(w->widget, w);
    }

    if (!ml_source && medialib_plugin && medialib_plugin->create_source) {
        sync_source_config();
        ml_source = medialib_plugin->create_source(CUI_SOURCE_PATH);
        if (ml_source) {
            owns_ml_source = 1;
            medialib_plugin->refresh(ml_source);
        }
    }

    if (medialib_plugin && ml_source) {
        cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw);
    }

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
    else if (id == DB_EV_CONFIGCHANGED) {
        deadbeef_api->conf_lock();
        ignore_prefix = deadbeef_api->conf_get_int("cui.ignore_prefix", 0);
        deadbeef_api->conf_unlock();
        // Requires restart for column layout/format string changes.
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

    gtkui_plugin->w_reg_widget("Facet Browser (CUI) v0.8.3", 0, cui_create_widget, "cui", NULL);
    fprintf(stderr, "deadbeef-cui: Facet Browser v0.8.3 registered successfully.\n");

    return 0;
}

int cui_stop(void) {
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

static const char settings_dlg[] =
    "property \"Column 1 Title\" entry cui.col1_title \"Genre\";\n"
    "property \"Column 1 Format\" entry cui.col1_format \"%genre%\";\n"
    "property \"Column 2 Title\" entry cui.col2_title \"Album Artist\";\n"
    "property \"Column 2 Format\" entry cui.col2_format \"$if2(%album artist%,%artist%)\";\n"
    "property \"Column 3 Title\" entry cui.col3_title \"Album\";\n"
    "property \"Column 3 Format\" entry cui.col3_format \"%album%\";\n"
    "property \"Column 4 Title\" entry cui.col4_title \"\";\n"
    "property \"Column 4 Format\" entry cui.col4_format \"\";\n"
    "property \"Column 5 Title\" entry cui.col5_title \"\";\n"
    "property \"Column 5 Format\" entry cui.col5_format \"\";\n"
    "property \"Ignore Prefix (The, A, An)\" checkbox cui.ignore_prefix 0;\n"
    ;

static DB_misc_t plugin = {
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 0,
    .plugin.version_minor = 8,
    .plugin.id = "cui",
    .plugin.name = "Columns UI for DeaDBeeF",
    .plugin.descr = "A faceted library browser for DeaDBeeF.",
    .plugin.copyright = "MIT License",
    .plugin.website = "https://github.com/bdkl/deadbeef-cui",
    .plugin.start = cui_start,
    .plugin.stop = cui_stop,
    .plugin.message = cui_message,
    .plugin.configdialog = settings_dlg,
};

DB_plugin_t *ddb_misc_cui_GTK3_load(DB_functions_t *api) {
    deadbeef_api = api;
    return (DB_plugin_t *)&plugin;
}