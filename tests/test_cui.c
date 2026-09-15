// GTest suite for the deadbeef-cui engine. Links the real cui_data.c /
// cui_widget.c / cui_scriptable.c against the fakes in mock_deadbeef.c (no
// running DeaDBeeF, no main.c). Pure-logic tests always run; the handful that
// need real GTK tree widgets are gated on a usable display and skipped headless.
//
// Run: cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure

#include "cui_globals.h"
#include "cui_data.h"
#include "cui_widget.h"
#include "cui_scriptable.h"
#include "mock_deadbeef.h"

#include <string.h>

static gboolean g_gtk_ok = FALSE;

// ---- scriptable helpers ----------------------------------------------------

static const char *find_prop(scriptableItem_t *it, const char *key) {
    for (scriptableKeyValue_t *kv = it->properties; kv; kv = kv->next) {
        if (strcmp(kv->key, key) == 0) return kv->value;
    }
    return NULL;
}

static int count_children(scriptableItem_t *it) {
    int n = 0;
    for (scriptableItem_t *c = it->children; c; c = c->next) n++;
    return n;
}

static scriptableItem_t *nth_child(scriptableItem_t *it, int n) {
    scriptableItem_t *c = it->children;
    while (c && n-- > 0) c = c->next;
    return c;
}

// A zeroed widget with just the fields a given test needs. Heap-allocated so the
// scriptable code, which g_free()s titles/formats during compaction, is happy.
static cui_widget_t *fresh_widget(void) {
    return g_new0(cui_widget_t, 1);
}

// ---- skip_prefix -----------------------------------------------------------

static void test_skip_prefix(void) {
    g_assert_cmpstr(skip_prefix("The Beatles", 1), ==, "Beatles");
    g_assert_cmpstr(skip_prefix("A Perfect Circle", 1), ==, "Perfect Circle");
    g_assert_cmpstr(skip_prefix("An Apple", 1), ==, "Apple");
    // "The"/"A"/"An" only strip when followed by the space — no false positives.
    g_assert_cmpstr(skip_prefix("Theory", 1), ==, "Theory");
    g_assert_cmpstr(skip_prefix("Anthrax", 1), ==, "Anthrax");
    // Case-insensitive.
    g_assert_cmpstr(skip_prefix("THE Who", 1), ==, "Who");
    // Disabled / NULL.
    g_assert_cmpstr(skip_prefix("The Beatles", 0), ==, "The Beatles");
    g_assert_null(skip_prefix(NULL, 1));
}

// ---- scriptable preset construction ---------------------------------------

static void test_scriptable_default(void) {
    cui_widget_t *cw = fresh_widget();
    init_my_preset(cw);

    g_assert_cmpint(cw->num_columns, ==, 3);
    g_assert_cmpstr(cw->titles[0], ==, "Genre");
    g_assert_cmpstr(cw->titles[1], ==, "Album Artist");
    g_assert_cmpstr(cw->titles[2], ==, "Album");

    scriptableItem_t *root = (scriptableItem_t *)cw->my_preset;
    g_assert_true((root->flags & SCRIPTABLE_FLAG_IS_LIST) != 0);
    g_assert_cmpstr(find_prop(root, "name"), ==, "Facets");
    // 3 facet children + the trailing %title% leaf.
    g_assert_cmpint(count_children(root), ==, 4);
    g_assert_cmpstr(find_prop(nth_child(root, 0), "name"), ==, "%genre%");
    g_assert_cmpstr(find_prop(nth_child(root, 3), "name"), ==, "%title%");

    my_scriptable_free((scriptableItem_t *)cw->my_preset);
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw->titles[i]); g_free(cw->formats[i]); }
    g_free(cw);
}

static void test_scriptable_compaction(void) {
    cui_widget_t *cw = fresh_widget();
    // A gap in the middle (empty col 2) must be compacted away, not left as a hole.
    cw->titles[0] = g_strdup("Genre");  cw->formats[0] = g_strdup("%genre%");
    cw->titles[1] = g_strdup("");        cw->formats[1] = g_strdup("");
    cw->titles[2] = g_strdup("Album");  cw->formats[2] = g_strdup("%album%");
    init_my_preset(cw);

    g_assert_cmpint(cw->num_columns, ==, 2);
    g_assert_cmpstr(cw->formats[0], ==, "%genre%");
    g_assert_cmpstr(cw->formats[1], ==, "%album%");

    scriptableItem_t *root = (scriptableItem_t *)cw->my_preset;
    g_assert_cmpint(count_children(root), ==, 3); // 2 facets + leaf
    g_assert_cmpstr(find_prop(nth_child(root, 2), "name"), ==, "%title%");

    my_scriptable_free((scriptableItem_t *)cw->my_preset);
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw->titles[i]); g_free(cw->formats[i]); }
    g_free(cw);
}

