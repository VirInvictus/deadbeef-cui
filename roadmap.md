# deadbeef-cui: Roadmap

What's done, what's next. Sequenced for feature-parity with foobar2000's Columns UI. Updated as of v1.3.4.

---

## Phase 0: Foundation & Core Logic
*The underlying engine connecting to DeaDBeeF's internal database.*

- [x] **Project Setup:** Scaffold C/C++ build system (CMake) with GTK3 and DeaDBeeF headers.
- [x] **Plugin Skeleton:** Compile a basic plugin that DeaDBeeF recognizes.
- [x] **Database Binding:** Read library data using DeaDBeeF's internal `medialib` API.
- [x] **Stability Fixes:** Correct pointer handling and official API usage to prevent segfaults.
- [x] **Memory Management:** Resolved memory leaks in widget destruction and plugin shutdown.
- [x] **Performance:** Eliminated 2-4 second startup delay by sharing the main UI's media library database.

## Phase 1: The Triple-Pane Layout
*Constructing the primary faceted browsing interface.*

- [x] **UI Layout:** Construct the triple-pane GTK widget layout (Genre, Album Artist, Album).
- [x] **Hierarchical Filtering:** Genre selection updates Artist list; Artist selection updates Album list.
- [x] **Aggregate Views:** Columns show all items by default when no parent filter is active.
- [x] **Robust Traversal:** Replaced hardcoded depth traversal with a recursive aggregation system.
- [x] **"Various Artists" Fix:** Implemented string-based aggregation for multi-genre artists.
- [x] **"All" Rows:** Added "All Genres/Artists/Albums" options for broad filtering.
- [x] **UI Polish:** Fixed empty whitespace horizontal scrolling in filter boxes.

## Phase 2: Playback & Basic Integration
*Connecting the browser to the player's core actions.*

- [x] **Playlist Driving:** Dynamically populate a dedicated "Library Viewer" playlist based on facet selection, protecting user playlists.
- [x] **Playback Integration:** Automatically trigger playback on selection double-click.

## Phase 3: Sorting & Interaction
*Refining the behavior to match professional library managers.*

- [x] **Alphabetical Ordering:** Fix the "jumping" behavior by ensuring all facet lists are strictly sorted alphabetically.
- [x] **Header Sort Buttons:** Implement toggleable sort modes (Alphabetical vs. Item Count) accessible via column headers.
- [x] **Selection Count:** Display the total number of items in each category next to the label in a dedicated "Count" column.
- [x] **Multi-Selection:** Support selecting multiple entries in a column (Ctrl/Shift-click) to aggregate filters across multiple genres/artists.
- [x] **Track Count Caching:** Pre-calculate track counts during initial library load to ensure instantaneous filtering for large libraries (50k+ tracks).
- [x] **Fix Scroll bar / Count being blended into each other:** Might look bad on some themes.

## Phase 4: Customization & Formatting
*Extending the flexibility of the column metadata.*

- [x] **Custom Search Formatting:** Full support for DeaDBeeF title formatting syntax (e.g., `%album% [%year%]`) for column patterns.
- [x] **User-Configurable Columns:** UI for adding, removing, and reordering facets (e.g., Year → Genre → Artist).
- [x] **Album Artist Logic:** Standardize tag priority handling (TPE2 vs. `ALBUM ARTIST`) across different filetypes (Opus, MP3, FLAC).
- [x] **Prefix Handling:** Option to ignore leading articles ("The", "A", "An") during alphabetical sorting.
- [x] **Show List Number:** Display the total number of items in each column's "All" row (e.g., [All (55 Genres)]) for a clear overview of selection size.

## Phase 5: Advanced CUI Features
*Deep integration with the DeaDBeeF ecosystem.*

