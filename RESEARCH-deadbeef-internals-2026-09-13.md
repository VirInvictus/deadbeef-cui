# RESEARCH: DeaDBeeF internals for the deadbeef-cui fix blitz

**Produced:** night of 2026-09-12, for the fix lane on 2026-09-13.
**Method:** 5 research-only subagents (GTKUI API deep-dive; our widget tree + fix design; ecosystem survey; issue #1 forensics + bug hunt; improvement backlog), synthesized in the main thread. Every load-bearing fact was verified from source in `/home/bdkl/.gitrepos/deadbeef` (upstream reference clone, read-only) and `/home/bdkl/.gitrepos/deadbeef-cui` (ours). No builds, no live DeaDBeeF runs, no installs. Nothing committed; this is the only file created in the repo. Third-party survey clones live in `/tmp/deadbeef-plugin-survey/`.

**Headline:** issue #1 is confirmed and is worse than the audit thought: the crashing call is deterministic on every DeaDBeeF 1.10.1+ runtime, including the dev machine (which actually runs 1.10.3, not 1.10.2). The prior audit's ABI-window story ("landed after tag 1.10.2") is refuted. The fix is to delete the call entirely; it never worked and is fully redundant. One masked bug (blank panes after dialog OK) must be fixed in the same commit, or deleting the crash exposes it.

---

## 1. Upstream state (established in the main thread)

- Clone: `/home/bdkl/.gitrepos/deadbeef`, origin `https://github.com/DeaDBeeF-Player/deadbeef`, branch `master`, working tree clean.
- HEAD: `140f284cd78736b2e88de23d3760ebee5ae3d191`, 2026-08-12, "sid: Add config option for selecting clock speed - Auto (default), PAL, NTSC (#3363)".
- Tags: the full 1.x tag set is present. Both reporter-relevant tags exist: `1.10.2` and `1.10.3` (plus `1.10.0`, `1.10.1`). Tagged-commit dates: 1.10.0 = 2025-03-26, 1.10.1 = 2026-03-08, 1.10.2 = 2026-03-16, 1.10.3 = 2026-06-10.
- Dev machine (checked tonight via `rpm -q`, package metadata only): `deadbeef-1.10.3-1.fc44.x86_64` and `deadbeef-devel-1.10.3-1.fc44.x86_64` are installed; `/usr/lib64/deadbeef/ddb_gui_GTK3.so` is dated 2026-06-10 (the 1.10.3 cut). **The README and roadmap statements "tested on DeaDBeeF 1.10.2" are stale: this machine currently runs 1.10.3.** Since 1.10.2 and 1.10.3 both contain the crashing API (section 2.1), the only consistent explanation for "never crashed locally" is that no Configure Facets OK click has happened on this machine since the call was introduced on 2026-04-24. The crash is latent here right now, one click away.

## 2. Findings by subagent

### 2.1 Agent 1: GTKUI API deep-dive

**The three 2.6 tail members.** `w_load_layout_from_conf_key`, `w_save_layout_to_conf_key`, and `w_send_message` were added to `ddb_gtkui_t` by upstream commit `9caadf5b1` (2025-10-30, "gtkui: Added plugin API for saving/loading of widget layout (#3242)"), inside `#if (DDB_GTKUI_API_LEVEL >= 206)` (`plugins/gtkui/gtkui_api.h:303-318`). `git tag --contains 9caadf5b1` = **1.10.1, 1.10.2, 1.10.3** (not 1.10.0). `gtkui_api.h` is byte-identical across 1.10.1 = 1.10.2 = 1.10.3 = master, and `git diff 1.10.2 1.10.3 -- plugins/gtkui` is empty. **The prior audit's claim that the API landed after tag 1.10.2 is false.**

**Contract** (`gtkui_api.h:311-314`, identical at master and 1.10.3):

```c
/// Serialize a widget to the config.
/// Works similarly as the conf_set_* functions.
/// val must be non-NULL.
/// Returns 0 if successful.
int (*w_save_layout_to_conf_key) (const char *key, ddb_gtkui_widget_t *val);
```

Parameter order is **(key, val)**: our call `w_save_layout_to_conf_key("layout", NULL)` passes the string as `key` and NULL as the documented non-NULL `val`.

**Implementation** (`w_save_to_conf`, `plugins/gtkui/widgets.c:719-737`): no NULL check on `val`; first statement is `_save_widget_to_json(val)`, whose first use of the argument is `if (!strcmp (w->type, "unknown"))` (`widgets.c:640-642`). With `val == NULL` that reads address 0: **deterministic SIGSEGV on every runtime that has the member at all (1.10.1+)**. It writes with `deadbeef->conf_set_str` and does not flush the config to disk.

**The caller pattern to mirror** (`w_save`, `widgets.c:740-748`):

```c
void w_save (void) {
    if (rootwidget == NULL) return;
    w_save_to_conf (DDB_GTKUI_CONF_LAYOUT, rootwidget->children);
    deadbeef->conf_save ();
}
```

`rootwidget` is a file-static (`widgets.c:296`, created as `w_create("box")` in `w_init`); `w_get_rootwidget()` (`widgets.c:324-327`, public since 2.0) returns it. gtkui passes `rootwidget->children` (the first top-level widget), not the root itself, under `DDB_GTKUI_CONF_LAYOUT` = `"gtkui.layout.1.9.0"` (defined in the **internal** header `plugins/gtkui/widgets.h:29`, not exported to plugins; the key name has migrated before, 0.6.2 to 1.9.0). `w_save` fires from `gtkui_quit_cb` (`gtkui.c:1806-1808`, every clean quit) and from the design-mode replace/delete/cut/paste handlers (`widgets.c:765, :784, :813, :852`).

**Member version map** (all verified by introducing commit + `git tag --contains`): `get_mainwin` (2010), the widget API block `w_reg_widget`/`w_unreg_widget`/`w_create`/`w_append`/... (2012), `w_override_signals` (2013), `w_set_design_mode` (2013), `w_get_container` (2013), copy/cut/paste_selection (2016) are all present in every 1.10.x release. `DDB_WF_SUPPORTS_EXTENDED_API` + `ddb_gtkui_widget_extended_api_t` landed 2021-12-27 (`a732f3229`), first release **1.9.0** (2022-05-13; finer 1.9.x mapping from agent 3). The only members newer than 1.10.0 are the three 2.6 tail members above (1.10.1+).

**Version-guard conventions.** Three layers: (1) compile-time `#if (DDB_GTKUI_API_LEVEL >= NNN)` blocks in `gtkui_api.h`; (2) the core loader gate on the deadbeef plugin API (`src/plugins.c:777-781`, refuses `api_vminor` newer than the runtime); (3) the runtime sub-API probe: gtkui publishes its API version as plugin fields, `.gui.plugin.version_major = DDB_GTKUI_API_VERSION_MAJOR` (2), `.version_minor = DDB_GTKUI_API_VERSION_MINOR` (6 on 1.10.1+; 2.5 on 1.9.x-1.10.0) (`gtkui.c:2344-2346`). The canonical check macro is `PLUG_TEST_COMPAT(plug,x,y)` (`deadbeef.h:273`): `((plug)->version_major == (x) && (plug)->version_minor >= (y))`. Best in-tree example: `plugins/converter/convgui.c:1828-1835` (checks `&converter_plugin->misc.plugin` before using newer members, refuses with a log line). **`ddb_gtkui_t` has no `_size` field**, so member-presence checks read struct-tail garbage on older runtimes and are unsound; version_major/minor is the only sound runtime channel. (The exapi struct DOES have `_size`, and gtkui defensively re-checks it: `widgets.c:544-546, :653-655`.)

**Serialization.** `_save_widget_to_json` (`widgets.c:640-695`) writes one JSON object per widget: `{"type": ..., "settings": {...}?, "children": [...]}`. For widgets registered with `DDB_WF_SUPPORTS_EXTENDED_API` and `api->_size >= sizeof(ddb_gtkui_widget_extended_api_t)`, it calls `api->serialize_to_keyvalues(w)` and stores the result under `"settings"` (`widgets.c:653-665`). **This is where our per-instance keyvalues ride along automatically; no plugin-side save call is needed.** Read-back at startup: `init_widget_layout` (`gtkui.c:1207-1270`) reads `DDB_GTKUI_CONF_LAYOUT`, `w_create_from_json` (`widgets.c:497-601`) rebuilds each widget through the type registry and calls `api->deserialize_from_keyvalues` (`widgets.c:548-561`). A well-formed root for `w_save_layout_to_conf_key` is what gtkui itself passes: `rootwidget->children`, the user's top-level layout widget. Passing a child serializes just that subtree.

### 2.2 Agent 2: our widget tree mapped to GTKUI + fix design

**Lifecycle.** `src/main.c:54`: `gtkui_plugin->w_reg_widget("Facet Browser (CUI) v1.3.3", DDB_WF_SUPPORTS_EXTENDED_API, cui_create_widget, "cui", NULL)`. `cui_widget_t` layout (`src/cui_globals.h:55-57`): `ddb_gtkui_widget_t base;` is the first member and `ddb_gtkui_widget_extended_api_t exapi;` sits immediately after it, exactly as the upstream contract requires. `cui_create_widget` (`src/cui_widget.c:1104-1161`) sets `exapi._size = sizeof(ddb_gtkui_widget_extended_api_t)` plus the three callbacks (`:1142-1145`); upstream gates on that `_size` (`widgets.c:655`), so our side is correct. `w_override_signals` at `:783-785`; serialize/deserialize/free at `:890-977`.

**Crash path.** `on_config_dialog_response` (`src/cui_widget.c:979-1041`): on `GTK_RESPONSE_ACCEPT` it reads the grid into `cw->titles/formats` (`:992-1023`), frees and rebuilds the preset (`:1027-1031`), `rebuild_columns` (`:1032`), `update_tree_data` (`:1033`), then `:1035-1037`:

```c
if (gtkui_plugin && gtkui_plugin->w_save_layout_to_conf_key) {
    gtkui_plugin->w_save_layout_to_conf_key("layout", NULL);
}
```

Mapping onto the real signature: `"layout"` binds to `key`, **NULL binds to `val`**; the fault is upstream `widgets.c:640-642` (two agents cite 641 vs 642 for the same `strcmp` line; numbering differs slightly by tag/context, the statement is unambiguous). The audit's phrase "passes NULL" is correct and precise: NULL is the second parameter.

**Intent and redundancy.** Introduced in our commit `6e80021` (2026-04-24, "Phase 7 Increment 2: Instance-Specific Settings") with the comment "Best effort, layout is global, might need user to save layout". Verdict: **the call is redundant even when fixed.** Per-instance settings live in the in-memory widget tree after Save and are serialized by the quit-time `w_save()` via our exapi hook; `w_save` also fires on every design-mode structural edit. The call has NEVER worked: on 1.10.1+ it segfaults; on pre-1.10.1 runtimes the guard read struct-tail garbage (zeros made it skip: no crash, no save). The only theoretical gap is persistence if the player crashes between dialog Save and quit.

**Fix options, ranked:**

1. **Delete the call. Recommended.** Fixes the crash on every version by construction; deletes dead code; no working behavior lost (nothing to regress); no coupling to private upstream constants. Must ride with the H1 invalidation (section 4) in the same commit.
2. **Version-guarded flush (fallback, only if flush-on-Save is ever deemed a requirement):** `if (PLUG_TEST_COMPAT(&gtkui_plugin->gui.plugin, 2, 6))` then `w_get_rootwidget()` + `root->children` + key `"gtkui.layout.1.9.0"` + `deadbeef->conf_save()`. Correct, but hardcodes two private upstream details (the unexported key string and the root-children convention).
3. **Version-guard alone: incomplete.** A passing guard still hands upstream NULL `val` and a dead key.
4. **Pass `(ddb_gtkui_widget_t *)cw`: DISQUALIFIED.** Would not crash, but it would overwrite the entire user layout conf with JSON describing only our widget; next start, the main window would be nothing but the Facet Browser. Silent user-data destruction.
5. **Cui-specific conf key: rejected.** Creates a second persistence channel alongside exapi with no defined winner on restore; this repo has already been burned by dual config sources (`autoplaylist_name`, CLAUDE.md §7.2).

**Regression-test design.** Today `tests/mock_deadbeef.c:10` defines the global `gtkui_plugin` but never populates it, so no test reaches any gtkui path. Design: populate it with a fake vtable in the mock: `gui.plugin.version_major/minor` switchable (2/6 and 2/5), a recording `w_save_layout_to_conf_key(key, val)` stub that captures its arguments and fails on `val == NULL`, and a switchable `w_get_rootwidget()` returning a fake root with a child. Tests (existing GTest style, `g_test_add_func` next to the block at `tests/test_cui.c:296-305`; no CMake change needed, `cui_tests` already links `src/cui_widget.c`):

- `/cui/config/save_layout` (the issue-#1 tripwire): with the recording stub installed, assert `mock_w_save_layout_called == 0` after exercising the reachable engine paths. Under the delete-fix this locks "this plugin never calls `w_save_layout_to_conf_key`, therefore never passes NULL". The stub itself also hard-asserts `val != NULL`, so any future reintroduction anywhere trips it.
- `/cui/config/save_layout_version_guard` and `/cui/config/save_layout_no_root`: only meaningful if the guarded-flush option is taken instead (guard off at 2.5; NULL root never reaches upstream).

**Honest limit:** `on_config_dialog_response` digs a GtkGrid out of a live GtkDialog and is not callable headless, so the unit suite tests the mock tripwire, not the dialog wiring. The dialog path itself is verified by the live smoke test in the checklist (section 6, step 9). If the flush option were taken instead, factor the flush into `void cui_save_layout(void)` (declared in `cui_widget.h`) as the testable seam.

### 2.3 Agent 3: plugin ecosystem survey

**In-tree.** `rg` sweep: only `pltbrowser` and `lyrics` are widget-registering external-style plugins (convgui/shellexecui are dialog plugins). pltbrowser, the closest structural analogue: registers with flags 0 (`plugins/pltbrowser/pltbrowser.c:915`), persists via global `gtkui.pltbrowser.*` conf keys, and **never calls any 2.6 member, so it needs no guard at all**. The canonical per-instance (exapi) flow is gtkui's own widgets (tabs `widgets.c:2004-2007`, scope `:2724-2727`, spectrum `:3225-3228`, volumebar `:4365-4368`, albumart `albumartwidget.c:367-370`): `_size` + three pointers, registered with the flag; **none of them ever calls `w_save_layout_to_conf_key`**; they rely on quit-time and design-mode `w_save()`.

**Third-party** (all cloned to `/tmp/deadbeef-plugin-survey/`):

| Plugin | Guard / persistence pattern | Citation |
|---|---|---|
| `cboxdoerfer/ddb_waveform_seekbar` (wiki-listed, AUR/openSUSE/FreeBSD) | Runtime probe `gtkui_plugin->gui.plugin.version_major == 2` **before any vtable member call**; refuse load (-1) on failure | `waveform.c:1336-1343` |
| `saivert/ddb_misc_headerbar_GTK3` (AUR, openSUSE) | Probe `version_major >= 2`; on failure prints a user-facing requirement message and refuses; soft-dependency checks (`plug_get_for_id("hotkeys")`) before use | `src/headerbarui.c:1327-1343` |
| `danpla/ddb_copy_info` (author is a DeaDBeeF maintainer) | CMake emits the final `.so` name via `OUTPUT_NAME ddb_copy_info_gtk${GTK_VERSION}` with `PREFIX ""`; no post-build rename | `CMakeLists.txt:26-33` |
| `DeaDBeeF-Player/deadbeef-plugin-builder` (official) | Manifest schema has **no version/min-DeaDBeeF field**; `out` lists the final `ddb_*.so` names; one folder per plugin under `plugins/` | `README.md:17-30`, `plugins/*/manifest.json` |

The brief's suggested lead `skyjack/plot` does not exist (searches return only `samyk/skyjack`, unrelated). Waveform seekbar is the closest real analogue and the model for the probe pattern.

**Recommended version floor for our plugin: DeaDBeeF 1.9.6** (core plugin API level 17, GTKUI API 2.5), conditional on deleting or guarding the 2.6 call. Symbol evidence (introducing commit, first tag):

| Symbol / feature | Commit | First release |
|---|---|---|
| `DB_mediasource_t` (create_item_tree, tree_item_*, add_listener, scanner_state) | `61e9c411d` (2023-03-11) | **1.9.6** (2023-11-07) |
| `plt_select_all` | `61e9c411d` | **1.9.6** |
| `DDB_WF_SUPPORTS_EXTENDED_API` + exapi struct | `a732f3229` (2021-12-27) | **1.9.0** (2022-05-13) |
| `trk_context_menu_build`, `gtkui_medialib_get_source` (both dlsym'd, NULL-guarded) | 2021 commits | 1.9.0 |
| `w_save_layout_to_conf_key` | `9caadf5b1` (2025-10-30) | **1.10.1** (the call being deleted) |

Every deadbeef_api/gtkui/medialib member we call ships in 1.9.6; the deleted call was the sole reason for a higher floor. How to record: README Compatibility (the builder manifest has no field for it). Mismatches to fix: `README.md:13` ("thoroughly tested ... DeaDBeeF 1.10.2", falsified by issue #1) and `README.md:49` / `spec.md:5` ("level 18+"; the `DB_mediasource_t` API is level 17 / 1.9.6).

### 2.4 Agent 4: issue #1 forensics + full bug hunt

Forensics summary (details in 2.1/2.2 and section 4): the click path is `show_config_dialog` (`:1043`) -> OK -> `on_config_dialog_response` (`:979`) -> `:1035-1037` -> upstream `w_save_to_conf` (`widgets.c:721-738`) -> `_save_widget_to_json` derefs `w->type` at `widgets.c:640-642` -> SIGSEGV. The reporter's 1.10.3 and a genuine 1.10.2 behave identically. The real (narrower) ABI window is **1.10.0 and older only**: on those runtimes the member slot read is past the end of the runtime vtable into adjacent static storage; zero-fill made the guard skip (no crash, no save), non-zero garbage would be a wild call. The bug hunt's full ranked output is section 4 below.

**Audit-accuracy verdict (confirmed/corrected):**

1. "Passes NULL where the contract demands non-NULL; upstream save path NULL-derefs": **CONFIRMED**, phrasing precise.
2. "Wrong conf key": **CONFIRMED** (`"layout"` is read by nothing; real key is internal-only), but the remediation is **deletion, not rekeying**.
3. "The API landed after tag 1.10.2, so 1.10.2 reads struct-tail garbage; that is why the dev machine never crashed": **REFUTED.** Member since 1.10.1; header and implementation byte-identical across 1.10.1/1.10.2/1.10.3/master. The genuine window is <=1.10.0. Main-thread check tonight: the dev machine runs 1.10.3 anyway (`rpm -q deadbeef` = 1.10.3-1.fc44), so the "1.10.2" premise was stale regardless; the no-crash explanation is "no OK click since 2026-04-24".
4. "Lockstep enforcement is opt-in and CI-blind": **HALF RIGHT.** The CI-blind half stands (`ci.yml` builds and tests, never compares the rebuilt `.so`). But `core.hooksPath` **is** set to `.githooks` in this clone (verified in the main thread tonight; agent 4's "not set" claim was wrong, caught and corrected by agent 5). The local hook is active; the gap is CI-only.
5. "GTK4 shim gaps" and "search matches title+artist only": **CONFIRMED** (section 4, L4/GTK4 list and search note).

### 2.5 Agent 5: improvement research, distribution, and corrections

**Distribution path.** Primary channel: a PR to `DeaDBeeF-Player/deadbeef-plugin-builder`: fork, add `plugins/<name>/manifest.json` (folder name = plugin name), iterate with `./build --arch=x86_64 cui`, mirror with `./docker-bootstrap.sh` + `./docker-build.sh`, then PR. Builder CI builds all plugins on PRs and, after merge, auto-builds artifacts and updates the website downloads page. The wiki listing (`github.com/DeaDBeeF-Player/deadbeef/wiki/Plugins`) is a plain community wiki edit (name + description + repo link + author); no formal process. Stale expectation found: `roadmap.md:126` says the Docker verify covers "x86_64 and i686", but the current builder promises linux x86_64, mac universal, windows x86_64; **no i686**. Issue #1 metadata: filed 2026-08-21 by UraYukimitsu, Debian 13 x86-64, **using the prebuilt `compiled/` binary**, SEGFAULT on Save "even without changing it from the default configuration", ~55-frame backtrace through `ddb_misc_cui_GTK3.so` -> `w_save_to_conf` -> `g_signal_emit`. Open, no replies.

**Corrections to the hunt's findings (all verified by agent 5, hooksPath re-verified in the main thread):**

- `core.hooksPath` = `.githooks` in this clone; the pre-commit lockstep hook is ACTIVE locally. The enforcement gap is CI-only.
- `restore_vscroll_idle` lives in `src/cui_data.c:322-335` (scheduled at `:454`): it has the `g_list_find` guard (`:324`) and lacks the `shutting_down` check. `deferred_column_changed_cb` (`src/cui_widget.c:91-98`) is the mirror image (has `shutting_down`, lacks `g_list_find`).
- `shutting_down` sites: declared `src/cui_globals.h:51`; written `src/main.c:10` (init), `:22` (DB_EV_TERMINATE), `:41` (cui_start), `:61` (cui_stop); read `src/main.c:33`, `src/cui_data.c:338`, `src/cui_widget.c:98, :1167, :1185, :1209, :1230`.
- **The repo has ZERO tags** (`git tag | wc -l` = 0) despite release commits v1.3.1 (`67b9215`), v1.3.2 (`0fd53e9`), v1.3.3 (`cbd9a01`, "release: v1.3.3 (fix crash on close)", 2026-05-28). The roadmap line "the granted v1.3.3 tag cuts with the queued lane" underestimates this.
- `audit/FULL-AUDIT-2026-09-12.md` (cited at `roadmap.md:153`) exists in the **workspace audit repo**, `~/.gitrepos/audit/FULL-AUDIT-2026-09-12.md` (Wave 21, lines 1078-1111, covers this plugin). It was "phantom" only relative to the plugin repo, where agent 5's check was scoped. On 2026-09-12 night the audit pages there (`Wave 21` addendum, `audit/deadbeef-cui/full-roadmap.md`, `audit/deadbeef-cui/app-specific-rules.md`, the `MAIN_AUDIT.md` blitz-order entry) were updated to carry this report's corrections; the plugin repo's roadmap block was rewritten in place the same night. All uncommitted.
- CI caveat: `ci.yml` runs `container: fedora:latest` (floating); a byte-compare of a CI-rebuilt `.so` against the committed one will false-red whenever the container toolchain drifts from whatever built the committed binary. Recommended instead: a git-level gate (any commit diff touching `src/` or `CMakeLists.txt` must also touch `compiled/ddb_misc_cui_GTK3.so`), leaving byte-exactness to the local hook.

**Decisions taken tonight (Brandon, via prompts; the backlog and checklist below already reflect them):**

1. **Tags: only v1.3.4 onward.** No catch-up v1.3.3 tag; the four untagged releases stay untagged permanently (deliberate waiver of the usual catch-up-tag rule).
2. **M1 (`shutting_down` atomics): v1.3.5**, keeping the crash release surgical.
3. **M2 (viewer-playlist collision): hidden meta marker + name fallback, v1.3.5.**
4. **Search album-field extension: deferred** (charter holds; revisit if a user asks).

## 3. Ranked fix design for issue #1

**Ship: delete `src/cui_widget.c:1035-1037` entirely.**

- Correctness: removes the only read of the API_LEVEL-206 tail members, which also dissolves the <=1.10.0 struct-tail exposure (we touch neither `w_load_layout_from_conf_key` nor `w_send_message` anywhere).
- No userspace regression: the call never worked on any runtime. On 1.10.1+ it crashed; on <=1.10.0 it was a skipped no-op. Per-instance settings persist via quit-time and design-mode `w_save()` through our exapi hook (section 2.2). Documented decision: **no flush-on-Save**; the Save button applies settings in memory immediately (panes update) and durability comes from the normal quit-time save. If crash-window durability is ever wanted, the guarded-flush design is recorded in section 2.2 option 2.
- Must ride in the same commit: the H1 invalidation (next section), the regression tests (2.2), the lockstep `.so` rebuild, the patchnotes entry, and the version bump (all in section 6).

**Rejected alternatives** (for the record, so tomorrow does not relitigate): version-guard alone (incomplete), passing our own widget pointer (destroys user layout data), own conf key (duplicate persistence channel; `autoplaylist_name` precedent).

## 4. Newly-found bug list, ranked

Severity: CRASH > HIGH > MEDIUM > LOW. The first two are the v1.3.4 lane; the rest queue per section 5.

1. **C1 (CRASH, the fix): the `w_save_layout_to_conf_key("layout", NULL)` call.** `src/cui_widget.c:1035-1037`. Deterministic SIGSEGV inside upstream `_save_widget_to_json` on every 1.10.1+ runtime; unsound member-presence guard on <=1.10.0. Fix: delete (section 3).
2. **H1 (HIGH, masked by C1): config-dialog OK leaves all panes blank when the library is quiet.** `src/cui_widget.c:1027-1033` rebuilds the preset, `rebuild_columns` creates fresh empty stores (`:658`), then `update_tree_data` early-returns on the modification-index cache (`src/cui_data.c:353-357`) because `last_ml_modification_idx` was never reset. The user sees empty panes until a library change or a search keystroke, in direct violation of CLAUDE.md §7.2 step 6. **Fixing C1 alone exposes this.** Fix: `cw->last_ml_modification_idx = -1;` immediately before `update_tree_data(cw)` at `:1033`. Same commit as C1.
3. **M1 (MEDIUM): `shutting_down` is a plain int read cross-thread** (sites in section 2.5). Formally a C11 data race; practically benign on x86 and backstopped by the second-line guards in `ml_event_idle_cb` (`src/cui_widget.c:1209-1210`). Fix (v1.3.5, decided): `g_atomic_int_get`/`g_atomic_int_set`, matching the existing atomic style of `ml_modification_idx`.
4. **M2 (MEDIUM, data-loss footgun): by-name viewer-playlist matching wipes user playlists at quit.** `find_viewer_playlist` matches any playlist by title (`src/cui_data.c:130-154`); `cui_clear_viewer_playlists` (`src/cui_widget.c:28-38`) `plt_clear`s every match on `DB_EV_TERMINATE`. A user playlist that happens to be named "Library Viewer" (or the per-instance configured name) is silently emptied every quit. Fix (v1.3.5, decided): hidden meta marker (e.g. `_cui_viewer`) set at creation in `get_or_create_viewer_playlist`; marker-first matching with name-equality fallback so pre-marker playlists keep working; clear marker-matched playlists only.
5. **L1 (LOW, upstream-side): `serialize_to_keyvalues` output is never freed by upstream.** Zero call sites of `free_serialized_keyvalues` in upstream `widgets.c` (1.10.3 and master); our ~27 strdup'd strings (`src/cui_widget.c:890-922`) leak on every layout save (each quit, each design-mode edit). Unfixable plugin-side (the API contract requires returning a heap array). Document in CLAUDE.md; do not change allocators.
6. **L2 (LOW): two-step-guard inconsistency.** `deferred_column_changed_cb` lacks the `g_list_find(all_cui_widgets, cw)` check (`src/cui_widget.c:91-98`); `restore_vscroll_idle` lacks the `shutting_down` check (`src/cui_data.c:322-335`). Both provably safe today (`cui_destroy` cancels both timeout ids at `:829-832` and removes the widget before freeing). One-line additions for uniformity; v1.3.5.
7. **L3 (LOW, process): lockstep is CI-blind.** Local hook active (verified); `.github/workflows/ci.yml` builds and runs ctest but never checks that `compiled/` tracks `src/`. Fix: git-level gate step in CI (diff touches `src/` or `CMakeLists.txt` => commit must touch `compiled/ddb_misc_cui_GTK3.so`); do not byte-compare against a floating `fedora:latest` container.
8. **L4 (LOW, ahead of any GTK4 port): plugin-id and bundle-name traps.** `DDB_GTKUI_PLUGIN_ID` resolves to the GTK2 id `"gtkui_1"` when compiled against GTK4 (upstream header only branches GTK3-vs-else, `gtkui_api.h:40-48`); `dlopen("ddb_gui_GTK3.so", ...)` is hardcoded (`src/cui_widget.c:374`, `:788`). Both need special-casing in a GTK4 build.
9. **GTK4 port-blocker list** (for the one README sentence plus CLAUDE.md detail): `GdkEventButton` in `on_tree_button_press` unguarded (`:519`; the shim maps `GdkEventKey` only, `src/cui_globals.h:33`); `gtk_widget_destroy(dialog)` (`:1040`); `gtk_container_get_children` (`:713`, `:984`); the whole GtkMenu/GtkMenuItem/GtkMenuShell block (`:555-616`, needs GtkPopoverMenu/GMenu); unguarded `"key-press-event"` connects (`:731`, `:753`, `:1158`) needing `GtkEventControllerKey`; shim semantic gaps (`gtk_widget_show_all` child-visibility loss, `gtk_box_pack_start` expand/fill/padding discard, `gtk_container_add` empty else-chain); drag-out needs `GtkDragSource` + `GdkContentProvider`. Non-blockers: GtkTreeView/GtkListStore/GtkDialog survive (deprecated) in GTK4.
10. **Doc bugs found by the hunt (fix in the doc pass):** CLAUDE.md §6.11 claims "pl_lock is not reentrant"; upstream creates it `PTHREAD_MUTEX_RECURSIVE` (`src/threading_pthread.c:151-155`) and `populate_playlist_from_cui` already nests it. The invariant's real justification is that tree text needs no locking, not deadlock. `roadmap.md:153` cites the audit file, which lives in the workspace audit repo (`~/.gitrepos/audit/`), not this repo; `roadmap.md:155-163` carries the refuted 1.10.2 ABI mechanism; `roadmap.md:126` promises an i686 Docker verify the builder does not offer; README/spec overclaims per section 2.3.

**Verified-correct; nobody may "fix" these** (this repo has a history of audit-induced regressions):

- `ml_modification_idx` is already race-safe in practice: every access goes through `g_atomic_int_inc` (`src/cui_widget.c:1236`) / `g_atomic_int_get` (`src/cui_data.c:353`); the check-then-act stores the pre-rebuild index (`:483`), so an event landing mid-rebuild correctly forces one more rebuild.
- The `track_counts_cache` +1/-1 encoding is correct (store `count+1` at `src/cui_data.c:49`, return `cached-1` on hit at `:31`; zero counts distinguishable from misses; no per-entry allocations; destroyed after `free_item_tree` at `:374-381`; safe when `create_item_tree` fails). The §6.7/§6.8 search coupling is intact (`:350`).
- The scriptable struct mirror is intact (`src/cui_scriptable.h:6-22` vs `.deadbeef/shared/scriptable/scriptable.c`).
- `conf_get_str_fast` discipline is clean (both uses inside `conf_lock`/`conf_unlock`, `src/cui_widget.c:633-638`, `:1113-1140`).
- Memory balance is otherwise clean: titles/formats/autoplaylist_name/search/fonts/saved_sels/scroll_restore/my_preset all pair up; store ref handling, menu floating ref, drag payload alloc+copy+receiver-unref, temp menu playlist unref all correct. One cosmetic nit: `collect_tracks_for_drag`'s `calloc` (`:456`) is unchecked.
- The `[All]`-row sort pinning is intact (`src/cui_data.c:76-79`) and test-locked.
- All three dlsym'd symbols exist at 1.10.3 (`gtkui_medialib_get_source` `medialibmanager.c:22`; `trk_context_menu_update_with_playlist` `plmenu.c:144`; `trk_context_menu_build` `plmenu.c:454`) and are NULL-guarded with graceful fallbacks (`src/cui_widget.c:370-391`, `:562-570`, `:787-797`).
- `create_item_tree(ml_source, preset, NULL)`: NULL third param is the documented no-filter path (`medialibtree.c:536`).
- `g_atomic_int_compare_and_exchange` (`src/main.c:33`) is `GLIB_AVAILABLE_IN_ALL`; no GLib floor issue.

**Search scope (assessed, deferred by decision):** `track_matches_search` (`src/cui_data.c:5-20`) matches title OR artist. `album` would be one more `pl_find_meta_raw` + `strcasestr` inside the already-held lock (negligible cost; keep `strcasestr` on raw bytes, needle downcased once at `src/cui_widget.c:165`; no `g_utf8_strdown` regressions). `path` assessed as not useful (leaks filesystem layout into facet results). Deferred per tonight's decision; the mock (`tests/mock_deadbeef.h:17-21`) only models title/artist and would need a third field if ever pursued.

## 5. Improvement backlog

Tags: fix / polish / doc / feature / release. Lanes: v1.3.4 (the issue-fix release), v1.3.5 (follow-up), later/gated. Decisions from tonight are already applied.

| Rank | Item | Tag | Lane | Files | Sketch |
|---|---|---|---|---|---|
| R1 | Delete the crashing call (C1) | fix | v1.3.4 | `src/cui_widget.c:1035-1037` | Remove the block; persistence is quit-time via exapi (section 3) |
| R2 | Invalidate cache after config apply (H1) | fix | v1.3.4 | `src/cui_widget.c:1033` | `cw->last_ml_modification_idx = -1;` before `update_tree_data(cw)`; same commit as R1 |
| R3 | Regression tests (mock gtkui vtable tripwire) | fix | v1.3.4 | `tests/mock_deadbeef.c`, `tests/mock_deadbeef.h`, `tests/test_cui.c` | Fake vtable: switchable 2.5/2.6, recording stub hard-fails on NULL val, switchable root; test asserts never-called; limits per section 2.2 |
| R4 | Lockstep rebuild of `compiled/` | release | every src commit in the lane | `compiled/ddb_misc_cui_GTK3.so` | `cmake --build build --target cui && command cp -f <built .so> compiled/ddb_misc_cui_GTK3.so`, staged in the same commit; the local hook enforces it |
| R5 | Version bump + patchnotes + v1.3.4 tag | release | v1.3.4 | `src/main.c:54, :113, :116`; `README.md:2, :13, :102`; `spec.md:3`; `patchnotes.md` | Entry draft in section 6. Tag v1.3.4 at the release commit, verbatim patchnotes message. Per tonight's decision there is NO v1.3.3 catch-up tag |
| R6 | Dead-URL fixes | fix | v1.3.4 (decided first-touch step 1) | `manifest.json:5`, `src/main.c:118` | `https://github.com/bdkl/deadbeef-cui` -> `https://github.com/VirInvictus/deadbeef-cui.git` (live remote verified); main.c change triggers R4 |
| R7 | README/spec honesty pass | polish | v1.3.4 | `README.md:13, :37, :49, :76`; `spec.md:5` | Drop "thoroughly tested"; state the verified floor: DeaDBeeF 1.9.6+ for source builds (API level 17; exapi since 1.9.0), tested on 1.10.x; fix "level 18+" to 17; add the one-sentence GTK4 shim caveat |
| R8 | Manifest out-name + CMake OUTPUT_NAME | fix | v1.3.4 (first-touch) | `manifest.json:13-15`; `CMakeLists.txt:14, :20`; `.githooks/pre-commit:23, :26`; `README.md:96`; CLAUDE.md §5 | `"out": ["ddb_misc_cui_GTK3.so"]` (builder convention; DeaDBeeF derives the `_load` symbol from the filename); add `OUTPUT_NAME ddb_misc_cui_GTK3` (keep `PREFIX ""`). Ripple: the hook's `build/cui.so` path and the README install snippet change in the same commit |
| R9 | CLAUDE.md doc pass | doc | v1.3.4 | `CLAUDE.md` §4, §5, §6.3, §6.11 | Replace the §4 `w_save_layout_to_conf_key` row with the deletion note + contract citation; correct §6.11 (mutex IS recursive; real reason is tree text needs no lock); add the L1 leak note, the L4/GTK4 traps, and the PLUG_TEST_COMPAT probe pattern |
| R10 | CI lockstep gate | polish | v1.3.4 | `.github/workflows/ci.yml` | Git-level gate: commit diff touching `src/` or `CMakeLists.txt` must also touch `compiled/ddb_misc_cui_GTK3.so`. No byte-compare on floating `fedora:latest` |
| R11 | Roadmap corrections | doc | v1.3.4 | `roadmap.md:126, :153-176` | Rewrite the refuted 1.10.2 ABI mechanism (member since 1.10.1, `9caadf5b1`, crash on every >=1.10.1, window is <=1.10.0); fix the audit citation (the file lives in `~/.gitrepos/audit/`, not this repo); fix the i686 expectation; tick items as shipped. DONE IN PLACE 2026-09-12 night (uncommitted); tomorrow only folds the edits into the docs commit and ticks boxes |
| R12 | `shutting_down` atomics (M1) | fix | v1.3.5 (decided) | sites in section 2.5 | `g_atomic_int_get`/`g_atomic_int_set`, matching the existing atomic style |
| R13 | Guard uniformity (L2) | polish | v1.3.5 | `src/cui_widget.c:91-98`; `src/cui_data.c:322-324` | Add the missing half of the two-step guard to each callback |
| R14 | Viewer-playlist identity marker (M2) | fix | v1.3.5 (decided) | `src/cui_data.c:130-173` (lookup), creation path in `src/cui_widget.c`, `src/cui_widget.c:28-38` (clear), tests | Hidden `_cui_viewer` meta at creation; marker-first matching with name fallback; clear marker-matched only. Changes shutdown behavior: Brandon has approved the approach; final design review at implementation time |
| R15 | API-version probe at `cui_start` | polish | v1.3.5 | `src/main.c` | `PLUG_TEST_COMPAT(&gtkui_plugin->gui.plugin, 2, 6)` logged-once probe; establishes the pattern for any future 2.6-only call. Future-proofing, not a fix |
| R16 | Search album field | feature | deferred (decided) | `src/cui_data.c:5-20`; `tests/mock_deadbeef.h:17-21` | Revisit only if a user asks; sketch in section 4 |
| R17 | Issue #1 reply + triage | fix | v1.3.4, after push | GitHub only | Draft in section 6; Brandon reviews before posting; label + close on release |
| R18 | Submission PR + Docker verify + GitHub hygiene | release | gated on Brandon | `DeaDBeeF-Player/deadbeef-plugin-builder` fork; repo settings | Decided order: after R6/R7 + v1.3.4 + R17: Docker verify (x86_64 only), then the PR adding `plugins/<name>/manifest.json`, then optional wiki edit; drop the `cpp` topic (pure C11) |

## 6. Checklist for tomorrow's fix lane

**Pre-flight (no changes):** confirm the tree is clean (verified tonight), `core.hooksPath` = `.githooks` (verified tonight), and `build/` is configured so the hook can rebuild (`build/` exists). The hook will block any src commit that forgets the `.so`; do not bypass with `--no-verify`.

**Commit order** (each commit that touches `src/` or `CMakeLists.txt` carries its rebuilt `.so`; decided re-gate order preserved: dead URLs first, then the crash fix, then the issue reply, then the Docker verify):

1. **Commit A, dead URLs (R6):** `manifest.json:5` + `src/main.c:118`, both to `https://github.com/VirInvictus/deadbeef-cui.git` (drop `.git` in main.c's human-facing website string if preferred; be consistent). Rebuild + stage the `.so`.
2. **Commit B, packaging (R8):** `manifest.json` out-name, `CMakeLists.txt` OUTPUT_NAME, the hook's two `build/cui.so` references (edit the hook file in the working tree as part of the commit or it blocks on the vanished path), `README.md:96` install snippet, CLAUDE.md §5 notes. Rebuild (the target output is now `build/ddb_misc_cui_GTK3.so`) + stage the `.so` at its unchanged `compiled/` path.
3. **Commit C, the fix (R1+R2+R3, one logical commit):** delete `src/cui_widget.c:1035-1037`; add the invalidation line at `:1033`; mock vtable + tripwire test in `tests/`. Rebuild + stage the `.so`.
4. **Commit D, docs (R7+R9+R11), no source change, no rebuild:** README/spec honesty pass, CLAUDE.md pass, roadmap corrections.
5. **Commit E, release (R5):** patchnotes v1.3.4 entry at top; version bumps at `src/main.c:54` (w_reg_widget title string), `:113` (`.plugin.version_minor` 3 -> 4), `:116` (`.plugin.descr`); `README.md:2` badge, `:13` note, `:102` verify line; `spec.md:3`. Rebuild + stage the `.so` (main.c changed).
6. **Tag v1.3.4** at commit E, per the house tagging procedure: extract the new entry verbatim from `patchnotes.md` into a scratch file (never retype); proofread it (version and date match, no em-dashes, no stray tails); `git tag -a v1.3.4 --cleanup=verbatim -F <file> <commit-E>`; verify with `git cat-file tag v1.3.4` (begins with the entry's title line in the repo's `## vX.Y.Z` style, full entry, as detailed as peer repos' tags); then the workspace tag sweep before any push. Per tonight's decision: **no v1.3.3 catch-up tag**; the four older releases stay untagged.
7. **Push main + tag only with Brandon's explicit approval.**
8. **Issue #1 reply (R17),** after push, after Brandon reviews this draft:

   > Thanks for the excellent report; the backtrace made this fast to root-cause. This is our bug, and it is exactly what your stack shows.
   >
   > The "Configure Facets" OK handler in deadbeef-cui calls the GTKUI function `w_save_layout_to_conf_key("layout", NULL)`. That function's contract requires the second argument to be a valid widget pointer; we passed NULL, and GTKUI's `_save_widget_to_json` dereferences it unconditionally, which is the segfault in your frame. Two further problems rode along: the settings that call was meant to persist are already saved automatically by GTKUI whenever the layout is saved (on quit and on design-mode edits), because the widget registers with the extended API; and the `"layout"` config key it wrote to is read by nothing. The call never worked and has been removed outright.
   >
   > One correction to expectations: that API member exists in every DeaDBeeF release since 1.10.1, so the crash fires on 1.10.1, 1.10.2, and 1.10.3 alike, and it fires regardless of what the dialog contains, which matches your "even without changing it from the default configuration" observation.
   >
   > Fixed in v1.3.4: the crashing call is deleted, and the prebuilt `compiled/ddb_misc_cui_GTK3.so` in the repo is rebuilt to match. Until you can update, Cancel is safe; only OK triggers the crash, and it fires after your changes are applied to the running session, so nothing else is affected. If you build from source, the fix is a three-line deletion in `src/cui_widget.c`.

9. **Live smoke test (the part no unit test can reach):** install the rebuilt `.so` to `~/.local/lib/deadbeef/`, restart DeaDBeeF, open Configure Facets, press OK with defaults (must not crash), confirm the new column layout appears immediately (H1), change a setting, quit, relaunch, confirm it persisted (quit-time exapi save). Watch for warnings with `DEADBEEF_CUI_DEBUG=1`.
10. **Docker verify, then the submission PR (R18),** when Brandon is ready: `./docker-bootstrap.sh` + `./docker-build.sh` against the builder (x86_64; update `roadmap.md:126`'s i686 expectation), then the fork + `plugins/<name>/manifest.json` + PR. Optional wiki edit afterwards.

**Patchnotes v1.3.4 entry draft** (edit freely; the tag message must end up identical to the final entry):

```markdown
## v1.3.4

### Bug fixes

**Fixed the crash when confirming the "Configure Facets" dialog (issue #1).**
The dialog's save handler called the GTKUI function `w_save_layout_to_conf_key`
with NULL where the contract requires a widget pointer, crashing inside GTKUI's
layout serializer on every DeaDBeeF 1.10.1 or newer. The call never served its
intended purpose: the `"layout"` config key it wrote is read by nothing, and the
widget's per-instance settings are already persisted by GTKUI itself whenever
the layout is saved (on quit and on design-mode edits). The call has been
removed. File: `src/cui_widget.c`.

**Fixed blank facet panes after confirming the configuration dialog.** Applying
a new column configuration rebuilt the internal trees, but the modification-index
cache short-circuited the refresh, so the new layout only appeared after the next
library change or search keystroke. The rebuild is now forced when the dialog is
accepted. This defect was masked by the crash above. File: `src/cui_widget.c`.
```

(Packaging/URL fixes land in the same release; mention them in the entry only if the release notes style wants them.)

## 7. Confidence note

**Verified to high confidence (source-read, three agents converging independently):** the root cause and its exact mechanics; the member history (`9caadf5b1`, first in 1.10.1, absent in 1.10.0, byte-identical through master); the redundancy of the call (quit-time `w_save()` + exapi, all cited); the H1 short-circuit; the version floor (1.9.6). The upstream tag set and the dev machine's actual runtime (1.10.3-1.fc44) were verified in the main thread.

**Resolved tonight:** the "why didn't it crash locally" question. The dev machine runs 1.10.3, which has the member, so the audit's struct-tail explanation never applied here; the only consistent story is that no Configure Facets OK click has happened on this machine since the call landed on 2026-04-24.

**Could not be verified without running DeaDBeeF (tonight's rules forbade it):**

- The live behavior of the fix: that OK applies changes and does not crash, that H1's invalidation visibly repopulates the panes, and that settings still persist across restart via the quit-time save. Step 9 of the checklist covers it tomorrow.
- The reporter's full backtrace beyond what the issue text quotes; the `w_save_to_conf` / `g_signal_emit` frames are consistent with our source reading but were not independently symbolized.
- The exact upstream line number of the faulting `strcmp` (agents cited `widgets.c:641` and `:642` for the same statement from different tags; harmless, but quote it as 640-642).
- Whether the dev machine ever clicked OK between 2026-04-24 and tonight (unknowable; irrelevant to the fix, relevant only to the anomaly narrative above).
- CI byte-compare viability on a floating `fedora:latest` container (moot: the git-level gate in R10 avoids it).

**Compliance:** no code changed anywhere tonight; no builds, no test runs, no installs against a live DeaDBeeF (the dev-machine version check read package metadata only); the upstream clone was only read; the only file created in any repo is this one; nothing committed.
