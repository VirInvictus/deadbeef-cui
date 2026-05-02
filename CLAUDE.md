# CLAUDE.md — deadbeef-cui

Operator's manual for AI agents (and humans) working in this repo. **This file overrides `~/CLAUDE.md` for everything scoped to `deadbeef-cui`.** It supersedes `design.md` — that file remains for the documented update workflow but is no longer the architecture reference. Read this end to end before touching code; most of what's in here is not derivable from the source.

---

## 1. What this plugin is

`deadbeef-cui` is a faceted library browser for [DeaDBeeF](https://deadbeef.sourceforge.io/) — a foobar2000 Columns UI / Facets clone implemented as a native GTK3 plugin. It compiles to a single shared object (`cui.so`, installed as `ddb_misc_cui_GTK3.so`) and registers a dockable widget called **"Facet Browser (CUI)"** through GTKUI's design-mode widget API.

What it actually does at runtime:

1. Acquires (or shares) a `medialib` plugin source instance.
2. Builds a `ddb_scriptable_item_t` preset describing the user's column layout (e.g. `%genre%` → `$if2(%album artist%,%artist%)` → `%album%`).
3. Asks medialib for an `ddb_medialib_item_t *` tree.
4. Recursively walks that tree to populate up to 5 `GtkTreeView` panes side by side.
5. On selection change, debounces (10 ms), recomputes downstream panes, and rebuilds a single dedicated playlist (default name: "Library Viewer") with the matching tracks via `plt_clear` + `plt_insert_item`.

The plugin **never owns its own track-list view** — it always pumps results into a DeaDBeeF playlist and lets the standard playlist widget render them. This is intentional (see §10.4).

Plugin metadata lives in `src/main.c:93-108`. The build version, the `w_reg_widget` title string, README badge, and `spec.md` Version line must all match.

---

## 2. Source map (`src/`)

Five files. Treat each module's responsibility as fixed unless you're explicitly refactoring.

| File | Lines | Responsibility |
|---|---|---|
| `src/main.c` | ~110 | Plugin entry points (`cui_start`, `cui_stop`, `cui_message`), the `DB_misc_t` plugin definition, the `Search Facets` action, global symbol exports. **Do not** put UI or medialib logic here. |
| `src/cui_globals.h` | ~85 | The `cui_widget_t` struct, GTK4 compat macros, `MAX_COLUMNS=5`, `CUI_SOURCE_PATH="cui"`, `CUI_DEBUG()` env-gated logging, all `extern` globals. Header included by every TU. |
| `src/cui_widget.c` / `.h` | ~760 | GTK layer: `cui_create_widget`, `rebuild_columns`, key handlers, context menu (`on_tree_button_press`), config dialog, the medialib listener callbacks (`ml_listener_cb` → `g_idle_add` → `ml_event_idle_cb` → debounced `deferred_lib_update_cb`), serialize/deserialize for `ddb_gtkui_widget_extended_api_t`. |
| `src/cui_data.c` / `.h` | ~420 | Tree → list pipeline: `update_tree_data`, `aggregate_recursive_multi`, `populate_list_multi`, `count_tracks_recursive` (memoized via `track_counts_cache`), `add_tracks_recursive_multi`, `track_matches_search` (uses `strcasestr` — do not regress this back to `g_utf8_strdown`). The `[All (...)]` row is synthesised here. |
| `src/cui_scriptable.c` / `.h` | ~110 | A **manually-mirrored** copy of the private `scriptableItem_t` layout from `.deadbeef/shared/scriptable/scriptable.c`. We allocate/populate it ourselves and hand it to `medialib_plugin->create_item_tree`. See §6.2 for why this exists. |

**Splitting rules:**
- New widget/UI code → `cui_widget.c`.
- New medialib traversal/aggregation → `cui_data.c`.
- New globals → `cui_globals.h`.
- Anything that changes the scriptable layout → `cui_scriptable.c`.
- New plugin API hooks (DB_misc_t fields, actions) → `main.c`.

Don't split `main.c` further unless you have a real reason; the historical mistake was the opposite.

---

## 3. `.deadbeef/` — vendored DeaDBeeF source as a reference

`.deadbeef/` is a clone of upstream DeaDBeeF kept solely as a **read-only API reference**. **It is not built, never linked, and never deployed.** `CMakeLists.txt` only adds `/usr/include` for headers — the system-installed `deadbeef-devel` package is what actually provides the compile-time API surface. `.deadbeef/` exists so we can `grep -r` real implementations when the public headers aren't enough.

### 3.1 The map you actually need

```
.deadbeef/
├── include/deadbeef/
│   ├── deadbeef.h          ← THE public plugin API. 2510 lines. Source of truth for every
│   │                         DB_functions_t / DB_plugin_t / ddb_*_t we touch. Look here first.
│   ├── common.h            ← misc. macros, generally not needed
│   ├── fastftoi.h          ← float→int helper, not used here
│   └── strdupa.h           ← strdupa() polyfill
│
├── plugins/
│   ├── gtkui/              ← GTKUI plugin internals. Where we go when deadbeef.h isn't enough.
│   │   ├── gtkui_api.h     ← ddb_gtkui_t, ddb_gtkui_widget_t, ddb_gtkui_widget_extended_api_t,
│   │   │                     DDB_WF_SUPPORTS_EXTENDED_API, DDB_GTKUI_PLUGIN_ID. Read in full
│   │   │                     before changing widget registration or serialization.
│   │   ├── gtkui.c         ← w_reg_widget impl, design-mode plumbing. Look here when a widget
│   │   │                     callback isn't firing as expected.
│   │   ├── widgets.c/.h    ← Widget container/append/replace impls. Reference for how the
│   │   │                     splitter and tabs handle child lifetimes — relevant if we ever
│   │   │                     want to support child-containing widgets.
│   │   ├── medialib/
│   │   │   ├── medialibmanager.c ← `gtkui_medialib_get_source()`. We dlsym this to share the
│   │   │   │                       main UI's medialib source instead of creating our own.
│   │   │   ├── medialibwidget.c  ← How GTKUI's own medialib browser populates its tree.
│   │   │   │                       Closest reference for our update_tree_data flow.
│   │   │   └── mlcellrendererpixbuf.c ← Cell renderer for cover art. Reference if/when we
│   │   │                                ever do an Album Art View (Phase 6 deferred).
│   │   ├── scriptable/
│   │   │   └── gtkScriptable*.c  ← Reusable preset-editor UI. We don't use it; if we ever
│   │   │                           want a polished column-config editor instead of our hand-
│   │   │                           rolled GtkGrid in show_config_dialog, this is the model.
│   │   ├── prefwin/        ← Preferences pages. prefwinmedialib.c shows how to drive
│   │   │                     medialib config (folders list, etc.) from a dialog.
│   │   ├── plmenu.c        ← Standard playlist context menu. Reference if we ever want to
│   │   │                     reuse "Add to playback queue", "Convert", etc. on facet rows.
│   │   ├── search.c        ← How the global search dialog works. Cross-check our search
│   │   │                     entry behavior against this when in doubt.
│   │   ├── hotkeys.c       ← How GTKUI exposes actions to the hotkeys plugin. Our
│   │   │                     `Search Facets` action shows up there because of DB_ACTION_COMMON.
│   │   └── ddb_splitter.c  ← Custom GtkPaned subclass with persisted size mode. We currently
│   │                         use stock gtk_paned_new — switch to this if we want sizes to
│   │                         survive layout reload.
│   │
│   ├── medialib/           ← The plugin we drive. Read this whenever a tree query misbehaves.
│   │   ├── medialib.c      ← Plugin entry, exposes the DB_mediasource_t vtable.
│   │   ├── medialib.h      ← ddb_medialib_plugin_api_t (the *extended* API, accessed via
│   │   │                     get_extended_api). We currently don't use this — it's how you'd
│   │   │                     edit the watched-folders list programmatically.
│   │   ├── medialibtree.c  ← _create_item_tree_from_collection. THIS is what our scriptable
│   │   │                     preset is fed into. Read this if you want to understand exactly
│   │   │                     how our preset becomes the tree we walk.
│   │   ├── medialibtree.h  ← ml_tree_item_s — internal layout. Our ddb_medialib_item_t * is
│   │   │                     a const cast over this. Don't dereference it directly.
│   │   ├── medialibsource.c/.h  ← The source object lifecycle (create_source / free_source).
│   │   ├── medialibscanner.c    ← Folder scanner state machine. Reference for understanding
│   │   │                          DDB_MEDIASOURCE_STATE_* values returned by scanner_state().
│   │   ├── medialibstate.c      ← Persisted state (selected/expanded items).
│   │   ├── medialibdb.c/.h      ← In-memory track database.
│   │   └── scriptable_tfquery.c ← How presets compile to title-formatting bytecode and run
│   │                              against tracks. Critical reading before touching
│   │                              cui_scriptable.c.
│   │
│   └── (every other plugin dir)  ← Decoders, output drivers, hotkeys, mpris, etc. We do not
│                                   touch these. Browse only when the use case is "how does
│                                   the canonical X plugin do Y?".
│
├── shared/
│   ├── scriptable/
│   │   ├── scriptable.h    ← The PUBLIC interface to scriptableItem_t (alloc/free/property
│   │   │                     accessors). DeaDBeeF doesn't actually export these symbols to
│   │   │                     plugins — that's why we mirror the struct in cui_scriptable.h.
│   │   │                     Read this header to confirm field semantics before changing
│   │   │                     our manual layout.
│   │   ├── scriptable.c    ← The implementation. Source of truth for the layout our
│   │   │                     cui_scriptable.h must match. Re-read if upstream ever bumps.
│   │   ├── scriptable_dsp.c, scriptable_encoder.c, scriptable_model.c
│   │   │                   ← Examples of building scriptable trees for different domains.
│   │   │                     The DSP one is the closest analogue to what we do.
│   ├── parser.c, growableBuffer.c, ctmap.c
│   │                       ← Generic helpers DeaDBeeF reuses. Generally don't touch.
│   └── deletefromdisk.c    ← The trash-files action. Reference if we ever surface a delete
│                             item in the context menu.
│
├── examples/
│   ├── decoder_template.c   ← Smallest possible plugin skeletons. Useful if you're starting
│   ├── dsp_template.c       │ over and need to remember what `_load` looks like.
│   └── eventhandler.c       └ Shows the message-handler pattern.
│
├── src/                    ← Core engine (streamer, playlist, conf, message bus, plugin
│                             loader). Almost never need to read this. Useful when:
│                             - Investigating why a sendmessage isn't reaching us
│                             - Understanding playlist locking / iter semantics
│                             - Verifying conf file formats
│
├── Tests/                  ← gtest suite. Reference for how upstream verifies medialib
│                             behavior — closest thing to documented expected behavior.
│
└── (everything else)       ← Build files (configure.ac, premake5.lua, Makefile.am), CI
                              scripts, docs, translations, icons. Ignore.
```

### 3.2 How to use `.deadbeef/` effectively

- **Default search path**: `rg <symbol> .deadbeef/include .deadbeef/plugins/{gtkui,medialib} .deadbeef/shared/scriptable`. That covers ~95% of useful hits. Only widen to `.deadbeef/src` or other plugins when those come up empty.
- **When the public header looks ambiguous**, find a real caller first (`rg "func_name *(" .deadbeef/plugins/`). One real call site beats inferring from the signature.
- **When you change anything that touches `cui_scriptable.h`**, diff against `.deadbeef/shared/scriptable/scriptable.c` first. The struct layout in our header is hand-mirrored; if upstream reordered fields, our code silently corrupts memory.
- **Don't grep `.deadbeef/` for build settings** — its `Makefile.am` / `premake5.lua` describe upstream's build, not ours. Our build is `CMakeLists.txt`, period.

---

## 4. Plugin API touch-points (what we actually call, and where it lives)

The points where this plugin meets DeaDBeeF. Use this table as the lookup index when verifying behavior.

| What we use | Defined in | Notes |
|---|---|---|
| `DB_misc_t plugin` | deadbeef.h | We're a `DB_PLUGIN_MISC` because we don't fit any other category and `DB_PLUGIN_GUI` is reserved for the actual GUI plugin. |
| `ddb_misc_cui_GTK3_load` | (exported by us) | Symbol name **must** match the installed `.so` filename minus the trailing `_GTK3` rules — DeaDBeeF derives the entry-point name from the file. Renaming the install target requires changing this symbol. |
| `plug_get_for_id("medialib")` | deadbeef.h:1354 | Returns NULL if the medialib plugin is disabled. We log and continue with a degraded plugin — the widget still renders, just empty. |
| `plug_get_for_id(DDB_GTKUI_PLUGIN_ID)` | gtkui_api.h:44 | `DDB_GTKUI_PLUGIN_ID` resolves to `"gtkui3_1"` for GTK3 builds, `"gtkui_1"` for GTK2. The macro expands at *plugin* compile time based on which GTK we built against. |
| `gtkui_plugin->w_reg_widget(...)` | gtkui_api.h:221 | Registered with `DDB_WF_SUPPORTS_EXTENDED_API` so our `cw->exapi` block (placed immediately after `base` in `cui_widget_t` — required by the API) is wired up for serialize/deserialize. |
| `gtkui_plugin->w_override_signals` | gtkui_api.h:227 | Required for design-mode (right-click → "Replace with…", drag-to-reorder, etc.). Called from `cui_init`. |
| `gtkui_plugin->w_save_layout_to_conf_key` | gtkui_api.h:314 | Called after the config dialog accepts changes so the per-instance keyvalues persist. Available since GTKUI API 2.6 — guard if we ever target older. |
| `gtkui_medialib_get_source` | plugins/gtkui/medialib/medialibmanager.c:22 | Resolved via `dlopen("ddb_gui_GTK3.so", RTLD_LAZY \| RTLD_NOLOAD) + dlsym`. **Not** part of the public ABI; if upstream renames it, we silently fall back to creating our own source. The fallback works but is slower (full second source = full second scan). |
| `medialib_plugin->create_source` | deadbeef.h:2425 | We **must** pass `"cui"` (not `"deadbeef"`) — see §6.1. |
| `medialib_plugin->create_item_tree` | deadbeef.h:2458 | Takes our scriptable preset. Returns NULL during scan or on memory pressure — handle it. |
| `medialib_plugin->free_item_tree` | deadbeef.h:2461 | Must be called before re-creating, and before destroying the source. Cached in `cw->cached_tree`. |
| `medialib_plugin->add_listener` / `remove_listener` | deadbeef.h:2450/2453 | **Listener fires on a background thread** (deadbeef.h:2448-2449 says so). Always `g_idle_add` to dispatch. |
| `medialib_plugin->scanner_state` | deadbeef.h:2464 | We poll this from the idle callback to skip refreshes during ongoing scans. |
| `medialib_plugin->refresh` | deadbeef.h:2443 | User-triggered (right-click → Sync library) and once after we create our own source. **Never** call from `DB_EV_TRACKINFOCHANGED` — that's the Gemini-Flash-era trap. |
| `medialib_plugin->tree_item_get_text/_track/_next/_children` | deadbeef.h:2486-2495 | The tree is immutable for the caller's lifetime. Don't free children — `free_item_tree` walks the whole thing. |
| `deadbeef_api->plt_*` | deadbeef.h ~970-1100 | Standard playlist API. Always wrap mutations with `pl_lock`/`pl_unlock` (we do in `populate_playlist_from_cui`). `plt_get_*` returns refcounted handles — pair with `plt_unref`. |
| `deadbeef_api->pl_item_alloc/copy/ref/unref/insert_item` | deadbeef.h ~1130-1200 | We **copy** tracks into the viewer playlist rather than referencing the medialib's. Copies are cheap; sharing tracks across playlists has subtle interaction issues with the playqueue. |
| `deadbeef_api->conf_get_str_fast` | deadbeef.h:1331 | **Not thread-safe.** Wrap in `conf_lock`/`conf_unlock`. We do for the bootstrap defaults read in `cui_create_widget`. |
| `deadbeef_api->sendmessage(DB_EV_PLAYLISTCHANGED, ...)` | deadbeef.h:512 | Send after mutating the viewer playlist so the playlist widget repaints. `DB_EV_TRACKINFOCHANGED` is for fine-grained per-track updates — don't conflate. |
| `DB_EV_TERMINATE` | deadbeef.h:504 | Our `cui_message` flips `shutting_down=1` here. Every long-lived callback checks it. |

If you add a new API call, add a row.

---

## 5. Build, install, run

### 5.1 Build

```bash
cmake -S . -B build
cmake --build build
```

Output: `build/cui.so`.

The build is `-Wall -Wextra -fPIC`, C11. There's currently **no warning policy** — be careful when adding code that the warning count doesn't grow. Don't slap `-Werror` on without a sweep first; the existing code has a few `(void)` casts but is otherwise clean.

### 5.2 Install

```bash
mkdir -p ~/.local/lib/deadbeef
cp build/cui.so ~/.local/lib/deadbeef/ddb_misc_cui_GTK3.so
```

The destination filename matters. DeaDBeeF derives the `_load` entry-point symbol from the file basename — the symbol `ddb_misc_cui_GTK3_load` is wired to the `ddb_misc_cui_GTK3.so` filename. If you rename the installed file, also rename the export in `main.c`.

### 5.3 Run / debug

- **Verbose plugin log**: launch `deadbeef --gui GTK3 -d` from a terminal. Our `fprintf(stderr, ...)` lines and the registration message land there.
- **Our own debug log**: set `DEADBEEF_CUI_DEBUG=1` in the environment. `CUI_DEBUG(...)` (defined in `cui_globals.h`) prints to stderr. There are call-sites in `cui_create_widget`, `update_tree_data`, `update_playlist_from_cui`, `init_my_preset`, and the dlsym path. Use it; don't add raw `fprintf`s.
- **Reload the plugin without restarting**: not possible — DeaDBeeF doesn't unload `DB_PLUGIN_MISC` plugins cleanly. Restart DeaDBeeF after every install.
- **Crash on shutdown** is the canonical bug shape — see §6.3.

### 5.4 Compiled binary

`compiled/` contains a pre-built `.so` for the current release. It's in git for users who don't want to build. Update it (and the version triple) only when cutting a release.

---

## 6. Critical invariants

Each rule here is here because violating it has caused, or will cause, a real bug. They are not stylistic preferences.

### 6.1 The medialib source path **must** be `"cui"`, not `"deadbeef"`

`CUI_SOURCE_PATH` in `cui_globals.h:37`. The GTKUI's own medialib browser creates its source with `create_source("deadbeef")`. If we use the same path, both plugins write to the same on-disk state files (`~/.config/deadbeef/medialib.deadbeef.dat`), interleave scanner runs, and corrupt each other's selection state. Always `"cui"`.

The shared-source dlsym shortcut (`gtkui_medialib_get_source`) bypasses this — when we use the GTKUI's source, we don't call `create_source` at all and `owns_ml_source` stays `0`. Only the fallback path creates our own.

### 6.2 The `scriptableItem_t` mirror is hand-maintained

DeaDBeeF doesn't export `scriptableItemAlloc` etc. to plugins, so we can't use the public scriptable API. We declare the struct ourselves in `cui_scriptable.h:6-22` and **the layout must byte-match** `.deadbeef/shared/scriptable/scriptable.c`. Verified at the time of writing; if you bump the `.deadbeef/` clone, re-verify before merging:

```bash
diff <(grep -A30 'struct scriptableItem_s' .deadbeef/shared/scriptable/scriptable.c) src/cui_scriptable.h
```

The fields we care about (in order): `next`, `flags`, `properties`, `parent`, `children`, `childrenTail`, `type`, `configDialog`, `overrides`. We only write `flags` and `properties`/`children` — the rest are zeroed by `calloc`. `medialib_plugin->create_item_tree` reads `flags & SCRIPTABLE_FLAG_IS_LIST` to know the root is a list, then walks children for the column hierarchy.

### 6.3 Shutdown is async — `shutting_down` must be checked everywhere

`gtkui_stop()` schedules `quit_gtk_cb` on GTK's idle loop. Widget `destroy` callbacks fire **before** our `cui_stop`. That means:

- A medialib listener can fire after the widget is destroyed but before `cui_stop` removes it.
- An idle callback enqueued by the listener can run on a freed `cui_widget_t *`.
- A `g_timeout_add` debounce can fire on a freed widget.

Defenses, all required:

- `shutting_down` is set both in `cui_message(DB_EV_TERMINATE)` and at the top of `cui_stop`.
- Every idle / timeout callback (`deferred_lib_update_cb`, `ml_event_idle_cb`, `deferred_column_changed_cb`, `restore_vscroll_idle`) **first** checks `shutting_down`, **then** verifies `g_list_find(all_cui_widgets, cw)` before dereferencing.
- `cui_destroy` removes the widget from `all_cui_widgets` **before** freeing anything else.
- `cui_destroy` cancels both pending timeouts (`changed_timeout_id`, `lib_update_timeout_id`).
- `cui_destroy` calls `medialib_plugin->remove_listener` *before* freeing the cached tree.

When you add any new async work touching a `cui_widget_t *`, add the same two-step guard. **No exceptions.** This is the single most damage-prone area of the codebase — see §11.

### 6.4 The medialib listener fires on a background thread

`add_listener`'s contract (deadbeef.h:2448-2449) explicitly says so. `ml_listener_cb` does *only* three things: check `shutting_down`, atomic-increment `ml_modification_idx`, and `g_idle_add(ml_event_idle_cb, cw)`. It must not touch widget state, GTK, or the playlist directly. If you "just need to log the event type" — log it, but don't reach into `cw`.

### 6.5 Selection-change handling is debounced (10 ms)

`on_column_changed` records the leftmost changed column in `cw->changed_col_idx` and arms `deferred_column_changed_cb` once. Without this, dragging across a column with multi-select fires hundreds of cascading rebuilds. Don't switch this to synchronous "for clarity" — it's load-bearing for selection performance on 50k-track libraries.

### 6.6 `update_tree_data` rebuilds atomically

`update_tree_data` (cui_data.c:310) is the choke point. It:
1. Saves current per-column selection texts into `saved_sels[]`.
2. Frees the cached tree and the track-counts cache.
3. Re-creates the tree from the scriptable preset.
4. Allocates a fresh `track_counts_cache`.
5. Populates column 0.
6. Walks `saved_sels[]` and re-applies selections (with signal blocking around `gtk_tree_selection_select_iter`) so downstream columns repopulate.
7. Schedules a vscroll restore on the idle loop (so it runs after GTK's own scroll-position recompute).
8. Stores `current_idx` in `cw->last_ml_modification_idx` to short-circuit redundant calls.

The modification-index check at the top is what prevents infinite loops with the listener. **Do not** remove the assignment on line 420 — there was a bug where the check ran but the assignment didn't, causing every call to do a full rebuild (fixed in v1.2.0). The cache-skip behavior is correct only if both halves are present.

### 6.7 Search invalidates the modification cache

When `search_text` changes, `update_tree_data` resets `last_ml_modification_idx = -1` (cui_data.c:323). This forces a full rebuild because the rebuild path is *also* where the search predicate (`track_matches_search`) gets applied — counts and visibility depend on the current search string.

### 6.8 The track-count cache is valid under search, but only because §6.7 always resets it

As of v1.2.5, `count_tracks_recursive` uses `cw->track_counts_cache` regardless of `cw->search_text`. The values are correct because §6.7 forces `last_ml_modification_idx = -1` whenever `search_text` changes, which makes the next `update_tree_data` destroy and recreate the cache. The two rules are coupled: if you ever reorder the rebuild path so search changes can hit `count_tracks_recursive` against an old cache, this breaks silently (returns last-search counts under the new filter). Either keep the §6.7 reset or re-key the cache by search string.

### 6.9 The `[All]` aggregate row is synthesised, not from the tree

`populate_list_multi` inserts the `[All (N Plurals)]` row with the third store column (`is_all`) set to `TRUE`. The selection hash in `update_selection_hash` checks this column — selecting an `[All]` row destroys the per-text hash entirely (a NULL hash means "all values match downstream"). Don't change the column count of the `GtkListStore` (currently 3: text, count, is_all) without auditing every reader.

### 6.10 The pluralization rule has a deliberate exception

cui_data.c:264-267: `"Album Artist"` collapses to `"Artist"` for the `[All]` label so it reads `[All (123 Artists)]` instead of `[All (123 Album Artists)]`. Don't generalize this — it's a single, deliberate special case for the most common column header.

### 6.11 `pl_lock` is not reentrant; don't take it around `track_matches_search` callers

`track_matches_search` already takes `pl_lock` internally. `aggregate_recursive_multi` and `populate_list_multi` don't lock — they only read tree text, which medialib already considers immutable for the caller. Don't add an outer `pl_lock` around tree traversal "for safety"; you'll deadlock against the streamer.

### 6.12 Track copies, not references, into the viewer playlist

`add_tracks_recursive_multi` does `pl_item_alloc` + `pl_item_copy`, not `pl_item_ref`. Sharing the same `DB_playItem_t *` across the medialib's internal list and the user-facing playlist breaks playqueue semantics and "now playing" tracking. The copy is cheap; keep it.

---

## 7. Configuration model

There are two layers, and migration between them is **read-once, write-never**:

### 7.1 Legacy global config (`cui.col1_format`, etc.)

Pre-1.2.2 the plugin used flat `conf_get_str/set_str` with global keys. New widgets created today will read these as defaults if no per-instance config exists yet (`cui_create_widget` in `cui_widget.c:673`). Once read, those values never get written back to global keys — they migrate into the per-instance keyvalue store the next time the layout is saved.

### 7.2 Per-instance keyvalues (current)

Persisted via `ddb_gtkui_widget_extended_api_t`. Keys: `col1_title`..`col5_title`, `col1_format`..`col5_format`, `split_tags`, `ignore_prefix`, `autoplaylist_name`. Serialization in `cui_serialize_to_keyvalues` (cui_widget.c:451), deserialization in `cui_deserialize_from_keyvalues` (cui_widget.c:485). The `found_any` guard ensures we only clobber defaults when the saved layout actually contains real column data.

When you add a new option:
1. Field on `cui_widget_t` in `cui_globals.h`.
2. Default in `cui_create_widget` (both branches — the bootstrap and the migration path).
3. Pair of entries in `cui_serialize_to_keyvalues` and `cui_deserialize_from_keyvalues`.
4. Widget in `show_config_dialog` and read-back in `on_config_dialog_response`.
5. If it affects the scriptable preset, plumb through `init_my_preset`.
6. Invalidate the cached tree on change (set `last_ml_modification_idx = -1` and call `update_tree_data`).

### 7.3 Source-config sync

`sync_source_config` (cui_widget.c:98) copies `medialib.deadbeef.paths` to `medialib.cui.paths` and enables our source. It only runs on the fallback (own-source) path, not when we share GTKUI's source. Run once at source-creation time only — it's not a continuous mirror.

---

## 8. Title formatting & the scriptable preset

`init_my_preset` (cui_scriptable.c:49) builds the tree DeaDBeeF will hand back to us. The structure:

```
root (SCRIPTABLE_FLAG_IS_LIST, name="Facets")
├── child name=<format string for column 1>   [+ split="; " if cw->split_tags]
├── child name=<format string for column 2>   [+ split="; " if cw->split_tags]
├── ...
└── leaf  name="%title%"                      ← always last; this is where tracks live
```

`scriptable_tfquery.c` in the medialib plugin compiles each `name` property as a title-formatting expression. The `split` property tells it to break multi-value tags on the given separator.

**Adding a new per-column option** (e.g. case-insensitive grouping): add a property to the child via `my_scriptable_set_prop`, then verify `.deadbeef/plugins/medialib/scriptable_tfquery.c` actually consumes that key. Inventing keys the medialib doesn't read is silent failure.

**Title formatting v2 migration** (Phase 8 roadmap item) means switching from raw `name=<format>` to `tf_compile`'d bytecode. That requires `deadbeef_api->tf_compile` (deadbeef.h:1510), `tf_eval` (deadbeef.h:1521), and a `ddb_tf_context_t` (deadbeef.h:785-818). The current scriptable interface compiles internally, so v2 only matters if we move evaluation client-side. Don't take this on without a measured win.

---

## 9. Update workflow (condensed from `design.md`)

`design.md` is still the canonical source for the textual workflow. The bullet-point version, in priority order:

1. **`patchnotes.md`** — append a new section for any user-visible change, bug fix, or refactor. Be specific: what changed, why, which files. Pre-existing entries are reverse-chronological with the current version on top.
2. **Version bump** — only when the user says we're cutting a release. Touch `src/main.c` (`.plugin.version_minor`, `.plugin.descr`, the `w_reg_widget` title string), `README.md` (badge + the registration version reference), `spec.md` (Version line). Semantic versioning.
3. **`roadmap.md`** — flip `[ ]` to `[x]` for completed items. Add new entries to the appropriate phase, or to "Deferred (v2.0+)" if they're scope-creep.
4. **`README.md` / `spec.md`** — only on architectural changes (new dependency, new GTK version, new top-level feature surface). Don't churn for cosmetic fixes.
5. **This file (`CLAUDE.md`)** — when an invariant changes, when the source map shifts, or when a new API touch-point is introduced. Sections 2, 4, and 6 are the most likely to need updates.
6. **`design.md`** — only if Section 2 ("What Code Does What Where") needs to change. Otherwise, prefer updating this file.
7. **Commit** — one logical change per commit. Never push without being asked. Follow `git log` style for messages.

---

## 10. Common pitfalls (with citations)

### 10.1 Don't recreate the medialib source on every widget

`ml_source` is **process-global**, not per-widget. Multiple Facet Browser instances share one source, scanner, and tree cache. The widget-creation path in `cui_init` is guarded by `if (!ml_source && medialib_plugin)`. If you ever change this to per-widget, you'll trigger N parallel scans.

### 10.2 Don't subscribe to `DB_EV_TRACKINFOCHANGED`

It fires on every play, skip, pause, and playqueue mutation — not just metadata edits. The pre-v1.2.0 version handled this event and rebuilt the tree on every play, scrolling all facets back to the top. The handler is gone; the test for "a new tag was edited" is now a user-initiated **Sync library** menu action instead. Don't reinstate the handler.

### 10.3 Don't ref-count the GtkMenu manually

The right-click menu uses `gtk_menu_popup_at_pointer` (cui_widget.c:255). GTK takes a floating reference; the menu is destroyed when dismissed. The pre-v1.1.0 code leaked menus by holding an extra ref. Trust GTK's lifecycle here.

### 10.4 Don't build a custom track-list view

This was deliberately decided: the plugin populates DeaDBeeF's playlist widget. Reasons (from `spec.md` §4 and historical context):

- Avoids reimplementing playqueue, focus tracking, drag-drop, column customization, replaygain UI, etc.
- The "Library Viewer" playlist is a real playlist — users can save it, modify it, send tracks elsewhere. A custom widget would be a one-way view.
- Performance: GTKUI's playlist already handles 50k+ rows efficiently. We'd just be reinventing it.

Push back on any feature request that requires its own track view.

### 10.5 Don't bypass the debounce on selection change

The debounce (`changed_timeout_id`, 10 ms in `on_column_changed`) protects against multi-select drags. Direct synchronous calls to `populate_list_multi` from selection callbacks were a Gemini-Flash regression — reverted, do not reintroduce.

### 10.6 Don't pass `NULL` to `g_utf8_collate` or `strcasestr`

`sort_func` early-outs if either name is NULL (cui_data.c:71). `track_matches_search` checks `title || artist` before calling. New comparison code must do the same.

### 10.7 Don't use `gtk_widget_destroy` on the main `cw->base.widget`

GTKUI owns it. Our `cui_destroy` cleans up children and our own state; the toplevel widget is destroyed by the framework. The pattern in `rebuild_columns` (destroy children, re-add new tree of children) is correct because it doesn't touch the parent.

### 10.8 Don't introduce GTK4-only code paths without the compat shim

`cui_globals.h:13-34` already maps the GTK3 API surface we use to GTK4 equivalents. New GTK calls that differ between major versions need a shim added there. The build defaults to GTK3; the GTK4 path exists to keep us forward-compatible (Phase 6, completed).

### 10.9 Beware `conf_get_str_fast` outside `conf_lock`

It's not thread-safe and the returned pointer is invalidated by the next config write. We use it inside `conf_lock`/`conf_unlock` only. If you need a value held across a long operation, copy it with `g_strdup` before unlocking.

---

## 11. The Gemini Flash incident — why this file exists

Between v1.1.0 and v1.2.0, an AI assistant (Gemini Flash) was given free rein on this repo and produced 14 commits attempting to debug shutdown crashes and listener races. All of them were reverted. The damage pattern was instructive:

- Listener callbacks were "fixed" by adding direct widget access on the background thread (the opposite of the right answer).
- Shutdown was "fixed" by adding `w_unreg_widget` calls in `cui_stop` (which crashed because GTKUI had already torn down).
- Debouncing was removed "for clarity," causing performance regressions on multi-select.
- The modification-index cache was edited until it was structurally broken and silently always rebuilding.
- The track-count cache was kept across search-string changes, returning wrong counts.

The fixes that actually held are codified in §6 above. **If you're tempted to "simplify" something in §6, find the patchnotes entry that put it there first.** Most rules trace back to a specific commit and a specific symptom.

The user's standing instruction: **stable and clean over clever**. Match style, leave the architecture alone, and don't refactor into the next problem.

---

## 12. What to read before doing each kind of change

| You're about to… | Read first |
|---|---|
| Add a new column option | §7, `cui_widget.c:451-538`, `cui_scriptable.c:49-107` |
| Touch the medialib listener path | §6.3, §6.4, `cui_widget.c:723-763`, `.deadbeef/plugins/medialib/medialib.c` listener emission |
| Change the scriptable preset shape | §6.2, §8, `.deadbeef/plugins/medialib/scriptable_tfquery.c`, `.deadbeef/shared/scriptable/scriptable.c` |
| Modify shutdown sequence | §6.3, `cui_widget.c:401-449`, `main.c:45-57` |
| Add a context-menu item | `cui_widget.c:215-259`, `.deadbeef/plugins/gtkui/plmenu.c` for reuse opportunities |
| Add a new keyboard shortcut | `main.c:59-91` (action), `cui_widget.c:120-163` (per-widget keypress), `.deadbeef/plugins/gtkui/hotkeys.c` |
| Optimize counting/aggregation | §6.6, §6.8, `cui_data.c:22-48` and `:191-288` |
| Bump the GTKUI API level we require | gtkui_api.h, ensure `DDB_GTKUI_API_LEVEL` guards on every newer-than-baseline call |
| Cut a release | §9, plus `compiled/` rebuild |

---

**End of CLAUDE.md.** When in doubt, ask — Brandon's standing preference is "I don't know, should I look?" over a confident guess that touches the wrong file.
