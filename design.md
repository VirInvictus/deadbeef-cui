# deadbeef-cui - Design & Workflow Guidelines

This document serves as the "brain" for AI agents (and human contributors) working on the `deadbeef-cui` project. It details the repository structure, how we interact with the DeaDBeeF API via the local `.deadbeef` source, what each part of our source code does, and the mandatory workflow for making changes.

## 1. Using the `.deadbeef` Reference Source

We keep a clone of the official DeaDBeeF source code in the `.deadbeef/` directory. **This code is NOT compiled as part of our plugin.** Instead, it serves as a critical local reference for understanding the DeaDBeeF Plugin API, which is often under-documented.

- **Primary Header:** `.deadbeef/include/deadbeef/deadbeef.h` contains the core `DB_functions_t` and `DB_plugin_t` structs, representing the main player API and track item (`DB_playItem_t`) structures.
- **GTKUI Definitions:** We often need to interact with the GTK UI. Look at `.deadbeef/plugins/gtkui/` to understand how `ddb_gtkui_t` works, how widgets are registered, and how the layout is managed.
- **Medialib API:** To understand how the faceted library queries work, we reference `.deadbeef/plugins/medialib/` and related headers to see how `ddb_mediasource_source_t` and item trees are constructed and traversed.
- **Usage Strategy:** When implementing a new feature or fixing a bug, use search tools (`grep_search`) within `.deadbeef/` to find examples of how core DeaDBeeF handles specific events (like `DB_EV_PLAYLISTCHANGED`), locking (`pl_lock`/`pl_unlock`), or configuration values.

## 2. Codebase Architecture: What Code Does What Where

Our plugin is a native C shared library (`cui.so`) built via CMake. The codebase is broken down into modular components:

### Core Modules
- **`src/main.c`**: Plugin entry points (`cui_start`, `cui_stop`, `cui_message`) and action handlers (like the global search hotkey). Connects DeaDBeeF to the UI layer.
- **`src/cui_widget.c` / `src/cui_widget.h`**: The GTK UI layer. Handles widget registration (`cui_create_widget`), building the multi-pane GTK container (`rebuild_columns`), configuration dialog UI, and event callbacks (key presses, clicks, and menu triggers).
- **`src/cui_data.c` / `src/cui_data.h`**: The media library engine. Interfaces with the `medialib` plugin to construct the data tree (`update_tree_data`), count tracks (`count_tracks_recursive`), aggregate selected nodes, and populate the generated "Library Viewer" playlist.
- **`src/cui_scriptable.c` / `src/cui_scriptable.h`**: Implements custom scriptable types mirroring the `medialib` internals. Used to build dynamic, format-driven tree schemas to query the DeaDBeeF database based on user column layouts.
- **`src/cui_globals.h`**: Contains shared data structures (`cui_widget_t`), constant definitions (`MAX_COLUMNS`), and external state pointers (`deadbeef_api`, `medialib_plugin`, etc.) that orchestrate the interaction between the above modules.

### Other Files
- `CMakeLists.txt`: Build configuration pointing to `/usr/include/deadbeef` and GTK3 to compile the `src/` modules into a shared library.
- `manifest.json.example`: Metadata format referencing how the plugin can be described (if using automated builders).

## 3. Mandatory Update Workflow

Whenever you make structural changes, add features, or fix bugs, you MUST follow these documentation procedures to keep the project synchronized.

### A. Updating `patchnotes.md`
- **When:** After completing any bug fix, refactor, or feature implementation.
- **How:** Add a timestamped/versioned entry. Describe what was changed, the rationale behind it, and any internal API shifts. Be precise about the files and functions modified.

### B. Updating Version Numbers
- **When:** When a feature is finalized or a release is conceptually ready (always confirm with user instructions).
- **Where:** 
  - `src/main.c` inside the `DB_misc_t plugin` definition (`plugin.version_major`, `plugin.version_minor`).
  - `README.md` (Update the version badges at the top).
  - `spec.md` (Update the Version line).
- **How:** Follow standard Semantic Versioning.

### C. Updating `roadmap.md`
- **When:** When a task is completed, mark it as done (e.g., `- [x]`). If new technical debt is introduced or future work is identified during implementation, append it to the roadmap.

### D. Updating `README.md` and `spec.md`
- **When:** ONLY if a significant architectural change or major new feature is added (e.g., adding a new settings dialog, changing dependencies, supporting a new platform like GTK4).
- **How:** Ensure that the description of the plugin's capabilities accurately reflects the current state of the codebase. Keep instructions for building and installation up to date.

### E. Updating this file (`design.md`)
- **When:** If the project structure changes (e.g., splitting `src/main.c` into multiple files like `ui.c`, `medialib_sync.c`), or if a new strategy for querying `.deadbeef` is adopted.
- **How:** Modify Section 2 ("What Code Does What Where") to reflect the new file layout so future AI interactions do not hallucinate the old structure.

### F. Committing to Git
- **When:** After completing any phase, increment, bug fix, or significant update (and after successfully verifying the build and updating all documentation files above).
- **How:** Use `git add` to stage the changes and `git commit` with a clear, descriptive message summarizing the changes. Never leave successful work uncommitted. Do not push to remote unless explicitly asked by the user.

---
**End of Guidelines.** Before executing changes, always cross-reference this file to ensure your strategy aligns with the established architecture and workflow.