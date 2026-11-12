#include "ui/window.hpp"
#include <algorithm>
#include <gdk/gdkkeysyms.h>

namespace dfiles::ui {

DFilesWindow::DFilesWindow(AdwApplication* app) {
    setupUI(app);
    setupSignals();
    setupActions();
    setupShortcuts();
}

DFilesWindow::~DFilesWindow() {
    // 1. Cancel all pending off-thread toast sources (REMED-1)
    {
        std::lock_guard<std::mutex> lock(m_toastMutex);
        m_isDestroyed = true;
        for (guint id : m_pendingToastSources) {
            g_source_remove(id);
        }
        m_pendingToastSources.clear();
    }

    // 2. Clear callbacks to prevent invoked signals from accessing half-destroyed window
    if (m_headerBar) {
        m_headerBar->onSplitToggled = nullptr;
        m_headerBar->onTerminalToggled = nullptr;
        m_headerBar->onInspectorToggled = nullptr;
    }
    if (m_sidebar) {
        m_sidebar->onPlaceSelected = nullptr;
    }
    if (m_splitView) {
        m_splitView->onActivePaneChanged = nullptr;
        if (m_splitView->getLeftPane()) {
            m_splitView->getLeftPane()->onDirectoryChanged = nullptr;
        }
        if (m_splitView->getRightPane()) {
            m_splitView->getRightPane()->onDirectoryChanged = nullptr;
        }
    }

    // 3. Disconnect and remove keyboard shortcut controller
    if (m_keyController) {
        if (m_keyPressedId > 0) {
            if (g_signal_handler_is_connected(m_keyController, m_keyPressedId)) {
                g_signal_handler_disconnect(m_keyController, m_keyPressedId);
            }
            m_keyPressedId = 0;
        }
        if (m_window) {
            gtk_widget_remove_controller(m_window, m_keyController);
        } else {
            g_clear_object(&m_keyController);
        }
        m_keyController = nullptr;
    }

    // 4. Reset encapsulated sub-components while native widgets are still valid
    m_splitView.reset();
    m_sidebar.reset();
    m_headerBar.reset();

    // 5. Destroy native window cleanly if still allocated
    if (m_window) {
        g_object_remove_weak_pointer(G_OBJECT(m_window), reinterpret_cast<gpointer*>(&m_window));
        gtk_window_destroy(GTK_WINDOW(m_window));
        m_window = nullptr;
    }
}

GtkWidget* DFilesWindow::getWidget() const {
    return m_window;
}

AdwApplicationWindow* DFilesWindow::getWindow() const {
    return m_window ? ADW_APPLICATION_WINDOW(m_window) : nullptr;
}

void DFilesWindow::present() {
    if (m_window) {
        gtk_window_present(GTK_WINDOW(m_window));
    }
}

void DFilesWindow::showToast(const std::string& message, uint32_t timeoutSeconds) {
    if (message.empty() || !m_toastOverlay) {
        return;
    }

    // Thread safety: schedule onto GTK main loop if invoked from worker thread
    if (!g_main_context_is_owner(g_main_context_default())) {
        struct ToastPayload {
            DFilesWindow* self;
            std::string msg;
            uint32_t timeout;
            guint sourceId{0};
        };
        auto* payload = new ToastPayload{this, message, timeoutSeconds, 0};
        GSource* source = g_idle_source_new();
        g_source_set_callback(
            source,
            +[](gpointer p) -> gboolean {
                auto* tp = static_cast<ToastPayload*>(p);
                if (!tp || !tp->self) {
                    return G_SOURCE_REMOVE;
                }
                auto* self = tp->self;
                const guint id = tp->sourceId;

                // REMED-1: Remove source ID before calling showToast
                {
                    std::lock_guard<std::mutex> lock(self->m_toastMutex);
                    auto it = std::find(self->m_pendingToastSources.begin(), self->m_pendingToastSources.end(), id);
                    if (it != self->m_pendingToastSources.end()) {
                        self->m_pendingToastSources.erase(it);
                    }
                }

                self->showToast(tp->msg, tp->timeout);
                return G_SOURCE_REMOVE;
            },
            payload,
            +[](gpointer p) {
                delete static_cast<ToastPayload*>(p);
            }
        );

        {
            std::lock_guard<std::mutex> lock(m_toastMutex);
            if (!m_isDestroyed) {
                guint id = g_source_attach(source, g_main_context_default());
                payload->sourceId = id;
                m_pendingToastSources.push_back(id);
            }
        }
        g_source_unref(source);
        return;
    }

    g_autoptr(AdwToast) toast = adw_toast_new(message.c_str());
    if (!toast) {
        return;
    }
    adw_toast_set_timeout(toast, timeoutSeconds);
    // adw_toast_overlay_add_toast consumes the toast reference (transfer full)
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(m_toastOverlay), g_steal_pointer(&toast));
}