- [x] **Integrated Search Bar:** Real-time filtering search box that narrows the facets as you type (activated by CTRL-SHIFT-F - should be hidden from sight otherwise but typing in Eminem would filter Genre/Artist/Album to only show any items that have a song with the word eminem in it (should be case-insensitive).
- [x] **Search bar fixes:** Currently, the search bar blends into the facet headers. A little more space would be ideal.
- [x] **Context Menus:** Right-click interaction for selection (Add to current playlist, Send to new playlist, Queue next).
- [x] **Multivalue Tag Support:** Correctly split and aggregate tags with multiple values (e.g., `Genre: Rock; Progressive`).
- [x] **Autoplaylist Persistence:** Option to link a specific playlist to the facet browser so it stays in sync.

## Phase 6: Visuals & Layout (The 1.0 Milestone)
*Polishing the aesthetic and reaching feature-parity.*

- [x] **Deep Bug Fixing** - Refactor anything worth refactoring. Clean up code use in testing. Make sure everything is as tight as it can be.
- [x] **Assure GTK4-compliance without breaking GTK3** - I gotta assume Deadbeef won't be GTK3 forever.
- [x] **Design Mode Integration:** Support for DeaDBeeF's Design Mode for seamless layout embedding.
- [x] **1.0.0 Stable Release:** Final documentation, icon assets, and feature-parity verification with foobar2000 Columns UI.

---

## Phase 7: Optimization & Technical Debt
*Hardening the architecture and improving performance for large libraries.*

- [x] **Selection Persistence:** Restore previously selected items after a list refresh.
- [x] **Efficient Playlist Lookup:** Investigated `plt_find_by_name`: no change. The swap never happened: the pickaxe hits that looked like adoption were DWARF strings in rebuilt `.so` blobs, and the manual iteration stayed. It is now also moot: viewer lookup is marker-based (the `_cui_viewer` meta, CLAUDE.md §6.14), which a title-only search cannot express.
- [x] **Search Allocation Storm:** Optimize `track_matches_search` by removing redundant `g_utf8_strdown` heap allocations.
- [x] **Thread-Safe Tree Teardown:** Fix the race condition in `cui_destroy` by ensuring `ml_source` remains valid until all widgets are destroyed.
- [x] **Instance-Specific Settings:** Move from global `cui.*` config keys to proper `ddb_gtkui_widget_extended_api_t` serialization to support multiple independent browser instances.
- [x] **Library Event Debouncing:** Implement a timer for `ml_listener_cb` to prevent redundant UI refreshes during batch metadata edits.
- [x] **Memory Management:** 
    - [x] Fix `GtkMenu` leak in right-click context menus.
    - [x] Properly disconnect main window signal handlers in `cui_stop`.
- [x] **Real-time Library Sync:** Ensure private media source remains synchronized with background library updates via `ml_listener_cb`.
- [x] **Standardized Shortcuts:** Unify shortcut keys (`CTRL-SHIFT-F`) and ensure they don't conflict with DeaDBeeF core.
- [x] **The [All] items don't play a song on double click** - Bug that should be fixed
- [x] **If focus is on the facet and a letter is pressed, the selection should jump to that letter. This should work for as many characters as the user types**

## Phase 8: Advanced Performance Refinement
*Pushing the limits of the faceted browsing engine.*

- [x] **Memoization Refresh:** Re-enable the `track_counts_cache` when search filters are active. Validity is guaranteed by the existing `last_ml_modification_idx = -1` reset on search change, which forces a full cache rebuild. (v1.2.5)

### Dropped from Phase 8

- ~~**Incremental Playlist Updates** via `DDB_PLAYLIST_CHANGE_CONTENT`~~ (investigated and dropped in v1.2.5). The flag value is `0`, which is what we already pass to `sendmessage(DB_EV_PLAYLISTCHANGED, 0, 0, 0)`. The playlist widget treats that event as a full rebuild signal regardless, so a diff-based incremental update would require reimplementing the rebuild path with per-track add/remove tracking against the current playlist contents; a complex change for ~50–100 ms savings on selection switches that nobody has flagged as sluggish. The v1.2.4 fix that stopped auto-populating the playlist on first init already addressed the only observed pain point.
- [x] **Modular Refactoring:** Break up the monolithic `main.c` into domain-specific modules for better maintainability (v1.2.3).

## Phase 9: Startup Latency & Theme Conformance
*Closing the gap between widget creation and a populated, theme-correct view.*

Measured baseline (6,367-track library, fresh launch with cui in layout but no GTKUI medialib widget): `ml playlist load time` 0.36 s, `scan time` 0.64 s, `tree build time` 0.10 s, plus a hardcoded 1000 ms debounce in `ml_event_idle_cb` between the first `CONTENT_DID_CHANGE` and our rebuild. Total observable empty-→-populated gap ≈ 1.5–2.0 s. Tree build itself is fast; the wins are in the wait state.

### Startup latency
- [x] **Skip the first-fire debounce.** `ml_event_idle_cb` always queues a 1000 ms `g_timeout_add` before `update_tree_data` runs. The debounce exists to coalesce batch tag-edit events; on the first content-did-change after widget creation it is pure overhead. Track an `initial_sync_done` flag on `cui_widget_t` and dispatch the first rebuild immediately, then resume the 1000 ms debounce for subsequent events. (v1.2.4)
- [x] **Defer the synchronous `update_tree_data` in `cui_init` to `g_idle_add`.** GTKUI's layout loader calls our `init` directly, so any work we do there blocks the rest of the layout from rendering. Pushing the initial rebuild to the next idle tick lets the empty columns paint instantly and runs the heavy work after the window is visible. (v1.2.4)
- [x] **Don't pre-populate the Library Viewer playlist on first init.** `update_tree_data` ends with an unconditional `on_column_changed` call that arms `deferred_column_changed_cb` → `update_playlist_from_cui`, which copies every track in the library into the viewer playlist while holding `pl_lock`. With no saved selection, this is a full-library copy the user hasn't asked for. (v1.2.4)

### Theme conformance
- [x] **Inherit `gtkui.font.listview_*` for row cells.** Currently our `GtkCellRendererText` instances use the default GTK theme font. When `gtkui.override_listview_colors=1` is set, the playlist widget reads `gtkui.font.listview_text` (e.g. `Söhne 12`) and our cells fall out of visual sync. Read `gtkui.font.listview_text` and apply it to the row renderer's `font` property; if override is off, leave the property unset so the GTK theme applies. (v1.2.4)
- [x] **Inherit `gtkui.font.listview_column_text` for headers.** Column header labels are separate widgets. Use `gtk_tree_view_column_set_widget` with a `GtkLabel` whose Pango font description comes from `gtkui.font.listview_column_text` (e.g. `Söhne Semi-Bold 14`). Same override-flag gating as row cells. (v1.2.4)
- [x] **Re-read fonts on `DB_EV_CONFIGCHANGED`.** When the user changes the playlist font in DeaDBeeF preferences, our cells refresh automatically. The message handler dispatches an idle that walks `all_cui_widgets`, compares cached font state against current `gtkui.font.listview_*` keys, and rebuilds only the widgets that actually changed (skips unrelated CONFIGCHANGED events like volume changes). (v1.2.5)

---

## Phase 10: Add to DeaDBeeF Plugin List
*Packaging and submitting for official inclusion in the DeaDBeeF ecosystem. (Was Phase 9.)*

- [x] **Consolidated Build System:** Removed the legacy `Makefile` in favor of a single, robust CMake-driven build process.
- [x] **Manifest Authoring:** `manifest.json` lives in the repo root. It tracks the example template (git source, cmake build at root, GTK3 env vars from the builder, output `ddb_misc_cui_GTK3.so`). Re-verify against the current `deadbeef-plugin-builder` schema when opening the submission PR. (v1.3.4: the builder schema check happened early; the manifests now list the final `ddb_misc_cui_GTK3.so` name and CMake emits it directly via `OUTPUT_NAME`, and the dead `bdkl/` git URL is fixed to `VirInvictus/deadbeef-cui`.)
- [x] **Static Linking Audit:** Audited via `ldd` on the built `ddb_misc_cui_GTK3.so`. The plugin links only against the system GTK3 / glib / cairo / pango stack, all of them libraries DeaDBeeF itself depends on (the `dlopen`/`dlsym` calls resolve from libc itself, as the README documents; there is no libdl link). Static-linking these would conflict with DeaDBeeF's own GTK and is incorrect for the plugin model. No non-core deps to address.
- [x] **Repository Readiness:** Repo is clean: README, spec, roadmap, patchnotes, CLAUDE.md, LICENSE, manifest.json, CMakeLists.txt, src/, compiled/ all present. No stale build artifacts checked in beyond the intentional `compiled/ddb_misc_cui_GTK3.so` for non-builders.

### Requires Brandon (external systems / decisions)
- [ ] **Cross-Platform Verification:** Run the `deadbeef-plugin-builder` Docker environment locally to verify the plugin builds for x86_64 (the builder offers no i686; the earlier "x86_64 and i686" wording was wrong). Manifest is in place; this is a `docker run` away when ready.
- [ ] **Submission PR:** Open a PR against `DeaDBeeF-Player/deadbeef-plugin-builder` adding the manifest. Requires GitHub credentials and your own description.
  *(DECIDED 2026-09-12 (Brandon): GO once the first-touch fixes land (the dead bdkl/ URL in manifest.json and main.c) and the builder-Docker verify runs; the PR itself needs your credentials and description.)*
- [ ] **v2.0.0 Tagging:** A v2.0 release implies a major-feature milestone; v1.3.4 is the current state. Defer until a feature warrants it (or rebrand "stable + plugin-list ready" as v2.0 if you prefer that framing).
  *(DECIDED 2026-09-12 (Brandon): defer until a feature warrants it.)*

---

## Phase 11: Columns UI parity expansion (v1.3.0)
*Bringing more of foobar2000's Filter panel UX into deadbeef-cui without breaking the existing chrome.*

- [x] **Standard DeaDBeeF track context menu on facet right-click.** Reuse GTKUI's own menu builder (`trk_context_menu_update_with_playlist` + `trk_context_menu_build`, dlsym'd from `ddb_gui_GTK3.so`) so right-clicking a facet row exposes Properties, Convert, Add to playqueue, Reload metadata, etc. Falls back silently to the v1.2.x hand-rolled items if either symbol is unavailable.
- [x] **Drag-out source from facet rows.** Each facet column is a drag source for its currently filtered tracks. Drop targets in playlist tabs and playlist views already accept the standard `TARGET_PLAYITEM_POINTERS` payload.
- [x] **"Send to new playlist `<row name>`" right-click menu item.** Auto-names the new playlist after the right-clicked tree's selected row(s). Pltbrowser doesn't accept drops, so this menu route is the deliberate alternative to drag-into-pltbrowser.
- [x] **Auto-highlight `[All]` when no row is selected.** Visual default; columns now read consistently next to neighbors that do have a selection. Semantically identical to no selection.

