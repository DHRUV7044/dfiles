#include "terminal/terminal_drawer.hpp"

#include <cstring>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

namespace dfiles::terminal {

namespace {
struct SpawnContext {
    TerminalDrawer* self{nullptr};
    std::weak_ptr<bool> aliveToken;
};
} // namespace

TerminalDrawer::TerminalDrawer()
    : m_aliveToken(std::make_shared<bool>(true)) {
    const char* home = g_get_home_dir();
    if (home) {
        g_autoptr(GFile) homeFile = g_file_new_for_path(home);
        if (homeFile) {
            g_autofree char* homeUri = g_file_get_uri(homeFile);
            if (homeUri) {
                m_lastSyncedUri = homeUri;
            }
        }
    }

    setupUI();
    spawnShell();

    AdwStyleManager* styleManager = adw_style_manager_get_default();
    updateTheme(adw_style_manager_get_dark(styleManager));

    m_styleNotifyId = g_signal_connect_swapped(
        styleManager,
        "notify::dark",
        G_CALLBACK(+[](TerminalDrawer* self) {
            if (self && self->m_aliveToken && *self->m_aliveToken) {
                AdwStyleManager* sm = adw_style_manager_get_default();
                self->updateTheme(adw_style_manager_get_dark(sm));
            }
        }),
        this
    );
}

TerminalDrawer::~TerminalDrawer() {
    onClosed = nullptr;

    if (m_styleNotifyId > 0) {
        AdwStyleManager* sm = adw_style_manager_get_default();
        if (sm && g_signal_handler_is_connected(sm, m_styleNotifyId)) {
            g_signal_handler_disconnect(sm, m_styleNotifyId);
        }
        m_styleNotifyId = 0;
    }

    if (m_aliveToken) {
        *m_aliveToken = false;
        m_aliveToken.reset();
    }

    if (m_spawnCancellable) {
        g_cancellable_cancel(m_spawnCancellable);
        g_clear_object(&m_spawnCancellable);
    }

    if (m_dirUriNotifyId > 0 && m_terminal) {
        if (g_signal_handler_is_connected(m_terminal, m_dirUriNotifyId)) {
            g_signal_handler_disconnect(m_terminal, m_dirUriNotifyId);
        }
        m_dirUriNotifyId = 0;
    }

    if (m_childExitedId > 0 && m_terminal) {
        if (g_signal_handler_is_connected(m_terminal, m_childExitedId)) {
            g_signal_handler_disconnect(m_terminal, m_childExitedId);
        }
        m_childExitedId = 0;
    }

    if (m_restartBtnSignalId > 0 && m_restartButton) {
        if (g_signal_handler_is_connected(m_restartButton, m_restartBtnSignalId)) {
            g_signal_handler_disconnect(m_restartButton, m_restartBtnSignalId);
        }
        m_restartBtnSignalId = 0;
    }

    if (m_clearBtnSignalId > 0 && m_clearButton) {
        if (g_signal_handler_is_connected(m_clearButton, m_clearBtnSignalId)) {
            g_signal_handler_disconnect(m_clearButton, m_clearBtnSignalId);
        }
        m_clearBtnSignalId = 0;
    }

    if (m_closeBtnSignalId > 0 && m_closeButton) {
        if (g_signal_handler_is_connected(m_closeButton, m_closeBtnSignalId)) {
            g_signal_handler_disconnect(m_closeButton, m_closeBtnSignalId);
        }
        m_closeBtnSignalId = 0;
    }

    if (m_childPid > 0) {
        kill(m_childPid, SIGHUP);
        kill(m_childPid, SIGTERM);
        g_spawn_close_pid(m_childPid);
        m_childPid = 0;
    }
}

void TerminalDrawer::setupUI() {
    m_rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(m_rootBox, -1, 180);
    gtk_widget_set_visible(m_rootBox, FALSE);

    // 1. Header toolbar
    m_headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(m_headerBox, "view");
    gtk_widget_set_margin_start(m_headerBox, 8);
    gtk_widget_set_margin_end(m_headerBox, 8);
    gtk_widget_set_margin_top(m_headerBox, 4);
    gtk_widget_set_margin_bottom(m_headerBox, 4);

    GtkWidget* titleLabel = gtk_label_new("Terminal");
    gtk_widget_add_css_class(titleLabel, "heading");
    gtk_widget_set_hexpand(titleLabel, TRUE);
    gtk_label_set_xalign(GTK_LABEL(titleLabel), 0.0f);
    gtk_box_append(GTK_BOX(m_headerBox), titleLabel);

    m_restartButton = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(m_restartButton, "Restart Shell");
    gtk_widget_add_css_class(m_restartButton, "flat");
    gtk_widget_set_visible(m_restartButton, FALSE);
    m_restartBtnSignalId = g_signal_connect_swapped(
        m_restartButton,
        "clicked",
        G_CALLBACK(+[](TerminalDrawer* self) {
            if (self) {
                self->restartShell();
            }
        }),
        this
    );
    gtk_box_append(GTK_BOX(m_headerBox), m_restartButton);

    m_clearButton = gtk_button_new_from_icon_name("edit-clear-symbolic");
    gtk_widget_set_tooltip_text(m_clearButton, "Clear terminal");
    gtk_widget_add_css_class(m_clearButton, "flat");
    m_clearBtnSignalId = g_signal_connect_swapped(
        m_clearButton,
        "clicked",
        G_CALLBACK(+[](TerminalDrawer* self) {
            if (self) {
                self->clear();
            }
        }),
        this
    );
    gtk_box_append(GTK_BOX(m_headerBox), m_clearButton);

    m_closeButton = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(m_closeButton, "Hide terminal (F4)");
    gtk_widget_add_css_class(m_closeButton, "flat");
    m_closeBtnSignalId = g_signal_connect_swapped(
        m_closeButton,
        "clicked",
        G_CALLBACK(+[](TerminalDrawer* self) {
            if (self) {
                self->setVisible(false);
                if (self->onClosed) {
                    self->onClosed();
                }
            }
        }),
        this
    );
    gtk_box_append(GTK_BOX(m_headerBox), m_closeButton);

    gtk_box_append(GTK_BOX(m_rootBox), m_headerBox);

    // 2. VTE Terminal inside scrolled frame
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), TRUE);

    m_terminal = VTE_TERMINAL(vte_terminal_new());
    gtk_widget_set_hexpand(GTK_WIDGET(m_terminal), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(m_terminal), TRUE);
    vte_terminal_set_scrollback_lines(m_terminal, 10000);
    vte_terminal_set_mouse_autohide(m_terminal, TRUE);

    m_dirUriNotifyId = g_signal_connect(
        m_terminal,
        "current-directory-uri-changed",
        G_CALLBACK(onCurrentDirectoryUriChangedCallback),
        this
    );

    m_childExitedId = g_signal_connect(
        m_terminal,
        "child-exited",
        G_CALLBACK(onChildExitedCallback),
        this
    );

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(m_terminal));
    gtk_box_append(GTK_BOX(m_rootBox), scrolled);
}