static void test_scriptable_split(void) {
    cui_widget_t *cw = fresh_widget();
    cw->titles[0] = g_strdup("Genre"); cw->formats[0] = g_strdup("%genre%");
    cw->split_tags = 1;
    init_my_preset(cw);
    g_assert_cmpstr(find_prop(nth_child((scriptableItem_t *)cw->my_preset, 0), "split"), ==, "; ");
    my_scriptable_free((scriptableItem_t *)cw->my_preset);
    cw->my_preset = NULL;
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw->titles[i]); g_free(cw->formats[i]); cw->titles[i] = cw->formats[i] = NULL; }

    cw->titles[0] = g_strdup("Genre"); cw->formats[0] = g_strdup("%genre%");
    cw->split_tags = 0;
    init_my_preset(cw);
    g_assert_null(find_prop(nth_child((scriptableItem_t *)cw->my_preset, 0), "split"));

    my_scriptable_free((scriptableItem_t *)cw->my_preset);
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw->titles[i]); g_free(cw->formats[i]); }
    g_free(cw);
}

// ---- search matching -------------------------------------------------------

static void test_search_match(void) {
    mock_track_t t = { .title = "Hello World", .artist = "Adele" };
    DB_playItem_t *it = (DB_playItem_t *)&t;

    g_assert_cmpint(track_matches_search(it, NULL), ==, 1); // no filter
    g_assert_cmpint(track_matches_search(it, ""), ==, 1);
    g_assert_cmpint(track_matches_search(it, "hello"), ==, 1);   // case-insensitive title
    g_assert_cmpint(track_matches_search(it, "ADELE"), ==, 1);   // case-insensitive artist
    g_assert_cmpint(track_matches_search(it, "wor"), ==, 1);     // substring
    g_assert_cmpint(track_matches_search(it, "zzz"), ==, 0);     // no match

    mock_track_t empty = { .title = NULL, .artist = NULL };
    g_assert_cmpint(track_matches_search((DB_playItem_t *)&empty, "x"), ==, 0);
}

// ---- recursive track counting + cache off-by-one ---------------------------

static void test_count_recursive(void) {
    cui_widget_t *cw = fresh_widget();
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    mock_node_t *rock = mock_group("Rock",
        mock_leaf("t1", "A", mock_leaf("t2", "B", NULL)), NULL);
    mock_node_t *pop = mock_group("Pop", mock_leaf("t3", "C", NULL), NULL);

    g_assert_cmpint(count_tracks_recursive((ddb_medialib_item_t *)rock, cw), ==, 2);
    g_assert_cmpint(count_tracks_recursive((ddb_medialib_item_t *)pop, cw), ==, 1);
    // Cached second call is stable.
    g_assert_cmpint(count_tracks_recursive((ddb_medialib_item_t *)rock, cw), ==, 2);

    g_hash_table_destroy(cw->track_counts_cache);
    mock_node_free(rock);
    mock_node_free(pop);
    g_free(cw);
}

static void test_count_cache_zero(void) {
    // A subtree with zero matches under the active search must cache as a real 0
    // (the count+1 / cached-1 trick), not register as "uncached" forever.
    cui_widget_t *cw = fresh_widget();
    cw->search_text = g_strdup("no-such-title");
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    mock_node_t *pop = mock_group("Pop", mock_leaf("t3", "C", NULL), NULL);

    g_assert_cmpint(count_tracks_recursive((ddb_medialib_item_t *)pop, cw), ==, 0);
    // The zero result is now memoized: the cache holds an entry for the node.
    g_assert_true(g_hash_table_size(cw->track_counts_cache) > 0);
    g_assert_cmpint(count_tracks_recursive((ddb_medialib_item_t *)pop, cw), ==, 0);

    g_hash_table_destroy(cw->track_counts_cache);
    g_free(cw->search_text);
    mock_node_free(pop);
    g_free(cw);
}

// ---- aggregation across the tree ("Various Artists" collision) -------------

