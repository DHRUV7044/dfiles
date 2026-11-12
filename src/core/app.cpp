#include "core/app.hpp"
#include "ui/window.hpp"

#include <iostream>

namespace dfiles::core {

DFilesApp::DFilesApp(const std::string& appId) {
    const char* id = appId.empty() ? "org.gnome.dfiles" : appId.c_str();
    m_app = adw_application_new(id, G_APPLICATION_HANDLES_OPEN);

    const GOptionEntry options[] = {
        {"test-mode", 't', 0, G_OPTION_ARG_NONE, nullptr, "Run in test mode for automated verification", nullptr},
        {"version", 'v', 0, G_OPTION_ARG_NONE, nullptr, "Show application version", nullptr},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}
    };
    g_application_add_main_option_entries(G_APPLICATION(m_app), options);

    m_handleLocalOptionsId = g_signal_connect(m_app, "handle-local-options", G_CALLBACK(onHandleLocalOptionsTrampoline), this);
    m_startupId = g_signal_connect(m_app, "startup", G_CALLBACK(onStartupTrampoline), this);
    m_activateId = g_signal_connect(m_app, "activate", G_CALLBACK(onActivateTrampoline), this);
    m_openId = g_signal_connect(m_app, "open", G_CALLBACK(onOpenTrampoline), this);
}

DFilesApp::~DFilesApp() {
#if ADW_CHECK_VERSION(1, 5, 0)
    if (m_activePrefsDialog) {
        g_object_remove_weak_pointer(G_OBJECT(m_activePrefsDialog), reinterpret_cast<gpointer*>(&m_activePrefsDialog));
        adw_dialog_force_close(m_activePrefsDialog);
        m_activePrefsDialog = nullptr;
    }
    if (m_activeAboutDialog) {
        g_object_remove_weak_pointer(G_OBJECT(m_activeAboutDialog), reinterpret_cast<gpointer*>(&m_activeAboutDialog));
        adw_dialog_force_close(m_activeAboutDialog);
        m_activeAboutDialog = nullptr;
    }
#else
    if (m_activePrefsWindow) {
        g_object_remove_weak_pointer(G_OBJECT(m_activePrefsWindow), reinterpret_cast<gpointer*>(&m_activePrefsWindow));
        gtk_window_destroy(GTK_WINDOW(m_activePrefsWindow));
        m_activePrefsWindow = nullptr;
    }
    if (m_activeAboutWindow) {
        g_object_remove_weak_pointer(G_OBJECT(m_activeAboutWindow), reinterpret_cast<gpointer*>(&m_activeAboutWindow));
        gtk_window_destroy(GTK_WINDOW(m_activeAboutWindow));
        m_activeAboutWindow = nullptr;
    }
#endif

    m_window.reset();
    if (m_app) {
        if (m_handleLocalOptionsId > 0) {
            if (g_signal_handler_is_connected(m_app, m_handleLocalOptionsId)) {
                g_signal_handler_disconnect(m_app, m_handleLocalOptionsId);
            }
            m_handleLocalOptionsId = 0;
        }
        if (m_startupId > 0) {
            if (g_signal_handler_is_connected(m_app, m_startupId)) {
                g_signal_handler_disconnect(m_app, m_startupId);
            }
            m_startupId = 0;
        }
        if (m_activateId > 0) {
            if (g_signal_handler_is_connected(m_app, m_activateId)) {
                g_signal_handler_disconnect(m_app, m_activateId);
            }
            m_activateId = 0;
        }
        if (m_openId > 0) {
            if (g_signal_handler_is_connected(m_app, m_openId)) {
                g_signal_handler_disconnect(m_app, m_openId);
            }
            m_openId = 0;
        }
        g_clear_object(&m_app);
    }
}

void DFilesApp::setTestMode(bool testMode) noexcept {
    m_testMode = testMode;
    if (testMode && m_app) {
        auto flags = g_application_get_flags(G_APPLICATION(m_app));
        g_application_set_flags(G_APPLICATION(m_app), static_cast<GApplicationFlags>(flags | G_APPLICATION_NON_UNIQUE));
    }
}

int DFilesApp::run(int argc, char* argv[]) {
    if (!m_app) {
        return 1;
    }
    return g_application_run(G_APPLICATION(m_app), argc, argv);
}

