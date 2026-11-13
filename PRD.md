# Project Requirements Document (PRD): dfiles

## 1. Executive Summary & Vision
**`dfiles`** is a high-performance Linux file manager designed to solve the classic Linux desktop dilemma:
* Combining the **clean, elegant, distraction-free GNOME / Libadwaita aesthetic** of Nautilus.
* With the **indispensable power-user features, speed, and workflow efficiency** of KDE Dolphin.

The project is built entirely in **Modern C++ (C++20)** using **GTK4**, **Libadwaita 1.x**, **VTE for GTK4 (`vte-2.91-gtk4`)**, and **GIO/GLib**, built with **Meson & Ninja**.

---

## 2. Multi-Agent Development Guidelines
> [!IMPORTANT]
> **Instructions for Parallel AI Agents / Subagents**:
> 1. **Modular Boundaries**: Work is partitioned into isolated modules (`src/core`, `src/views`, `src/terminal`, `src/inspector`, `src/operations`, `src/navigation`). Do NOT modify files outside your assigned module without referencing `ARCHITECTURE.md`.
> 2. **Shared Contracts First**: All cross-module communication uses signals (GObject signals / C++ callbacks) and interfaces defined in `ARCHITECTURE.md`. Never hardcode direct coupling between views.
> 3. **Non-Blocking Architecture**: The UI thread MUST remain responsive at 60fps. All filesystem I/O, hashing, thumbnailing, and file transfers must be asynchronous (`GTask`, `gio::FileAsync`, or worker threads).
> 4. **Incremental Verifiability**: Every sub-task must build cleanly via `meson compile -C build` and pass unit checks before being marked complete in `PRD.md` and `progress.txt`.

### 2.1. Installed Plugin & Skill Synergy Framework
This project is pre-configured to utilize the full suite of installed plugins and skills:
* **Ralph Loop (`ralph-loop-plugin`)**: Drives continuous iterative development. Tracks persistent state in `PRD.md` and logs verification steps to `progress.txt`, preventing context rot across long coding sessions.
* **Roo Code (`roo-code-plugin`)**: Guides agent execution according to `.clinerules`, maintaining laser focus on one discrete sub-task per turn with step-by-step verification.
* **CodeRabbit (`coderabbit-plugin`)**: Operates as an automated quality gate using `.coderabbit.yaml`. Rigorously audits all C++20 and GTK4 changes for GObject reference leaks, thread safety, and memory management before approval.
* **Adversarial Council (`adversarial-council`)**: Can be deployed at major milestone boundaries (2 Supervisors, 3 Workers, 2 Testers, and the 6-Judge High Court) to stress-test concurrency, thread safety, and UI ergonomics through adversarial trial.
* **GSD (`gsd-*` suite)**: Manages phase planning (`gsd-plan-phase`), wave parallelization (`gsd-execute-phase`), parallel workstreams (`gsd-workstreams`), and automated verification (`gsd-code-review`, `gsd-audit-fix`).
* **Reverse Engineer Anything (REA / `rea` MCP)**: Inspects local system binaries (`/usr/bin/nautilus` and `/usr/bin/dolphin`) when deep inspection of native Freedesktop actions, D-Bus interfaces, or shortcut tables is required.

---

## 3. Technology Stack & Dependencies
* **Core Language**: Modern C++ (C++20 standard: designated initializers, concepts, `std::filesystem`, `std::format`).
* **UI Framework**: GTK4 (`gtk4 >= 4.12`) & Libadwaita (`libadwaita-1 >= 1.4`).
* **Terminal Emulator**: VTE GTK4 widget (`vte-2.91-gtk4 >= 0.74`).
* **VFS & I/O**: `GIO` / `GLib-2.0` (GFile, GFileEnumerator, GFileMonitor, GAppInfo, GVFS).
* **Build System**: `meson` (>= 0.60) + `ninja`.
* **Compiler**: `g++` (>= 11, detected 15.2 on host) or `clang++`.
* **Testing & Quality**: GoogleTest / custom test harness, `clang-format`, and CodeRabbit CI review standards.

---