void TerminalDrawer::spawnShell() {
    const char* envShell = g_getenv("SHELL");
    char* userShell = nullptr;
    if (!envShell || envShell[0] == '\0') {
        userShell = vte_get_user_shell();
    }
    const char* shellToRun = (envShell && envShell[0] != '\0') ? envShell :
                             (userShell ? userShell : "/bin/bash");

    char* argv[2] = { const_cast<char*>(shellToRun), nullptr };

    m_spawnCancellable = g_cancellable_new();
    auto* ctx = new SpawnContext{this, m_aliveToken};
    vte_terminal_spawn_async(
        m_terminal,
        VTE_PTY_DEFAULT,
        g_get_home_dir(),
        argv,
        nullptr,
        G_SPAWN_DEFAULT,
        nullptr,
        nullptr,
        nullptr,
        -1,
        m_spawnCancellable,
        onSpawnAsyncCallback,
        ctx
    );

    if (userShell) {
        g_free(userShell);
    }
}

void TerminalDrawer::onSpawnAsyncCallback(VteTerminal* /*terminal*/, GPid pid, GError* error, gpointer userData) {
    auto* ctx = static_cast<SpawnContext*>(userData);
    if (!ctx) {
        if (pid > 0) {
            kill(pid, SIGHUP);
            kill(pid, SIGTERM);
            g_spawn_close_pid(pid);
        }
        return;
    }

    auto token = ctx->aliveToken.lock();
    if (!token || !(*token)) {
        if (pid > 0) {
            kill(pid, SIGHUP);
            kill(pid, SIGTERM);
            g_spawn_close_pid(pid);
        }
        delete ctx;
        return;
    }

    auto* self = ctx->self;
    delete ctx;

    if (self->m_spawnCancellable) {
        g_clear_object(&self->m_spawnCancellable);
    }

    if (error) {
        return;
    }

    self->m_childPid = pid;
    if (self->m_restartButton) {
        gtk_widget_set_visible(self->m_restartButton, FALSE);
    }
}

void TerminalDrawer::onChildExitedCallback(VteTerminal* /*terminal*/, int /*status*/, gpointer userData) {
    auto* self = static_cast<TerminalDrawer*>(userData);
    if (!self) {
        return;
    }

    if (self->m_childPid > 0) {
        g_spawn_close_pid(self->m_childPid);
        self->m_childPid = 0;
    }

    if (self->m_restartButton) {
        gtk_widget_set_visible(self->m_restartButton, TRUE);
    }
}

void TerminalDrawer::restartShell() {
    if (m_childPid > 0) {
        kill(m_childPid, SIGHUP);
        kill(m_childPid, SIGTERM);
        g_spawn_close_pid(m_childPid);
        m_childPid = 0;
    }
    if (m_terminal) {
        vte_terminal_reset(m_terminal, TRUE, TRUE);
    }
    if (m_restartButton) {
        gtk_widget_set_visible(m_restartButton, FALSE);
    }
    spawnShell();
}

