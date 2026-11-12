# Technical Architecture & Interface Contracts: dfiles

This document serves as the binding contract for all subagents working concurrently on `dfiles`. All code written in C++ must adhere to the namespaces, conventions, and interface designs detailed below.

---

## 1. System Namespaces & Coding Standards
* **Namespace**: All classes and structs must reside in `namespace dfiles` or a sub-namespace (e.g., `dfiles::ui`, `dfiles::model`, `dfiles::ops`).
* **C++ Standard**: C++20 (`-std=c++20`).
* **Memory Management**: Use RAII and smart pointers (`std::unique_ptr`, `std::shared_ptr`) for C++ objects; use `g_autoptr` / `g_object_unref` or standard GObject wrappers for GLib/GTK handles.
* **Naming Conventions**:
  * Classes / Structs: `PascalCase` (e.g., `PaneView`, `TerminalDrawer`, `FileItem`)
  * Member Variables: `m_camelCase` or `camelCase_` (e.g., `m_currentDirectory`, `m_isActive`)
  * Signals / Callbacks: `onActionName` or `handleActionName`

---

## 2. Directory & Component Layout

```
src/
├── main.cpp                        # GApplication startup
├── core/
│   ├── app.hpp/.cpp                # class DFilesApp : AdwApplication
│   ├── types.hpp                   # Enums: ViewMode (List/Grid), SortColumn, SortOrder
│   └── settings.hpp/.cpp           # Persistent user preferences (GSettings / JSON)
├── ui/
│   ├── window.hpp/.cpp             # class DFilesWindow : AdwApplicationWindow
│   ├── headerbar.hpp/.cpp          # class DFilesHeaderBar : AdwHeaderBar
│   └── sidebar.hpp/.cpp            # class PlacesSidebar : GtkBox (places & mounts)
├── views/
│   ├── split_view.hpp/.cpp         # class SplitView : GtkPaned (Hosts Left/Right PaneView)
│   ├── pane_view.hpp/.cpp          # class PaneView : GtkBox (PathBar + Filter + FileView)
│   ├── file_view.hpp/.cpp          # class FileView : GtkBox (GtkColumnView / GtkGridView)
│   ├── path_bar.hpp/.cpp           # class PathBar : GtkBox (Breadcrumbs + Text Entry)
│   └── filter_bar.hpp/.cpp         # class FilterBar : GtkSearchBar
├── terminal/
│   └── terminal_drawer.hpp/.cpp    # class TerminalDrawer : GtkBox (VteTerminal + Controls)
├── inspector/
│   ├── inspector_sidebar.hpp/.cpp  # class InspectorSidebar : GtkBox (Metadata + Preview)
│   └── checksum_helper.hpp/.cpp    # class ChecksumHelper (Async MD5/SHA-256 worker)
├── model/
│   ├── file_item.hpp/.cpp          # GObject FileItem wrapping GFileInfo
│   └── directory_model.hpp/.cpp    # GListModel provider with sorting and filtering
└── operations/
    ├── file_ops.hpp/.cpp           # class FileOperations (Async copy, move, trash, delete)
    └── transfer_dialog.hpp/.cpp    # Overwrite/skip collision resolution
```

---

## 3. Core Class Specifications & Contracts

### 3.1. `FileItem` (`src/model/file_item.hpp`)
Represents an individual file/folder entry.
```cpp
namespace dfiles::model {

class FileItem {
public:
    explicit FileItem(GFile* file, GFileInfo* info);
    ~FileItem();

    [[nodiscard]] std::string getName() const;
    [[nodiscard]] std::string getPath() const;
    [[nodiscard]] std::string getMimeType() const;
    [[nodiscard]] uint64_t getSize() const;
    [[nodiscard]] std::string getFormattedSize() const;
    [[nodiscard]] int64_t getModifiedTime() const;
    [[nodiscard]] uint32_t getUnixMode() const;
    [[nodiscard]] bool isDirectory() const;
    [[nodiscard]] bool isHidden() const;
    [[nodiscard]] bool isSymlink() const;
    [[nodiscard]] GIcon* getIcon() const;
    [[nodiscard]] GFile* getGFile() const;
};

} // namespace dfiles::model
```