## 4. Key Functional Features (The Hybrid Matrix)

### 4.1. Clean GNOME / Libadwaita Visual Shell (Nautilus Heritage)
* **AdwApplicationWindow & HeaderBar**: Seamless GNOME HIG integration, dynamic window sizing, rounded corners, responsive layout.
* **Modern Places Sidebar**: Standard XDG user directories (Home, Desktop, Documents, Downloads, Music, Pictures, Videos), Root filesystem, Trash, and auto-detected removable media / external mounts (`GVolumeMonitor`).
* **Dark / Light Mode**: Dynamic switching following system GNOME color scheme (`AdwStyleManager`).
* **Toast Overlays**: Non-intrusive notifications (`AdwToastOverlay`) for undoable actions, file copy progress, errors, and clipboard events.

### 4.2. Dual-Pane Split View (`F3` Dolphin Superpower)
* **Side-by-side Twin Panes**: Toggleable via `F3` key or HeaderBar button.
* **Active / Inactive Pane State**: Clear subtle visual indication (accent outline / header highlight) showing which pane has keyboard focus.
* **Independent or Synchronized Navigation**: Each pane maintains its own current directory, history stack, selection state, and sort options.
* **Cross-Pane Drag & Drop and Transfer**: Direct copy (`F5` / drag) and move (`F6` / shift-drag) between left and right panes.

### 4.3. Integrated Bottom Terminal (`F4` Dolphin Superpower)
* **Docked VTE Terminal**: Smoothly expandable/collapsible bottom drawer powered by `VteTerminal`.
* **Bidirectional Directory Sync**:
  * As user browses folders in the active pane, the terminal automatically updates its working directory (`cd <active_dir>`).
  * When user executes `cd` inside the terminal, `dfiles` intercepts directory change (via VTE OSC 7 notification) and navigates the active pane to match!
* **Keyboard Toggle**: Instant toggle with `F4` without losing shell state or process output.

### 4.4. Live Inspector & Metadata Sidebar (`F11` Dolphin Superpower)
* **Collapsible Right Sidebar**: Toggled via `F11` or HeaderBar action.
* **Instant File Metadata**: Name, exact size (bytes + human readable), MIME type, last modified/accessed timestamps, owner/group, and POSIX permissions (`rwxr-xr-x`).
* **Live Previews**: Large image thumbnails, text/code preview snippet, audio/video duration and dimensions.
* **Live Checksum Generator**: One-click / on-demand calculation of MD5, SHA-1, SHA-256 for verified downloads.

### 4.5. Fast Path Navigation & Instant Filter Bar
* **Smart Dual-Mode Pathbar**:
  * Default: Clickable breadcrumb pills (Nautilus style).
  * `Ctrl+L`: Switches to direct editable text entry with path auto-completion. `Escape` or `Enter` returns to breadcrumbs.
* **Instant Filter Bar (`Ctrl+I`)**:
  * Floating or header-docked real-time filter input.
  * Dynamically filters visible directory contents as the user types (substring or wildcard match) without reload delay.

### 4.6. Asynchronous File Operations Engine
* **Non-Blocking File I/O**: Copy, move, trash, permanent delete, and directory creation execute asynchronously with cancellable `GCancellable`.
* **Conflict Resolution**: Interactive prompt for Overwrite / Skip / Rename on filename collision.
* **Trash & Undo**: Full Freedesktop.org Trash specification compliance via `g_file_trash()`.

---

## 5. Architectural Module Boundaries