void TerminalDrawer::onCurrentDirectoryUriChangedCallback(VteTerminal* terminal, gpointer userData) {
    auto* self = static_cast<TerminalDrawer*>(userData);
    if (!self || !self->m_aliveToken || !(*self->m_aliveToken)) {
        return;
    }

    std::string uriStr;
#if VTE_CHECK_VERSION(0, 78, 0)
    GUri* guri = vte_terminal_ref_termprop_uri(terminal, VTE_TERMPROP_CURRENT_DIRECTORY_URI);
    if (guri) {
        char* str = g_uri_to_string(guri);
        if (str) {
            uriStr = str;
            g_free(str);
        }
        g_uri_unref(guri);
    }
#else
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    const char* uri = vte_terminal_get_current_directory_uri(terminal);
    if (uri) {
        uriStr = uri;
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
#endif

    if (uriStr.empty() || uriStr.size() > 4096 || !g_str_has_prefix(uriStr.c_str(), "file://")) {
        return;
    }

    g_autofree char* hostname = nullptr;
    GError* convErr = nullptr;
    g_autofree char* localPath = g_filename_from_uri(uriStr.c_str(), &hostname, &convErr);
    if (convErr) {
        g_error_free(convErr);
        return;
    }
    if (hostname && *hostname && std::strcmp(hostname, "localhost") != 0) {
        return;
    }
    if (!localPath || !g_utf8_validate(localPath, -1, nullptr)) {
        return;
    }
    if (!g_file_test(localPath, G_FILE_TEST_IS_DIR)) {
        return;
    }

    g_autoptr(GFile) shellFile = g_file_new_for_path(localPath);
    if (!shellFile) {
        return;
    }
    g_autofree char* canonicalUri = g_file_get_uri(shellFile);
    if (!canonicalUri) {
        return;
    }
    std::string canonicalUriStr(canonicalUri);

    if (canonicalUriStr == self->m_lastSyncedUri) {
        return;
    }
    self->m_lastSyncedUri = canonicalUriStr;

    if (self->onShellDirectoryChanged) {
        self->onShellDirectoryChanged(canonicalUriStr);
    }
}

void TerminalDrawer::toggleVisibility() {
    setVisible(!isVisible());
}

bool TerminalDrawer::isVisible() const {
    return m_rootBox && gtk_widget_get_visible(m_rootBox);
}

void TerminalDrawer::setVisible(bool visible) {
    if (!m_rootBox) {
        return;
    }
    gtk_widget_set_visible(m_rootBox, visible);
    if (visible && m_terminal) {
        gtk_widget_grab_focus(GTK_WIDGET(m_terminal));
    }
}

void TerminalDrawer::grabFocus() {
    if (m_terminal) {
        gtk_widget_grab_focus(GTK_WIDGET(m_terminal));
    }
}

void TerminalDrawer::syncDirectory(GFile* directory) {
    if (!m_terminal || !directory || m_childPid <= 0) {
        return;
    }

    VtePty* pty = vte_terminal_get_pty(m_terminal);
    if (pty) {
        int ptyFd = vte_pty_get_fd(pty);
        if (ptyFd >= 0) {
            pid_t fgPgrp = tcgetpgrp(ptyFd);
            if (fgPgrp > 0 && fgPgrp != m_childPid) {
                // Foreground interactive program (vim, nano, python, etc.) running, do not inject cd!
                return;
            }
        }
    }

    char* uri = g_file_get_uri(directory);
    if (uri) {
        if (m_lastSyncedUri == uri) {
            g_free(uri);
            return;
        }
        m_lastSyncedUri = uri;
        g_free(uri);
    }

    char* path = g_file_get_path(directory);
    if (!path) {
        return;
    }

    size_t pathLen = std::strlen(path);
    if (pathLen > 4096) {
        g_free(path);
        return;
    }

    // Reject carriage return and newlines to prevent command injection in child terminal
    for (const char* p = path; *p; ++p) {
        if (*p == '\n' || *p == '\r') {
            g_free(path);
            return;
        }
    }

    // Standard POSIX single-quote escaping: ' becomes '\''
    std::string escaped = "'";
    for (const char* p = path; *p; ++p) {
        if (*p == '\'') {
            escaped.append("'\\''");
        } else {
            escaped.push_back(*p);
        }
    }
    escaped.push_back('\'');
    g_free(path);

    std::string command = "cd " + escaped + "\n";
    vte_terminal_feed_child(m_terminal, command.c_str(), static_cast<gssize>(command.size()));
}

void TerminalDrawer::clear() {
    if (m_terminal) {
        vte_terminal_reset(m_terminal, TRUE, TRUE);
    }
}

void TerminalDrawer::updateTheme(bool isDark) {
    if (!m_terminal) {
        return;
    }

    GdkRGBA bg;
    GdkRGBA fg;
    if (isDark) {
        gdk_rgba_parse(&bg, "#1e1e1e");
        gdk_rgba_parse(&fg, "#f6f6f6");
    } else {
        gdk_rgba_parse(&bg, "#ffffff");
        gdk_rgba_parse(&fg, "#2e3436");
    }

    vte_terminal_set_color_background(m_terminal, &bg);
    vte_terminal_set_color_foreground(m_terminal, &fg);
}

} // namespace dfiles::terminal