---

## Phase 12: Test harness & maintenance (post-1.3.0)
*Locking in correctness so future changes (and audits) can't regress the engine silently.*

- [x] **Engine test suite.** GLib GTest suite under `tests/` (no new dependency) driving the real engine TUs against fakes for `deadbeef_api` and the medialib source. Covers `skip_prefix`, scriptable preset build (default / compaction / split), search matching, recursive counting + zero-memoization, cross-tree "Various Artists" aggregation, autoplaylist name selection, and the `[All]`-pinned sort invariant. Behind `-DBUILD_TESTS=ON`; clean under ASan/UBSan; GTK-widget cases self-skip headless.
- [x] **Fix: per-instance autoplaylist name was ignored.** `get_or_create_viewer_playlist` read the dead global `cui.autoplaylist_name` key instead of the widget's per-instance value, so the dialog setting did nothing and instances collided on one playlist. Now reads `cw->autoplaylist_name`.
- [x] **Fix: widget-destroy leaks.** `cui_destroy` now frees `formats[]`, the scriptable preset, and `autoplaylist_name` (previously leaked on every teardown / layout reload).
- [x] **Investigated, no change:** `[All]` row sort position. `sort_func`'s order-aware pinning keeps `[All]` at iter 0 in all sort orders; the original code was correct. Documented in CLAUDE.md §6.13 and covered by the sort test.
- [x] **Untracked `pick-it-up.md` at repo root** (workspace sweep, 2026-06-09). Decide whether it should be committed (it is committed in other repos) or removed; right now it is invisible to clones. **Moot: the file no longer exists.** It was removed at some point between that sweep and 2026-07-23, so the "commit or remove" question answered itself as "removed". Closed in the 2026-07-23 reconciliation sweep. Build itself is clean: zero compiler warnings from a from-scratch cmake + make.

