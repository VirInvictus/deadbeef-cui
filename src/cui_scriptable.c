#include "cui_scriptable.h"

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

    for (int i = 0; i < cw->num_columns; i++) {
        scriptableItem_t *child = my_scriptable_alloc();
        my_scriptable_set_prop(child, "name", cw->formats[i]);

        if (cw->split_tags) {
            my_scriptable_set_prop(child, "split", "; ");
        }

        my_scriptable_add_child(p, child);
    }

    scriptableItem_t *track_child = my_scriptable_alloc();
    my_scriptable_set_prop(track_child, "name", "%title%");
    my_scriptable_add_child(p, track_child);

    cw->my_preset = (ddb_scriptable_item_t *)p;
}
