// strcasestr (used in track_matches_search) is a GNU extension: without this
// define it only compiles where glibc leaks the declaration by default; musl
// and strict feature-test macros reject the implicit declaration.
#define _GNU_SOURCE

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
    // index reset at its top), so any cached count always reflects the current
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

    // Stored as count+1 so a genuine 0 (nothing matched under the search) is
    // distinguishable from a cache MISS (lookup returns NULL): cached reads
    // subtract the 1 back off. No per-entry allocation this way.
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
            // [All] pinning, order-aware ON PURPOSE: GtkListStore negates the
            // comparator under GTK_SORT_DESCENDING, so a plain -1 here would
            // sink [All] to the bottom on descending sorts. This branch is
            // what keeps [All] at iter 0 in every sort column and direction,
            // which auto_select_all_if_empty relies on. Do not simplify;
            // /cui/sort/all_row locks it in (CLAUDE.md §6.13 records an audit
            // once breaking exactly this).
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

// The effective viewer name for this instance: the per-instance configured
// name, or the default when unset or empty.
static const char *viewer_name(cui_widget_t *cw) {
    return (cw->autoplaylist_name && cw->autoplaylist_name[0])
               ? cw->autoplaylist_name : "Library Viewer";
}

// Core lookup shared by the two finders. With marker_only=0 a playlist that
// merely carries the viewer's title is returned as a fallback, so viewers
// created before the marker existed (v1.3.4 and earlier) keep working; with
// marker_only=1 only marker-matched playlists qualify — that is what the
// shutdown clear uses, so a same-named user playlist is never touched.
static ddb_playlist_t *find_viewer_playlist_impl(cui_widget_t *cw, int marker_only) {
    const char *target_name = viewer_name(cw);

    ddb_playlist_t *fallback = NULL;
    int count = deadbeef_api->plt_get_count();
    for (int i = 0; i < count; i++) {
        ddb_playlist_t *plt = deadbeef_api->plt_get_for_idx(i);
        if (!plt) continue;

        deadbeef_api->pl_lock();
        const char *marker = deadbeef_api->plt_find_meta(plt, CUI_VIEWER_MARKER);
        int marked = marker && strcmp(marker, target_name) == 0;
        deadbeef_api->pl_unlock();

        if (marked) {
            // fallback is usually NULL here (the marker check runs first, so
            // a marked viewer is returned before any title fallback exists).
            // plt_unref has no NULL guard upstream and would segfault.
            if (fallback) {
                deadbeef_api->plt_unref(fallback);
            }
            return plt;
        }
        if (!marker_only && !fallback) {
            // Per-instance name (set via the config dialog, serialized into
            // the widget's keyvalues). The old global cui.autoplaylist_name
            // key was never written by the per-instance path, so reading it
            // here ignored the dialog setting and made every instance
            // collide on one "Library Viewer" playlist.
            char title[256];
            deadbeef_api->plt_get_title(plt, title, sizeof(title));
            if (strcmp(title, target_name) == 0) {
                fallback = plt; // ref held for the fallback return
                continue;
            }
        }
        deadbeef_api->plt_unref(plt);
    }
    return fallback;
}

// Marker-first lookup with legacy name fallback: what population and
// activation use. Returns a refcounted handle (caller unrefs) or NULL when no
// viewer exists yet; never creates one.
ddb_playlist_t *find_viewer_playlist(cui_widget_t *cw) {
    return find_viewer_playlist_impl(cw, 0);
}

// Marker-only lookup for the shutdown clear: a playlist is only emptied when
// the plugin itself marked it, so a user playlist that happens to share the
// viewer's name survives every quit.
ddb_playlist_t *find_marked_viewer_playlist(cui_widget_t *cw) {
    return find_viewer_playlist_impl(cw, 1);
}

