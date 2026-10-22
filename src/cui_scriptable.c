#include "cui_scriptable.h"

#include <time.h>

scriptableItem_t *my_scriptable_alloc(void) {
    return calloc(1, sizeof(scriptableItem_t));
}

void my_scriptable_free(scriptableItem_t *item) {
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

void my_scriptable_set_prop(scriptableItem_t *item, const char *key, const char *value) {
    scriptableKeyValue_t *kv = calloc(1, sizeof(scriptableKeyValue_t));
    kv->key = strdup(key);
    kv->value = strdup(value);
    kv->next = item->properties;
    item->properties = kv;
}

void my_scriptable_add_child(scriptableItem_t *parent, scriptableItem_t *child) {
    if (parent->childrenTail) {
        parent->childrenTail->next = child;
    } else {
        parent->children = child;
    }
    parent->childrenTail = child;
    child->parent = parent;
}

// A facet column whose format is exactly %rating% renders as stars. Medialib
// groups tracks by the evaluated title format (medialibtree.c tf_evals each
// level's `name` against every track), so the star text must be produced by
// the preset expression itself: rewriting at display time would leave the
// tree grouped under the raw numbers. $ifequal compares with atoi (tf.c), and
// an absent rating evaluates to an empty string (the public tf_eval passes
// fail_on_undef=0), i.e. 0 — everything that is not 1..5 lands on Unrated.
// The stored colN_format keeps the user's %rating%; this rewrite happens only
// at preset build. Composite formats that merely contain %rating% stay
// verbatim. Locked in by /cui/scriptable/rating_column.
static const char *rating_display_tf =
    "$ifequal(%rating%,1,★,"
    "$ifequal(%rating%,2,★★,"
    "$ifequal(%rating%,3,★★★,"
    "$ifequal(%rating%,4,★★★★,"
    "$ifequal(%rating%,5,★★★★★,"
    "Unrated)))))";

static int column_format_is_rating(const char *fmt) {
    if (!fmt) return 0;
    char *trimmed = g_strdup(fmt);
    g_strstrip(trimmed);
    int is_rating = strcmp(trimmed, "%rating%") == 0;
    g_free(trimmed);
    return is_rating;
}

// The recency facet (CUI_RECENT_TOKEN) expands to this expression at preset
// build. DeaDBeeF 1.10.3 has no %added% field and no clock function in tf, so
// the cutoffs are injected here: :MEDIALIB_SCAN_TIME is the unix-seconds stamp
// the medialib scanner writes when a file's current contents are first read
// (retags re-stamp, which is intake-relevant), and $ifgreater compares
// numerically via atoi (tf.c). Kept a pure function of (days, now) so the
// suite can assert exact cutoffs. days is clamped 1..365.
//
// LABELS MUST NEVER CONTAIN PARENTHESES (or any other bracket): tf's argument
// parser closes an argument list at the FIRST ')' with no nesting awareness
// (tf.c tf_compile_func), so a literal paren inside an argument truncates the
// function and the whole expression fails to evaluate — and since medialib
// concatenates every level's expression into one path string for grouping
// (medialibtree.c _create_tf_tree), one broken level collapses the ENTIRE
// tree into single-value columns. Nested $func() calls are fine: their ')'
// unwinds the naive LIFO correctly, which is exactly why the %rating% star
// chain works. Caught live 2026-10-22 ("New (1d)" blanked every facet).
void cui_build_recent_expression(int days, gint64 now, char *out, size_t size) {
    if (days < 1) days = 1;
    if (days > 365) days = 365;
    gint64 recent_cut = now - (gint64)days * 86400;
    gint64 month_cut = now - (gint64)30 * 86400;
    snprintf(out, size,
             "$ifgreater(%%:MEDIALIB_SCAN_TIME%%,%lld,New %dd,"
             "$ifgreater(%%:MEDIALIB_SCAN_TIME%%,%lld,Recent 30d,Library))",
             (long long)recent_cut, days, (long long)month_cut);
}

static int column_format_is_recent(const char *fmt) {
    if (!fmt) return 0;
    char *trimmed = g_strdup(fmt);
    g_strstrip(trimmed);
    int is_recent = strcmp(trimmed, CUI_RECENT_TOKEN) == 0;
    g_free(trimmed);
    return is_recent;
}

void init_my_preset(cui_widget_t *cw) {
    CUI_DEBUG("init_my_preset called");
    if (cw->my_preset) {
        my_scriptable_free((scriptableItem_t *)cw->my_preset);
        cw->my_preset = NULL;
    }

    // Compact the titles and formats to remove any gaps left by migration or dialog
    char *new_titles[MAX_COLUMNS] = {NULL};
    char *new_formats[MAX_COLUMNS] = {NULL};
    int valid_idx = 0;
    for (int i = 0; i < MAX_COLUMNS; i++) {
        if (cw->titles[i] && cw->formats[i] && cw->titles[i][0] && cw->formats[i][0]) {
            new_titles[valid_idx] = cw->titles[i];
            new_formats[valid_idx] = cw->formats[i];
            valid_idx++;
        } else {
            if (cw->titles[i]) g_free(cw->titles[i]);
            if (cw->formats[i]) g_free(cw->formats[i]);
        }
    }
    for (int i = 0; i < MAX_COLUMNS; i++) {
        cw->titles[i] = new_titles[i];
        cw->formats[i] = new_formats[i];
    }
    cw->num_columns = valid_idx;

    // Fallback if user cleared everything or migration failed
    if (cw->num_columns == 0) {
        cw->titles[0] = g_strdup("Genre");
        cw->formats[0] = g_strdup("%genre%");
        cw->titles[1] = g_strdup("Album Artist");
        cw->formats[1] = g_strdup("$if2(%album artist%,%artist%)");
        cw->titles[2] = g_strdup("Album");
        cw->formats[2] = g_strdup("%album%");
        cw->num_columns = 3;
    }

    scriptableItem_t *p = my_scriptable_alloc();
    p->flags = SCRIPTABLE_FLAG_IS_LIST;
    my_scriptable_set_prop(p, "name", "Facets");

    int has_recent = 0;
    for (int i = 0; i < cw->num_columns; i++) {
        scriptableItem_t *child = my_scriptable_alloc();
        if (column_format_is_rating(cw->formats[i])) {
            my_scriptable_set_prop(child, "name", rating_display_tf);
        } else if (column_format_is_recent(cw->formats[i])) {
            char expr[512];
            cui_build_recent_expression(cw->recent_days, (gint64)time(NULL), expr, sizeof(expr));
            my_scriptable_set_prop(child, "name", expr);
            has_recent = 1;
        } else {
            my_scriptable_set_prop(child, "name", cw->formats[i]);
        }

        if (cw->split_tags) {
            my_scriptable_set_prop(child, "split", "; ");
        }

        my_scriptable_add_child(p, child);
    }
    cw->preset_has_recent = has_recent;
    cw->preset_built_at = (gint64)time(NULL);

    scriptableItem_t *track_child = my_scriptable_alloc();
    my_scriptable_set_prop(track_child, "name", "%title%");
    my_scriptable_add_child(p, track_child);

    cw->my_preset = (ddb_scriptable_item_t *)p;
}
