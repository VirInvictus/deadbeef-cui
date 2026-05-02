#ifndef CUI_GLOBALS_H
#define CUI_GLOBALS_H

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
    int initial_sync_done;

    GtkWidget *search_entry;
    char *search_text;
    char *last_search_text;
} cui_widget_t;

extern GList *all_cui_widgets;

#endif // CUI_GLOBALS_H