// Adopt a pre-marker viewer found by title: stamp the marker so every later
// lookup (the shutdown clear in particular) matches it by marker alone.
// No-op when the marker is already correct. Called only from the population
// path — never from the shutdown clear, which must not mark anything the
// plugin did not create.
static void stamp_viewer_marker(cui_widget_t *cw, ddb_playlist_t *plt) {
    const char *ap_name = viewer_name(cw);
    deadbeef_api->pl_lock();
    const char *marker = deadbeef_api->plt_find_meta(plt, CUI_VIEWER_MARKER);
    int already = marker && strcmp(marker, ap_name) == 0;
    deadbeef_api->pl_unlock();
    if (!already) {
        deadbeef_api->plt_replace_meta(plt, CUI_VIEWER_MARKER, ap_name);
    }
}

ddb_playlist_t *get_or_create_viewer_playlist(cui_widget_t *cw) {
    ddb_playlist_t *existing = find_viewer_playlist(cw);
    if (existing) {
        stamp_viewer_marker(cw, existing);
        return existing;
    }

    const char *target_name = viewer_name(cw);

    int new_idx = deadbeef_api->plt_add(deadbeef_api->plt_get_count(), target_name);
    if (new_idx >= 0) {
        ddb_playlist_t *plt = deadbeef_api->plt_get_for_idx(new_idx);
        if (plt) {
            // Ownership marker: every later lookup — including the shutdown
            // clear — identifies the viewer by this even if the user retitles
            // the playlist, and same-named user playlists stay out of scope.
            deadbeef_api->plt_replace_meta(plt, CUI_VIEWER_MARKER, target_name);
            return plt;
        }
    }
    return NULL;
}

void populate_playlist_from_cui(cui_widget_t *cw, ddb_playlist_t *plt, int clear_first) {
    if (!cw->cached_tree || !deadbeef_api || !medialib_plugin) return;

    // A synchronous populate supersedes any in-flight chunked fill (its
    // frames would interleave inserts with this walk).
    cui_fill_cancel(cw);

    gint64 t0 = g_get_monotonic_time();
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
    CUI_DEBUG("populate_playlist_from_cui: %d tracks in %.1f ms",
              deadbeef_api->plt_get_item_count(plt, PL_MAIN),
              (g_get_monotonic_time() - t0) / 1000.0);
    deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0);
}

// ---- chunked viewer fill (CLAUDE.md §6.15) ---------------------------------
//
// update_playlist_from_cui's async mode mirrors the filtered library into the
// viewer playlist on the idle queue, fill_budget_us of work per tick, so a
// whole-library mirror (~1 s of copy on a 10k library) never freezes the UI.
//
// Cancellation points, ALL required:
//   - cui_fill_cancel at the top of every synchronous populate (supersede),
//     at every cached_tree free in update_tree_data (frames point INTO the
//     tree), in cui_destroy, and before starting any new fill;
//   - the chunk itself re-checks shutting_down and widget liveness each tick;
//   - playlist_dirty clears only at fill COMPLETION, so an aborted fill
//     leaves the dirty flag set and the next activation rebuilds.

typedef struct {
    const ddb_medialib_item_t *node;   // parent whose children we iterate
    const ddb_medialib_item_t *child;  // next child to process
    int level;                         // level of those children
} cui_fill_frame_t;

static void cui_fill_frame_free(gpointer p) {
    free(p);
}

// Tear down any in-flight fill. Safe to call when nothing is running.
void cui_fill_cancel(cui_widget_t *cw) {
    if (cw->fill_idle_id) {
        g_source_remove(cw->fill_idle_id);
        cw->fill_idle_id = 0;
    }
    if (cw->fill_stack) {
        g_ptr_array_unref(cw->fill_stack);
        cw->fill_stack = NULL;
    }
    if (cw->fill_after) {
        deadbeef_api->pl_item_unref(cw->fill_after);
        cw->fill_after = NULL;
    }
    if (cw->fill_plt) {
        deadbeef_api->plt_unref(cw->fill_plt);
        cw->fill_plt = NULL;
    }
    cw->fill_generation++;
}

