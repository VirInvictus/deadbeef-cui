#ifndef CUI_MODES_H
#define CUI_MODES_H

#include "cui_globals.h"

// Facet modes + templates engine (v2.2.0). Mode defaults and templates live
// in a user-editable GKeyFile INI named cui-modes.ini inside DeaDBeeF's
// config directory, written with defaults on first run and re-read on every
// menu open / mode switch, so hand edits land without a restart:
//
//   [mode:play]            title1..5 / format1..5
//   [mode:intake]          ditto
//   [template:Name]        ditto; offered in the right-click Templates menu
//
// A missing or unparseable file falls back to the compiled defaults; the
// file is never overwritten once it exists. Only titles/formats are read
// from the INI (per-column sorts always start at name/ascending); the live
// per-instance state (including edits made in the config dialog) persists in
// the widget's keyvalue store, not here.

// Absolute path of the INI (stable for the process lifetime). Uses DeaDBeeF's
// get_config_dir when available (honors portable installs), falling back to
// $XDG_CONFIG_HOME/deadbeef.
const char *cui_modes_ini_path(void);

// Fill the built-in (compiled) defaults for a mode. g_strdup'ed values;
// release with cui_modes_free_columns.
void cui_modes_builtin_columns(int mode, char **out_titles, char **out_formats);

// Load a mode's column set, preferring the INI group and falling back to the
// built-ins when the file is missing, unparseable, or lacks the group.
void cui_modes_load_columns(int mode, char **out_titles, char **out_formats);

// A column set counts as populated when any slot is non-NULL. Empty-string
// slots do not count (the serializer stores empties for absent columns).
int cui_modes_columns_present(char *const *titles, char *const *formats);

// Free and clear both arrays (MAX_COLUMNS slots each).
void cui_modes_free_columns(char **titles, char **formats);

// Swap the live column arrays (titles/formats/sorts) with the widget's
// alt_* arrays. Pointer swaps: both sides are widget-owned, ownership moves
// with the pointers, nothing is copied or freed.
void cui_modes_exchange_alt(cui_widget_t *cw);

// Template names ([template:*] groups, file order) as g_free'able strings in
// a GPtrArray; empty (not NULL) when the file has none. Re-read per call.
GPtrArray *cui_modes_list_templates(void);

// Copy a template's columns into out arrays. Returns 0 on success, -1 when
// unknown (arrays left NULL-filled).
int cui_modes_template_columns(const char *name, char **out_titles, char **out_formats);

// Write the defaults INI unless it already exists; never overwrites. Called
// once per process from the first widget creation so the file exists for
// hand-editing before any mode switch. Failures are logged, non-fatal.
void cui_modes_ensure_default_file(void);

// Test surface: the pure halves of the file-backed operations. The suite
// feeds in-memory GKeyFiles so it never touches the user's real INI.
void cui_modes_build_default_keyfile(GKeyFile *kf);
void cui_modes_parse_group_columns(GKeyFile *kf, const char *group, char **out_titles, char **out_formats);
void cui_modes_collect_templates(GKeyFile *kf, GPtrArray *out);

#endif // CUI_MODES_H
