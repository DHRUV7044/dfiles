#include "views/pane_view.hpp"

#include <mutex>

namespace dfiles::views {

namespace {

void ensureCssInitialized() {
    static std::once_flag s_cssInitOnce;
    std::call_once(s_cssInitOnce, []() {
        GtkCssProvider* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(provider,
            ".pane-active { border: 2px solid @accent_bg_color; border-radius: 8px; }\n"
            ".pane-inactive { border: 2px solid transparent; border-radius: 8px; }\n"
        );
        GdkDisplay* display = gdk_display_get_default();
        if (display) {
            gtk_style_context_add_provider_for_display(
                display,
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
            );
        }
        g_object_unref(provider);
    });
}

constexpr size_t MAX_HISTORY_ENTRIES = 50;

} // namespace

PaneView::PaneView() {
    ensureCssInitialized();

    m_model = std::make_unique<model::DirectoryModel>();
    m_fileView = std::make_unique<FileView>();
    m_fileView->setModel(m_model.get());

    setupUI();
    setupSignals();
    updateNavButtons();
}

PaneView::~PaneView() {
    // 1. Clear callbacks to prevent invoked signals from accessing half-destroyed pane
    onDirectoryChanged = nullptr;
    onActivated = nullptr;
    onFileActivated = nullptr;
    onSelectionChanged = nullptr;

    if (m_model) {
        m_model->onLoadingStarted = nullptr;
        m_model->onLoadingFinished = nullptr;
        m_model->onError = nullptr;
    }

    if (m_fileView) {
        m_fileView->onItemActivated = nullptr;
        m_fileView->onSelectionChanged = nullptr;
        m_fileView->setModel(nullptr);
    }

    // 2. Safely unref all history GFile objects
    for (GFile* f : m_backStack) {
        if (f) {
            g_object_unref(f);
        }
    }
    m_backStack.clear();

    for (GFile* f : m_forwardStack) {
        if (f) {
            g_object_unref(f);
        }
    }
    m_forwardStack.clear();

    if (m_currentDir) {
        g_object_unref(m_currentDir);
        m_currentDir = nullptr;
    }
}

GtkWidget* PaneView::getWidget() const noexcept {
    return m_rootBox;
}

GFile* PaneView::getCurrentDirectory() const noexcept {
    return m_currentDir;
}

model::DirectoryModel* PaneView::getModel() const noexcept {
    return m_model.get();
}

FileView* PaneView::getFileView() const noexcept {
    return m_fileView.get();
}

std::vector<model::FileItem> PaneView::getSelectedItems() const {
    if (m_fileView) {
        return m_fileView->getSelectedItems();
    }
    return {};
}

void PaneView::grabFocus() {
    if (m_fileView) {
        m_fileView->grabFocus();
    }
}

void PaneView::setActive(bool active) {
    if (m_isActive == active) {
        if (m_isActive) {
            grabFocus();
        }
        return;
    }
    m_isActive = active;

    if (m_isActive) {
        gtk_widget_add_css_class(m_rootBox, "pane-active");
        gtk_widget_remove_css_class(m_rootBox, "pane-inactive");
        grabFocus();
    } else {
        gtk_widget_add_css_class(m_rootBox, "pane-inactive");
        gtk_widget_remove_css_class(m_rootBox, "pane-active");
    }
}

void PaneView::navigateTo(GFile* dir) {
    if (!dir) {
        return;
    }

    if (m_currentDir) {
        if (g_file_equal(m_currentDir, dir)) {
            m_model->loadDirectory(dir);
            return;
        }
        m_backStack.push_back(G_FILE(g_object_ref(m_currentDir)));
        if (m_backStack.size() > MAX_HISTORY_ENTRIES) {
            g_object_unref(m_backStack.front());
            m_backStack.erase(m_backStack.begin());
        }
    }

    // Clear forward stack
    for (GFile* f : m_forwardStack) {
        if (f) {
            g_object_unref(f);
        }
    }
    m_forwardStack.clear();

    if (m_currentDir) {
        g_object_unref(m_currentDir);
    }
    m_currentDir = G_FILE(g_object_ref(dir));

    m_model->loadDirectory(m_currentDir);
    updateNavButtons();

    g_autofree char* path = g_file_get_parse_name(m_currentDir);
    gtk_label_set_text(GTK_LABEL(m_pathLabel), path ? path : "");

    if (onDirectoryChanged) {
        onDirectoryChanged(m_currentDir);
    }
}

void PaneView::navigateUp() {
    if (!m_currentDir) {
        return;
    }
    g_autoptr(GFile) parent = g_file_get_parent(m_currentDir);
    if (parent) {
        navigateTo(parent);
    }
}

void PaneView::goBack() {
    if (m_backStack.empty()) {
        return;
    }

    GFile* target = m_backStack.back();
    m_backStack.pop_back();

    if (m_currentDir) {
        m_forwardStack.push_back(m_currentDir);
        if (m_forwardStack.size() > MAX_HISTORY_ENTRIES) {
            g_object_unref(m_forwardStack.front());
            m_forwardStack.erase(m_forwardStack.begin());
        }
    }
    m_currentDir = target; // transfers ownership from backStack

    m_model->loadDirectory(m_currentDir);
    updateNavButtons();

    g_autofree char* path = g_file_get_parse_name(m_currentDir);
    gtk_label_set_text(GTK_LABEL(m_pathLabel), path ? path : "");

    if (onDirectoryChanged) {
        onDirectoryChanged(m_currentDir);
    }
}

void PaneView::goForward() {
    if (m_forwardStack.empty()) {
        return;
    }

    GFile* target = m_forwardStack.back();
    m_forwardStack.pop_back();

    if (m_currentDir) {
        m_backStack.push_back(m_currentDir);
        if (m_backStack.size() > MAX_HISTORY_ENTRIES) {
            g_object_unref(m_backStack.front());
            m_backStack.erase(m_backStack.begin());
        }
    }
    m_currentDir = target; // transfers ownership from forwardStack

    m_model->loadDirectory(m_currentDir);
    updateNavButtons();

    g_autofree char* path = g_file_get_parse_name(m_currentDir);
    gtk_label_set_text(GTK_LABEL(m_pathLabel), path ? path : "");

    if (onDirectoryChanged) {
        onDirectoryChanged(m_currentDir);
    }
}

void PaneView::reload() {
    if (m_currentDir) {
        m_model->loadDirectory(m_currentDir);
    }
}

void PaneView::setupUI() {
    m_rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(m_rootBox, "pane-inactive");
    gtk_widget_set_hexpand(m_rootBox, TRUE);
    gtk_widget_set_vexpand(m_rootBox, TRUE);

    // Click gesture to activate pane
    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick*, int, double, double, gpointer userData) {
        auto* self = static_cast<PaneView*>(userData);
        if (self && !self->isActive()) {
            self->setActive(true);
            if (self->onActivated) {
                self->onActivated(self);
            }
        }
    }), this);
    gtk_widget_add_controller(m_rootBox, GTK_EVENT_CONTROLLER(click));

    setupNavBar();
    setupCenterArea();
    setupStatusBar();
}

