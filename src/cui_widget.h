#ifndef CUI_WIDGET_H
#define CUI_WIDGET_H

#include "cui_globals.h"

void update_selection_hash(GtkTreeSelection *selection, GHashTable **hash_ptr);
void on_column_changed(GtkTreeSelection *selection, gpointer data);
void auto_select_all_if_empty(cui_widget_t *cw, int col_idx);

// The extended-API (exapi) implementation trio: exposed for the test suite;
// installed into cw->exapi by cui_create_widget.
const char **cui_serialize_to_keyvalues(ddb_gtkui_widget_t *w);
void cui_deserialize_from_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues);
void cui_free_serialized_keyvalues(ddb_gtkui_widget_t *w, const char **keyvalues);

ddb_gtkui_widget_t *cui_create_widget(void);
void show_config_dialog(GtkMenuItem *item, gpointer user_data);
void rebuild_columns(cui_widget_t *cw);
void cui_setup_menu_autodestroy(GtkWidget *menu);
void cui_widget_stop(void);
void cui_clear_viewer_playlists(void);

void sync_source_config(void);

gboolean deferred_lib_update_cb(gpointer data);
void cui_update_hint(cui_widget_t *cw);
gboolean ml_event_idle_cb(gpointer data);
gboolean cui_handle_config_change(gpointer user_data);
void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data);

#endif // CUI_WIDGET_H
