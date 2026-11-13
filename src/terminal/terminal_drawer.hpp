#pragma once

#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include <functional>
#include <memory>
#include <string>

namespace dfiles::terminal {

/**
 * @brief Collapsible embedded VTE terminal drawer widget.
 *
 * Provides an integrated terminal drawer at the bottom of the window (toggled via F4),
 * featuring bidirectional directory synchronization with the active file browser pane,
 * shell process lifetime management (preventing zombie processes), and dynamic Adwaita
 * dark/light theme adaptation.
 */
class TerminalDrawer {
public:
    explicit TerminalDrawer();
    ~TerminalDrawer();

    TerminalDrawer(const TerminalDrawer&) = delete;
    TerminalDrawer& operator=(const TerminalDrawer&) = delete;
    TerminalDrawer(TerminalDrawer&&) = delete;
    TerminalDrawer& operator=(TerminalDrawer&&) = delete;

    /**
     * @brief Access the root widget container (vertical GtkBox) for embedding in window layouts.
     */
    [[nodiscard]] GtkWidget* getWidget() const noexcept { return m_rootBox; }

    /**
     * @brief Access the underlying VteTerminal widget.
     */
    [[nodiscard]] VteTerminal* getTerminal() const noexcept { return m_terminal; }

    /**
     * @brief Toggle terminal drawer visibility.
     */
    void toggleVisibility();

    /**
     * @brief Returns true if the terminal drawer is currently visible.
     */
    [[nodiscard]] bool isVisible() const;

    /**
     * @brief Explicitly show or hide the terminal drawer.
     */
    void setVisible(bool visible);

    /**
     * @brief Focus the VTE terminal keyboard input.
     */
    void grabFocus();

    /**
     * @brief Sends a cd command to the child shell to match the active pane directory.
     * @param directory The GFile representing the target directory.
     */
    void syncDirectory(GFile* directory);

    /**
     * @brief Resets terminal state and clears scrollback buffer history.
     */
    void clear();

    /**
     * @brief Callback invoked when shell emits OSC 7 current directory notification.
     */
    std::function<void(const std::string& uri)> onShellDirectoryChanged;

    /**
     * @brief Callback invoked when user clicks the close button in drawer header.
     */
    std::function<void()> onClosed;

    /**
     * @brief Restarts the terminal shell process if exited or terminated.
     */
    void restartShell();

private:
    void setupUI();
    void spawnShell();
    void updateTheme(bool isDark);

    static void onSpawnAsyncCallback(VteTerminal* terminal, GPid pid, GError* error, gpointer userData);
    static void onChildExitedCallback(VteTerminal* terminal, int status, gpointer userData);
    static void onCurrentDirectoryUriChangedCallback(VteTerminal* terminal, gpointer userData);

    GtkWidget* m_rootBox{nullptr};
    GtkWidget* m_headerBox{nullptr};
    VteTerminal* m_terminal{nullptr};

    GPid m_childPid{0};
    gulong m_styleNotifyId{0};
    gulong m_dirUriNotifyId{0};
    gulong m_childExitedId{0};

    GtkWidget* m_restartButton{nullptr};
    GtkWidget* m_clearButton{nullptr};
    GtkWidget* m_closeButton{nullptr};
    gulong m_restartBtnSignalId{0};
    gulong m_clearBtnSignalId{0};
    gulong m_closeBtnSignalId{0};

    GCancellable* m_spawnCancellable{nullptr};
    std::string m_lastSyncedUri;
    std::shared_ptr<bool> m_aliveToken;
};

} // namespace dfiles::terminal