## New findings 2026-09-12 (six-lens audit Wave 21 + the night research blitz; rewritten in place after the blitz verified it)

Detail: the workspace audit repo's FULL-AUDIT-2026-09-12.md (Wave 21,
the local audit workspace) and this repo's RESEARCH-deadbeef-internals-2026-09-13.md
(the 2026-09-12 night synthesis of five research agents; the authoritative
reference for the 09-13 fix lane). Where the original audit wording and the
report disagree, the report wins; the corrections are already applied below.

- [x] **CRITICAL (issue #1, unacknowledged): Configure Facets Save
      SEGFAULTs on DeaDBeeF 1.10.1+.** cui_widget.c:1035-1037 passes NULL
      as the val parameter of gtkui_plugin->w_save_layout_to_conf_key
      (contract: "val must be non-NULL"; upstream _save_widget_to_json
      derefs it at widgets.c:640-642; the reporter's stack matches) and
      uses the dead key "layout". The member shipped in 1.10.1 (commit
      9caadf5b1), so the crash is deterministic on 1.10.1, 1.10.2, and
      1.10.3 alike; the earlier "landed after tag 1.10.2 / struct-tail
      garbage" mechanism is refuted (the genuine ABI window is <=1.10.0,
      and this machine runs 1.10.3: no local crash means no OK click
      since 2026-04-24). Fix (decided): DELETE the call - it never
      worked, and per-instance settings persist via quit-time w_save()
      through the extended API - and ship v1.3.4.
      (SHIPPED v1.3.4, commit f0986ff: the call is deleted, with the
      contract and the never-call rule recorded in CLAUDE.md §4; a mock
      gtkui-vtable tripwire test (`/cui/config/save_layout`) asserts the
      plugin never calls the function and hard-fails on a NULL val.)
- [x] **Must pair with the deletion, same commit: blank panes after
      dialog OK.** The Save handler rebuilds the preset and columns but
      never resets last_ml_modification_idx, so update_tree_data
      early-returns and the new layout appears only after the next
      library change or search keystroke. Masked by the crash until now
      (cui_widget.c:1033; report section 4, H1).
      (SHIPPED v1.3.4, same commit: last_ml_modification_idx = -1 before
      update_tree_data in the OK handler.)
- [ ] **The submission-PR GO is re-gated:** dead-URL fixes (manifest.json
      + main.c + the .so rebuild) + the crash fix + issue reply + the
      Docker verify (x86_64 only; the builder offers no i686), in that
      order.
- [x] **ADDED 2026-09-13 (Brandon): the issue-#1 reply is gated on FULL
      testing of the fix, not just the push.** Before replying, ALL of
      the following must pass: (a) the mock-vtable tripwire test + the
      full suite green locally; (b) CI green on the release commit;
      (c) the live 1.10.3 smoke: install the rebuilt .so, restart,
      open Configure Facets, press OK with the DEFAULT configuration
      (the reporter's exact scenario) - no crash, panes refresh
      immediately (the H1 fix), change a setting + OK, quit, relaunch,
      confirm persistence; (d) a build of the tagged commit. The reply
      is only drafted until (a)-(d) are done. This supersedes the
      research report's step order, which had the smoke test after the
      reply.
      (DONE 2026-09-13: all four gates passed on the v1.3.4 release
      commit; the reply posted with the diagnosis and the release link;
      issue #1 closed as completed. Recorded in the PROGRESS note
      below.)
- [ ] **Lockstep enforcement is CI-blind (the local half is fine):** the
      pre-commit hook is live here (core.hooksPath = .githooks), but CI
      never checks compiled/. Add a git-level CI gate (a commit touching
      src/ or CMakeLists.txt must touch compiled/ddb_misc_cui_GTK3.so);
      no byte-compare against the floating fedora:latest container.
- [x] **Docs:** README's "1.10.x thoroughly tested" is falsified by issue
      #1 (state the verified floor after the fix: DeaDBeeF 1.9.6+ for
      source builds, core API level 17, exapi since 1.9.0); main.c's
      .plugin.website carries the dead URL baked into the shipped .so;
      the GTK4 shim gap (menus/dialogs) deserves one README sentence;
      CLAUDE.md §6.11's "pl_lock is not reentrant" is false (upstream
      creates it recursive) and §4 needs the deleted call noted.
      (SHIPPED v1.3.4: README/spec state the 1.9.6 verified floor, name
      1.10.3 as the tested runtime, and carry the GTK4 shim caveat;
      CLAUDE.md §4 records the deleted call, §6.11 is corrected, and
      §10.10-10.12 cover the GTK4 gaps, the upstream serialize leak, and
      the PLUG_TEST_COMPAT probe; the website fix rode commit 19132f7.)
