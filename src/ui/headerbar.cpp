#include "ui/headerbar.hpp"

namespace dfiles::ui {

DFilesHeaderBar::DFilesHeaderBar() {
    setupUI();
}

DFilesHeaderBar::~DFilesHeaderBar() {
    onSplitToggled = nullptr;
    onTerminalToggled = nullptr;
    onInspectorToggled = nullptr;

    if (m_splitButton && m_splitToggledId > 0) {
        if (g_signal_handler_is_connected(m_splitButton, m_splitToggledId)) {
            g_signal_handler_disconnect(m_splitButton, m_splitToggledId);
        }
        m_splitToggledId = 0;
    }
    if (m_terminalButton && m_terminalToggledId > 0) {
        if (g_signal_handler_is_connected(m_terminalButton, m_terminalToggledId)) {
            g_signal_handler_disconnect(m_terminalButton, m_terminalToggledId);
        }
        m_terminalToggledId = 0;
    }
    if (m_inspectorButton && m_inspectorToggledId > 0) {
        if (g_signal_handler_is_connected(m_inspectorButton, m_inspectorToggledId)) {
            g_signal_handler_disconnect(m_inspectorButton, m_inspectorToggledId);
        }
        m_inspectorToggledId = 0;
    }
}

GtkWidget* DFilesHeaderBar::getWidget() const {
    return m_headerBar;
}

AdwHeaderBar* DFilesHeaderBar::getHeaderBar() const {
    return ADW_HEADER_BAR(m_headerBar);
}

void DFilesHeaderBar::setTitle(const std::string& title, const std::string& subtitle) {
    if (!m_titleWidget) {
        return;
    }
    adw_window_title_set_title(ADW_WINDOW_TITLE(m_titleWidget), title.empty() ? "dfiles" : title.c_str());
    if (!subtitle.empty()) {
        adw_window_title_set_subtitle(ADW_WINDOW_TITLE(m_titleWidget), subtitle.c_str());
    } else {
        adw_window_title_set_subtitle(ADW_WINDOW_TITLE(m_titleWidget), nullptr);
    }
}

void DFilesHeaderBar::setSplitActive(bool active) {
    if (!m_splitButton) {
        return;
    }
    m_blockSignals = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_splitButton), active ? TRUE : FALSE);
    m_blockSignals = false;
}

bool DFilesHeaderBar::isSplitActive() const {
    if (!m_splitButton) {
        return false;
    }
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_splitButton)) == TRUE;
}

void DFilesHeaderBar::setTerminalActive(bool active) {
    if (!m_terminalButton) {
        return;
    }
    m_blockSignals = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_terminalButton), active ? TRUE : FALSE);
    m_blockSignals = false;
}

bool DFilesHeaderBar::isTerminalActive() const {
    if (!m_terminalButton) {
        return false;
    }
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_terminalButton)) == TRUE;
}

void DFilesHeaderBar::setInspectorActive(bool active) {
    if (!m_inspectorButton) {
        return;
    }
    m_blockSignals = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_inspectorButton), active ? TRUE : FALSE);
    m_blockSignals = false;
}

bool DFilesHeaderBar::isInspectorActive() const {
    if (!m_inspectorButton) {
        return false;
    }
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_inspectorButton)) == TRUE;
}

void DFilesHeaderBar::setupMenu() {
    m_menuButton = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_menuButton), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(m_menuButton, "Main Menu");

    // Construct primary application menu model
    g_autoptr(GMenu) menu = g_menu_new();
    g_menu_append(menu, "Preferences", "app.preferences");
    g_menu_append(menu, "About dfiles", "app.about");
    g_menu_append(menu, "Quit", "app.quit");

    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(m_menuButton), G_MENU_MODEL(menu));
}

void DFilesHeaderBar::setupUI() {
    m_headerBar = adw_header_bar_new();

    // Window title widget
    m_titleWidget = adw_window_title_new("dfiles", nullptr);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(m_headerBar), m_titleWidget);

    // Split View toggle button (F3)
    m_splitButton = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(m_splitButton), "view-dual-symbolic");
    gtk_widget_set_tooltip_text(m_splitButton, "Split View (F3)");
    m_splitToggledId = g_signal_connect(m_splitButton, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
        auto* self = static_cast<DFilesHeaderBar*>(data);
        if (self && !self->m_blockSignals && self->onSplitToggled) {
            self->onSplitToggled();
        }
    }), this);

    // Terminal drawer toggle button (F4)
    m_terminalButton = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(m_terminalButton), "utilities-terminal-symbolic");
    gtk_widget_set_tooltip_text(m_terminalButton, "Terminal (F4)");
    m_terminalToggledId = g_signal_connect(m_terminalButton, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
        auto* self = static_cast<DFilesHeaderBar*>(data);
        if (self && !self->m_blockSignals && self->onTerminalToggled) {
            self->onTerminalToggled();
        }
    }), this);

    // Inspector sidebar toggle button (F11)
    m_inspectorButton = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(m_inspectorButton), "sidebar-show-right-symbolic");
    gtk_widget_set_tooltip_text(m_inspectorButton, "Inspector (F11)");
    m_inspectorToggledId = g_signal_connect(m_inspectorButton, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
        auto* self = static_cast<DFilesHeaderBar*>(data);
        if (self && !self->m_blockSignals && self->onInspectorToggled) {
            self->onInspectorToggled();
        }
    }), this);

    // Primary menu
    setupMenu();

    // Pack into headerbar from right to left
    adw_header_bar_pack_end(ADW_HEADER_BAR(m_headerBar), m_menuButton);
    adw_header_bar_pack_end(ADW_HEADER_BAR(m_headerBar), m_inspectorButton);
    adw_header_bar_pack_end(ADW_HEADER_BAR(m_headerBar), m_terminalButton);
    adw_header_bar_pack_end(ADW_HEADER_BAR(m_headerBar), m_splitButton);
}

} // namespace dfiles::ui
