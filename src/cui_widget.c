#include "cui_widget.h"
#include "cui_data.h"
#include "cui_scriptable.h"

static int global_key_connected = 0;
static gulong mainwin_key_handler_id = 0;

void cui_widget_stop(void) {
    if (global_key_connected) {
        GtkWidget *mainwin = gtkui_plugin->get_mainwin ? gtkui_plugin->get_mainwin() : NULL;
        if (mainwin && mainwin_key_handler_id) {
            g_signal_handler_disconnect(mainwin, mainwin_key_handler_id);
            mainwin_key_handler_id = 0;
        }
        global_key_connected = 0;
    }
}

void update_selection_hash(GtkTreeSelection *selection, GHashTable **hash_ptr) {
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

void on_column_changed(GtkTreeSelection *selection, gpointer data) {
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

void sync_source_config(void) {    
    char paths[4096];
    deadbeef_api->conf_get_str("medialib.deadbeef.paths", "", paths, sizeof(paths));
    if (paths[0]) {
        deadbeef_api->conf_set_str("medialib." CUI_SOURCE_PATH ".paths", paths);
    }
    deadbeef_api->conf_set_str("medialib." CUI_SOURCE_PATH ".enabled", "1");
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    cui_widget_t *cw = (cui_widget_t *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    if (cw->search_text) {
        g_free(cw->search_text);
        cw->search_text = NULL;
    }
    if (text && text[0]) {
        cw->search_text = g_utf8_strdown(text, -1);
    }
    update_tree_data(cw);
}

static gboolean on_mainwin_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
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
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    
    if (event->keyval == GDK_KEY_Escape) {
        if (gtk_widget_get_visible(cw->search_entry)) {
            gtk_entry_set_text(GTK_ENTRY(cw->search_entry), "");
            gtk_widget_hide(cw->search_entry);
            
            // Return focus to the first column
            if (cw->num_columns > 0 && cw->trees[0]) {
                gtk_widget_grab_focus(cw->trees[0]);
            }
            return TRUE;
        }
    }
    
    return FALSE;
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

static void on_menu_add_to_current(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    ddb_playlist_t *plt = deadbeef_api->plt_get_curr();
    if (plt) {
        populate_playlist_from_cui(cw, plt, 0);
        deadbeef_api->plt_unref(plt);
    }
}

static void on_menu_send_to_new(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    int count = deadbeef_api->plt_get_count();
    int new_idx = deadbeef_api->plt_add(count, "New Playlist");
    if (new_idx >= 0) {
        ddb_playlist_t *plt = deadbeef_api->plt_get_for_idx(new_idx);
        if (plt) {
            populate_playlist_from_cui(cw, plt, 1);
            deadbeef_api->plt_set_curr(plt);
            deadbeef_api->plt_unref(plt);
        }
    }
}

static void on_menu_sync_library(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    if (!medialib_plugin || !ml_source) return;
    medialib_plugin->refresh(ml_source);
}

static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTreeView *tv = GTK_TREE_VIEW(widget);
        GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);

        GtkTreePath *path = NULL;
        if (gtk_tree_view_get_path_at_pos(tv, (int)event->x, (int)event->y,
                                           &path, NULL, NULL, NULL) && path) {
            if (!gtk_tree_selection_path_is_selected(sel, path)) {
                gtk_tree_selection_unselect_all(sel);
                gtk_tree_selection_select_path(sel, path);
            }
            gtk_tree_path_free(path);
        }

        GtkWidget *menu = gtk_menu_new();

        GtkWidget *item_add = gtk_menu_item_new_with_label("Add selection to current playlist");
        g_signal_connect(item_add, "activate", G_CALLBACK(on_menu_add_to_current), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_add);

        GtkWidget *item_new = gtk_menu_item_new_with_label("Send selection to new playlist");
        g_signal_connect(item_new, "activate", G_CALLBACK(on_menu_send_to_new), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new);

        GtkWidget *sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

        GtkWidget *item_sync = gtk_menu_item_new_with_label("Sync library");
        g_signal_connect(item_sync, "activate", G_CALLBACK(on_menu_sync_library), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_sync);

        GtkWidget *sep2 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep2);

        GtkWidget *item_config = gtk_menu_item_new_with_label("Configure Facets...");
        g_signal_connect(item_config, "activate", G_CALLBACK(show_config_dialog), user_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_config);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

// Read the user's listview font overrides from gtkui config so our cells visually
// match the playlist widget. When gtkui.override_listview_colors is off, we leave
// both pointers NULL and the default GTK theme font applies.
static void cui_get_listview_fonts(char **out_row_font, char **out_header_font) {
    *out_row_font = NULL;
    *out_header_font = NULL;
    if (!deadbeef_api->conf_get_int("gtkui.override_listview_colors", 0)) return;

    deadbeef_api->conf_lock();
    const char *row = deadbeef_api->conf_get_str_fast("gtkui.font.listview_text", NULL);
    if (row && row[0]) *out_row_font = g_strdup(row);
    const char *hdr = deadbeef_api->conf_get_str_fast("gtkui.font.listview_column_text", NULL);
    if (hdr && hdr[0]) *out_header_font = g_strdup(hdr);
    deadbeef_api->conf_unlock();
}

static void cui_apply_header_font(GtkTreeViewColumn *column, const char *title, const char *header_font) {
    if (!header_font) return;
    GtkWidget *label = gtk_label_new(title);
    PangoFontDescription *desc = pango_font_description_from_string(header_font);
    if (desc) {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_font_desc_new(desc));
        gtk_label_set_attributes(GTK_LABEL(label), attrs);
        pango_attr_list_unref(attrs);
        pango_font_description_free(desc);
    }
    gtk_widget_show(label);
    gtk_tree_view_column_set_widget(column, label);
}

