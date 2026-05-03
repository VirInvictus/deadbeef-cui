# cui-update.md — Columns UI parity research

Research artifact, not a commitment. Goal: catalogue what foobar2000's Columns UI offers
that deadbeef-cui currently doesn't, sort it into "feasible / nice / forget it", and tie
each viable candidate to the actual DeaDBeeF API surface it would have to ride on.

**Status:** v1.2.5 ships. Phase 6 ("1.0 milestone") is closed; the plugin is documented
as feature-complete pending community feedback. This document is for the next batch of
discretionary features, if and when we want to pick them up.

**No code in this doc.** It exists to inform a future planning conversation.

---

## 1. Source material

- `.cui/` — fresh `git clone --depth 1 https://github.com/reupen/columns_ui.git`
  (LGPL v3). Cloned 2026-05-03. Read-only reference.
- `.deadbeef/` — vendored DeaDBeeF source (already present), the canonical reference
  for what plugin APIs we can call. CLAUDE.md §3 covers how to navigate it.
- DeaDBeeF compile-time API: system `/usr/include/deadbeef/` from `deadbeef-devel`.
- The actual GTKUI plugin shared object: `/usr/lib64/deadbeef/ddb_gui_GTK3.so` —
  several useful entry points beyond the public API are exported here (verified with
  `nm -D`).

### Columns UI panel inventory (from `.cui/foo_ui_columns/`)

| fb2k panel | What it does | Closest deadbeef-cui analogue |
|---|---|---|
| **Filter panel** (`filter.cpp`, `filter.h`) | One column per panel, stack via splitters, multi-select, sort, inline-edit, drag-drop, send-to-playlist | Our entire plugin. Already broadly at parity; gaps below. |
| **NG Playlist** (`ng_playlist/`) | Custom playlist with grouping, inline edit, sticky headings | Out of scope — DeaDBeeF's playlist widget is the user-facing track view, by design (CLAUDE.md §10.4). |
| **Item Details** (`item_details.cpp`) | Title-formatted info pane for now-playing/selected track | None. Candidate (§3). |
| **Item Properties** (`item_properties.cpp`) | Tag table for selected/playing track | DeaDBeeF already ships the Track Properties dialog. We can surface it (§2.1) but not duplicate it. |
| **Artwork view** (`artwork.cpp`) | Cover art for current/selected track | DeaDBeeF's `Album art` widget exists in GTKUI. We can pull a column-mode variant (§3) but not duplicate the panel. |
| **Buttons toolbar** | Configurable button bar | Out of scope (orthogonal to faceted browsing). |
| **Audio track toolbar** | Multi-track audio file UI | Out of scope. |
| **Playlist tabs / switcher** | Tab strip / sidebar of playlists | DeaDBeeF already ships these. |
| **Splitter / tabs containers** | Layout primitives | DeaDBeeF's GTKUI design mode covers this. |
| **Status bar / status pane** | Bottom-of-window line | Orthogonal. |

The only fb2k panel that is genuinely *the same product* as ours is the Filter panel.
Everything else is either already in DeaDBeeF in some form, or out of scope.

---

## 2. Filter-panel parity gaps (in priority order)

These are things Columns UI's Filter panel does that we don't, ranked by user value vs.
implementation risk.

### 2.1 Reuse the standard track context menu — **strong recommend**

Right now `on_tree_button_press` (cui_widget.c:215) builds a hand-rolled menu with two
items: "Add selection to current playlist" and "Send selection to new playlist". Columns
UI's filter panel exposes the *full* track context menu (Properties, Convert, Add to
playqueue, Reload metadata, Delete from disk, etc.).

DeaDBeeF's GTKUI exposes the same canonical menu builder used by the standard medialib
widget:

- Symbol: `list_context_menu_with_dynamic_track_list (ddb_playlist_t *, trkproperties_delegate_t *)`
- Header: `.deadbeef/plugins/gtkui/plmenu.h:40`
- Verified exported from `/usr/lib64/deadbeef/ddb_gui_GTK3.so` via `nm -D`.
- Pattern to copy: `medialibwidget.c:670-680` — alloc temp playlist, insert selected
  tracks, hand to `list_context_menu_with_dynamic_track_list`, unref.

How to access it without changing our load model: dlsym on the same `ddb_gui_GTK3.so`
handle we already use for `gtkui_medialib_get_source` (cui_widget.c:417-426). If the
symbol is missing on a future GTKUI build, fall back to the existing hand-rolled menu.

