import re

with open("src/main.c", "r") as f:
    content = f.read()

end_marker = "// --- Plugin entry points ---"
end_idx = content.find(end_marker)

if end_idx == -1:
    print("Could not find end marker")
    exit(1)

new_functions = """
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
    
    cw->ignore_prefix = 0;
    cw->split_tags = 1;
    cw->autoplaylist_name = g_strdup("Library Viewer");
    
    int found_any = 0;
    
    if (keyvalues) {
        for (int i = 0; keyvalues[i]; i += 2) {
            const char *k = keyvalues[i];
            const char *v = keyvalues[i+1];
            if (!v) continue;
            
            if (strncmp(k, "col", 3) == 0 && strlen(k) >= 10) {
                int col = k[3] - '1';
                if (col >= 0 && col < MAX_COLUMNS) {
                    if (strstr(k, "_title")) {
                        cw->titles[col] = g_strdup(v);
                        found_any = 1;
                    } else if (strstr(k, "_format")) {
                        cw->formats[col] = g_strdup(v);
                        found_any = 1;
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
    
    if (!found_any) {
        cw->titles[0] = g_strdup("Genre");
        cw->formats[0] = g_strdup("%genre%");
        cw->titles[1] = g_strdup("Album Artist");
        cw->formats[1] = g_strdup("$if2(%album artist%,%artist%)");
        cw->titles[2] = g_strdup("Album");
        cw->formats[2] = g_strdup("%album%");
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
        // Find inputs and update cw.
        GtkWidget *content_area = gtk_dialog_get_content_area(dialog);
        GList *children = gtk_container_get_children(GTK_CONTAINER(content_area));
        if (children && GTK_IS_GRID(children->data)) {
            GtkGrid *grid = GTK_GRID(children->data);
            for (int i = 0; i < MAX_COLUMNS; i++) {
                GtkWidget *title_entry = gtk_grid_get_child_at(grid, 1, i + 1);
                GtkWidget *format_entry = gtk_grid_get_child_at(grid, 2, i + 1);
                if (title_entry && format_entry) {
                    if (cw->titles[i]) g_free(cw->titles[i]);
                    if (cw->formats[i]) g_free(cw->formats[i]);
                    cw->titles[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(title_entry)));
                    cw->formats[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(format_entry)));
                }
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

        // Rebuild internal data
        if (cw->my_preset) {
            my_scriptable_free((scriptableItem_t *)cw->my_preset);
            cw->my_preset = NULL;
        }
        init_my_preset(cw);
        rebuild_columns(cw);
        update_tree_data(cw);
        
        if (gtkui_plugin && gtkui_plugin->w_save_layout_to_conf_key) {
            gtkui_plugin->w_save_layout_to_conf_key("layout", NULL); // Best effort, layout is global, might need user to save layout
        }
    }
    
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void show_config_dialog(GtkMenuItem *item, gpointer user_data) {
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

"""
content = content[:end_idx] + new_functions + content[end_idx:]

with open("src/main.c", "w") as f:
    f.write(content)