void DFilesApp::onStartup() {
    if (m_startupDone) {
        return;
    }
    m_startupDone = true;
    setupActions();
    setupAccelerators();
}

void DFilesApp::onActivate() {
    if (m_app && !g_application_get_is_registered(G_APPLICATION(m_app))) {
        g_application_register(G_APPLICATION(m_app), nullptr, nullptr);
    }
    if (!m_startupDone) {
        onStartup();
    }
    if (!m_window || !m_window->getWindow()) {
        m_window = std::make_unique<ui::DFilesWindow>(m_app);
        if (m_window->getWindow()) {
            g_signal_connect(m_window->getWindow(), "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
                auto* self = static_cast<DFilesApp*>(userData);
                if (self) {
                    self->onWindowDestroyed();
                }
            }), this);
        }
    }
    m_window->present();

    if (m_testMode) {
        g_print("[test-mode] Application activated and DFilesWindow instantiated successfully.\n");
        g_idle_add(+[](gpointer userData) -> gboolean {
            auto* self = static_cast<DFilesApp*>(userData);
            if (self) {
                self->onActionQuit();
            }
            return G_SOURCE_REMOVE;
        }, this);
    }
}

void DFilesApp::onWindowDestroyed() {
    if (m_window) {
        m_window.reset();
    }
}

void DFilesApp::onOpen(GFile** files, gint nFiles, const gchar* hint) {
    (void)hint;
    // REMED-2: Ensure valid window instance before opening files
    if (!m_window || !m_window->getWindow()) {
        onActivate();
    }
    if (!m_window || !m_window->getWindow()) {
        return;
    }

    if (nFiles > 0 && files && files[0]) {
        g_autofree char* uri = g_file_get_uri(files[0]);
        if (uri && g_uri_is_valid(uri, G_URI_FLAGS_NONE, nullptr)) {
            if (auto* sidebar = m_window->getSidebar()) {
                sidebar->selectPlaceByUri(uri);
            }
            if (auto* hb = m_window->getHeaderBar()) {
                hb->setTitle("dfiles", uri);
            }
            m_window->showToast(std::string("Opened: ") + uri);
        }
    }
}

void DFilesApp::setupActions() {
    auto* map = G_ACTION_MAP(m_app);

    // 1. Action: app.quit
    g_autoptr(GSimpleAction) quitAction = g_simple_action_new("quit", nullptr);
    g_signal_connect(quitAction, "activate", G_CALLBACK(+[](GSimpleAction* /*action*/, GVariant* /*parameter*/, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->onActionQuit();
        }
    }), this);
    g_action_map_add_action(map, G_ACTION(quitAction));

    // 2. Action: app.preferences
    g_autoptr(GSimpleAction) prefAction = g_simple_action_new("preferences", nullptr);
    g_signal_connect(prefAction, "activate", G_CALLBACK(+[](GSimpleAction* /*action*/, GVariant* /*parameter*/, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->onActionPreferences();
        }
    }), this);
    g_action_map_add_action(map, G_ACTION(prefAction));

    // 3. Action: app.about
    g_autoptr(GSimpleAction) aboutAction = g_simple_action_new("about", nullptr);
    g_signal_connect(aboutAction, "activate", G_CALLBACK(+[](GSimpleAction* /*action*/, GVariant* /*parameter*/, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->onActionAbout();
        }
    }), this);
    g_action_map_add_action(map, G_ACTION(aboutAction));
}

void DFilesApp::setupAccelerators() {
    auto* gtkApp = GTK_APPLICATION(m_app);

    // Global application actions
    const char* const quitAccels[] = {"<Control>q", "<Control>w", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "app.quit", quitAccels);

    const char* const prefAccels[] = {"<Control>comma", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "app.preferences", prefAccels);

    // Window action bindings
    const char* const toggleSplitAccels[] = {"F3", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "win.toggle-split", toggleSplitAccels);

    const char* const toggleTerminalAccels[] = {"F4", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "win.toggle-terminal", toggleTerminalAccels);

    const char* const toggleInspectorAccels[] = {"F11", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "win.toggle-inspector", toggleInspectorAccels);

    const char* const focusPathbarAccels[] = {"<Control>l", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "win.focus-pathbar", focusPathbarAccels);

    const char* const focusFilterAccels[] = {"<Control>i", nullptr};
    gtk_application_set_accels_for_action(gtkApp, "win.focus-filter", focusFilterAccels);
}