static GtkWidget *create_column(const char *title, GtkListStore **out_store, GtkWidget **out_tree, cui_widget_t *cw,
                                 const char *row_font, const char *header_font) {
    GtkListStore *store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 0, sort_func, cw, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 1, sort_func, cw, NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0, GTK_SORT_ASCENDING);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    if (row_font) g_object_set(renderer, "font", row_font, NULL);
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", 0, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_min_width(column, 50);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sort_column_id(column, 0);
    gtk_tree_view_column_set_expand(column, TRUE);
    cui_apply_header_font(column, title, header_font);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    GtkCellRenderer *count_renderer = gtk_cell_renderer_text_new();
    g_object_set(count_renderer, "xalign", 1.0, "xpad", 16, NULL);
    if (row_font) g_object_set(count_renderer, "font", row_font, NULL);
    GtkTreeViewColumn *count_column = gtk_tree_view_column_new_with_attributes("Count", count_renderer, "text", 1, NULL);
    gtk_tree_view_column_set_sizing(count_column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_column_set_sort_column_id(count_column, 1);
    cui_apply_header_font(count_column, "Count", header_font);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), count_column);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    if (out_store) *out_store = store;
    if (out_tree) *out_tree = tree;

    return scroll;
}

void rebuild_columns(cui_widget_t *cw) {
    if (cw->num_columns <= 0) return;

    // Read font config once at the top so every column built in this pass uses
    // the same values, and cache them on cw so the CONFIGCHANGED handler can
    // tell whether a future event represents an actual font change.
    g_free(cw->last_row_font);
    g_free(cw->last_header_font);
    cw->last_row_font = NULL;
    cw->last_header_font = NULL;
    cui_get_listview_fonts(&cw->last_row_font, &cw->last_header_font);
    cw->last_listview_override = deadbeef_api->conf_get_int("gtkui.override_listview_colors", 0);

    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(cw->base.widget));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    GtkWidget *col_widgets[MAX_COLUMNS] = {NULL};
    for (int i = 0; i < cw->num_columns; i++) {
        col_widgets[i] = create_column(cw->titles[i], &cw->stores[i], &cw->trees[i], cw,
                                        cw->last_row_font, cw->last_header_font);

        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cw->trees[i]));
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
        g_signal_connect(sel, "changed", G_CALLBACK(on_column_changed), cw);
        g_signal_connect(cw->trees[i], "row-activated", G_CALLBACK(on_row_activated), cw);
