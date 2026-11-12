# Multi-Agent Engineering Roadmap: dfiles

This roadmap is designed for concurrent execution across autonomous agents and specialized subagents. Dependencies and concurrency rules are explicitly marked for each phase.

---

## Roadmap Overview & Concurrency Dependency Graph

```
           [Phase 1: Foundation & Window Shell]
                             │
            ┌────────────────┴────────────────┐
            ▼                                 ▼
[Phase 2: File Views & F3 Dual-Pane]   [Phase 3: F4 Terminal Drawer]
            │                                 │
            ├─────────────────────────────────┘
            ▼
[Phase 4: PathBar, Filter Bar & Inspector (F11)]
            │
            ▼
[Phase 5: Async File Operations & Cross-Pane DND]
            │
            ▼
[Phase 6: Integration, Shortcuts, Verification & CodeRabbit Review]
```

---

## Phase 1: Foundation & Window Shell
* **Status**: Ready to Begin
* **Parallelizable**: Lead agent / Foundation setup
* **Dependencies**: None
* **Recommended Skills & Plugins**: `roo-code` (execution via `.clinerules`), `ralph-loop` (iteration tracking in `progress.txt`), `coderabbit` (memory & header review)
* **Deliverables**:
  - `meson.build`: Root build configuration checking C++20, `gtk4`, `libadwaita-1`, `vte-2.91-gtk4`, and `gio-2.0`.
  - `src/main.cpp`: GApplication entry point, resource loading, signal hooking.
  - `src/core/app.hpp/.cpp`: `DFilesApp` subclassing `AdwApplication` with actions (`quit`, `preferences`, `about`).
  - `src/ui/window.hpp/.cpp`: `DFilesWindow` subclassing `AdwApplicationWindow` with `AdwToolbarView` and `AdwToastOverlay`.
  - `src/ui/headerbar.hpp/.cpp`: HeaderBar with window controls, view toggles, split-view button (`F3`), and terminal button (`F4`).
  - `src/ui/sidebar.hpp/.cpp`: Places Sidebar with XDG standard directories, Root (`/`), Trash, and mount monitoring.
* **Verification Criteria**:
  - `meson setup build && ninja -C build` builds cleanly without warnings.
  - Window launches, presents clean Libadwaita window with sidebar, responsive headerbar, and proper theme adaptation.

---

## Phase 2: File Views & Dual-Pane (`F3`) Engine
* **Status**: Blocked on Phase 1
* **Parallelizable**: Can be split between 2 subagents (Pane container vs File Item / View model)
* **Dependencies**: Phase 1
* **Recommended Skills & Plugins**: `gsd-workstreams` (parallel subagents), `roo-code` (discrete tasks), `coderabbit` (verify GListModel memory management)
* **Deliverables**:
  - `src/model/file_item.hpp/.cpp`: GObject wrapper representing a directory item (name, path, size, mtime, MIME icon, is_dir, symlink target).
  - `src/views/file_view.hpp/.cpp`: Directory content renderer:
    - List View using `GtkColumnView` (Columns: Name, Size, Modified Date, Permissions).
    - Grid View using `GtkGridView` with large file icons and centered labels.
    - Sorting by Name, Size, Date, Type with natural sort order.
  - `src/views/pane.hpp/.cpp`: Single autonomous browsing pane containing:
    - Active directory tracker (`GFile`).
    - History stack (Back/Forward navigation, `Alt+Left` / `Alt+Right`).
    - File selection tracking (single, multi-selection).
  - `src/views/split_view.hpp/.cpp`: Twin-pane container (`GtkPaned`):
    - Toggled via `F3` or headerbar button.
    - Active pane highlight indicator (subtle accent focus outline).
    - Focus transfer via `Tab` or click.
* **Verification Criteria**:
  - Browsing folders loads directory contents quickly.
  - Pressing `F3` opens/closes the secondary pane smoothly.
  - Clicking on a folder in either pane navigates independently.

---

## Phase 3: Embedded Terminal (`F4`) with Bidirectional Sync
* **Status**: Blocked on Phase 1 (Can run concurrently with Phase 2)
* **Parallelizable**: Independent Agent (Terminal specialist)
* **Dependencies**: Phase 1 window layout
* **Recommended Skills & Plugins**: `reverse-engineer-anything` (inspect native Dolphin VTE/Konsole OSC 7 behavior if needed), `roo-code`
* **Deliverables**:
  - `src/terminal/terminal_drawer.hpp/.cpp`:
    - Collapsible bottom drawer container with grab handle / resizer.
    - Integrated `VteTerminal` widget spawning the user's default `$SHELL`.
    - Custom color scheme matching Libadwaita theme (dark/light palette).
  - **Directory Sync Engine**:
    - **UI -> Terminal**: When active pane changes folder, terminal syncs working directory via shell injection or background chdir.
    - **Terminal -> UI**: Terminal emits OSC 7 (URI notify); `dfiles` catches this event and updates active pane to current shell directory.
  - Shortcut handling: `F4` toggles terminal visibility instantly without resetting running terminal processes.