```
dfiles/
├── src/
│   ├── main.cpp                    # Application entry point, GApplication setup
│   ├── core/                       # Core signals, event bus, app configuration
│   │   ├── app.hpp/.cpp            # DFilesApp (GtkApplication / AdwApplication)
│   │   ├── config.hpp/.cpp         # User settings (split view state, terminal state)
│   │   └── types.hpp               # Shared types, structs, enums
│   ├── ui/                         # Top-level window and layout
│   │   ├── window.hpp/.cpp         # DFilesWindow (AdwApplicationWindow, toasts)
│   │   ├── headerbar.hpp/.cpp      # AdwHeaderBar, view switchers, global actions
│   │   └── sidebar.hpp/.cpp        # Places sidebar (XDG folders, drives, trash)
│   ├── views/                      # Main file browsing views
│   │   ├── split_view.hpp/.cpp     # F3 dual pane container (GtkPaned)
│   │   ├── pane.hpp/.cpp           # Single pane view (Pathbar + Filter + List/Grid)
│   │   ├── file_view.hpp/.cpp      # GtkColumnView (List) and GtkGridView (Grid)
│   │   ├── file_item.hpp/.cpp      # GObject wrapper for GFileInfo / file model
│   │   └── path_bar.hpp/.cpp       # Breadcrumbs + Ctrl+L raw text switcher
│   ├── terminal/                   # F4 embedded terminal
│   │   └── terminal_drawer.hpp/.cpp# VteTerminal wrapper, OSC 7 sync, drawer animation
│   ├── inspector/                  # F11 inspection sidebar
│   │   ├── inspector.hpp/.cpp      # Metadata panel, image preview, permissions
│   │   └── checksum.hpp/.cpp       # Background SHA256/MD5 computation
│   └── operations/                 # Background filesystem tasks
│       ├── file_ops.hpp/.cpp       # Async copy, move, trash, delete, rename
│       └── progress_toast.hpp/.cpp # Progress feedback and cancel handlers
├── data/
│   ├── org.gnome.dfiles.desktop    # XDG Desktop launcher
│   └── style.css                   # Custom Libadwaita accent styling
└── meson.build                     # Meson build definitions
```

---

## 6. Completion Promise & Actionable Tasks

### Milestone 1: Core Architecture & Build Foundation
- [x] Task 1.1: Setup Meson build system with C++20, GTK4, Libadwaita-1, and VTE GTK4 dependencies.
- [x] Task 1.2: Implement `DFilesApp` and main application lifecycle with Libadwaita theming.
- [x] Task 1.3: Build `DFilesWindow` containing `AdwToolbarView`, `AdwHeaderBar`, and `AdwToastOverlay`.
- [x] Task 1.4: Implement modern Places Sidebar with XDG locations, root, trash, and drive monitor.

### Milestone 2: File Browser & Dual-Pane Engine (`F3`)
- [x] Task 2.1: Implement `FileItem` model wrapping `GFileInfo` with sorting, icons, and metadata.
- [x] Task 2.2: Implement `FileView` supporting List View (`GtkColumnView`) and Grid View (`GtkGridView`).
- [x] Task 2.3: Build `PaneView` with navigation history (Back/Forward), selection, and folder scanning.
- [x] Task 2.4: Build `SplitView` (`F3`): side-by-side twin panes with active pane focus tracking and sync.
- [ ] Task 2.5: Build `PathBar` with clickable breadcrumb pills and `Ctrl+L` direct edit switcher.
- [ ] Task 2.6: Implement inline instant Filter Bar (`Ctrl+I`).

### Milestone 3: Integrated Terminal (`F4`) & Live Inspector (`F11`)
- [x] Task 3.1: Build `TerminalDrawer` wrapping `VteTerminal` with smooth collapse/expand (`F4`).
- [x] Task 3.2: Implement terminal directory auto-sync: pane navigation triggers terminal `cd`.
- [x] Task 3.3: Implement terminal OSC 7 parsing: terminal `cd` triggers pane navigation.
- [x] Task 3.4: Build `InspectorSidebar` (`F11`) with metadata, permissions, and preview thumbnail.
- [x] Task 3.5: Add background checksum generation (MD5, SHA-256) inside inspector.

### Milestone 4: Operations, Polish, and Quality Gates
- [x] Task 4.1: Implement async file operations (Copy, Cut, Paste, Trash, Permanent Delete, New Folder).
- [x] Task 4.2: Add cross-pane drag-and-drop and keyboard shortcuts (`F5` Copy, `F6` Move).
- [x] Task 4.3: Add comprehensive context menus (Open With, Open in Terminal, Copy Path, Properties).
- [x] Task 4.4: Verify complete build, run headless/unit tests, and run CodeRabbit review compliance.