#if GTK_MAJOR_VERSION < 4
        g_signal_connect(cw->trees[i], "button-press-event", G_CALLBACK(on_tree_button_press), cw);
#endif
        g_signal_connect(cw->trees[i], "key-press-event", G_CALLBACK(on_key_press), cw);
        gtk_tree_view_set_enable_search(GTK_TREE_VIEW(cw->trees[i]), TRUE);
        gtk_tree_view_set_search_column(GTK_TREE_VIEW(cw->trees[i]), 0);
    }

    GtkWidget *top_widget = col_widgets[cw->num_columns - 1];
    for (int i = cw->num_columns - 2; i >= 0; i--) {
        GtkWidget *pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(pane), col_widgets[i], TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(pane), top_widget, TRUE, FALSE);
        top_widget = pane;
    }

    cw->search_entry = gtk_search_entry_new();
    g_signal_connect(cw->search_entry, "changed", G_CALLBACK(on_search_changed), cw);
    g_signal_connect(cw->search_entry, "key-press-event", G_CALLBACK(on_key_press), cw);
    gtk_widget_set_no_show_all(cw->search_entry, TRUE);
    gtk_widget_hide(cw->search_entry);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(cw->search_entry, 4);
    gtk_widget_set_margin_end(cw->search_entry, 4);
    gtk_widget_set_margin_top(cw->search_entry, 4);
    gtk_widget_set_margin_bottom(cw->search_entry, 4);
    gtk_box_pack_start(GTK_BOX(vbox), cw->search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), top_widget, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(cw->base.widget), vbox);
    gtk_widget_show_all(cw->base.widget);
}

static void cui_init(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;
    if (!cw->my_preset) init_my_preset(cw);
    
    rebuild_columns(cw);

    if (!global_key_connected) {
        GtkWidget *mainwin = gtkui_plugin->get_mainwin ? gtkui_plugin->get_mainwin() : NULL;
        if (mainwin) {
            mainwin_key_handler_id = g_signal_connect(mainwin, "key-press-event", G_CALLBACK(on_mainwin_key_press), NULL);
            global_key_connected = 1;
        }
    }

    if (gtkui_plugin && gtkui_plugin->w_override_signals) {
        gtkui_plugin->w_override_signals(w->widget, w);
    }

    if (!ml_source && medialib_plugin) {
        void *gtkui_handle = dlopen("ddb_gui_GTK3.so", RTLD_LAZY | RTLD_NOLOAD);
        if (gtkui_handle) {
            ddb_mediasource_source_t * (*get_shared_source)(void) =
                (ddb_mediasource_source_t * (*)(void))dlsym(gtkui_handle, "gtkui_medialib_get_source");
            if (get_shared_source) {
                ml_source = get_shared_source();
                CUI_DEBUG("Using shared medialib source from GTKUI");
            }
            dlclose(gtkui_handle);
        }

        if (!ml_source && medialib_plugin->create_source) {
            sync_source_config();
            ml_source = medialib_plugin->create_source(CUI_SOURCE_PATH);
            if (ml_source) {
                owns_ml_source = 1;
                medialib_plugin->refresh(ml_source);
            }
        }
    }

    if (medialib_plugin && ml_source) {
        cw->listener_id = medialib_plugin->add_listener(ml_source, ml_listener_cb, cw);
    }

    if (!g_list_find(all_cui_widgets, cw)) {
        all_cui_widgets = g_list_append(all_cui_widgets, cw);
    }

    // Defer the first build to the next idle tick so layout finishes painting
    // before we run the heavy tree+populate pipeline. Reuses lib_update_timeout_id
    // so cui_destroy's cleanup covers it; if a CONTENT_DID_CHANGE arrives first,
    // its handler will see the id is non-zero and coalesce with this scheduled run.
    if (cw->lib_update_timeout_id == 0) {
        cw->lib_update_timeout_id = g_idle_add(deferred_lib_update_cb, cw);
    }
}

