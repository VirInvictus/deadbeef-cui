#include "cui_data.h"
#include "cui_scriptable.h"
#include "cui_widget.h"

int track_matches_search(DB_playItem_t *track, const char *search_text) {
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

int count_tracks_recursive(const ddb_medialib_item_t *node, cui_widget_t *cw) {
    // Cache is valid under search too — update_tree_data destroys and recreates
    // track_counts_cache whenever cw->search_text changes (via the modification
    // index reset at line 323), so any cached count always reflects the current
    // filter. The previous code disabled the cache under search out of caution.
    GHashTable *cache = cw->track_counts_cache;
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

const char *skip_prefix(const char *str, int ignore) {
    if (!ignore || !str) return str;
    if (strncasecmp(str, "The ", 4) == 0) return str + 4;
    if (strncasecmp(str, "A ", 2) == 0) return str + 2;
    if (strncasecmp(str, "An ", 3) == 0) return str + 3;
    return str;
}

int sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
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

void add_tracks_recursive_multi(const ddb_medialib_item_t *node, int current_level,
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

ddb_playlist_t *get_or_create_viewer_playlist(void) {
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

void populate_playlist_from_cui(cui_widget_t *cw, ddb_playlist_t *plt, int clear_first) {
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

void update_playlist_from_cui(cui_widget_t *cw) {
    CUI_DEBUG("update_playlist_from_cui called");
    ddb_playlist_t *plt = get_or_create_viewer_playlist();
    if (!plt) return;
    deadbeef_api->plt_set_curr(plt);

    populate_playlist_from_cui(cw, plt, 1);

    deadbeef_api->plt_unref(plt);
}

void aggregate_recursive_multi(const ddb_medialib_item_t *node,
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

void populate_list_multi(GtkListStore *store, int target_level, cui_widget_t *cw, int col_idx) {
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

void update_tree_data(cui_widget_t *cw) {
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

    // Mark the widget as having received real data so subsequent CONTENT_DID_CHANGE
    // events go through the 1s debounce. The first sync — when the source is still
    // loading and the tree comes back empty — keeps the flag clear so the listener
    // can re-fire immediately once data is available.
    if (medialib_plugin->tree_item_get_children(cw->cached_tree)) {
        cw->initial_sync_done = 1;
    }

    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    populate_list_multi(cw->stores[0], 1, cw, 0);

    // Highest column index whose store was populated by the cascade so far.
    // Col 0 was just populated unconditionally; saved-selection matches push
    // this further when they cause downstream columns to be populated with
    // selection-filtered content.
    int populated_through = 0;

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
                    populated_through = i + 1;
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

    // Populate any column the saved-selection cascade didn't reach so unselected
    // facets show their full aggregate (the [All] row plus every value). The
    // populated_through marker is what distinguishes "really empty" from
    // "previously filled with stale [All (0 X)] from an empty-tree first build" —
    // checking gtk_tree_model_get_iter_first alone would falsely skip the latter.
    // We deliberately do not fire update_playlist_from_cui here; the pre-v1.2.4
    // behavior copied the entire library into the viewer playlist on first init,
    // which was slow and surprising. Selection-driven playlist population still
    // happens through on_column_changed when the user clicks a row.
    for (int i = populated_through + 1; i < cw->num_columns; i++) {
        populate_list_multi(cw->stores[i], i + 1, cw, i);
    }

    cw->last_ml_modification_idx = current_idx;
}