DFilesHeaderBar* DFilesWindow::getHeaderBar() const {
    return m_headerBar.get();
}

PlacesSidebar* DFilesWindow::getSidebar() const {
    return m_sidebar.get();
}

views::SplitView* DFilesWindow::getSplitView() const noexcept {
    return m_splitView.get();
}

GtkWidget* DFilesWindow::getContentArea() const {
    return m_contentArea;
}

void DFilesWindow::setupUI(AdwApplication* app) {
    if (!app) {
        return;
    }

    // 1. Root application window
    m_window = adw_application_window_new(GTK_APPLICATION(app));
    g_object_add_weak_pointer(G_OBJECT(m_window), reinterpret_cast<gpointer*>(&m_window));

    gtk_window_set_default_size(GTK_WINDOW(m_window), 1050, 680);
    gtk_window_set_title(GTK_WINDOW(m_window), "dfiles");

    // 2. Encapsulated sub-components
    m_headerBar = std::make_unique<DFilesHeaderBar>();
    m_sidebar = std::make_unique<PlacesSidebar>();

    // 3. AdwToolbarView layout container
    m_toolbarView = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(m_toolbarView), m_headerBar->getWidget());

    // 4. AdwToastOverlay for non-intrusive notification banners
    m_toastOverlay = adw_toast_overlay_new();
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(m_toolbarView), m_toastOverlay);

    // 5. Horizontal GtkPaned (Sidebar on left, main content on right)
    m_mainPaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(m_mainPaned), 220);

    // Left pane: PlacesSidebar
    gtk_paned_set_start_child(GTK_PANED(m_mainPaned), m_sidebar->getWidget());
    gtk_paned_set_resize_start_child(GTK_PANED(m_mainPaned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(m_mainPaned), FALSE);

    // Right pane: Content container hosting SplitView
    m_contentArea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(m_contentArea, TRUE);
    gtk_widget_set_vexpand(m_contentArea, TRUE);

    // Phase 2: Host SplitView inside content area
    m_splitView = std::make_unique<views::SplitView>();
    gtk_box_append(GTK_BOX(m_contentArea), m_splitView->getWidget());

    gtk_paned_set_end_child(GTK_PANED(m_mainPaned), m_contentArea);
    gtk_paned_set_resize_end_child(GTK_PANED(m_mainPaned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(m_mainPaned), FALSE);

    // Pack into toast overlay & window
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(m_toastOverlay), m_mainPaned);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(m_window), m_toolbarView);

    // Initial navigation: Load $HOME into primary pane
    const char* home = g_get_home_dir();
    if (home) {
        g_autoptr(GFile) homeDir = g_file_new_for_path(home);
        m_splitView->getLeftPane()->navigateTo(homeDir);
    }
}