static void test_aggregate_va_collision(void) {
    cui_widget_t *cw = fresh_widget();
    cw->num_columns = 3;
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    // genre -> artist -> track. Artist "A" appears under two genres and must
    // aggregate to a single row with the summed count.
    mock_node_t *rock = mock_group("Rock",
        mock_group("A", mock_leaf("r1", "A", NULL),
        mock_group("B", mock_leaf("r2", "B", NULL), NULL)), NULL);
    mock_node_t *pop = mock_group("Pop",
        mock_group("A", mock_leaf("p1", "A", NULL), NULL), NULL);
    rock->next = pop;

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (mock_node_t *g = rock; g; g = g->next) {
        aggregate_recursive_multi((ddb_medialib_item_t *)g, 1, 2, cw, seen);
    }

    g_assert_cmpint(g_hash_table_size(seen), ==, 2);
    int *a = g_hash_table_lookup(seen, "A");
    int *b = g_hash_table_lookup(seen, "B");
    g_assert_nonnull(a); g_assert_cmpint(*a, ==, 2);
    g_assert_nonnull(b); g_assert_cmpint(*b, ==, 1);

    g_hash_table_destroy(seen);
    g_hash_table_destroy(cw->track_counts_cache);
    mock_node_free(rock); // frees pop via ->next
    g_free(cw);
}

// ---- fix #1: viewer playlist follows the per-instance autoplaylist name -----

static void test_autoplaylist_name(void) {
    cui_widget_t *cw = fresh_widget();

    cw->autoplaylist_name = g_strdup("My Custom List");
    mock_reset();
    get_or_create_viewer_playlist(cw);
    g_assert_cmpint(mock_plt_add_called, ==, 1);
    g_assert_cmpstr(mock_last_plt_add_title, ==, "My Custom List");

    g_free(cw->autoplaylist_name);
    cw->autoplaylist_name = NULL;
    mock_reset();
    get_or_create_viewer_playlist(cw);
    g_assert_cmpstr(mock_last_plt_add_title, ==, "Library Viewer");

    cw->autoplaylist_name = g_strdup("");
    mock_reset();
    get_or_create_viewer_playlist(cw);
    g_assert_cmpstr(mock_last_plt_add_title, ==, "Library Viewer");

    g_free(cw->autoplaylist_name);
    g_free(cw);
}

// ---- fix: the viewer playlist is identified by a hidden marker, not name ---
//
// find_viewer_playlist used to match by title alone, so a user playlist that
// happened to be named "Library Viewer" was adopted as the viewer, populated
// over, and emptied by the shutdown clear on every quit (data loss). The
// plugin now stamps a hidden _cui_viewer meta (value = the viewer name) on
// playlists it creates and on legacy viewers found by title; the shutdown
// clear matches by marker ONLY, so a same-named user playlist is out of its
// reach. All four tests are headless: no GTK widget is involved.

static void test_viewer_marker_on_create(void) {
    cui_widget_t *cw = fresh_widget();
    cw->autoplaylist_name = g_strdup("My List");
    mock_reset();
    ddb_playlist_t *plt = get_or_create_viewer_playlist(cw);
    g_assert_nonnull(plt);
    deadbeef_api->pl_lock();
    const char *marker = deadbeef_api->plt_find_meta(plt, CUI_VIEWER_MARKER);
    g_assert_cmpstr(marker, ==, "My List");
    deadbeef_api->pl_unlock();
    g_assert_cmpint(mock_plt_add_called, ==, 1);

    g_free(cw->autoplaylist_name);
    g_free(cw);
}

static void test_viewer_collision_not_cleared(void) {
    cui_widget_t *cw = fresh_widget();
    cw->autoplaylist_name = g_strdup("Library Viewer");
    mock_reset();
    // The user's own playlist with the colliding title, no marker...
    int user_idx = deadbeef_api->plt_add(0, "Library Viewer");
    // ...and our marked viewer, which the user happened to retitle.
    int ours_idx = deadbeef_api->plt_add(1, "Renamed by user");
    deadbeef_api->plt_replace_meta(deadbeef_api->plt_get_for_idx(ours_idx),
                                   CUI_VIEWER_MARKER, "Library Viewer");

    // The shutdown clear must empty ours and never touch the user's.
    all_cui_widgets = g_list_append(all_cui_widgets, cw);
    cui_clear_viewer_playlists();
    all_cui_widgets = g_list_remove(all_cui_widgets, cw);
    g_assert_cmpint(mock_plt_clear_called, ==, 1);
    g_assert_true(mock_plt_was_cleared(ours_idx));
    g_assert_false(mock_plt_was_cleared(user_idx));

    g_free(cw->autoplaylist_name);
    g_free(cw);
}

