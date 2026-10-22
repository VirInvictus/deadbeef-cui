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

// Switch this instance's facet mode (CUI_MODE_PLAY / CUI_MODE_INTAKE),
// exchanging the live and alt column sets and rebuilding. Exposed for the
// test suite; the right-click Mode submenu is its only runtime caller.
void cui_set_mode(cui_widget_t *cw, int mode);

// Install a template's column set as the live columns. TEMPLATES ALWAYS
// TARGET PLAY MODE: called from Intake the widget swaps home first and the
// intake set stays untouched (its editor is cui-modes.ini). Takes ownership
// of the caller's arrays; the caller rebuilds afterwards. Exposed for the
// test suite; the Templates submenu is its only runtime caller.
void cui_template_apply_columns(cui_widget_t *cw, char **titles, char **formats);

void sync_source_config(void);

gboolean deferred_lib_update_cb(gpointer data);
void cui_update_hint(cui_widget_t *cw);
gboolean ml_event_idle_cb(gpointer data);
gboolean cui_handle_config_change(gpointer user_data);
void ml_listener_cb(ddb_mediasource_event_type_t event, void *user_data);

// Start playback in the current playlist (the viewer, after activate_row
// makes it current). Exposed for the test suite; see cui_widget.c for why
// shuffled orders must go through DB_EV_PLAY_NUM, not DB_EV_PLAY_RANDOM.
void cui_start_viewer_playback(void);

#endif // CUI_WIDGET_H