void DFilesWindow::setupSignals() {
    auto updateHeaderForPane = [this](views::PaneView* pane) {
        if (!pane || !m_headerBar) {
            return;
        }
        GFile* dir = pane->getCurrentDirectory();
        if (dir) {
            g_autofree char* path = g_file_get_parse_name(dir);
            g_autofree char* basename = g_file_get_basename(dir);
            m_headerBar->setTitle(basename ? basename : "dfiles", path ? path : "");

            g_autofree char* uri = g_file_get_uri(dir);
            if (uri && m_sidebar) {
                m_sidebar->selectPlaceByUri(uri);
            }
        }
    };

    if (m_splitView) {
        m_splitView->onActivePaneChanged = [this, updateHeaderForPane](views::PaneView* activePane) {
            updateHeaderForPane(activePane);
        };

        if (m_splitView->getLeftPane()) {
            m_splitView->getLeftPane()->onDirectoryChanged = [this, updateHeaderForPane](GFile*) {
                if (m_splitView->getActivePane() == m_splitView->getLeftPane()) {
                    updateHeaderForPane(m_splitView->getLeftPane());
                }
            };
        }

        if (m_splitView->getRightPane()) {
            m_splitView->getRightPane()->onDirectoryChanged = [this, updateHeaderForPane](GFile*) {
                if (m_splitView->getActivePane() == m_splitView->getRightPane()) {
                    updateHeaderForPane(m_splitView->getRightPane());
                }
            };
        }
    }

    if (m_headerBar) {
        m_headerBar->onSplitToggled = [this]() {
            if (m_splitView) {
                m_splitView->toggleSplit();
                const bool active = m_splitView->isSplit();
                if (m_headerBar && m_headerBar->isSplitActive() != active) {
                    m_headerBar->setSplitActive(active);
                }
                showToast(active ? "Split View enabled (F3)" : "Split View disabled (F3)");

                if (m_window) {
                    if (auto* act = g_action_map_lookup_action(G_ACTION_MAP(m_window), "toggle-split")) {
                        g_autoptr(GVariant) state = g_action_get_state(act);
                        if (state && g_variant_get_boolean(state) != static_cast<gboolean>(active)) {
                            g_simple_action_set_state(G_SIMPLE_ACTION(act), g_variant_new_boolean(active));
                        }
                    }
                }
            }
        };

        m_headerBar->onTerminalToggled = [this]() {
            if (m_headerBar) {
                const bool active = m_headerBar->isTerminalActive();
                showToast(active ? "Terminal drawer opened (F4)" : "Terminal drawer closed (F4)");
                if (m_window) {
                    if (auto* act = g_action_map_lookup_action(G_ACTION_MAP(m_window), "toggle-terminal")) {
                        g_autoptr(GVariant) state = g_action_get_state(act);
                        if (state && g_variant_get_boolean(state) != static_cast<gboolean>(active)) {
                            g_simple_action_set_state(G_SIMPLE_ACTION(act), g_variant_new_boolean(active));
                        }
                    }
                }
            }
        };

        m_headerBar->onInspectorToggled = [this]() {
            if (m_headerBar) {
                const bool active = m_headerBar->isInspectorActive();
                showToast(active ? "Inspector sidebar opened (F11)" : "Inspector sidebar closed (F11)");
                if (m_window) {
                    if (auto* act = g_action_map_lookup_action(G_ACTION_MAP(m_window), "toggle-inspector")) {
                        g_autoptr(GVariant) state = g_action_get_state(act);
                        if (state && g_variant_get_boolean(state) != static_cast<gboolean>(active)) {
                            g_simple_action_set_state(G_SIMPLE_ACTION(act), g_variant_new_boolean(active));
                        }
                    }
                }
            }
        };
    }

    if (m_sidebar) {
        m_sidebar->onPlaceSelected = [this](const std::string& uri) {
            if (m_splitView && m_splitView->getActivePane()) {
                g_autoptr(GFile) file = g_file_new_for_uri(uri.c_str());
                if (file) {
                    m_splitView->getActivePane()->navigateTo(file);
                }
            }
        };
    }
}

