#pragma once

#include "core/types.hpp"
#include "model/directory_model.hpp"
#include "model/file_item.hpp"
#include "views/file_view.hpp"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dfiles::views {

/**
 * @brief An autonomous directory browsing pane.
 *
 * Encapsulates DirectoryModel, FileView, navigation bar (Back/Forward/Up/Path/ViewMode/Hidden),
 * loading spinner, empty/error state handling, and status bar.
 */
class PaneView {
public:
    PaneView();
    ~PaneView();

    PaneView(const PaneView&) = delete;
    PaneView& operator=(const PaneView&) = delete;
    PaneView(PaneView&&) noexcept = default;
    PaneView& operator=(PaneView&&) noexcept = default;

    [[nodiscard]] GtkWidget* getWidget() const noexcept;
    [[nodiscard]] GFile* getCurrentDirectory() const noexcept;
    [[nodiscard]] model::DirectoryModel* getModel() const noexcept;
    [[nodiscard]] FileView* getFileView() const noexcept;

    // Navigation engine
    void navigateTo(GFile* dir);
    void navigateUp();
    void goBack();
    void goForward();
    void reload();

    // Pane activation state
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept { return m_isActive; }
    void grabFocus();

    // Selection
    [[nodiscard]] std::vector<model::FileItem> getSelectedItems() const;
    /// Return the list of selected GFiles (transfer full - caller must unref each GFile*).
    [[nodiscard]] std::vector<GFile*> getSelectedFiles() const;

    // Context Menu
    void showContextMenu();
    void showItemContextMenu();

    // Signals
    std::function<void(GFile* newDir)> onDirectoryChanged;
    std::function<void(PaneView* pane)> onActivated;
    std::function<void(DfilesFileItem* file)> onFileActivated;
    std::function<void(const std::vector<model::FileItem>& items)> onSelectionChanged;

private:
    void setupUI();
    void setupNavBar();
    void setupCenterArea();
    void setupStatusBar();
    void setupSignals();
    void updateNavButtons();
    void updateStatusLabel(size_t totalItems, size_t dirCount, size_t fileCount, uint64_t totalSize);
    void updateSelectionStatus(const std::vector<model::FileItem>& selected);

    std::unique_ptr<model::DirectoryModel> m_model;
    std::unique_ptr<FileView> m_fileView;

    // UI Widgets
    GtkWidget* m_rootBox{nullptr};
    GtkWidget* m_navBar{nullptr};
    GtkWidget* m_backBtn{nullptr};
    GtkWidget* m_forwardBtn{nullptr};
    GtkWidget* m_upBtn{nullptr};
    GtkWidget* m_pathLabel{nullptr};
    GtkWidget* m_viewModeBtn{nullptr};
    GtkWidget* m_hiddenBtn{nullptr};

    GtkWidget* m_centerOverlay{nullptr};
    GtkWidget* m_spinner{nullptr};
    GtkWidget* m_statusPage{nullptr};

    GtkWidget* m_statusBar{nullptr};
    GtkWidget* m_statusLabel{nullptr};

    // History and state
    GFile* m_currentDir{nullptr};
    std::vector<GFile*> m_backStack;
    std::vector<GFile*> m_forwardStack;
    bool m_isActive{false};
};

} // namespace dfiles::views