### 3.2. `PaneView` (`src/views/pane_view.hpp`)
Encapsulates a single directory browsing pane (left or right).
```cpp
namespace dfiles::views {

class PaneView {
public:
    explicit PaneView();
    GtkWidget* getWidget() const;

    // Navigation
    void navigateTo(GFile* directory);
    void navigateUp();
    void goBack();
    void goForward();
    void reload();

    // State
    [[nodiscard]] GFile* getCurrentDirectory() const;
    [[nodiscard]] bool isActive() const;
    void setActive(bool active);

    // Selection
    [[nodiscard]] std::vector<std::shared_ptr<model::FileItem>> getSelectedItems() const;

    // Signals / Callbacks
    std::function<void(GFile* newDir)> onDirectoryChanged;
    std::function<void(const std::vector<std::shared_ptr<model::FileItem>>&)> onSelectionChanged;
    std::function<void(PaneView* pane)> onActivated;
};

} // namespace dfiles::views
```

### 3.3. `SplitView` (`src/views/split_view.hpp`)
Coordinates the primary and secondary panes (`F3`).
```cpp
namespace dfiles::views {

class SplitView {
public:
    explicit SplitView();
    GtkWidget* getWidget() const;

    void toggleSplit();
    [[nodiscard]] bool isSplit() const;

    [[nodiscard]] PaneView* getActivePane() const;
    [[nodiscard]] PaneView* getInactivePane() const;

    // Shortcuts: F5 (Copy active -> inactive), F6 (Move active -> inactive)
    void transferSelection(bool isMove);

    std::function<void(PaneView* activePane)> onActivePaneChanged;
};

} // namespace dfiles::views
```

### 3.4. `TerminalDrawer` (`src/terminal/terminal_drawer.hpp`)
Handles the embedded VTE terminal (`F4`).
```cpp
namespace dfiles::terminal {

class TerminalDrawer {
public:
    explicit TerminalDrawer();
    GtkWidget* getWidget() const;

    void toggleVisibility();
    [[nodiscard]] bool isVisible() const;

    // Directory Sync
    void syncDirectory(GFile* directory); // Called when active pane directory changes
    
    // OSC 7 callback from VTE when shell executes 'cd'
    std::function<void(const std::string& uri)> onShellDirectoryChanged;
};

} // namespace dfiles::terminal
```

### 3.5. `InspectorSidebar` (`src/inspector/inspector_sidebar.hpp`)
Displays live file inspection details (`F11`).
```cpp
namespace dfiles::inspector {

class InspectorSidebar {
public:
    explicit InspectorSidebar();
    GtkWidget* getWidget() const;

    void toggleVisibility();
    [[nodiscard]] bool isVisible() const;

    // Updates whenever the active pane selection changes
    void inspectItems(const std::vector<std::shared_ptr<model::FileItem>>& items);
};

} // namespace dfiles::inspector
```

---

## 4. Communication & Event Flow

```
                      ┌────────────────────────────────────────┐
                      │              DFilesWindow              │
                      └────┬──────────────┬──────────────┬─────┘
                           │              │              │
                           ▼              ▼              ▼
                    ┌──────────────┐ ┌──────────┐ ┌───────────────┐
                    │  SplitView   │ │ Terminal │ │   Inspector   │
                    │   PaneView   │ │  Drawer  │ │    Sidebar    │
                    └──────┬───────┘ └────┬─────┘ └───────┬───────┘
                           │              │               │
  User clicks folder       │              │               │
  ─────────────────────────┘              │               │
      onDirectoryChanged(GFile*)          │               │
      ───────────────────────────────────►│               │
                               syncDirectory (updates cd) │
                                                          │
  User selects a file                                     │
  ────────────────────────────────────────────────────────┘
      onSelectionChanged(vector<FileItem>)
      ───────────────────────────────────────────────────►│
                                           inspectItems (renders preview & stats)
```

---

## 5. Build Requirements & Compiler Flags
* **Meson Options**:
  ```meson
  project('dfiles', ['cpp', 'c'],
    version: '0.1.0',
    default_options: [
      'cpp_std=c++20',
      'warning_level=3',
      'werror=false'
    ]
  )
  ```
* **Required `pkg-config` dependencies**:
  * `gtk4` (>= 4.12)
  * `libadwaita-1` (>= 1.4)
  * `vte-2.91-gtk4` (>= 0.74)
  * `gio-2.0` (>= 2.74)
  * `gio-unix-2.0`