void PaneView::setupNavBar() {
    m_navBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(m_navBar, 6);
    gtk_widget_set_margin_end(m_navBar, 6);
    gtk_widget_set_margin_top(m_navBar, 4);
    gtk_widget_set_margin_bottom(m_navBar, 4);

    // Back button
    m_backBtn = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(m_backBtn, "Back (Alt+Left)");
    g_signal_connect(m_backBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* self = static_cast<PaneView*>(userData);
        if (self) self->goBack();
    }), this);

    // Forward button
    m_forwardBtn = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_tooltip_text(m_forwardBtn, "Forward (Alt+Right)");
    g_signal_connect(m_forwardBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* self = static_cast<PaneView*>(userData);
        if (self) self->goForward();
    }), this);

    // Up button
    m_upBtn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_set_tooltip_text(m_upBtn, "Up (Alt+Up)");
    g_signal_connect(m_upBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* self = static_cast<PaneView*>(userData);
        if (self) self->navigateUp();
    }), this);

    // Navigation buttons box
    GtkWidget* navBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(navBox), m_backBtn);
    gtk_box_append(GTK_BOX(navBox), m_forwardBtn);
    gtk_box_append(GTK_BOX(navBox), m_upBtn);
    gtk_box_append(GTK_BOX(m_navBar), navBox);

    // Path / location label
    m_pathLabel = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(m_pathLabel), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(m_pathLabel), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(m_pathLabel, TRUE);
    gtk_widget_add_css_class(m_pathLabel, "heading");
    gtk_widget_set_margin_start(m_pathLabel, 8);
    gtk_widget_set_margin_end(m_pathLabel, 8);
    gtk_box_append(GTK_BOX(m_navBar), m_pathLabel);

    // View Mode switcher (List vs Grid)
    m_viewModeBtn = gtk_button_new_from_icon_name("view-grid-symbolic");
    gtk_widget_set_tooltip_text(m_viewModeBtn, "Switch to Grid View");
    g_signal_connect(m_viewModeBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* self = static_cast<PaneView*>(userData);
        if (!self) return;
        if (self->m_fileView->getViewMode() == core::ViewMode::List) {
            self->m_fileView->setViewMode(core::ViewMode::Grid);
            gtk_button_set_icon_name(GTK_BUTTON(self->m_viewModeBtn), "view-list-symbolic");
            gtk_widget_set_tooltip_text(self->m_viewModeBtn, "Switch to List View");
        } else {
            self->m_fileView->setViewMode(core::ViewMode::List);
            gtk_button_set_icon_name(GTK_BUTTON(self->m_viewModeBtn), "view-grid-symbolic");
            gtk_widget_set_tooltip_text(self->m_viewModeBtn, "Switch to Grid View");
        }
    }), this);
    gtk_box_append(GTK_BOX(m_navBar), m_viewModeBtn);

    // Show/Hide hidden files toggle
    m_hiddenBtn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(m_hiddenBtn), "view-more-symbolic");
    gtk_widget_set_tooltip_text(m_hiddenBtn, "Show Hidden Files (Ctrl+H)");
    g_signal_connect(m_hiddenBtn, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer userData) {
        auto* self = static_cast<PaneView*>(userData);
        if (!self) return;
        self->m_model->setShowHidden(gtk_toggle_button_get_active(btn) == TRUE);
    }), this);
    gtk_box_append(GTK_BOX(m_navBar), m_hiddenBtn);

    gtk_box_append(GTK_BOX(m_rootBox), m_navBar);
}