- [x] **GitHub:** triage issue #1 (the report is high quality); tag
      policy DECIDED 2026-09-12: v1.3.4 onward only, no catch-up tags
      for the four untagged releases; drop the cpp topic (pure C11);
      wiki optionally off.
      (DONE 2026-09-15: the remaining two calls landed with Brandon's
      approval - the cpp topic is dropped (pure C11) and the empty wiki
      is disabled. Issue #1 triaged, answered, and closed on 2026-09-13
      per the earlier progress note.)
- [x] **Queued for v1.3.5 (decided, report section 5):** shutting_down
      via g_atomic wrappers; hidden-marker identity for the viewer
      playlist (a user playlist sharing the name currently gets wiped at
      quit); two-step-guard uniformity; the PLUG_TEST_COMPAT api probe
      as a first-class pattern. Search album-field: deferred (charter
      holds).
      (SHIPPED in the v1.3.5 queue, 2026-09-15: atomics in 5e943f0;
      marker identity in 02f0afa with the data-loss fix recorded in
      CLAUDE.md §6.14 and locked by the /cui/viewer/* tests; guard
      uniformity in bb80417 making §6.3 true; PLUG_TEST_COMPAT was
      already documented as the first-class pattern in §10.12 during
      v1.3.4 and no current call site needs a probe. Album field stays
      deferred.)

- [x] **Plugin-update rule (Brandon, 2026-09-13): build + attach on every
      tagged release.** Land with v1.3.4: a tag-triggered workflow job
      that builds the .so from the tag, runs ctest, and uploads
      `ddb_misc_cui_GTK3.so` as the Release asset (create the Release on
      tag push; `permissions: contents: write` - the bindery 403
      lesson). From v1.3.4 onward, no plugin update ships without a
      built binary attached to its tag's Release on green CI. Until the
      workflow exists, the v1.3.4 lane attaches the locally built,
      CI-verified binary manually.
      (SHIPPED v1.3.4: `.github/workflows/release.yml` - on a `v*` tag
      push it builds in the same fedora container as CI, runs ctest, then
      creates the Release with the CI-built `ddb_misc_cui_GTK3.so`
      attached; the Release body is the tag message, i.e. the verbatim
      patchnotes entry. v1.3.4's own asset was attached MANUALLY per the
      rule's fallback: the tag points one commit before the workflow
      landed, and GitHub evaluates workflows at the pushed ref, so the
      workflow cannot fire for it. From v1.3.5 on, tags are cut at
      commits that contain release.yml and the workflow covers them.)

### Final audit 2026-09-13 (THE FINAL AUDIT: NEW findings, one line each; full detail in audit-final/deadbeef-cui/FINAL-REPORT.md)
- [x] **HIGH — release.yml will ship a note-less Release on its first real fire (v1.3.5): gh release create has no notes flag, so the documented "body is the tag message" claim is unimplemented in the workflow path (v1.3.4's body exists only because it was hand-made).** Add --notes-from-tag; optionally backfill v1.3.4's empty title.
      (DONE 2026-09-15: --notes-from-tag added to the release job; v1.3.4's
      empty title backfilled to "v1.3.4" via gh release edit, body and asset
      untouched. Brandon confirmed both, Q1/Q3.)
- [x] MED — Blank panes on playlist-font change: CONFIGCHANGED rebuilds all stores but update_tree_data early-returns on the modification-index cache (cui_widget.c:1191-1209, cui_data.c:355-357) — the v1.3.4 bug family, missed in the CONFIGCHANGED path. Set last_ml_modification_idx = -1 before the refill.
      (SHIPPED v1.3.5 queue, commit 3cab3fe: the reset rides cui_handle_config_change,
      with the invalidation contract locked by /cui/update/modification_index_invalidation.)
- [x] MED — Library events and search keystrokes silently steal the current playlist: store clears fire the unblocked selection-changed handler → deferred_column_changed_cb unconditionally plt_set_curr(Library Viewer) + clear-and-copy 10 ms after every event/keystroke, no user click (the exact behavior the v1.2.4 fix removed). Block the handlers around the clears or disarm the timeout in update_tree_data.
      (SHIPPED v1.3.5 queue, commit 74942ba: the handler is blocked across the clear
      inside populate_list_multi, covering every call site; locked by
      /cui/populate/no_selection_steal. Suite now also runs full on a desktop again:
      g_test_init's fatal-warnings aborted at gtk_init_check on themes with a CSS
      parse warning.)
- [x] MED — roadmap.md:76's ticked plt_find_by_name box is false (zero call sites anywhere; the pickaxe hits are DWARF strings in rebuilt .so blobs). Untick and reword "investigated, no change", or actually switch find_viewer_playlist.
      (RESOLVED 2026-09-15: the Phase 7 box is reworded to investigated-no-change;
      switching is moot now that viewer lookup is marker-based, which
      plt_find_by_name's title-only search cannot express.)
- [x] MED — §6.3's "Defenses, all required" claim is falsified: deferred_column_changed_cb skips the g_list_find guard; restore_vscroll_idle skips the shutting_down check (saved only by cui_destroy's cancellation). Fix the doc or add the missing halves.
      (RESOLVED 2026-09-15, commit bb80417: the missing halves were ADDED (the
      queued guard-uniformity item), so all four named callbacks check
      shutting_down then g_list_find before touching anything, and §6.3's
      claim is true as written. Timeout-id clears moved after the guards.)
- [x] MED — design.md (workflow-canonical per CLAUDE.md §9) is stale on the v1.3.4 changes: still says cui.so, its Mandatory Update Workflow omits the compiled/ lockstep step, its carrier list omits three. Honesty pass or demote the §9 wording.
      (DONE 2026-09-15, commit bff3e7d: superseded banner added, the real .so
      name restored, the lockstep step added as workflow item F, the carrier
      list completed, and CLAUDE.md §9 demoted to "CLAUDE.md is the
      authority".)
- [x] MED — roadmap.md:190-201 issue-gate box unticked though all gates passed and the issue closed (tick per house pattern); roadmap.md:126 Phase 10 still promises i686 (the corrected scope is x86_64-only).
      (RESOLVED 2026-09-15: the issue-#1 gate box ticked with its shipped note;
      the Phase 10 box now promises x86_64 only.)
- [x] MED — .githooks/pre-commit:35 still compares against build/cui.so (renamed in 10219e1): the allow-path is dead and byte-identical src commits (comment-only edits) are falsely blocked. One-path edit; then land the R10 lockstep CI gate (still open by decision).
      (DONE 2026-09-15: hook allow-path fixed in ee98268, exercised for real by
      the comment batch on top of it; the R10 git-level CI gate landed in
      04a16be with a comment-only escape matching the hook's allow-path, green
      in CI.)
- [x] MED — Comment/contract batch: ml_listener_cb needs the threading-contract header comment; the modification-index paired-invariant comment at both ends; the [All] sort-pin do-not-simplify note; the scriptableItem_t mirror warning (highest memory-corruption potential uncommented); deferred_column_changed_cb's cancellation-only rationale.
      (SHIPPED v1.3.5 queue, commit 75d6dc4: all five, sitting at the exact
      lines someone would edit; the last one became a uniform-guard note when
      bb80417 added the missing halves.)
- [x] LOW — Threading/robustness: unlocked pl_item_copy walks in the menu/drag paths; popup GtkMenu leaks per right-click; g_atomic_int_compare_and_exchange needs GLib 2.74+ vs the documented glibc-2.34 floor; strcasestr without _GNU_SOURCE; get_selected_facet_names can split UTF-8 into the playlist title.
      (SHIPPED v1.3.5 queue, commit 5e943f0: pl_lock wraps on the menu/drag
      walks; ref_sink + deactivate-destroy on the popup menu; CAS replaced
      with get/set so no GLib 2.74 floor is added; _GNU_SOURCE defined;
      UTF-8-safe title clamp. shutting_down atomics rode the same batch;
      the marker identity is the queued-v1.3.5 box below, commit 02f0afa.)
- [x] LOW — Polish: the empty if-block; duplicated ap_name strncpy; four same-shape tree walks; version_minor=4 renders "1.4" beside descr 1.3.4 (record the convention or re-encode); dead sys/time.h; stale line cites (CLAUDE.md ×4, cui_data.c:24); ROADMAP duplicate box; roadmap libdl claim; "compiance" typo; the tab-strip drag-drop upstream FIXME leak (handoff).
      (DONE 2026-09-15: empty if-shell and draft comments in 9416682; the
      viewer-name triplication collapsed into a viewer_name helper (9416682);
      the version encoding switched to the minor-line convention, DECIDED
      2026-09-15, v1.3.5 ships version_minor=3 so the display reads "1.3";
      sys/time.h in a42327b; the line-cite refresh, duplicate box, libdl
      claim, and typo in bff3e7d; the drag-target comment now records the
      upstream tab-strip FIXME leak (9416682). Reviewed, staying as-is: the
      four same-shape tree walks differ in output and filtering; unifying
      them is refactor-risk without a bug under complete-posture.)
- [x] LOW — Hygiene: stale beta branch (merged, no origin twin); manifest.json/.example byte-identical (restore placeholder, document, or drop); nested .deadbeef//.cui clones vs the workspace-level clone (canonical location + git-clean -xd hazard note); workspace-internal paths in the public RESEARCH file + roadmap.md:156 (genericize); no logo.svg (mark or record); no issue template (bug.yml with the fields issue #1 proved load-bearing); ci.yml permissions + SHA pins; dead .gitignore entries.
      (DONE 2026-09-15: beta deleted, the pair documented, and the
      nested-clones-are-canonical decision + git clean -xd hazard recorded in
      CLAUDE.md §3.3 (a42327b, all Brandon-approved); RESEARCH header fixed
      and both workspace-internal paths genericized (bff3e7d); logo-not-wanted
      recorded in §3.3; bug.yml + config.yml created and the ci.yml read-only
      permissions block added (c150b12); the dead .gitignore entries replaced
      by the real compile_commands.json name (a42327b). The three stock
      GitHub Actions stay un-SHA-pinned on purpose: first-party actions,
      reviewed as they drift.)
- [x] Feature candidates logged (FINAL-REPORT L4; complete-posture respected): sort persistence per column (finishes shipped Phase 3; zero default change); in-widget empty-state hint (timed to the plugin-list submission); MAX_COLUMNS lift or spec softening (spec already promises arbitrary); pane-width persistence via our own exapi keyvalues (the DdbSplitter pointer was mechanism-wrong). Confirmed stays-parked: search album field, gtkScriptable editor, Album Art View, GTK4 port (the blocker list is the plan).
      (DONE 2026-09-15: sort persistence shipped in d85c963 and the
      empty-state hint in 940d360 (Brandon chose v1.3.5 over waiting for the
      submission re-gate). MAX_COLUMNS lift and pane-width persistence stay
      PARKED, recorded here as the reopen candidates; the rest stay parked as
      before.)
- [x] Prose pass: 68 live em-dashes (CLAUDE.md 52, README 9, roadmap 5, spec 1); the README marketing stratum ("seamlessly", "combining the power of", "bridges this gap", "gold standard… legendary"); README:13/:37 duplication; spec "robust" echo of a v0.5.0-alpha patchnote sentence.
      (DONE 2026-09-15, commit 4aae8ae: all recast with real punctuation; the
      audit block's own severity markers keep the audit's verbatim formatting.)

### Release v1.3.6 (2026-09-16, SHIPPED)

Post-blitz crash fix, caught by Brandon's own double-click on the live
machine: the v1.3.5 marker lookup unref'd its title-fallback handle when it
was NULL (the normal case once a viewer is stamped), and the real plt_unref
has no NULL guard, so the first play after v1.3.5 segfaulted before playback.
Guarded in b4d834f; the mock's plt_unref now refuses NULL like the real API
(closing the mock blind spot that hid it), and
/cui/viewer/marker_first_no_fallback walks the exact path. Live-verified
playing again before tagging.

### Release v1.3.5 (2026-09-15, SHIPPED)

Tag v1.3.5 cut at e2f4efa (CI green, verbatim patchnotes tag body), Release
live with the verbatim entry as body and the CI-built .so attached. The live
1.10.3 smoke on a real 10.7k-track library caught one real regression BEFORE
the tag: the new menu-leak teardown destroyed menus mid-activation, breaking
every right-click item; fixed in e2f4efa with a deferred teardown and a
tripwire test (commit e2f4efa, on the tag). Smoke covered: registration,
startup populate, menu open/activate, defaults-OK with forced pane refresh,
selection cascade with viewer populate, clean shutdown. The release job's
first run failed on a gh limitation (--notes-from-tag + --repo unsupported;
fixed in 91767e8) and the title needed --title (c642c77); the Release was
created by hand with the CI-built artifact per the plugin-update rule's
fallback. The submission-PR builder-Docker verify remains the one open gate
(Brandon's session).

The final-blitz release content: the release.yml notes flag (the one HIGH), the
stale-cache/callback bug pair, the hidden-marker viewer identity (the data-loss
fix), the queued v1.3.5 hygiene (shutting_down atomics, guard uniformity,
strcasestr/_GNU_SOURCE, GLib-2.74 CAS removal), the comment/contract batch, the
lockstep CI gate + hook fix, per-column sort persistence, the empty-state hint,
the docs-truth and prose passes, and the hygiene/removal set. Full notes in
patchnotes.md.

**CONFIRMED-prior (final-audit verification):** the queued v1.3.5 items (shutting_down atomics, viewer-name identity, guard uniformity, PLUG_TEST_COMPAT) unchanged; lockstep CI-blind open by decision; strcasestr/_GNU_SOURCE. SUPERSEDED (verified fixed with tripwire): the CRITICAL NULL-val call and the HIGH blank-panes-after-dialog; dead bdkl/ URLs; the 1.10.x-tested claim; the stale roadmap header. Audit-side: the sheet's build-output paths are pre-rename (build/ddb_misc_cui_GTK3.so now). Slop-reader verdict: two prose strata: the current forensic voice is human-grade; the residue is 68 live em-dashes, the README marketing paragraphs, and the pre-v1.2.4 patchnotes stratum (records policy, author's call).