**Effort:** small. ~50 lines including the dlsym fallback path.
**Risk:** low. The menu builder owns its lifetime; we just have to keep the temp
playlist alive across the popup (the existing medialibwidget pattern shows how).
**Verdict:** Should ship. Replaces ~30 lines of menu code with a one-liner and gives
us *every* track action DeaDBeeF supports for free, including future ones.

Companion: keep our existing "Configure Facets..." and "Sync library" entries — those
are widget-specific, not track-list actions.

### 2.2 Drag selection out of the facet onto a playlist tab — **recommend**

Columns UI's Filter panel is a drag source for tracks. In GTK terms the medialib widget
already shows the exact pattern (`medialibwidget.c:933-936`):

```
GtkTargetEntry entry = { .target = TARGET_PLAYITEM_POINTERS, ... };
gtk_drag_source_set (tree, GDK_BUTTON1_MASK, &entry, 1, GDK_ACTION_COPY);
g_signal_connect (tree, "drag_data_get", G_CALLBACK (_drag_data_get), w);
```

`TARGET_PLAYITEM_POINTERS` is `"DDB_PLAYITEM_POINTERLIST"` (defined at
`.deadbeef/plugins/gtkui/playlist/ddblistview.h:38`). The receivers — playlist tab strip
(`ddbtabstrip.c:146`) and the playlist view itself — already understand this format.

For us this needs:
- For each selected row, walk the tree (we already have this — `add_tracks_recursive_multi`
  in cui_data.c:95 is the exact same iteration).
- Pack `DB_playItem_t **` pointers into the `GtkSelectionData` payload.
- Don't `pl_item_ref` them — the receiver decides whether to copy.

**Effort:** small. The pointer-packing already exists conceptually in
`add_tracks_recursive_multi`; we just need a sibling that fills an array instead of a
playlist, plus the GTK plumbing.
**Risk:** low. No new lifecycle concerns (data-get fires synchronously at drag start).
**Verdict:** Ship after 2.1.

### 2.3 Sort indicators on column headers — **recommend (cosmetic)**

Currently `gtk_tree_view_column_set_sort_column_id` is called (cui_widget.c:311, 321),
which gives us the *behavior* of sortable columns but not the visible up/down arrow,
because we override the header widget with our own GtkLabel (cui_widget.c:289) for font
inheritance and that bypasses the default header chrome.

To get arrows back: when applying our custom header label, also build a small box
containing the label + a `GtkArrow` (GTK3) / `GtkImage` with `pan-up/pan-down-symbolic`
(GTK4-safe), and update its visibility/orientation in response to
`g_signal_connect (column, "notify::sort-indicator", ...)`. fb2k's filter panel does
the same thing; CUI exposes "Show sort indicators" as a toggle.

**Effort:** small. Confined to `cui_apply_header_font` and a per-column "sort changed"
listener.
**Risk:** low. Pure UI; doesn't affect the data path.
**Verdict:** Ship if 2.1+2.2 land — easy polish.

### 2.4 "Add to playqueue" and "Play next" — **recommend**

Two things missing from the menu surfaced by 2.1 (depending on how `list_context_menu`
sequences items): explicit "Add to playqueue" and "Play next." DeaDBeeF API:

- `playqueue_push (DB_playItem_t *)` — deadbeef.h:1527
- `playqueue_remove (DB_playItem_t *)` — deadbeef.h:1529
- `playqueue_test (DB_playItem_t *)` — deadbeef.h:1531

If 2.1's standard menu already covers these (it does — DeaDBeeF's `actionhandlers.c`
provides a `Add to playback queue` action), this is automatic. If not, we wire two
direct menu items.

**Verdict:** Almost certainly free with 2.1. Verify when implementing.

### 2.5 Search-bar fields — **maybe**

`track_matches_search` (cui_data.c:5) hard-codes `title` and `artist`. Columns UI lets
the user pick the fields the search runs against, including via a TF script. Concrete
options for us:

- Add a config-dialog field "Search fields:" (comma-separated list, default
  `title,artist`).
- Loop the list in `track_matches_search` calling `pl_find_meta_raw` per field.

What we should *not* do without explicit ask: support DeaDBeeF's full title-formatting
search, because that means a per-track `tf_eval` in a hot path called from facet
aggregation. CLAUDE.md Phase 8 already dropped that line of thinking.

**Effort:** trivial.
**Risk:** low — `track_matches_search` is already correctly locked.
**Verdict:** Reasonable small win. Defer until someone asks; don't speculatively add
config surface.

### 2.6 Toggle for the `[All]` row — **defer**