static void cui_destroy(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;

    if (cw->changed_timeout_id) {
        g_source_remove(cw->changed_timeout_id);
        cw->changed_timeout_id = 0;
    }
    if (cw->lib_update_timeout_id) {
        g_source_remove(cw->lib_update_timeout_id);
        cw->lib_update_timeout_id = 0;
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
    }
    
    // If we're the last widget and we own the source, it'll be freed in cui_stop.
    // However, if we're just one of many, we keep it alive.
    if (all_cui_widgets == NULL && owns_ml_source) {
        // Source will be cleaned up in cui_stop or when last widget is gone if needed
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
        g_free(cw->titles[i]);
    }
    
    g_free(cw->search_text);
    cw->search_text = NULL;
    g_free(cw->last_search_text);
    cw->last_search_text = NULL;
    g_free(cw->last_row_font);
    cw->last_row_font = NULL;
    g_free(cw->last_header_font);
    cw->last_header_font = NULL;
}

static const char **cui_serialize_to_keyvalues(ddb_gtkui_widget_t *w) {
    cui_widget_t *cw = (cui_widget_t *)w;
    int num_items = MAX_COLUMNS * 2 + 3; // formats, titles, split_tags, ignore_prefix, autoplaylist
    char **keyvalues = calloc(num_items * 2 + 1, sizeof(char *));
    int idx = 0;
    
    for (int i = 0; i < MAX_COLUMNS; i++) {
        char key_title[32], key_format[32];
        snprintf(key_title, sizeof(key_title), "col%d_title", i + 1);
        snprintf(key_format, sizeof(key_format), "col%d_format", i + 1);
        
        keyvalues[idx++] = strdup(key_title);
        keyvalues[idx++] = cw->titles[i] ? strdup(cw->titles[i]) : strdup("");
        
        keyvalues[idx++] = strdup(key_format);
        keyvalues[idx++] = cw->formats[i] ? strdup(cw->formats[i]) : strdup("");
    }
    
    keyvalues[idx++] = strdup("split_tags");
    char temp[16];
    snprintf(temp, sizeof(temp), "%d", cw->split_tags);
    keyvalues[idx++] = strdup(temp);
    
    keyvalues[idx++] = strdup("ignore_prefix");
    snprintf(temp, sizeof(temp), "%d", cw->ignore_prefix);
    keyvalues[idx++] = strdup(temp);
    
    keyvalues[idx++] = strdup("autoplaylist_name");
    keyvalues[idx++] = cw->autoplaylist_name ? strdup(cw->autoplaylist_name) : strdup("Library Viewer");
    
    keyvalues[idx] = NULL;
    return (const char **)keyvalues;
}

static void cui_deserialize_from_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues) {
    cui_widget_t *cw = (cui_widget_t *)w;
    
    if (keyvalues) {
        int found_any = 0;
        for (int i = 0; keyvalues[i]; i += 2) {
            if (strncmp(keyvalues[i], "col", 3) == 0 && strstr(keyvalues[i], "_format")) {
                found_any = 1;
                break;
            }
        }
        
        if (found_any) {
            // Clear migrated defaults so we can load instance settings
            for (int i = 0; i < MAX_COLUMNS; i++) {
                g_free(cw->titles[i]); cw->titles[i] = NULL;
                g_free(cw->formats[i]); cw->formats[i] = NULL;
            }
            
            for (int i = 0; keyvalues[i]; i += 2) {
                const char *k = keyvalues[i];
                const char *v = keyvalues[i+1];
                if (!v) continue;
                
                if (strncmp(k, "col", 3) == 0 && strlen(k) >= 10) {
                    int col = k[3] - '1';
                    if (col >= 0 && col < MAX_COLUMNS) {
                        if (strstr(k, "_title")) {
                            cw->titles[col] = g_strdup(v);
                        } else if (strstr(k, "_format")) {
                            cw->formats[col] = g_strdup(v);
                        }
                    }
                } else if (strcmp(k, "split_tags") == 0) {
                    cw->split_tags = atoi(v);
                } else if (strcmp(k, "ignore_prefix") == 0) {
                    cw->ignore_prefix = atoi(v);
                } else if (strcmp(k, "autoplaylist_name") == 0) {
                    if (cw->autoplaylist_name) g_free(cw->autoplaylist_name);
                    cw->autoplaylist_name = g_strdup(v);
                }
            }
        }
    }
}

