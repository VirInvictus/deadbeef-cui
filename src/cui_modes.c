#include "cui_modes.h"

// Compiled fallbacks, used when cui-modes.ini is missing, unparseable, or
// lacks a group. These mirror the shipped default INI (see
// cui_modes_build_default_keyfile); a change here is a change there.
static const char *const BUILTIN_TITLES[2][MAX_COLUMNS] = {
    { "Rating", "Genre", "Album Artist", "Album" },
    { "Added", "Codec", "Genre", "Album Artist", "Album" },
};
static const char *const BUILTIN_FORMATS[2][MAX_COLUMNS] = {
    { "%rating%", "%genre%", "$if2(%album artist%,%artist%)", "%album%" },
    { CUI_RECENT_TOKEN, "%codec%", "%genre%", "$if2(%album artist%,%artist%)", "%album%" },
};

void cui_modes_builtin_columns(int mode, char **out_titles, char **out_formats) {
    int m = (mode == CUI_MODE_INTAKE) ? CUI_MODE_INTAKE : CUI_MODE_PLAY;
    for (int i = 0; i < MAX_COLUMNS; i++) {
        out_titles[i] = BUILTIN_TITLES[m][i] ? g_strdup(BUILTIN_TITLES[m][i]) : NULL;
        out_formats[i] = BUILTIN_FORMATS[m][i] ? g_strdup(BUILTIN_FORMATS[m][i]) : NULL;
    }
}

const char *cui_modes_ini_path(void) {
    static char path[1024];
    if (!path[0]) {
        // get_config_dir is the only config-dir API in the ABI (deprecated
        // since 1.8 but present and correct); the GLib fallback covers the
        // test mock, where the member is NULL.
        const char *cfg = (deadbeef_api && deadbeef_api->get_config_dir)
                              ? deadbeef_api->get_config_dir() : NULL;
        if (cfg && cfg[0]) {
            snprintf(path, sizeof(path), "%s/cui-modes.ini", cfg);
        } else {
            snprintf(path, sizeof(path), "%s/deadbeef/cui-modes.ini", g_get_user_config_dir());
        }
    }
    return path;
}

void cui_modes_parse_group_columns(GKeyFile *kf, const char *group, char **out_titles, char **out_formats) {
    // Sized so "format" + a worst-case %d cannot truncate (keeps
    // -Wformat-truncation silent; the loop bounds i+1 to 1..5 anyway).
    char key[24];
    for (int i = 0; i < MAX_COLUMNS; i++) {
        out_titles[i] = NULL;
        out_formats[i] = NULL;
        snprintf(key, sizeof(key), "title%d", i + 1);
        gchar *t = g_key_file_get_string(kf, group, key, NULL);
        snprintf(key, sizeof(key), "format%d", i + 1);
        gchar *f = g_key_file_get_string(kf, group, key, NULL);
        // A slot needs both halves; a lone title or format is dropped, like
        // the config dialog's compaction.
        if (t && t[0] && f && f[0]) {
            out_titles[i] = t;
            out_formats[i] = f;
        } else {
            g_free(t);
            g_free(f);
        }
    }
}

void cui_modes_load_columns(int mode, char **out_titles, char **out_formats) {
    cui_modes_builtin_columns(mode, out_titles, out_formats);

    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, cui_modes_ini_path(), G_KEY_FILE_NONE, &err)) {
        // A missing file is the ordinary pre-first-run state (the built-ins
        // above are correct); anything else is worth a log line.
        if (!g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            cui_log(DDB_LOG_LAYER_DEFAULT,
                    "deadbeef-cui: could not read cui-modes.ini (%s); built-in mode defaults in use.\n",
                    err && err->message ? err->message : "unknown error");
        }
        g_clear_error(&err);
        g_key_file_unref(kf);
        return;
    }

    // Sized for a literal suffix; large enough that -Wformat-truncation
    // can flag nothing for the bounded literals below.
    char group[32];
    snprintf(group, sizeof(group), "mode:%s", mode == CUI_MODE_INTAKE ? "intake" : "play");
    if (g_key_file_has_group(kf, group)) {
        char *titles[MAX_COLUMNS];
        char *formats[MAX_COLUMNS];
        cui_modes_parse_group_columns(kf, group, titles, formats);
        int any = cui_modes_columns_present(titles, formats);
        if (any) {
            cui_modes_free_columns(out_titles, out_formats);
            for (int i = 0; i < MAX_COLUMNS; i++) {
                out_titles[i] = titles[i];
                out_formats[i] = formats[i];
            }
        } else {
            cui_modes_free_columns(titles, formats);
        }
    }
    g_key_file_unref(kf);
}