static gboolean cui_fill_chunk(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    if (g_atomic_int_get(&shutting_down) || !g_list_find(all_cui_widgets, cw)
        || !cw->fill_stack) {
        cui_fill_cancel(cw);
        return G_SOURCE_REMOVE;
    }

    GPtrArray *stack = cw->fill_stack;
    gint64 deadline = g_get_monotonic_time() + cw->fill_budget_us;

    deadbeef_api->pl_lock();
    while (stack->len > 0) {
        cui_fill_frame_t *top = g_ptr_array_index(stack, stack->len - 1);
        if (!top->child) {
            // parent exhausted
            g_ptr_array_remove_index(stack, stack->len - 1);
            continue;
        }
        const ddb_medialib_item_t *node = top->child;
        top->child = medialib_plugin->tree_item_get_next(node);

        // Selection filter, mirroring add_tracks_recursive_multi: a filtered
        // node's whole subtree is pruned.
        if (top->level >= 1 && top->level <= cw->num_columns && cw->sel_texts[top->level - 1]) {
            const char *text = medialib_plugin->tree_item_get_text(node);
            if (!text || !g_hash_table_contains(cw->sel_texts[top->level - 1], text)) {
                if (cw->fill_budget_us == 0) break;
                continue;
            }
        }

        DB_playItem_t *track = medialib_plugin->tree_item_get_track(node);
        if (track && track_matches_search(track, cw->search_text)) {
            DB_playItem_t *track_new = deadbeef_api->pl_item_alloc();
            deadbeef_api->pl_item_copy(track_new, track);
            DB_playItem_t *inserted = deadbeef_api->plt_insert_item(cw->fill_plt, cw->fill_after, track_new);
            if (cw->fill_after) {
                deadbeef_api->pl_item_unref(cw->fill_after);
            }
            cw->fill_after = inserted;
            deadbeef_api->pl_item_ref(cw->fill_after);
            deadbeef_api->pl_item_unref(track_new);
            cw->fill_inserted++;
        }

        const ddb_medialib_item_t *children = medialib_plugin->tree_item_get_children(node);
        if (children) {
            cui_fill_frame_t *f = g_new(cui_fill_frame_t, 1);
            f->node = node;
            f->child = children;
            f->level = top->level + 1;
            g_ptr_array_add(stack, f);
        }

        // fill_budget_us == 0 is the deterministic test mode: one tree step
        // per chunk.
        if (cw->fill_budget_us == 0 || g_get_monotonic_time() >= deadline) break;
    }
    deadbeef_api->pl_unlock();

    if (stack->len > 0) {
        return G_SOURCE_CONTINUE;
    }

    CUI_DEBUG("chunked fill complete: %d tracks", cw->fill_inserted);
    deadbeef_api->plt_modified(cw->fill_plt);
    deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0);
    cui_fill_cancel(cw);
    cw->playlist_dirty = 0;
    return G_SOURCE_REMOVE;
}