* **Verification Criteria**:
  - `F4` opens terminal at bottom; user can execute shell commands.
  - Navigating to `/etc` in the file pane syncs terminal to `/etc`.
  - Running `cd /tmp` in the terminal updates file pane to `/tmp`.

---

## Phase 4: Path Navigation (`Ctrl+L`), Filter Bar (`Ctrl+I`) & Live Inspector (`F11`)
* **Status**: Blocked on Phase 2
* **Parallelizable**: Can be split between Navigation agent and Inspector agent
* **Dependencies**: Phase 2 Pane & Model
* **Recommended Skills & Plugins**: `gsd-ui-phase` / `gsd-ui-review` (Libadwaita visual contract and alignment audit), `roo-code`
* **Deliverables**:
  - `src/navigation/path_bar.hpp/.cpp`:
    - Nautilus-style breadcrumb pills with clickable folder segments.
    - `Ctrl+L` toggles between breadcrumbs and editable `GtkEntry` with path auto-completion.
  - `src/navigation/filter_bar.hpp/.cpp`:
    - Instant search/filter bar activated via `Ctrl+I`.
    - Real-time `GtkCustomFilter` filtering visible items in active pane as user types.
  - `src/inspector/inspector.hpp/.cpp`:
    - Collapsible right sidebar (`F11`).
    - Live file metadata: file name, type, full path, permissions (`chmod` format), owner, group, access/modify times.
    - Image and media preview generation via `GdkTexture` / thumbnailer.
    - Checksum generator (`src/inspector/checksum.hpp/.cpp`): Asynchronous MD5 and SHA-256 calculation on demand.
* **Verification Criteria**:
  - Clicking breadcrumb navigates up parent hierarchy. `Ctrl+L` allows typing `/usr/share` and pressing Enter.
  - Typing in `Ctrl+I` filters list in real-time.
  - Selecting a file and pressing `F11` shows detailed inspector and calculates SHA-256 hash.

---

## Phase 5: Asynchronous File Operations & Cross-Pane Actions
* **Status**: Blocked on Phase 2 & 4
* **Parallelizable**: Operations Agent
* **Dependencies**: Phase 2 (Pane & Selection), Phase 1 (Toasts)
* **Recommended Skills & Plugins**: `coderabbit` (verify async `GCancellable` thread safety & zero memory leaks), `ralph-loop`
* **Deliverables**:
  - `src/operations/file_ops.hpp/.cpp`:
    - Asynchronous non-blocking file copy, move, trash, delete, rename, and directory creation.
    - Cancellation via `GCancellable`.
    - Progress reporting via `AdwToast` with progress bar or percentage.
  - **Dolphin Cross-Pane Integration**:
    - `F5`: Quick copy selected items from active pane to inactive pane.
    - `F6`: Quick move selected items from active pane to inactive pane.
    - Drag and drop between left and right split panes with copy/move modifier keys.
  - Context menu actions: "Open With Default App", "Open in Terminal", "Copy Path", "Move to Trash", "Compress...", "Properties".
* **Verification Criteria**:
  - Copying large files does not freeze the UI.
  - Cross-pane `F5` / `F6` transfers files seamlessly.
  - Trashing files sends them to `trash:///` with undo toast notification.

---

## Phase 6: Polish, Performance, Packaging & CodeRabbit Quality Gate
* **Status**: Final Stage
* **Parallelizable**: Testing & Review Squad
* **Dependencies**: All previous phases
* **Recommended Skills & Plugins**: `adversarial-council` (deploy 2 Supervisors, 3 Workers, 2 Testers and 6-Judge High Court for full adversarial trial), `gsd-audit-fix`, `coderabbit`
* **Deliverables**:
  - Performance profiling: zero memory leaks via Valgrind/ASan, responsive directory loading with 10,000+ files.
  - Keyboard shortcuts verification: `F3`, `F4`, `F5`, `F6`, `F11`, `Ctrl+L`, `Ctrl+I`, `Ctrl+H` (show hidden files).
  - Desktop integration: `data/org.gnome.dfiles.desktop`, icon assets, appdata/metainfo XML.
  - Code review and automated checks meeting `.coderabbit.yaml` strict profile.
* **Verification Criteria**:
  - Clean build with `-Wall -Wextra -Wpedantic`.
  - Zero critical issues reported by linter or CodeRabbit review.
