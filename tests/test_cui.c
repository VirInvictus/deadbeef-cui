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

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
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
    g_test_add_func("/cui/sort/all_row", test_sort_all_row);

    return g_test_run();
}
