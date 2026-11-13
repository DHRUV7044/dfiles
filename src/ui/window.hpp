#pragma once

#include <adwaita.h>
#include <gio/gio.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "inspector/inspector_sidebar.hpp"
#include "operations/file_ops.hpp"
#include "terminal/terminal_drawer.hpp"
#include "ui/headerbar.hpp"
#include "ui/sidebar.hpp"
#include "views/split_view.hpp"

namespace dfiles::ui {

/**
 * @brief Main application window for dfiles.
 *
 * Encapsulates AdwApplicationWindow, AdwToolbarView, AdwToastOverlay,
 * and the primary horizontal GtkPaned containing the PlacesSidebar and main content area.
 */
class DFilesWindow {
public:
    explicit DFilesWindow(AdwApplication* app);
    ~DFilesWindow();

    DFilesWindow(const DFilesWindow&) = delete;
    DFilesWindow& operator=(const DFilesWindow&) = delete;
    DFilesWindow(DFilesWindow&&) noexcept = default;
    DFilesWindow& operator=(DFilesWindow&&) noexcept = default;

    [[nodiscard]] GtkWidget* getWidget() const;
    [[nodiscard]] AdwApplicationWindow* getWindow() const;

    void present();
    void showToast(const std::string& message, uint32_t timeoutSeconds = 3);

    [[nodiscard]] DFilesHeaderBar* getHeaderBar() const;
    [[nodiscard]] PlacesSidebar* getSidebar() const;
    [[nodiscard]] views::SplitView* getSplitView() const noexcept;
    [[nodiscard]] terminal::TerminalDrawer* getTerminalDrawer() const noexcept;
    [[nodiscard]] inspector::InspectorSidebar* getInspectorSidebar() const noexcept;
    [[nodiscard]] std::shared_ptr<operations::FileOperations> getFileOperations() const noexcept { return m_fileOps; }
    [[nodiscard]] GtkWidget* getContentArea() const;

    void promptTextInput(
        const std::string& heading,
        const std::string& defaultText,
        const std::string& confirmBtnText,
        std::function<void(const std::string& text)> onConfirm
    );

private:
    void setupUI(AdwApplication* app);
    void setupSignals();
    void setupActions();
    void setupShortcuts();

    GtkWidget* m_window{nullptr};
    GtkWidget* m_toolbarView{nullptr};
    GtkWidget* m_toastOverlay{nullptr};
    GtkWidget* m_mainPaned{nullptr};
    GtkWidget* m_contentArea{nullptr};
    GtkWidget* m_contentVerticalPaned{nullptr};
    GtkWidget* m_workspaceHorizontalPaned{nullptr};

    std::unique_ptr<DFilesHeaderBar> m_headerBar;
    std::unique_ptr<PlacesSidebar> m_sidebar;
    std::unique_ptr<views::SplitView> m_splitView;
    std::unique_ptr<terminal::TerminalDrawer> m_terminalDrawer;
    std::unique_ptr<inspector::InspectorSidebar> m_inspectorSidebar;
    std::shared_ptr<operations::FileOperations> m_fileOps;

    bool m_syncingFromTerminal{false};

    // REMED-1: Off-thread toast thread safety & pending source lifecycle tracking
    std::mutex m_toastMutex;
    std::vector<guint> m_pendingToastSources;
    bool m_isDestroyed{false};
    std::shared_ptr<bool> m_aliveToken;

    GtkEventController* m_keyController{nullptr};
    gulong m_keyPressedId{0};
};

} // namespace dfiles::ui