void PaneView::setupCenterArea() {
    m_centerOverlay = gtk_overlay_new();
    gtk_widget_set_vexpand(m_centerOverlay, TRUE);
    gtk_widget_set_hexpand(m_centerOverlay, TRUE);

    // Main child: FileView
    gtk_overlay_set_child(GTK_OVERLAY(m_centerOverlay), m_fileView->getWidget());

    // Spinner overlay
    m_spinner = gtk_spinner_new();
    gtk_widget_set_halign(m_spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(m_spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(m_spinner, 32, 32);
    gtk_widget_set_visible(m_spinner, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(m_centerOverlay), m_spinner);

    // Empty/Error status page overlay
    m_statusPage = adw_status_page_new();
    adw_status_page_set_title(ADW_STATUS_PAGE(m_statusPage), "Folder is Empty");
    adw_status_page_set_description(ADW_STATUS_PAGE(m_statusPage), "No files or folders in this directory.");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(m_statusPage), "folder-symbolic");
    gtk_widget_set_visible(m_statusPage, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(m_centerOverlay), m_statusPage);

    gtk_box_append(GTK_BOX(m_rootBox), m_centerOverlay);
}

void PaneView::setupStatusBar() {
    m_statusBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(m_statusBar, 10);
    gtk_widget_set_margin_end(m_statusBar, 10);
    gtk_widget_set_margin_top(m_statusBar, 4);
    gtk_widget_set_margin_bottom(m_statusBar, 4);

    m_statusLabel = gtk_label_new("0 items");
    gtk_label_set_xalign(GTK_LABEL(m_statusLabel), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(m_statusLabel), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(m_statusLabel, "dim-label");
    gtk_widget_add_css_class(m_statusLabel, "caption");
    gtk_widget_set_hexpand(m_statusLabel, TRUE);

    gtk_box_append(GTK_BOX(m_statusBar), m_statusLabel);
    gtk_box_append(GTK_BOX(m_rootBox), m_statusBar);
}

void PaneView::setupSignals() {
    m_fileView->onItemActivated = [this](DfilesFileItem* item) {
        if (!item) return;

        gboolean isDir = dfiles_file_item_is_directory(item);
        GFile* file = dfiles_file_item_get_gfile(item);

        // Check if symlink targets a directory
        if (!isDir && file && dfiles_file_item_is_symlink(item)) {
            GFileType ft = g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, nullptr);
            if (ft == G_FILE_TYPE_DIRECTORY) {
                isDir = TRUE;
            }
        }

        if (isDir) {
            if (file) {
                navigateTo(file);
            }
        } else {
            if (onFileActivated) {
                onFileActivated(item);
            }
        }
    };

    m_fileView->onSelectionChanged = [this](const std::vector<model::FileItem>& items) {
        updateSelectionStatus(items);
        if (onSelectionChanged) {
            onSelectionChanged(items);
        }
    };

    m_model->onLoadingStarted = [this]() {
        gtk_widget_set_visible(m_spinner, TRUE);
        gtk_spinner_start(GTK_SPINNER(m_spinner));
        gtk_widget_set_visible(m_statusPage, FALSE);
    };

    m_model->onLoadingFinished = [this](size_t totalItems, size_t dirCount, size_t fileCount, uint64_t totalSize) {
        gtk_spinner_stop(GTK_SPINNER(m_spinner));
        gtk_widget_set_visible(m_spinner, FALSE);

        if (totalItems == 0) {
            adw_status_page_set_title(ADW_STATUS_PAGE(m_statusPage), "Folder is Empty");
            adw_status_page_set_description(ADW_STATUS_PAGE(m_statusPage), "No files or folders in this directory.");
            adw_status_page_set_icon_name(ADW_STATUS_PAGE(m_statusPage), "folder-symbolic");
            gtk_widget_set_visible(m_statusPage, TRUE);
        } else {
            gtk_widget_set_visible(m_statusPage, FALSE);
        }

        updateStatusLabel(totalItems, dirCount, fileCount, totalSize);
    };

    m_model->onError = [this](const std::string& errorMsg) {
        gtk_spinner_stop(GTK_SPINNER(m_spinner));
        gtk_widget_set_visible(m_spinner, FALSE);
        adw_status_page_set_title(ADW_STATUS_PAGE(m_statusPage), "Unable to Open Folder");
        adw_status_page_set_description(ADW_STATUS_PAGE(m_statusPage), errorMsg.c_str());
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(m_statusPage), "dialog-error-symbolic");
        gtk_widget_set_visible(m_statusPage, TRUE);
        updateStatusLabel(0, 0, 0, 0);
    };
}