static void cui_free_serialized_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues) {
    (void)w;
    if (!keyvalues) return;
    for (int i = 0; keyvalues[i]; i++) {
        free((void *)keyvalues[i]);
    }
    free((void *)keyvalues);
}

static void on_config_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    cui_widget_t *cw = (cui_widget_t *)user_data;
    
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget *content_area = gtk_dialog_get_content_area(dialog);
        GList *children = gtk_container_get_children(GTK_CONTAINER(content_area));
        if (children && GTK_IS_GRID(children->data)) {
            GtkGrid *grid = GTK_GRID(children->data);
            
            char *new_titles[MAX_COLUMNS] = {NULL};
            char *new_formats[MAX_COLUMNS] = {NULL};
            int valid_idx = 0;
            
            for (int i = 0; i < MAX_COLUMNS; i++) {
                GtkWidget *title_entry = gtk_grid_get_child_at(grid, 1, i + 1);
                GtkWidget *format_entry = gtk_grid_get_child_at(grid, 2, i + 1);
                if (title_entry && format_entry) {
                    const char *t = gtk_entry_get_text(GTK_ENTRY(title_entry));
                    const char *f = gtk_entry_get_text(GTK_ENTRY(format_entry));
                    if (t && f && t[0] && f[0]) {
                        new_titles[valid_idx] = g_strdup(t);
                        new_formats[valid_idx] = g_strdup(f);
                        valid_idx++;
                    }
                }
            }
            
            for (int i = 0; i < MAX_COLUMNS; i++) {
                if (cw->titles[i]) g_free(cw->titles[i]);
                if (cw->formats[i]) g_free(cw->formats[i]);
                cw->titles[i] = new_titles[i];
                cw->formats[i] = new_formats[i];
            }
            
            GtkWidget *ignore_cb = gtk_grid_get_child_at(grid, 1, MAX_COLUMNS + 1);
            if (ignore_cb) cw->ignore_prefix = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignore_cb));
            
            GtkWidget *split_cb = gtk_grid_get_child_at(grid, 1, MAX_COLUMNS + 2);
            if (split_cb) cw->split_tags = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_cb));
            
            GtkWidget *ap_entry = gtk_grid_get_child_at(grid, 1, MAX_COLUMNS + 3);
            if (ap_entry) {
                if (cw->autoplaylist_name) g_free(cw->autoplaylist_name);
                cw->autoplaylist_name = g_strdup(gtk_entry_get_text(GTK_ENTRY(ap_entry)));
            }
        }
        g_list_free(children);

        if (cw->my_preset) {
            my_scriptable_free((scriptableItem_t *)cw->my_preset);
            cw->my_preset = NULL;
        }
        init_my_preset(cw);
        rebuild_columns(cw);
        update_tree_data(cw);
        
        if (gtkui_plugin && gtkui_plugin->w_save_layout_to_conf_key) {
            gtkui_plugin->w_save_layout_to_conf_key("layout", NULL);
        }
    }
    
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void show_config_dialog(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    cui_widget_t *cw = (cui_widget_t *)user_data;
    
    GtkWidget *mainwin = gtkui_plugin->get_mainwin ? gtkui_plugin->get_mainwin() : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Configure Facets",
                                                    GTK_WINDOW(mainwin),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_OK", GTK_RESPONSE_ACCEPT,
                                                    NULL);
                                                    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Title"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Format"), 2, 0, 1, 1);
    
    for (int i = 0; i < MAX_COLUMNS; i++) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "Column %d:", i + 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(label_text), 0, i + 1, 1, 1);
        
        GtkWidget *title_entry = gtk_entry_new();
        if (cw->titles[i]) gtk_entry_set_text(GTK_ENTRY(title_entry), cw->titles[i]);
        gtk_grid_attach(GTK_GRID(grid), title_entry, 1, i + 1, 1, 1);
        
        GtkWidget *format_entry = gtk_entry_new();
        if (cw->formats[i]) gtk_entry_set_text(GTK_ENTRY(format_entry), cw->formats[i]);
        gtk_widget_set_hexpand(format_entry, TRUE);
        gtk_grid_attach(GTK_GRID(grid), format_entry, 2, i + 1, 1, 1);
    }
    
    GtkWidget *ignore_cb = gtk_check_button_new_with_label("Ignore Prefix (The, A, An)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ignore_cb), cw->ignore_prefix);
    gtk_grid_attach(GTK_GRID(grid), ignore_cb, 1, MAX_COLUMNS + 1, 2, 1);
    
    GtkWidget *split_cb = gtk_check_button_new_with_label("Split Multivalue Tags (; )");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_cb), cw->split_tags);
    gtk_grid_attach(GTK_GRID(grid), split_cb, 1, MAX_COLUMNS + 2, 2, 1);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Autoplaylist Name:"), 0, MAX_COLUMNS + 3, 1, 1);
    GtkWidget *ap_entry = gtk_entry_new();
    if (cw->autoplaylist_name) gtk_entry_set_text(GTK_ENTRY(ap_entry), cw->autoplaylist_name);
    gtk_grid_attach(GTK_GRID(grid), ap_entry, 1, MAX_COLUMNS + 3, 2, 1);
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    gtk_widget_show_all(dialog);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_config_dialog_response), cw);
}

