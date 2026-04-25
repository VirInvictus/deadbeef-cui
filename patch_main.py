import re

with open("src/main.c", "r") as f:
    content = f.read()

# 1. Add definitions
prototypes = """
static void rebuild_columns(cui_widget_t *cw);
static void cui_init(ddb_gtkui_widget_t *w);
static const char **cui_serialize_to_keyvalues(ddb_gtkui_widget_t *w);
static void cui_deserialize_from_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues);
static void cui_free_serialized_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues);
static void cui_initmenu(ddb_gtkui_widget_t *w, GtkWidget *menu);

"""

content = content.replace("static GList *all_cui_widgets = NULL;\n", "static GList *all_cui_widgets = NULL;\n" + prototypes)

# 2. Extract the huge cui_create_widget block and replace it
start_marker = "static ddb_gtkui_widget_t *cui_create_widget(void) {"
end_marker = "// --- Plugin entry points ---"
start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx == -1 or end_idx == -1:
    print("Could not find cui_create_widget block")
    exit(1)

new_block = """
static void rebuild_columns(cui_widget_t *cw) {
    if (cw->num_columns <= 0) return;
    
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(cw->base.widget));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    GtkWidget *col_widgets[MAX_COLUMNS] = {NULL};
    for (int i = 0; i < cw->num_columns; i++) {
        col_widgets[i] = create_column(cw->titles[i], &cw->stores[i], &cw->trees[i], cw);

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
    update_tree_data(cw);
}

static ddb_gtkui_widget_t *cui_create_widget(void) {
    CUI_DEBUG("cui_create_widget called");

    cui_widget_t *cw = calloc(1, sizeof(cui_widget_t));
    cw->changed_col_idx = -1;
    ddb_gtkui_widget_t *w = &cw->base;
    w->type = "cui";

    w->exapi._size = sizeof(ddb_gtkui_widget_extended_api_t);
    w->exapi.serialize_to_keyvalues = cui_serialize_to_keyvalues;
    w->exapi.deserialize_from_keyvalues = cui_deserialize_from_keyvalues;
    w->exapi.free_serialized_keyvalues = cui_free_serialized_keyvalues;

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

"""
content = content[:start_idx] + new_block + content[end_idx:]

with open("src/main.c", "w") as f:
    f.write(content)