static void test_viewer_marker_first_matching(void) {
    cui_widget_t *cw = fresh_widget();
    cw->autoplaylist_name = g_strdup("Library Viewer");
    mock_reset();
    // Title collision (the user's playlist) plus our marker-matched playlist
    // under a different title: marker-first matching must adopt ours.
    deadbeef_api->plt_add(0, "Library Viewer");
    int ours_idx = deadbeef_api->plt_add(1, "Whatever");
    deadbeef_api->plt_replace_meta(deadbeef_api->plt_get_for_idx(ours_idx),
                                   CUI_VIEWER_MARKER, "Library Viewer");
    int adds_after_setup = mock_plt_add_called;

    ddb_playlist_t *plt = get_or_create_viewer_playlist(cw);
    g_assert_true(plt == deadbeef_api->plt_get_for_idx(ours_idx));
    g_assert_cmpint(mock_plt_add_called, ==, adds_after_setup);  // nothing new created

    g_free(cw->autoplaylist_name);
    g_free(cw);
}

static void test_viewer_legacy_stamped(void) {
    cui_widget_t *cw = fresh_widget();
    cw->autoplaylist_name = g_strdup("Library Viewer");
    mock_reset();
    // A pre-marker viewer (title match only) keeps working and gains the
    // marker on first find, so the shutdown clear can match it from then on.
    int legacy_idx = deadbeef_api->plt_add(0, "Library Viewer");
    int adds_after_setup = mock_plt_add_called;

    ddb_playlist_t *plt = get_or_create_viewer_playlist(cw);
    g_assert_true(plt == deadbeef_api->plt_get_for_idx(legacy_idx));
    g_assert_cmpint(mock_plt_add_called, ==, adds_after_setup);
    deadbeef_api->pl_lock();
    const char *marker = deadbeef_api->plt_find_meta(plt, CUI_VIEWER_MARKER);
    g_assert_cmpstr(marker, ==, "Library Viewer");
    deadbeef_api->pl_unlock();

    g_free(cw->autoplaylist_name);
    g_free(cw);
}

// ---- issue #1: the plugin never calls w_save_layout_to_conf_key ------------
//
// The Configure Facets OK handler used to call
// gtkui_plugin->w_save_layout_to_conf_key("layout", NULL) — NULL where the
// contract requires a widget pointer — segfaulting inside GTKUI's layout
// serializer on every DeaDBeeF 1.10.1+. The call was deleted in v1.3.4 (it
// never worked and was redundant: per-instance settings persist through the
// extended API when GTKUI saves the layout). The mock's stub hard-fails on a
// NULL val, and this test locks in "the plugin never calls it at all" across
// the engine paths the suite can reach. Honest limit: the dialog handler
// itself digs a GtkGrid out of a live GtkDialog and is not callable headless;
// that path is covered by the real-player smoke test, not here.

static void exercise_engine_paths(void) {
    cui_widget_t *cw = fresh_widget();
    init_my_preset(cw);
    g_assert_nonnull(cw->my_preset);

    mock_node_t *rock = mock_group("Rock",
        mock_leaf("t1", "A", mock_leaf("t2", "B", NULL)), NULL);
    cw->track_counts_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_assert_cmpint(count_tracks_recursive((ddb_medialib_item_t *)rock, cw), ==, 2);

    cw->autoplaylist_name = g_strdup("Viewer");
    get_or_create_viewer_playlist(cw);

    my_scriptable_free((scriptableItem_t *)cw->my_preset);
    // init_my_preset's default fallback g_strups the titles/formats onto cw.
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw->titles[i]); g_free(cw->formats[i]); }
    g_hash_table_destroy(cw->track_counts_cache);
    g_free(cw->autoplaylist_name);
    g_free(cw);
    mock_node_free(rock);
}

static void test_config_never_saves_layout(void) {
    mock_reset();

    // gtkui 2.6 (DeaDBeeF 1.10.1+): the API level where the deleted call
    // crashed. Exercise the reachable engine paths and assert the layout-save
    // stub is never touched.
    mock_gtkui_set_api_version(2, 6);
    exercise_engine_paths();
    g_assert_cmpint(mock_w_save_layout_called, ==, 0);

    // gtkui 2.5 (DeaDBeeF <= 1.10.0): the member is absent on those runtimes;
    // nothing may call it there either.
    mock_gtkui_set_api_version(2, 5);
    exercise_engine_paths();
    g_assert_cmpint(mock_w_save_layout_called, ==, 0);

    mock_gtkui_set_api_version(2, 6);
}

// ---- sort_func: [All] is pinned to the top in every sort order -------------
//
// sort_func reads the active sort order and special-cases the is_all row so it
// counteracts GtkListStore's descending-order negation of the comparator. Net
// effect: the synthetic [All] row sits at iter 0 regardless of sort column or
// direction — which is what auto_select_all_if_empty relies on. This test locks
// that behavior in (and would fail if the order-aware pinning were "simplified"
// out, leaving [All] to sink to the bottom on descending sorts).