static void cui_initmenu(ddb_gtkui_widget_t *w, GtkWidget *menu) {
    GtkWidget *item = gtk_menu_item_new_with_mnemonic("Configure Facets...");
    gtk_widget_show(item);
    gtk_container_add(GTK_CONTAINER(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(show_config_dialog), w);
}

ddb_gtkui_widget_t *cui_create_widget(void) {
    CUI_DEBUG("cui_create_widget called");

    cui_widget_t *cw = calloc(1, sizeof(cui_widget_t));
    cw->changed_col_idx = -1;
    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "cui";

    deadbeef_api->conf_lock();
    if (!deadbeef_api->conf_get_str_fast("cui.col1_format", NULL)) {
        cw->titles[0] = g_strdup("Genre");
        cw->formats[0] = g_strdup("%genre%");
        cw->titles[1] = g_strdup("Album Artist");
        cw->formats[1] = g_strdup("$if2(%album artist%,%artist%)");
        cw->titles[2] = g_strdup("Album");
        cw->formats[2] = g_strdup("%album%");
        cw->ignore_prefix = 0;
        cw->split_tags = 1;
        cw->autoplaylist_name = g_strdup("Library Viewer");
    } else {
        for (int i = 0; i < MAX_COLUMNS; i++) {
            char key_title[32], key_format[32];
            snprintf(key_title, sizeof(key_title), "cui.col%d_title", i + 1);
            snprintf(key_format, sizeof(key_format), "cui.col%d_format", i + 1);
            const char *t = deadbeef_api->conf_get_str_fast(key_title, "");
            const char *f = deadbeef_api->conf_get_str_fast(key_format, "");
            if (f && f[0]) {
                cw->titles[i] = g_strdup(t);
                cw->formats[i] = g_strdup(f);
            }
        }
        cw->ignore_prefix = deadbeef_api->conf_get_int("cui.ignore_prefix", 0);
        cw->split_tags = deadbeef_api->conf_get_int("cui.split_tags", 1);
        cw->autoplaylist_name = g_strdup(deadbeef_api->conf_get_str_fast("cui.autoplaylist_name", "Library Viewer"));
    }
    deadbeef_api->conf_unlock();

    cw->exapi._size = sizeof(ddb_gtkui_widget_extended_api_t);
    cw->exapi.serialize_to_keyvalues = cui_serialize_to_keyvalues;
    cw->exapi.deserialize_from_keyvalues = cui_deserialize_from_keyvalues;
    cw->exapi.free_serialized_keyvalues = cui_free_serialized_keyvalues;

    w->initmenu = cui_initmenu;
    w->init = cui_init;
    w->destroy = cui_destroy;

#if GTK_MAJOR_VERSION >= 4
    w->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
    w->widget = gtk_event_box_new();
#endif
    gtk_widget_show(w->widget);

    g_signal_connect(w->widget, "key-press-event", G_CALLBACK(on_key_press), cw);

    return w;
}

gboolean deferred_lib_update_cb(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    cw->lib_update_timeout_id = 0;

    if (shutting_down) return G_SOURCE_REMOVE;

    if (g_list_find(all_cui_widgets, cw)) {
        if (medialib_plugin && ml_source) {
            update_tree_data(cw);
        }
    }
    return G_SOURCE_REMOVE;
}

// Idle callback fired by main.c's cui_message on DB_EV_CONFIGCHANGED. Walks
// every live cui widget, compares cached font state against the current
// gtkui.font.listview_* keys, and rebuilds only the widgets that actually
// changed. CONFIGCHANGED fires for unrelated keys too (volume, output, etc.),
// hence the per-widget comparison rather than blind rebuilds.
gboolean cui_handle_config_change(gpointer user_data) {
    (void)user_data;
    g_atomic_int_set(&config_change_pending, 0);
    if (shutting_down) return G_SOURCE_REMOVE;

    int new_override = deadbeef_api->conf_get_int("gtkui.override_listview_colors", 0);
    char *new_row = NULL;
    char *new_hdr = NULL;
    cui_get_listview_fonts(&new_row, &new_hdr);

    for (GList *l = all_cui_widgets; l; l = l->next) {
        cui_widget_t *cw = (cui_widget_t *)l->data;
        if (cw->last_listview_override != new_override ||
            g_strcmp0(cw->last_row_font, new_row) != 0 ||
            g_strcmp0(cw->last_header_font, new_hdr) != 0) {
            rebuild_columns(cw);
            update_tree_data(cw);
        }
    }

    g_free(new_row);
    g_free(new_hdr);
    return G_SOURCE_REMOVE;
}

gboolean ml_event_idle_cb(gpointer data) {
    cui_widget_t *cw = (cui_widget_t *)data;
    if (shutting_down) return G_SOURCE_REMOVE;
    if (!g_list_find(all_cui_widgets, cw)) return G_SOURCE_REMOVE;
    if (!medialib_plugin || !ml_source) return G_SOURCE_REMOVE;

    if (medialib_plugin->scanner_state(ml_source) != DDB_MEDIASOURCE_STATE_IDLE) {
        return G_SOURCE_REMOVE;
    }

    if (cw->lib_update_timeout_id == 0) {
        // First content event after widget creation runs immediately; subsequent
        // events use the 1s debounce that protects against batch tag-edit storms.
        if (cw->initial_sync_done) {
            cw->lib_update_timeout_id = g_timeout_add(1000, deferred_lib_update_cb, cw);
        } else {
            cw->lib_update_timeout_id = g_idle_add(deferred_lib_update_cb, cw);
        }
    }
    return G_SOURCE_REMOVE;
}

void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data) {
    if (shutting_down) return;
    if (event != DDB_MEDIASOURCE_EVENT_STATE_DID_CHANGE &&
        event != DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE) {
        return;
    }
    if (event == DDB_MEDIASOURCE_EVENT_CONTENT_DID_CHANGE) {
        g_atomic_int_inc(&ml_modification_idx);
    }
    g_idle_add(ml_event_idle_cb, user_data);
}