void DFilesApp::onActionQuit() {
    if (m_app) {
        g_application_quit(G_APPLICATION(m_app));
    }
}

void DFilesApp::onActionPreferences() {
    if (!m_app) {
        return;
    }
    GtkWindow* activeWin = gtk_application_get_active_window(GTK_APPLICATION(m_app));
    if (!activeWin && m_window) {
        activeWin = GTK_WINDOW(m_window->getWindow());
    }

#if ADW_CHECK_VERSION(1, 5, 0)
    // REMED-5: Prevent stacking multiple dialogs on repeated shortcut presses
    if (m_activePrefsDialog) {
        if (activeWin) {
            adw_dialog_present(m_activePrefsDialog, GTK_WIDGET(activeWin));
        }
        return;
    }

    AdwDialog* prefs = adw_preferences_dialog_new();
    m_activePrefsDialog = prefs;
    g_object_add_weak_pointer(G_OBJECT(prefs), reinterpret_cast<gpointer*>(&m_activePrefsDialog));
    g_signal_connect(prefs, "closed", G_CALLBACK(+[](AdwDialog*, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->m_activePrefsDialog = nullptr;
        }
    }), this);

    AdwPreferencesPage* page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "General");
    adw_preferences_page_set_icon_name(page, "preferences-other-symbolic");

    AdwPreferencesGroup* group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    // REMED-5: Use unescaped "View & Layout" for Libadwaita title
    adw_preferences_group_set_title(group, "View and Layout");
    adw_preferences_group_set_description(group, "Preferences for split view and embedded terminal");

    AdwActionRow* splitRow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(splitRow), "Dual-Pane Split (F3)");
    adw_action_row_set_subtitle(splitRow, "Show twin directory browsing panes side-by-side");

    AdwActionRow* termRow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(termRow), "Terminal Drawer (F4)");
    adw_action_row_set_subtitle(termRow, "Synchronize embedded terminal directory with active pane");

    adw_preferences_group_add(group, GTK_WIDGET(splitRow));
    adw_preferences_group_add(group, GTK_WIDGET(termRow));
    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(prefs), page);

    if (activeWin) {
        adw_dialog_present(prefs, GTK_WIDGET(activeWin));
    } else {
        g_object_ref_sink(prefs);
        g_object_unref(prefs);
    }
#else
    // REMED-5: Prevent stacking multiple dialogs on repeated shortcut presses
    if (m_activePrefsWindow) {
        gtk_window_present(GTK_WINDOW(m_activePrefsWindow));
        return;
    }

    GtkWidget* prefs = adw_preferences_window_new();
    m_activePrefsWindow = ADW_PREFERENCES_WINDOW(prefs);
    g_object_add_weak_pointer(G_OBJECT(prefs), reinterpret_cast<gpointer*>(&m_activePrefsWindow));
    g_signal_connect(prefs, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->m_activePrefsWindow = nullptr;
        }
    }), this);

    if (activeWin) {
        gtk_window_set_transient_for(GTK_WINDOW(prefs), activeWin);
        gtk_window_set_modal(GTK_WINDOW(prefs), TRUE);
    }

    AdwPreferencesPage* page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "General");
    adw_preferences_page_set_icon_name(page, "preferences-other-symbolic");

    AdwPreferencesGroup* group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    // REMED-5: Use unescaped "View & Layout" for Libadwaita title
    adw_preferences_group_set_title(group, "View and Layout");
    adw_preferences_group_set_description(group, "Preferences for split view and embedded terminal");

    AdwActionRow* splitRow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(splitRow), "Dual-Pane Split (F3)");
    adw_action_row_set_subtitle(splitRow, "Show twin directory browsing panes side-by-side");

    AdwActionRow* termRow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(termRow), "Terminal Drawer (F4)");
    adw_action_row_set_subtitle(termRow, "Synchronize embedded terminal directory with active pane");

    adw_preferences_group_add(group, GTK_WIDGET(splitRow));
    adw_preferences_group_add(group, GTK_WIDGET(termRow));
    adw_preferences_page_add(page, group);
    adw_preferences_window_add(ADW_PREFERENCES_WINDOW(prefs), page);

    gtk_window_present(GTK_WINDOW(prefs));
#endif
}