void DFilesWindow::setupActions() {
    if (!m_window) {
        return;
    }

    auto* actionMap = G_ACTION_MAP(m_window);

    // 1. Action: win.toggle-split (stateful boolean, REMED-3)
    g_autoptr(GSimpleAction) splitAction = g_simple_action_new_stateful(
        "toggle-split",
        nullptr,
        g_variant_new_boolean(FALSE)
    );
    g_signal_connect(splitAction, "activate", G_CALLBACK(+[](GSimpleAction* action, GVariant*, gpointer) {
        g_autoptr(GVariant) state = g_action_get_state(G_ACTION(action));
        const gboolean current = g_variant_get_boolean(state);
        g_action_change_state(G_ACTION(action), g_variant_new_boolean(!current));
    }), this);
    g_signal_connect(splitAction, "change-state", G_CALLBACK(+[](GSimpleAction* action, GVariant* value, gpointer data) {
        auto* self = static_cast<DFilesWindow*>(data);
        if (!self || !value) {
            return;
        }
        const gboolean active = g_variant_get_boolean(value);
        g_simple_action_set_state(action, value);

        if (self->m_splitView && self->m_splitView->isSplit() != static_cast<bool>(active)) {
            self->m_splitView->toggleSplit();
        }

        if (self->m_headerBar) {
            if (self->m_headerBar->isSplitActive() != static_cast<bool>(active)) {
                self->m_headerBar->setSplitActive(active);
            }
        }
    }), this);
    g_action_map_add_action(actionMap, G_ACTION(splitAction));

    // 2. Action: win.toggle-terminal (stateful boolean, REMED-3)
    g_autoptr(GSimpleAction) termAction = g_simple_action_new_stateful(
        "toggle-terminal",
        nullptr,
        g_variant_new_boolean(FALSE)
    );
    g_signal_connect(termAction, "activate", G_CALLBACK(+[](GSimpleAction* action, GVariant*, gpointer) {
        g_autoptr(GVariant) state = g_action_get_state(G_ACTION(action));
        const gboolean current = g_variant_get_boolean(state);
        g_action_change_state(G_ACTION(action), g_variant_new_boolean(!current));
    }), this);
    g_signal_connect(termAction, "change-state", G_CALLBACK(+[](GSimpleAction* action, GVariant* value, gpointer data) {
        auto* self = static_cast<DFilesWindow*>(data);
        if (!self || !value) {
            return;
        }
        const gboolean active = g_variant_get_boolean(value);
        g_simple_action_set_state(action, value);
        if (self->m_headerBar) {
            if (self->m_headerBar->isTerminalActive() != static_cast<bool>(active)) {
                self->m_headerBar->setTerminalActive(active);
            }
            if (self->m_headerBar->onTerminalToggled) {
                self->m_headerBar->onTerminalToggled();
            }
        }
    }), this);
    g_action_map_add_action(actionMap, G_ACTION(termAction));

    // 3. Action: win.toggle-inspector (stateful boolean, REMED-3)
    g_autoptr(GSimpleAction) inspAction = g_simple_action_new_stateful(
        "toggle-inspector",
        nullptr,
        g_variant_new_boolean(FALSE)
    );
    g_signal_connect(inspAction, "activate", G_CALLBACK(+[](GSimpleAction* action, GVariant*, gpointer) {
        g_autoptr(GVariant) state = g_action_get_state(G_ACTION(action));
        const gboolean current = g_variant_get_boolean(state);
        g_action_change_state(G_ACTION(action), g_variant_new_boolean(!current));
    }), this);
    g_signal_connect(inspAction, "change-state", G_CALLBACK(+[](GSimpleAction* action, GVariant* value, gpointer data) {
        auto* self = static_cast<DFilesWindow*>(data);
        if (!self || !value) {
            return;
        }
        const gboolean active = g_variant_get_boolean(value);
        g_simple_action_set_state(action, value);
        if (self->m_headerBar) {
            if (self->m_headerBar->isInspectorActive() != static_cast<bool>(active)) {
                self->m_headerBar->setInspectorActive(active);
            }
            if (self->m_headerBar->onInspectorToggled) {
                self->m_headerBar->onInspectorToggled();
            }
        }
    }), this);
    g_action_map_add_action(actionMap, G_ACTION(inspAction));

    // 4. Action: win.focus-pathbar (stateless, REMED-3)
    g_autoptr(GSimpleAction) focusPathAction = g_simple_action_new("focus-pathbar", nullptr);
    g_signal_connect(focusPathAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
        auto* self = static_cast<DFilesWindow*>(data);
        if (self) {
            self->showToast("Focus Pathbar (<Control>l)");
        }
    }), this);
    g_action_map_add_action(actionMap, G_ACTION(focusPathAction));

    // 5. Action: win.focus-filter (stateless, REMED-3)
    g_autoptr(GSimpleAction) focusFilterAction = g_simple_action_new("focus-filter", nullptr);
    g_signal_connect(focusFilterAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
        auto* self = static_cast<DFilesWindow*>(data);
        if (self) {
            self->showToast("Focus Filter (<Control>i)");
        }
    }), this);
    g_action_map_add_action(actionMap, G_ACTION(focusFilterAction));
}

void DFilesWindow::setupShortcuts() {
    if (!m_window) {
        return;
    }

    m_keyController = gtk_event_controller_key_new();
    m_keyPressedId = g_signal_connect(m_keyController, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer data) -> gboolean {
            auto* self = static_cast<DFilesWindow*>(data);
            if (!self || !self->m_splitView) {
                return GDK_EVENT_PROPAGATE;
            }

            if (keyval == GDK_KEY_Tab) {
                if (self->m_splitView->isSplit()) {
                    self->m_splitView->cycleFocus();
                    return GDK_EVENT_STOP;
                }
            }
            return GDK_EVENT_PROPAGATE;
        }), this);

    gtk_widget_add_controller(m_window, m_keyController);
}

} // namespace dfiles::ui