void update_playlist_from_cui(cui_widget_t *cw, int synchronous, int make_current) {
    CUI_DEBUG("update_playlist_from_cui (sync=%d, curr=%d)", synchronous, make_current);
    gint64 t0 = g_get_monotonic_time();
    ddb_playlist_t *plt = get_or_create_viewer_playlist(cw);
    if (!plt) return;
    // Making the viewer current is reserved for interaction-driven fills
    // (facet selection, activation). Programmatic refills — the rebuild-driven
    // mirror at the end of update_tree_data — must never steal the user's
    // current playlist; v1.3.7's launch pre-fill did exactly that and switched
    // the restored current playlist on every startup.
    if (make_current) {
        deadbeef_api->plt_set_curr(plt);
    }

    if (synchronous || !cw->cached_tree) {
        populate_playlist_from_cui(cw, plt, 1);
        cw->playlist_dirty = 0;
        deadbeef_api->plt_unref(plt);
        CUI_DEBUG("update_playlist_from_cui done in %.1f ms",
                  (g_get_monotonic_time() - t0) / 1000.0);
        return;
    }

    // Chunked: clear now, then walk the tree on the idle queue. plt (ref'd
    // above) becomes the fill's target for the fill's lifetime.
    cui_fill_cancel(cw);
    deadbeef_api->plt_clear(plt);

    cw->fill_plt = plt;
    cw->fill_after = NULL;
    cw->fill_inserted = 0;
    cw->fill_stack = g_ptr_array_new_with_free_func(cui_fill_frame_free);
    cui_fill_frame_t *root = g_new(cui_fill_frame_t, 1);
    root->node = cw->cached_tree;
    root->child = medialib_plugin->tree_item_get_children(cw->cached_tree);
    root->level = 1;
    g_ptr_array_add(cw->fill_stack, root);

    cw->fill_idle_id = g_idle_add(cui_fill_chunk, cw);
    CUI_DEBUG("chunked fill started (gen %d)", cw->fill_generation);
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

    // The clear below destroys the iters of any selected rows, which fires this
    // tree's selection "changed" signal. Every populate is programmatic (a
    // library event, a search keystroke, or the cascade after a click in an
    // upstream column) — never a user selection on this column — so block the
    // handler across it. Unblocked, every rebuild armed the selection debounce,
    // and deferred_column_changed_cb then made the viewer playlist current and
    // rebuilt it ~10 ms later with no user click (the behavior the v1.2.4
    // deferral removed). Callers that already block around this call just nest.
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[col_idx]));
    g_signal_handlers_block_by_func(sel, (gpointer)on_column_changed, cw);
    gtk_list_store_clear(store);
    g_signal_handlers_unblock_by_func(sel, (gpointer)on_column_changed, cw);
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
    // Uniform two-step guard (§6.3). This idle has NO cancellation in
    // cui_destroy (nothing tracks its id), so the guards are its only
    // protection; sr is ours to free on every path.
    if (g_atomic_int_get(&shutting_down)) {
        free(sr);
        return G_SOURCE_REMOVE;
    }
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
    if (g_atomic_int_get(&shutting_down) || !medialib_plugin || !ml_source) return;

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
    // Modification-index cache. PAIRED with the assignment at the very end of
    // this function — the check and the store must stay in sync or every call
    // becomes a full rebuild (the v1.2.0 bug was losing the store half). The
    // flip side: a caller that swaps the stores/preset behind our back must
    // reset last_ml_modification_idx to -1 before calling, or this skip
    // leaves its new stores blank (the dialog-OK bug fixed in v1.3.4 and the
    // CONFIGCHANGED bug fixed in v1.3.5 were both missing resets).
    if (cw->last_ml_modification_idx == current_idx && cw->cached_tree) {
        return;
    }
    // A real rebuild past this point (library change, search change, or first
    // build) leaves the viewer playlist out of sync with the new tree. We don't
    // repopulate it here (the v1.2.4 deferral below), so flag it stale instead;
    // activate_row rebuilds on the next activation.
    cw->playlist_dirty = 1;
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
        // The fill's frames point into this tree; cancel before freeing (§6.15).
        cui_fill_cancel(cw);
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
    for (int i = populated_through + 1; i < cw->num_columns; i++) {
        populate_list_multi(cw->stores[i], i + 1, cw, i);
    }

    // Visual default: any column that ended up with no selection (because no
    // saved selection cascaded into it, or the saved selection didn't match
    // any current row) gets [All] selected so the columns look consistent.
    // Skipped for columns whose saved-selection restore actually matched.
    for (int i = 0; i < cw->num_columns; i++) {
        auto_select_all_if_empty(cw, i);
    }

    // Second half of the paired modification-index invariant (see the cache
    // check at the top): record the index the rebuild was built from so the
    // next call can skip. Removing this line turns every call into a full
    // rebuild; that regression shipped once as v1.2.0.
    cw->last_ml_modification_idx = current_idx;

    // Rebuild-driven viewer refill: a real rebuild means the library, search,
    // or column config changed under the playlist mirror, so re-mirror it now
    // — chunked (never blocks) and without stealing the current playlist.
    // This reverses the v1.2.4 deferral, whose rationale (a ~1 s synchronous
    // whole-library copy per library event) died with the chunked fill in
    // v1.3.7; after the deferral, the populated tab held stale data until a
    // facet click or relaunch. It also subsumes v1.3.7's one-shot launch
    // pre-fill: playlist_dirty starts set at widget creation, so the first
    // successful build still fills the tab at launch. Gated on
    // initial_sync_done so the empty first build while the source is still
    // loading doesn't churn the playlist; a library that later empties still
    // refills (to empty) because the flag, once set, never clears.
    if (cw->playlist_dirty && cw->initial_sync_done && cw->cached_tree) {
        update_playlist_from_cui(cw, FALSE, FALSE);
    }
}