void DFilesApp::onActionAbout() {
    if (!m_app) {
        return;
    }
    GtkWindow* activeWin = gtk_application_get_active_window(GTK_APPLICATION(m_app));
    if (!activeWin && m_window) {
        activeWin = GTK_WINDOW(m_window->getWindow());
    }

#if ADW_CHECK_VERSION(1, 5, 0)
    // REMED-5: Prevent stacking multiple dialogs on repeated shortcut presses
    if (m_activeAboutDialog) {
        if (activeWin) {
            adw_dialog_present(m_activeAboutDialog, GTK_WIDGET(activeWin));
        }
        return;
    }

    AdwDialog* dialog = adw_about_dialog_new();
    m_activeAboutDialog = dialog;
    g_object_add_weak_pointer(G_OBJECT(dialog), reinterpret_cast<gpointer*>(&m_activeAboutDialog));
    g_signal_connect(dialog, "closed", G_CALLBACK(+[](AdwDialog*, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->m_activeAboutDialog = nullptr;
        }
    }), this);

    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(dialog), "dfiles");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(dialog), "system-file-manager");
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(dialog), "0.1.0");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(dialog), "dfiles contributors");
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(dialog),
        "High-performance Linux file manager blending GNOME elegance with Dolphin power.");
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(dialog), "https://github.com/dhruv/dfiles");

    if (activeWin) {
        adw_dialog_present(dialog, GTK_WIDGET(activeWin));
    } else {
        g_object_ref_sink(dialog);
        g_object_unref(dialog);
    }
#else
    // REMED-5: Prevent stacking multiple dialogs on repeated shortcut presses
    if (m_activeAboutWindow) {
        gtk_window_present(GTK_WINDOW(m_activeAboutWindow));
        return;
    }

    GtkWidget* window = adw_about_window_new();
    m_activeAboutWindow = ADW_ABOUT_WINDOW(window);
    g_object_add_weak_pointer(G_OBJECT(window), reinterpret_cast<gpointer*>(&m_activeAboutWindow));
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
        auto* self = static_cast<DFilesApp*>(userData);
        if (self) {
            self->m_activeAboutWindow = nullptr;
        }
    }), this);

    adw_about_window_set_application_name(ADW_ABOUT_WINDOW(window), "dfiles");
    adw_about_window_set_application_icon(ADW_ABOUT_WINDOW(window), "system-file-manager");
    adw_about_window_set_version(ADW_ABOUT_WINDOW(window), "0.1.0");
    adw_about_window_set_developer_name(ADW_ABOUT_WINDOW(window), "dfiles contributors");
    adw_about_window_set_comments(ADW_ABOUT_WINDOW(window),
        "High-performance Linux file manager blending GNOME elegance with Dolphin power.");
    adw_about_window_set_license_type(ADW_ABOUT_WINDOW(window), GTK_LICENSE_GPL_3_0);
    adw_about_window_set_website(ADW_ABOUT_WINDOW(window), "https://github.com/dhruv/dfiles");

    if (activeWin) {
        gtk_window_set_transient_for(GTK_WINDOW(window), activeWin);
        gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    }
    gtk_window_present(GTK_WINDOW(window));
#endif
}

gint DFilesApp::handleLocalOptions(GVariantDict* options) {
    if (g_variant_dict_contains(options, "version")) {
        g_print("dfiles 0.1.0\n");
        return 0;
    }

    if (g_variant_dict_contains(options, "test-mode")) {
        setTestMode(true);
    }

    return -1;
}

void DFilesApp::onStartupTrampoline(GApplication* /*app*/, gpointer userData) {
    auto* self = static_cast<DFilesApp*>(userData);
    if (self) {
        self->onStartup();
    }
}

void DFilesApp::onActivateTrampoline(GApplication* /*app*/, gpointer userData) {
    auto* self = static_cast<DFilesApp*>(userData);
    if (self) {
        self->onActivate();
    }
}

void DFilesApp::onOpenTrampoline(GApplication* /*app*/, GFile** files, gint nFiles, const gchar* hint, gpointer userData) {
    auto* self = static_cast<DFilesApp*>(userData);
    if (self) {
        self->onOpen(files, nFiles, hint);
    }
}

gint DFilesApp::onHandleLocalOptionsTrampoline(GApplication* /*app*/, GVariantDict* options, gpointer userData) {
    auto* self = static_cast<DFilesApp*>(userData);
    if (self && options) {
        return self->handleLocalOptions(options);
    }
    return -1;
}

} // namespace dfiles::core