static GtkListStore *build_sorted_store(cui_widget_t *cw, gint col, GtkSortType order) {
    GtkListStore *store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 0, sort_func, cw, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 1, sort_func, cw, NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), col, order);
    GtkTreeIter it;
    gtk_list_store_insert_with_values(store, &it, -1, 0, "[All (2)]", 1, 99, 2, TRUE,  -1);
    gtk_list_store_insert_with_values(store, &it, -1, 0, "Zappa",     1,  5, 2, FALSE, -1);
    gtk_list_store_insert_with_values(store, &it, -1, 0, "Beatles",   1, 50, 2, FALSE, -1);
    return store;
}

static gboolean row_is_all(GtkTreeModel *m, int idx) {
    GtkTreeIter it;
    if (!gtk_tree_model_iter_nth_child(m, &it, NULL, idx)) return FALSE;
    gboolean is_all = FALSE;
    gtk_tree_model_get(m, &it, 2, &is_all, -1);
    return is_all;
}

static void assert_all_pinned_top(cui_widget_t *cw, gint col, GtkSortType order) {
    GtkListStore *store = build_sorted_store(cw, col, order);
    g_assert_true(row_is_all(GTK_TREE_MODEL(store), 0));
    g_object_unref(store);
}

static void test_sort_all_row(void) {
    if (!g_gtk_ok) { g_test_skip("no display for GtkListStore"); return; }
    cui_widget_t *cw = fresh_widget();
    cw->ignore_prefix = 0;

    assert_all_pinned_top(cw, 0, GTK_SORT_ASCENDING);   // by name, asc
    assert_all_pinned_top(cw, 0, GTK_SORT_DESCENDING);  // by name, desc
    assert_all_pinned_top(cw, 1, GTK_SORT_ASCENDING);   // by count, asc
    assert_all_pinned_top(cw, 1, GTK_SORT_DESCENDING);  // by count, desc

    g_free(cw);
}

// ---- fix: programmatic store clears must not arm the selection debounce ----
//
// populate_list_multi's gtk_list_store_clear destroys the iters of any selected
// rows, which fires the tree's selection "changed" signal. update_tree_data
// populates through that clear on every library event and search keystroke;
// with the handler unblocked, each of those events armed the 10 ms selection
// debounce, and deferred_column_changed_cb then made the viewer playlist
// current and rebuilt it — silently stealing the user's playlist with no click
// (the behavior the v1.2.4 deferral removed). This test locks in that a
// programmatic repopulate never re-arms the debounce.

static void test_populate_does_not_steal_selection(void) {
    if (!g_gtk_ok) { g_test_skip("no display for GtkTreeView"); return; }
    cui_widget_t *cw = fresh_widget();
    cw->changed_col_idx = -1;
    cw->num_columns = 1;
    // populate_list_multi reads titles[col_idx] for the [All (...)] label;
    // every real caller runs with titles set by init_my_preset.
    cw->titles[0] = g_strdup("Genre");
    cw->stores[0] = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    cw->trees[0] = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cw->stores[0]));
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[0]));
    g_signal_connect(sel, "changed", G_CALLBACK(on_column_changed), cw);

    // A real (mock) library tree so the populate has rows to build.
    mock_node_t *tree = mock_group(NULL,
        mock_group("Rock", mock_leaf("t1", "A", NULL), NULL), NULL);
    cw->cached_tree = (ddb_medialib_item_t *)tree;

    // Simulate the legitimate user click that precedes a cascade repopulate:
    // select a row (fires the handler, arms the debounce), then consume the
    // armed debounce exactly like activate_row does.
    GtkTreeIter it;
    gtk_list_store_insert_with_values(cw->stores[0], &it, -1, 0, "Rock", 1, 1, 2, FALSE, -1);
    gtk_tree_selection_select_iter(sel, &it);
    g_assert_cmpint(cw->changed_col_idx, ==, 0);
    g_assert_cmpint(cw->changed_timeout_id, !=, 0);
    g_source_remove(cw->changed_timeout_id);
    cw->changed_timeout_id = 0;
    cw->changed_col_idx = -1;

    // The programmatic repopulate (cascade / library event / search keystroke)
    // clears the selected row's store. It must not re-arm anything.
    populate_list_multi(cw->stores[0], 1, cw, 0);
    g_assert_cmpint(cw->changed_col_idx, ==, -1);
    g_assert_cmpint(cw->changed_timeout_id, ==, 0);

    // ...and the store was really rebuilt: [All] at iter 0 plus the row.
    GtkTreeModel *m = GTK_TREE_MODEL(cw->stores[0]);
    g_assert_cmpint(gtk_tree_model_iter_n_children(m, NULL), ==, 2);

    g_signal_handlers_disconnect_by_func(sel, (gpointer)on_column_changed, cw);
    cw->cached_tree = NULL;
    g_object_unref(cw->trees[0]);
    g_object_unref(cw->stores[0]);
    g_free(cw->titles[0]);
    mock_node_free(tree);
    g_free(cw);
}

