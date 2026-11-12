#pragma once

#include <adwaita.h>
#include <gio/gio.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ui/headerbar.hpp"
#include "ui/sidebar.hpp"

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
    [[nodiscard]] GtkWidget* getContentArea() const;

private:
    void setupUI(AdwApplication* app);
    void setupSignals();
    void setupActions();

    GtkWidget* m_window{nullptr};
    GtkWidget* m_toolbarView{nullptr};
    GtkWidget* m_toastOverlay{nullptr};
    GtkWidget* m_mainPaned{nullptr};
    GtkWidget* m_contentArea{nullptr};

    std::unique_ptr<DFilesHeaderBar> m_headerBar;
    std::unique_ptr<PlacesSidebar> m_sidebar;

    // REMED-1: Off-thread toast thread safety & pending source lifecycle tracking
    std::mutex m_toastMutex;
    std::vector<guint> m_pendingToastSources;
    bool m_isDestroyed{false};
};

} // namespace dfiles::ui
