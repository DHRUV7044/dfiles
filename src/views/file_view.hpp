#pragma once

#include "core/types.hpp"
#include "model/directory_model.hpp"
#include "model/file_item.hpp"

#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <vector>

namespace dfiles::views {

/**
 * @brief High-performance dual-mode directory content renderer.
 *
 * Supports both GtkColumnView (List View with Name, Size, Date, Permissions)
 * and GtkGridView (Grid View with large icons and file labels).
 * Uses GtkMultiSelection and GtkSignalListItemFactory with strict lifecycle cleanup.
 */
class FileView {
public:
    FileView();
    ~FileView();

    FileView(const FileView&) = delete;
    FileView& operator=(const FileView&) = delete;
    FileView(FileView&&) noexcept = default;
    FileView& operator=(FileView&&) noexcept = default;

    [[nodiscard]] GtkWidget* getWidget() const noexcept;

    void setModel(model::DirectoryModel* model);
    [[nodiscard]] model::DirectoryModel* getModel() const noexcept { return m_model; }

    void setViewMode(core::ViewMode mode);
    [[nodiscard]] core::ViewMode getViewMode() const noexcept { return m_mode; }

    void grabFocus();

    [[nodiscard]] std::vector<model::FileItem> getSelectedItems() const;
    void selectAll();
    void unselectAll();

    [[nodiscard]] GtkWidget* getItemPopover() const noexcept { return m_itemPopover; }
    [[nodiscard]] GtkWidget* getBackgroundPopover() const noexcept { return m_bgPopover; }

    void showContextMenu();
    void showItemContextMenu();

    // Callbacks
    std::function<void(DfilesFileItem* item)> onItemActivated;
    std::function<void(const std::vector<model::FileItem>& items)> onSelectionChanged;

private:
    void setupUI();
    void setupColumnView();
    void setupGridView();
    void setupContextMenus();
    void onRightClick(double x, double y);

    // Signal trampolines
    static void onItemActivatedTrampoline(GtkWidget* widget, guint position, gpointer userData);
    static void onSelectionChangedTrampoline(GtkSelectionModel* model, guint position, guint nItems, gpointer userData);
    static void onSorterChangedTrampoline(GtkSorter* sorter, int change, gpointer userData);

    GtkWidget* m_scrolledWindow{nullptr};
    GtkWidget* m_columnView{nullptr};
    GtkWidget* m_gridView{nullptr};

    GtkWidget* m_itemPopover{nullptr};
    GtkWidget* m_bgPopover{nullptr};
    GtkGesture* m_rightClickGesture{nullptr};
    gulong m_rightClickPressedId{0};

    GtkColumnViewColumn* m_colName{nullptr};
    GtkColumnViewColumn* m_colSize{nullptr};
    GtkColumnViewColumn* m_colModified{nullptr};
    GtkColumnViewColumn* m_colPermissions{nullptr};

    model::DirectoryModel* m_model{nullptr};
    GtkMultiSelection* m_selectionModel{nullptr};
    gulong m_selectionChangedHandlerId{0};
    gulong m_colActivateId{0};
    gulong m_gridActivateId{0};
    gulong m_sorterChangedId{0};

    core::ViewMode m_mode{core::ViewMode::List};
};

} // namespace dfiles::views