void PaneView::updateNavButtons() {
    gtk_widget_set_sensitive(m_backBtn, !m_backStack.empty());
    gtk_widget_set_sensitive(m_forwardBtn, !m_forwardStack.empty());

    if (m_currentDir) {
        g_autoptr(GFile) parent = g_file_get_parent(m_currentDir);
        gtk_widget_set_sensitive(m_upBtn, parent != nullptr);
    } else {
        gtk_widget_set_sensitive(m_upBtn, FALSE);
    }
}

void PaneView::updateStatusLabel(size_t totalItems, size_t dirCount, size_t fileCount, uint64_t totalSize) {
    if (!m_statusLabel) {
        return;
    }
    g_autofree char* formattedSize = g_format_size(totalSize);
    std::string text = std::to_string(totalItems) + " items (" +
                       std::to_string(dirCount) + " folders, " +
                       std::to_string(fileCount) + " files) — " +
                       (formattedSize ? formattedSize : "0 B");
    gtk_label_set_text(GTK_LABEL(m_statusLabel), text.c_str());
}

void PaneView::updateSelectionStatus(const std::vector<model::FileItem>& selected) {
    if (!m_statusLabel) {
        return;
    }
    if (selected.empty()) {
        if (m_model) {
            updateStatusLabel(
                m_model->getItemCount(),
                m_model->getDirectoryCount(),
                m_model->getFileCount(),
                m_model->getTotalSize()
            );
        }
        return;
    }

    uint64_t selSize = 0;
    for (const auto& item : selected) {
        selSize += item.getSize();
    }
    g_autofree char* formattedSelSize = g_format_size(selSize);
    std::string text = std::to_string(selected.size()) + " selected (" +
                       (formattedSelSize ? formattedSelSize : "0 B") + ") of " +
                       std::to_string(m_model ? m_model->getItemCount() : 0) + " items";
    gtk_label_set_text(GTK_LABEL(m_statusLabel), text.c_str());
}

} // namespace dfiles::views