// ---- fix: the CONFIGCHANGED refill must invalidate the modification cache --
//
// cui_handle_config_change (playlist-font change) and the config-dialog OK
// handler both replace the column stores underneath the engine and then rely
// on update_tree_data to refill them. update_tree_data early-returns when
// last_ml_modification_idx matches the library's current index and a tree is
// cached — correct for pure library events, wrong for a caller that just
// swapped the stores. Both callers must reset the index to -1 first (the
// dialog OK path since v1.3.4, CONFIGCHANGED since v1.3.5). This test locks
// the contract those callers depend on: unchanged index skips, -1 rebuilds.
// The CONFIGCHANGED call site itself drives a live widget hierarchy
// (rebuild_columns), so it is covered by the real-player smoke test, not here.

static void test_modification_index_invalidation(void) {
    if (!g_gtk_ok) { g_test_skip("no display for GtkTreeView"); return; }
    cui_widget_t *cw = fresh_widget();
    cw->changed_col_idx = -1;
    cw->titles[0] = g_strdup("Genre");
    cw->formats[0] = g_strdup("%genre%");
    init_my_preset(cw);
    g_assert_cmpint(cw->num_columns, ==, 1);

    cw->stores[0] = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    cw->trees[0] = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cw->stores[0]));
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[0]));
    g_signal_connect(sel, "changed", G_CALLBACK(on_column_changed), cw);

    mock_node_t *tree = mock_group(NULL,
        mock_group("Rock", mock_leaf("t1", "A", mock_leaf("t2", "B", NULL)), NULL), NULL);
    mock_set_item_tree(tree);
    ml_source = (ddb_mediasource_source_t *)tree;  // any non-NULL handle

    // First build (cache cold: last=0 from the zeroed struct, library idx=1).
    update_tree_data(cw);
    GtkTreeModel *m = GTK_TREE_MODEL(cw->stores[0]);
    g_assert_cmpint(gtk_tree_model_iter_n_children(m, NULL), ==, 2); // [All] + Rock
    g_assert_cmpint(cw->last_ml_modification_idx, ==, g_atomic_int_get(&ml_modification_idx));

    // The short-circuit: same index + cached tree leaves an emptied store
    // empty. This is exactly why both store-replacing callers must invalidate.
    gtk_list_store_clear(cw->stores[0]);
    cw->changed_col_idx = -1;  // the clear (legitimately) fired the handler
    if (cw->changed_timeout_id) {
        g_source_remove(cw->changed_timeout_id);
        cw->changed_timeout_id = 0;
    }
    update_tree_data(cw);
    g_assert_cmpint(gtk_tree_model_iter_n_children(m, NULL), ==, 0);

    // The invalidation contract: -1 forces the real rebuild the caller's new
    // stores need.
    cw->last_ml_modification_idx = -1;
    update_tree_data(cw);
    g_assert_cmpint(gtk_tree_model_iter_n_children(m, NULL), ==, 2);

    g_signal_handlers_disconnect_by_func(sel, (gpointer)on_column_changed, cw);
    cw->cached_tree = NULL;
    ml_source = NULL;
    mock_set_item_tree(NULL);
    g_object_unref(cw->trees[0]);
    g_object_unref(cw->stores[0]);
    my_scriptable_free((scriptableItem_t *)cw->my_preset);
    g_hash_table_destroy(cw->track_counts_cache);
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw->titles[i]); g_free(cw->formats[i]); }
    mock_node_free(tree);
    g_free(cw);
}

// ---- fix: menu item activation survives the autodestroy teardown -----------
//
// The right-click menu is destroyed on close so dismissed menus don't leak.
// Destroying it synchronously on "deactivate" broke every menu item: GTK
// emits deactivate BEFORE the activated item's "activate" emission completes
// (gtk_menu_shell_activate_item pops the shell down first), so the items were
// freed mid-activation and nothing on the menu worked (caught in the v1.3.5
// live smoke). The teardown is now deferred to the idle; this test drives the
// real gtk_menu_shell_activate_item sequence and requires the item's
// activate handler to fire exactly once, with the menu finalized afterwards
// (no leak).

