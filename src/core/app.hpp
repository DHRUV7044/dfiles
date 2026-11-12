#pragma once

#include "core/types.hpp"

#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <memory>
#include <string>

// Forward declaration of UI window class
namespace dfiles::ui {
class DFilesWindow;
} // namespace dfiles::ui

namespace dfiles::core {

/**
 * @brief Main application controller encapsulating AdwApplication.
 *
 * Manages application lifecycle, command-line arguments (including --test-mode
 * and --version), global GActions, accelerator bindings, and window presentation.
 */
class DFilesApp {
public:
    explicit DFilesApp(const std::string& appId = "org.gnome.dfiles");
    ~DFilesApp();

    // Prevent copies and moves to maintain strict RAII ownership of AdwApplication
    DFilesApp(const DFilesApp&) = delete;
    DFilesApp& operator=(const DFilesApp&) = delete;
    DFilesApp(DFilesApp&&) = delete;
    DFilesApp& operator=(DFilesApp&&) = delete;

    /**
     * @brief Run the GApplication event loop.
     * @param argc Argument count.
     * @param argv Argument vector.
     * @return Exit status code.
     */
    int run(int argc, char* argv[]);

    /**
     * @brief Returns true if application was started in automated test mode.
     */
    [[nodiscard]] bool isTestMode() const noexcept { return m_testMode; }

    /**
     * @brief Programmatically configure test mode.
     */
    void setTestMode(bool testMode) noexcept;

    /**
     * @brief Access the encapsulated AdwApplication handle.
     */
    [[nodiscard]] AdwApplication* getNative() const noexcept { return m_app; }

    /**
     * @brief Access the active window instance, if created.
     */
    [[nodiscard]] ui::DFilesWindow* getWindow() const noexcept { return m_window.get(); }

    // Lifecycle handlers
    void onStartup();
    void onActivate();
    void onOpen(GFile** files, gint nFiles, const gchar* hint);
    void onWindowDestroyed();

    // Global action handlers
    void onActionQuit();
    void onActionPreferences();
    void onActionAbout();

private:
    void setupActions();
    void setupAccelerators();
    gint handleLocalOptions(GVariantDict* options);

    // GObject signal trampolines
    static void onStartupTrampoline(GApplication* app, gpointer userData);
    static void onActivateTrampoline(GApplication* app, gpointer userData);
    static void onOpenTrampoline(GApplication* app, GFile** files, gint nFiles, const gchar* hint, gpointer userData);
    static gint onHandleLocalOptionsTrampoline(GApplication* app, GVariantDict* options, gpointer userData);

    AdwApplication* m_app{nullptr};
    std::unique_ptr<ui::DFilesWindow> m_window;
    bool m_testMode{false};
    bool m_startupDone{false};

    // Active dialog tracking to prevent duplicate dialog stacking (REMED-5)
    AdwPreferencesWindow* m_activePrefsWindow{nullptr};
    AdwAboutWindow* m_activeAboutWindow{nullptr};
#if ADW_CHECK_VERSION(1, 5, 0)
    AdwDialog* m_activePrefsDialog{nullptr};
    AdwDialog* m_activeAboutDialog{nullptr};
#endif

    gulong m_handleLocalOptionsId{0};
    gulong m_startupId{0};
    gulong m_activateId{0};
    gulong m_openId{0};
    guint m_testQuitIdleId{0};
};

} // namespace dfiles::core
