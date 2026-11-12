# Project Charter: dfiles (C++ GTK4/Libadwaita File Manager)

## 1. Vision & Purpose
`dfiles` bridges the gap between two Linux file management philosophies:
1. **GNOME Nautilus**: Known for clean, uncluttered visual design, adherence to GNOME Human Interface Guidelines (HIG), modern Libadwaita styling, and adaptive layout.
2. **KDE Dolphin**: Renowned among developers and power users for dual-pane navigation (`F3`), integrated terminal (`F4`), metadata inspector (`F11`), inline filtering (`Ctrl+I`), and seamless cross-pane transfers.

`dfiles` delivers the best of both worlds: a pure C++ application built natively for GNOME/Libadwaita with Dolphin's power-user workflow built directly into the core.

---

## 2. Core Architectural Principles
* **Pure Native Performance**: Written in Modern C++ (C++20) to guarantee sub-millisecond responsiveness, instant directory rendering, and minimal memory overhead.
* **Strict GNOME HIG & Libadwaita**: Every widget, dialog, and styling adheres to GNOME design standards. No awkward Qt-to-GTK translation layers.
* **Asynchronous Non-Blocking Core**: UI operations remain locked at 60 FPS. Background scanning, file operations, checksums, and metadata extraction execute off the GTK main loop.
* **Modular Multi-Agent Decoupling**: Clean internal interfaces and signals allow parallel agent implementation of UI views, VTE terminal, file system operations, and inspector components.

---

## 3. Milestones & High-Level Plan
- [ ] **Milestone 1**: Foundation & Window Shell (Meson, C++20, GTK4, Libadwaita, Places Sidebar)
- [ ] **Milestone 2**: File Views & Dual-Pane Engine (`F3` split view, list/grid, history)
- [ ] **Milestone 3**: Power Features (`F4` VTE terminal with bidirectional sync, `F11` live inspector)
- [ ] **Milestone 4**: Navigation & Operations (`Ctrl+L` pathbar, `Ctrl+I` filter, async file ops, `F5`/`F6` cross-pane transfer)
- [ ] **Milestone 5**: Polish, Shortcuts, System Integration & CodeRabbit Gate