static int g_menu_activations = 0;
static void on_test_item_activate(GtkMenuItem *item, gpointer data) {
    (void)item; (void)data;
    g_menu_activations++;
}

static void test_menu_activation_survives_teardown(void) {
    if (!g_gtk_ok) { g_test_skip("no display for GtkMenu"); return; }
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item = gtk_menu_item_new_with_label("Tripwire");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_test_item_activate), NULL);
    gtk_widget_show_all(menu);
    gtk_widget_realize(menu);

    // The ref_sink + idle teardown ends with destroy + unref: when both land,
    // the object finalizes and the weak pointer goes NULL.
    gpointer weak = menu;
    g_object_add_weak_pointer(G_OBJECT(menu), &weak);
    cui_setup_menu_autodestroy(menu);

    g_menu_activations = 0;
    // A never-popped-up shell is not "active", so activate_item's internal
    // deactivate would be a no-op here; emit it first to mirror the real
    // popdown that precedes item activation on screen.
    g_signal_emit_by_name(menu, "deactivate");
    gtk_menu_shell_activate_item(GTK_MENU_SHELL(menu), item, TRUE);
    g_assert_cmpint(g_menu_activations, ==, 1);

    // Run the main context so the deferred teardown actually runs.
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    g_assert_null(weak); // destroyed AND finalized: no leaked menu shell
}

// ---- feature: in-widget empty-state hint ------------------------------------
//
// The only medialib-missing diagnostic used to be a stderr line: with the
// medialib plugin disabled (or an empty library), the widget rendered a
// silently blank layout. cui_update_hint now shows a transient status line
// for exactly those states and hides it once real rows exist. GTK-gated: it
// drives a real GtkLabel.

static void assert_hint_state(cui_widget_t *cw, const char *needle) {
    if (needle) {
        g_assert_true(gtk_widget_get_visible(cw->hint_label));
        g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(cw->hint_label)), needle));
    } else {
        g_assert_false(gtk_widget_get_visible(cw->hint_label));
    }
}

static void test_empty_state_hint(void) {
    if (!g_gtk_ok) { g_test_skip("no display for GtkLabel"); return; }
    cui_widget_t *cw = fresh_widget();
    cw->hint_label = gtk_label_new(NULL);
    // Never parented here (rebuild_columns normally sinks it into the vbox),
    // so take and drop our own ref around the test.
    g_object_ref_sink(cw->hint_label);
    gtk_widget_set_no_show_all(cw->hint_label, TRUE);

    // (1) medialib plugin absent.
    DB_mediasource_t *saved_ml = medialib_plugin;
    medialib_plugin = NULL;
    cui_update_hint(cw);
    assert_hint_state(cw, "medialib plugin is disabled");
    medialib_plugin = saved_ml;

    // (2) plugin present, source unavailable.
    ml_source = NULL;
    cui_update_hint(cw);
    assert_hint_state(cw, "source is unavailable");

    // (3) source present, empty tree, scanner idle: no folders configured.
    mock_node_t *empty = mock_group(NULL, NULL, NULL);
    ml_source = (ddb_mediasource_source_t *)empty;
    cw->cached_tree = (ddb_medialib_item_t *)empty;
    mock_scanner_state = 0; // IDLE
    cui_update_hint(cw);
    assert_hint_state(cw, "No music folders");

    // (4) same, scanner working: scanning message.
    mock_scanner_state = 2; // SCANNING
    cui_update_hint(cw);
    assert_hint_state(cw, "Scanning");

    // (5) tree has real children: hidden.
    mock_node_t *full = mock_group(NULL,
        mock_group("Rock", mock_leaf("t1", "A", NULL), NULL), NULL);
    cw->cached_tree = (ddb_medialib_item_t *)full;
    mock_scanner_state = 0;
    cui_update_hint(cw);
    assert_hint_state(cw, NULL);

    cw->cached_tree = NULL;
    ml_source = NULL;
    gtk_widget_destroy(cw->hint_label);
    g_object_unref(cw->hint_label);
    mock_node_free(empty);
    mock_node_free(full);
    g_free(cw);
}

// ---- feature: per-column sort persistence (colN_sort) ----------------------

