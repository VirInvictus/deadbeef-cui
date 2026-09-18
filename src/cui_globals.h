#ifndef CUI_GLOBALS_H
#define CUI_GLOBALS_H

#include <deadbeef/deadbeef.h>
#include <gtk/gtk.h>
#include <deadbeef/gtkui_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// GTK4 forward-compat shims. KNOWN GAPS (a GTK4 build of this plugin has
// never been made; see CLAUDE.md §10.10 and the README GTK4 note): GdkEventButton
// in on_tree_button_press, gtk_widget_destroy(dialog) in the config-dialog
// path, gtk_container_get_children, the whole GtkMenu block (GTK4 wants
// GtkPopoverMenu/GMenu), the unguarded "key-press-event" connects (need
// GtkEventControllerKey), and drag-out (needs GtkDragSource). Also
// GTK2-era: DDB_GTKUI_PLUGIN_ID and the hardcoded "ddb_gui_GTK3.so" dlopen
// names need special-casing under GTK4.
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

// Global state declarations
extern DB_functions_t *deadbeef_api;
extern ddb_gtkui_t *gtkui_plugin;
extern DB_mediasource_t *medialib_plugin;
extern ddb_mediasource_source_t *ml_source;
extern int shutting_down;
extern int owns_ml_source;
extern int ml_modification_idx;

typedef struct {
    ddb_gtkui_widget_t base;
    ddb_gtkui_widget_extended_api_t exapi;
    int num_columns;
    GtkListStore *stores[MAX_COLUMNS];
    GtkWidget *trees[MAX_COLUMNS];
    GHashTable *sel_texts[MAX_COLUMNS];
    char *titles[MAX_COLUMNS];
    char *formats[MAX_COLUMNS];
    // Active sort per column (id 0 = name, 1 = count; order 0 = ascending,
    // 1 = descending), tracked from the stores' sort-column-changed signal and
    // applied to every freshly built store so the choice survives font-change
    // rebuilds and quit/relaunch (serialized as colN_sort).
    int sort_ids[MAX_COLUMNS];
    int sort_orders[MAX_COLUMNS];
    int ignore_prefix;
    int split_tags;
    char *autoplaylist_name;
    ddb_scriptable_item_t *my_preset;

    int listener_id;
    ddb_medialib_item_t *cached_tree;
    GHashTable *track_counts_cache;

    // Chunked viewer fill (update_playlist_from_cui's async mode). The fill
    // walks cached_tree on the idle queue, ~fill_budget_us of work per tick,
    // so whole-library mirrors don't freeze the UI. Invariants (CLAUDE.md
    // §6.15): the frames hold pointers INTO cached_tree, so every
    // cached_tree free/replace must cui_fill_cancel first; a new fill bumps
    // fill_generation and the running chunk self-aborts on mismatch; only
    // fill completion clears playlist_dirty.
    guint fill_idle_id;
    int fill_generation;
    GPtrArray *fill_stack;          // of cui_fill_frame_t* (the DFS stack)
    DB_playItem_t *fill_after;      // our ref'd insert cursor (owned)
    ddb_playlist_t *fill_plt;       // the fill's target playlist (our ref)
    int fill_inserted;              // tracks inserted so far (debug/telemetry)
    int fill_budget_us;             // per-chunk work budget (tests set it to 0)

    int last_ml_modification_idx;
    guint changed_timeout_id;
    guint lib_update_timeout_id;
    int changed_col_idx;
    int initial_sync_done;
    // Set when the viewer playlist no longer matches the current selection/
    // search/library state; cleared once update_playlist_from_cui rebuilds it.
    // Lets activate_row skip a redundant full-library rebuild on every [All]
    // activation (the cost that made shutdown slow).
    int playlist_dirty;

    GtkWidget *search_entry;
    // Transient status line shown only when the widget would otherwise be
    // silently blank (medialib disabled, source unavailable, empty library,
    // or an in-progress first scan). Hidden once real data populates.
    GtkWidget *hint_label;
    char *search_text;
    char *last_search_text;

    // Cached font state from the last rebuild_columns. Compared against current
    // gtkui.font.listview_* keys when DB_EV_CONFIGCHANGED fires; mismatch means
    // the user changed playlist fonts and our renderers need to refresh.
    char *last_row_font;
    char *last_header_font;
    int last_listview_override;
} cui_widget_t;

extern GList *all_cui_widgets;
extern int config_change_pending;

#endif // CUI_GLOBALS_H