int cui_modes_columns_present(char *const *titles, char *const *formats) {
    for (int i = 0; i < MAX_COLUMNS; i++) {
        if ((titles[i] && titles[i][0]) || (formats[i] && formats[i][0])) return 1;
    }
    return 0;
}

void cui_modes_free_columns(char **titles, char **formats) {
    for (int i = 0; i < MAX_COLUMNS; i++) {
        g_free(titles[i]);
        titles[i] = NULL;
        g_free(formats[i]);
        formats[i] = NULL;
    }
}

void cui_modes_exchange_alt(cui_widget_t *cw) {
    for (int i = 0; i < MAX_COLUMNS; i++) {
        char *t = cw->titles[i];
        cw->titles[i] = cw->alt_titles[i];
        cw->alt_titles[i] = t;
        char *f = cw->formats[i];
        cw->formats[i] = cw->alt_formats[i];
        cw->alt_formats[i] = f;
        int sid = cw->sort_ids[i];
        cw->sort_ids[i] = cw->alt_sort_ids[i];
        cw->alt_sort_ids[i] = sid;
        int so = cw->sort_orders[i];
        cw->sort_orders[i] = cw->alt_sort_orders[i];
        cw->alt_sort_orders[i] = so;
    }
}

void cui_modes_collect_templates(GKeyFile *kf, GPtrArray *out) {
    gchar **groups = g_key_file_get_groups(kf, NULL);
    for (gchar **g = groups; g && *g; g++) {
        if (g_str_has_prefix(*g, "template:") && (*g)[9]) {
            g_ptr_array_add(out, g_strdup(*g + strlen("template:")));
        }
    }
    g_strfreev(groups);
}

GPtrArray *cui_modes_list_templates(void) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, cui_modes_ini_path(), G_KEY_FILE_NONE, NULL)) {
        cui_modes_collect_templates(kf, arr);
    }
    g_key_file_unref(kf);
    return arr;
}

int cui_modes_template_columns(const char *name, char **out_titles, char **out_formats) {
    for (int i = 0; i < MAX_COLUMNS; i++) {
        out_titles[i] = NULL;
        out_formats[i] = NULL;
    }
    if (!name || !name[0]) return -1;

    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, cui_modes_ini_path(), G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        return -1;
    }
    char *group = g_strdup_printf("template:%s", name);
    int found = g_key_file_has_group(kf, group);
    if (found) {
        cui_modes_parse_group_columns(kf, group, out_titles, out_formats);
    }
    g_free(group);
    g_key_file_unref(kf);
    return found ? 0 : -1;
}