static void test_sort_persistence_roundtrip(void) {
    // Serialize writes colN_sort from the tracked fields; deserialize parses
    // them back, rejecting out-of-range values. Headless: the keyvalue paths
    // touch no widgets (applying the sort to a live store rides the desktop
    // suite like the other widget-dependent paths).
    cui_widget_t *cw = fresh_widget();
    cw->titles[0] = g_strdup("Genre");
    cw->formats[0] = g_strdup("%genre%");
    cw->autoplaylist_name = g_strdup("V");
    cw->sort_ids[0] = 1;    // count
    cw->sort_orders[0] = 1; // descending
    cw->sort_ids[2] = 1;
    cw->sort_orders[2] = 0; // count, ascending

    const char **kv = cui_serialize_to_keyvalues((ddb_gtkui_widget_t *)cw);
    const char *saved_sort1 = NULL;
    const char *saved_sort3 = NULL;
    for (int i = 0; kv[i]; i += 2) {
        if (strcmp(kv[i], "col1_sort") == 0) saved_sort1 = kv[i + 1];
        if (strcmp(kv[i], "col3_sort") == 0) saved_sort3 = kv[i + 1];
    }
    g_assert_cmpstr(saved_sort1, ==, "1:1");
    g_assert_cmpstr(saved_sort3, ==, "1:0");
    cui_free_serialized_keyvalues((ddb_gtkui_widget_t *)cw, kv);

    // Round-trip into a fresh widget, plus one malformed key that must be
    // ignored (fields stay at the name/ascending default).
    const char *handmade[] = {
        "col1_format", "%genre%",       // satisfies the found_any guard
        "col1_sort",   "1:1",
        "col2_sort",   "9:9",           // invalid: rejected
        "col3_sort",   "garbage",       // invalid: rejected
        NULL, NULL,
    };
    cui_widget_t *cw2 = fresh_widget();
    cui_deserialize_from_keyvalues((ddb_gtkui_widget_t *)cw2, handmade);
    g_assert_cmpint(cw2->sort_ids[0], ==, 1);
    g_assert_cmpint(cw2->sort_orders[0], ==, 1);
    g_assert_cmpint(cw2->sort_ids[1], ==, 0);
    g_assert_cmpint(cw2->sort_orders[1], ==, 0);
    g_assert_cmpint(cw2->sort_ids[2], ==, 0);
    g_assert_cmpint(cw2->sort_orders[2], ==, 0);

    // deserialize g_strup'd the format value onto cw2.
    for (int i = 0; i < MAX_COLUMNS; i++) { g_free(cw2->titles[i]); g_free(cw2->formats[i]); }
    g_free(cw2);

    g_free(cw->titles[0]);
    g_free(cw->formats[0]);
    g_free(cw->autoplaylist_name);
    g_free(cw);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    // g_test_init promotes warnings to fatal, which aborts the whole suite at
    // gtk_init_check on any machine whose GTK theme carries a CSS property the
    // GTK3 parser rejects (Kanagawa-Dark-Dragon ships border-spacing, a GTK4
    // property). Theme parse noise is not a test failure: keep criticals fatal
    // (they mean a NULL/bad call in the code under test) and let warnings print.
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_gtk_ok = gtk_init_check(&argc, &argv);
    mock_deadbeef_install();

    g_test_add_func("/cui/skip_prefix", test_skip_prefix);
    g_test_add_func("/cui/scriptable/default", test_scriptable_default);
    g_test_add_func("/cui/scriptable/compaction", test_scriptable_compaction);
    g_test_add_func("/cui/scriptable/split", test_scriptable_split);
    g_test_add_func("/cui/search/match", test_search_match);
    g_test_add_func("/cui/count/recursive", test_count_recursive);
    g_test_add_func("/cui/count/cache_zero", test_count_cache_zero);
    g_test_add_func("/cui/aggregate/va_collision", test_aggregate_va_collision);
    g_test_add_func("/cui/autoplaylist/name", test_autoplaylist_name);
    g_test_add_func("/cui/viewer/marker_on_create", test_viewer_marker_on_create);
    g_test_add_func("/cui/viewer/collision_not_cleared", test_viewer_collision_not_cleared);
    g_test_add_func("/cui/viewer/marker_first_matching", test_viewer_marker_first_matching);
    g_test_add_func("/cui/viewer/legacy_stamped", test_viewer_legacy_stamped);
    g_test_add_func("/cui/config/save_layout", test_config_never_saves_layout);
    g_test_add_func("/cui/sort/all_row", test_sort_all_row);
    g_test_add_func("/cui/populate/no_selection_steal", test_populate_does_not_steal_selection);
    g_test_add_func("/cui/update/modification_index_invalidation", test_modification_index_invalidation);
    g_test_add_func("/cui/sort/persistence", test_sort_persistence_roundtrip);
    g_test_add_func("/cui/hint/empty_state", test_empty_state_hint);
    g_test_add_func("/cui/menu/activation_survives_teardown", test_menu_activation_survives_teardown);

    return g_test_run();
}