`populate_list_multi` (cui_data.c:283) always inserts the synthetic `[All (N)]` row.
Columns UI exposes "Show all node" as a per-panel toggle. Adds one config bool, one
condition, one re-key into `cui_serialize_to_keyvalues`/`cui_deserialize_from_keyvalues`.

CLAUDE.md §6.9 calls out that the `is_all` column is load-bearing for selection
semantics — hiding the row is fine (just don't insert it), but never break the column
schema. The selection-hash code (cui_widget.c:34-43) already handles "no [All] row
present" correctly because absence of `is_all=TRUE` is the default branch.

**Effort:** trivial.
**Verdict:** Low value relative to 2.1-2.4. Add on request.

### 2.7 Sort-direction persistence — **defer**

Columns UI remembers sort column + direction across sessions. We persist columns
themselves but not the sort state. Would add two keyvalues per facet
(`col1_sort_id`, `col1_sort_dir`) and read them in `cui_init`.

**Effort:** small.
**Verdict:** Modest user value. Add on request.

### 2.8 Source switch: medialib vs. active playlist — **don't**

fb2k's filter panel filters the *active playlist* by default; the "library" panels are
a separate plugin (`foo_facets`). We picked medialib up front, and the entire data
pipeline (tree from `create_item_tree`, listener on `add_listener`) is medialib-shaped.
A playlist-source mode would mean a parallel pipeline that builds the tree from a
playlist iter and listens to `DB_EV_PLAYLISTCHANGED` — meaningful new code volume,
overlap with the playlist widget itself, and confusing UX.

**Verdict:** Don't. If someone asks, suggest right-click → "Filter from this
playlist" elsewhere or rethink scope.

### 2.9 Inline editing of tag values — **don't (yet)**

Columns UI lets you double-click a row to rename a tag value, applying
`pl_replace_meta` + `junk_rewrite_tags` to every track under that node. This is
genuinely useful (merge "Hip Hop" → "Hip-Hop") but:

- Touches §6.4 (background-thread listener will refire while the rewrite is in
  progress) and §6.6 (rebuild-tree atomicity) of CLAUDE.md.
- `junk_rewrite_tags` (deadbeef.h:1300) is an actual file write — needs error
  handling, scanner-state checks, and likely a confirmation dialog.
- No undo. DeaDBeeF has an undo manager but exposing it from a plugin needs care.

**Verdict:** Real feature, real risk. Don't build it without explicit scoping; if
asked, write a separate plan first that lays out the locking story, the abort path,
and the user confirmation flow.

---

## 3. New widgets (separate from the Facet Browser)

Columns UI ships several non-filter panels. Two could plausibly become standalone
widgets registered alongside ours via a second `w_reg_widget` call in `main.c:48`. They
are explicitly *new widgets*, not modifications to the existing one.

### 3.1 Item Details (`cui_item_details`) — **plausible**

A box that displays a title-formatting script evaluated against the now-playing track,
or the selected playlist row. Columns UI calls this "Item Details."

Required APIs (already available, all level ≤ 18):
- `tf_compile` / `tf_eval` / `tf_free` — deadbeef.h:1510, 1521, 1512.
- `ddb_tf_context_t` — deadbeef.h:783-818 (verified, `it`, `plt`, `idx`, `iter` are
  what we need).
- Subscribe to `DB_EV_SONGCHANGED` (deadbeef.h:555) and `DB_EV_TRACKINFOCHANGED`
  (deadbeef.h:559) for now-playing tracking.
- For "selected track" tracking: listen for `DB_EV_PLAYLISTCHANGED` with
  `DDB_PLAYLIST_CHANGE_SELECTION` (line 528 — `DB_EV_SELCHANGED` is gone, replaced by
  this).
- Re-eval is already cheap, debounce isn't strictly required.

Widget shape: a single `GtkLabel` with `use-markup` if we sanitize, or a `GtkTextView`
with non-editable buffer. Per-instance config via the same
`ddb_gtkui_widget_extended_api_t` pattern we already use: `script` (default
`%title% — %artist%\n%album% — %tracknumber%/%totaltracks%`), `tracking_mode` (now
playing / selected), `font`, `alignment`.

**Effort:** medium-small (~300 lines, one new TU).
**Risk:** low — independent from Facet Browser; failure modes don't touch the existing
shutdown invariants. New widget = new lifecycle, but the patterns are already
established in cui_widget.c.
**Verdict:** Reasonable Phase-11 candidate. Genuinely useful (no DeaDBeeF widget does
quite this — `Selection Properties` and `Music dir browser` are different).
The build-system change is a single-file addition; no third-party deps.

### 3.2 Album-art column / browse-by-cover — **plausible (deferred from Phase 6)**

Already on the roadmap as deferred. Two flavours:

(a) **Standalone Album Art Viewer widget** — shows cover for the current track.
DeaDBeeF's GTKUI already has `coverart_pixbuf_loader` and an artwork widget, so we'd
mostly be duplicating. Don't.

(b) **Album-art column** — a column in the existing facet stack that, instead of text
rows, shows a grid/list of cover thumbnails per album. This is the more interesting
shape and is what users actually want.

Required APIs:
- `ddb_artwork_plugin_t` — `.deadbeef/plugins/artwork/artwork.h:93-139`.
  `cover_get` (line 108) is async, callback-based.
  `add_listener` (line 119) for cache-invalidate.
  `cover_info_release` (line 116) for the result struct.
- `plug_get_for_id ("artwork")` to discover the plugin at startup. Failure should
  gracefully fall back to a text column with an `[Album]` placeholder.

Lifecycle care:
- Cover queries are async — a `cui_widget_t` could be destroyed before its in-flight
  callback fires. Use `allocate_source_id` (line 134) per widget and call
  `cancel_queries_with_source_id` from `cui_destroy`.
- The artwork plugin is *optional* in DeaDBeeF builds. Treat absence the same as
  medialib being absent: log, continue, no covers.

**Effort:** medium-large. Not just the API; needs a `GtkIconView`-style widget that
plays nicely with selection in a way our cascading filter expects, and the
cell-renderer pattern from `mlcellrendererpixbuf.c` we'd want to reuse.
**Risk:** medium. Async + GTK + our shutdown story is the main hazard
(CLAUDE.md §6.3).
**Verdict:** Real Phase-11 work, not a quick win. Worth a dedicated plan before
starting.

---

## 4. Things to explicitly decline

| Feature | Why not |
|---|---|
| Dark-mode theming | DeaDBeeF inherits the GTK theme. Nothing to do. |
| DirectWrite / colour emoji / variable fonts | Windows-only, irrelevant on Linux. |
| HDR artwork / colour management | Same. |
| SVG buttons toolbar | Out of scope — we're not a buttons toolbar. |
| Sticky group headings | Belongs to the playlist widget, not us. |
| Smooth scrolling | GtkTreeView's behavior is the GTK theme's call. |
| Replacing the playlist widget | CLAUDE.md §10.4. Architectural no. |
| Title-formatting query language | Too expensive in a hot path; CLAUDE.md Phase 8 already considered and dropped client-side `tf_eval`. |

---

## 5. Suggested ordering, if/when we pick this up

A loose dependency chain. Each step is independently shippable.

1. **2.1 Standard track context menu** — biggest UX win for least code; unlocks
   Properties, Convert, Add-to-playqueue for free. (v1.3.0 candidate)
2. **2.2 Drag-out source** — natural follow-up; same `dlsym ddb_gui_GTK3.so` story,
   reuses the iteration we already have.
3. **2.3 Sort indicators** — cosmetic, do alongside 2.1/2.2.
4. **3.1 Item Details widget** — separate body of work, separate version bump
   (probably v1.4.0). Discrete, low-risk.
5. **3.2 Album-art column** — own plan, own version bump (v2.0 candidate as a
   capstone). Don't start until 3.1 lands.
6. **2.5 Search fields**, **2.6 [All] toggle**, **2.7 sort persistence** — only
   on user request. Each is small but adds config surface.
7. **2.9 Inline tag editing** — only after a separate dedicated design pass.

---

## 6. Open questions to resolve before any work starts

- Do we actually want to bump beyond 1.x? Phase 10 (plugin-list submission) suggests
  v2.0 is being held for "a major-feature milestone." Album art view qualifies; Item
  Details probably qualifies as a paired release. 2.1+2.2+2.3 together feel like a
  v1.3.0.
- The `cui_scriptable.h` mirror (CLAUDE.md §6.2) — re-verify against the current
  `.deadbeef/shared/scriptable/scriptable.c` before any release that builds against a
  bumped DeaDBeeF clone.
- Cross-platform verification (roadmap Phase 10) is still pending. None of the
  candidates above change the build matrix; they all stay GTK3-with-GTK4-shims.

---

**End.** This document is research input for a planning conversation, not a roadmap
update. The roadmap (`roadmap.md`) and patchnotes (`patchnotes.md`) get touched only
once we pick something here and ship it.