void cui_modes_build_default_keyfile(GKeyFile *kf) {
    g_key_file_set_string(kf, "mode:play", "title1", "Rating");
    g_key_file_set_string(kf, "mode:play", "format1", "%rating%");
    g_key_file_set_string(kf, "mode:play", "title2", "Genre");
    g_key_file_set_string(kf, "mode:play", "format2", "%genre%");
    g_key_file_set_string(kf, "mode:play", "title3", "Album Artist");
    g_key_file_set_string(kf, "mode:play", "format3", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "mode:play", "title4", "Album");
    g_key_file_set_string(kf, "mode:play", "format4", "%album%");

    g_key_file_set_string(kf, "mode:intake", "title1", "Added");
    g_key_file_set_string(kf, "mode:intake", "format1", CUI_RECENT_TOKEN);
    g_key_file_set_string(kf, "mode:intake", "title2", "Codec");
    g_key_file_set_string(kf, "mode:intake", "format2", "%codec%");
    g_key_file_set_string(kf, "mode:intake", "title3", "Genre");
    g_key_file_set_string(kf, "mode:intake", "format3", "%genre%");
    g_key_file_set_string(kf, "mode:intake", "title4", "Album Artist");
    g_key_file_set_string(kf, "mode:intake", "format4", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "mode:intake", "title5", "Album");
    g_key_file_set_string(kf, "mode:intake", "format5", "%album%");

    // The reset-to-standard template goes first so it leads the Templates
    // menu: one click back to the canonical play chain after experimenting.
    g_key_file_set_string(kf, "template:Standard play", "title1", "Rating");
    g_key_file_set_string(kf, "template:Standard play", "format1", "%rating%");
    g_key_file_set_string(kf, "template:Standard play", "title2", "Genre");
    g_key_file_set_string(kf, "template:Standard play", "format2", "%genre%");
    g_key_file_set_string(kf, "template:Standard play", "title3", "Album Artist");
    g_key_file_set_string(kf, "template:Standard play", "format3", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "template:Standard play", "title4", "Album");
    g_key_file_set_string(kf, "template:Standard play", "format4", "%album%");

    g_key_file_set_string(kf, "template:Decades", "title1", "Decade");
    g_key_file_set_string(kf, "template:Decades", "format1", "$if(%year%,$cut(%year%,3)0s,No year)");
    g_key_file_set_string(kf, "template:Decades", "title2", "Genre");
    g_key_file_set_string(kf, "template:Decades", "format2", "%genre%");
    g_key_file_set_string(kf, "template:Decades", "title3", "Album Artist");
    g_key_file_set_string(kf, "template:Decades", "format3", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "template:Decades", "title4", "Album");
    g_key_file_set_string(kf, "template:Decades", "format4", "%album%");

    g_key_file_set_string(kf, "template:Artist Initials", "title1", "Initial");
    g_key_file_set_string(kf, "template:Artist Initials", "format1", "$left($stripprefix(%artist%),1)");
    g_key_file_set_string(kf, "template:Artist Initials", "title2", "Genre");
    g_key_file_set_string(kf, "template:Artist Initials", "format2", "%genre%");
    g_key_file_set_string(kf, "template:Artist Initials", "title3", "Album Artist");
    g_key_file_set_string(kf, "template:Artist Initials", "format3", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "template:Artist Initials", "title4", "Album");
    g_key_file_set_string(kf, "template:Artist Initials", "format4", "%album%");

    g_key_file_set_string(kf, "template:Quality", "title1", "Codec");
    g_key_file_set_string(kf, "template:Quality", "format1", "%codec%");
    g_key_file_set_string(kf, "template:Quality", "title2", "Bitrate");
    g_key_file_set_string(kf, "template:Quality", "format2",
                          "$ifgreater(%bitrate%,199,200+ kbps,$ifgreater(%bitrate%,127,128-199 kbps,Under 128))");
    g_key_file_set_string(kf, "template:Quality", "title3", "Sample rate");
    g_key_file_set_string(kf, "template:Quality", "format3",
                          "$ifgreater(%samplerate%,48000,Hi-res,Standard)");
    g_key_file_set_string(kf, "template:Quality", "title4", "Album");
    g_key_file_set_string(kf, "template:Quality", "format4", "%album%");

    g_key_file_set_string(kf, "template:Tag Gaps", "title1", "Tag gaps");
    g_key_file_set_string(kf, "template:Tag Gaps", "format1",
                          "$if(%genre%,$if(%year%,OK,No year),No genre)");
    g_key_file_set_string(kf, "template:Tag Gaps", "title2", "Genre");
    g_key_file_set_string(kf, "template:Tag Gaps", "format2", "%genre%");
    g_key_file_set_string(kf, "template:Tag Gaps", "title3", "Album Artist");
    g_key_file_set_string(kf, "template:Tag Gaps", "format3", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "template:Tag Gaps", "title4", "Album");
    g_key_file_set_string(kf, "template:Tag Gaps", "format4", "%album%");

    g_key_file_set_string(kf, "template:Year", "title1", "Year");
    g_key_file_set_string(kf, "template:Year", "format1", "%year%");
    g_key_file_set_string(kf, "template:Year", "title2", "Genre");
    g_key_file_set_string(kf, "template:Year", "format2", "%genre%");
    g_key_file_set_string(kf, "template:Year", "title3", "Album Artist");
    g_key_file_set_string(kf, "template:Year", "format3", "$if2(%album artist%,%artist%)");
    g_key_file_set_string(kf, "template:Year", "title4", "Album");
    g_key_file_set_string(kf, "template:Year", "format4", "%album%");
}

void cui_modes_ensure_default_file(void) {
    static int attempted = 0;
    if (attempted) return;
    attempted = 1;

    const char *path = cui_modes_ini_path();
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return;

    GKeyFile *kf = g_key_file_new();
    cui_modes_build_default_keyfile(kf);
    GError *err = NULL;
    if (g_key_file_save_to_file(kf, path, &err)) {
        cui_log(DDB_LOG_LAYER_INFO, "deadbeef-cui: wrote the default mode/template file (cui-modes.ini).\n");
    } else {
        cui_log(DDB_LOG_LAYER_DEFAULT,
                "deadbeef-cui: could not write cui-modes.ini (%s); built-in defaults will be used.\n",
                err && err->message ? err->message : "unknown error");
    }
    g_clear_error(&err);
    g_key_file_unref(kf);
}
