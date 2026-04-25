#include "cui_globals.h"
#include "cui_widget.h"
#include "cui_data.h"
#include "cui_scriptable.h"

DB_functions_t *deadbeef_api;
ddb_gtkui_t *gtkui_plugin;
DB_mediasource_t *medialib_plugin;
ddb_mediasource_source_t *ml_source;
int shutting_down = 0;
int owns_ml_source = 0;
int ml_modification_idx = 1;
GList *all_cui_widgets = NULL;

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

    gtkui_plugin->w_reg_widget("Facet Browser (CUI) v1.2.3", DDB_WF_SUPPORTS_EXTENDED_API, cui_create_widget, "cui", NULL);
    fprintf(stderr, "deadbeef-cui: Facet Browser v1.2.3 registered successfully.\n");

    return 0;
}

int cui_stop(void) {
    shutting_down = 1;

    cui_widget_stop();
    
    // For now I'll just check `owns_ml_source`.
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
    .plugin.descr = "A faceted library browser for DeaDBeeF. Version 1.2.3",
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
