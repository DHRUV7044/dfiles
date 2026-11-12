#include "ui/sidebar.hpp"

#include <unordered_set>

namespace dfiles::ui {

PlacesSidebar::PlacesSidebar() {
    setupUI();
}

PlacesSidebar::~PlacesSidebar() {
    onPlaceSelected = nullptr;

    if (m_refreshIdleId > 0) {
        g_source_remove(m_refreshIdleId);
        m_refreshIdleId = 0;
    }

    m_drivesHeaderRow = nullptr;

    if (m_listBox && m_rowActivatedId > 0) {
        if (g_signal_handler_is_connected(m_listBox, m_rowActivatedId)) {
            g_signal_handler_disconnect(m_listBox, m_rowActivatedId);
        }
        m_rowActivatedId = 0;
    }

    if (m_volumeMonitor) {
        auto disconnectHandler = [this](gulong& id) {
            if (id > 0) {
                if (g_signal_handler_is_connected(m_volumeMonitor, id)) {
                    g_signal_handler_disconnect(m_volumeMonitor, id);
                }
                id = 0;
            }
        };

        disconnectHandler(m_mountAddedId);
        disconnectHandler(m_mountRemovedId);
        disconnectHandler(m_mountChangedId);
        disconnectHandler(m_mountPreUnmountId);
        disconnectHandler(m_volumeAddedId);
        disconnectHandler(m_volumeRemovedId);
        disconnectHandler(m_volumeChangedId);
        disconnectHandler(m_driveConnectedId);
        disconnectHandler(m_driveDisconnectedId);
        disconnectHandler(m_driveChangedId);

        g_clear_object(&m_volumeMonitor);
    }
}

GtkWidget* PlacesSidebar::getWidget() const {
    return m_container;
}

GtkListBox* PlacesSidebar::getListBox() const {
    return GTK_LIST_BOX(m_listBox);
}

void PlacesSidebar::setupUI() {
    // Outer vertical container
    m_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // Scrolled window for long lists of places and mounts
    m_scrolledWindow = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scrolledWindow),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(m_scrolledWindow, TRUE);
    gtk_widget_set_hexpand(m_scrolledWindow, TRUE);

    // List box with standard Libadwaita sidebar styling
    m_listBox = gtk_list_box_new();
    gtk_widget_add_css_class(m_listBox, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m_listBox), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(m_listBox), TRUE);

    // Row activation signal
    m_rowActivatedId = g_signal_connect(m_listBox, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* self = static_cast<PlacesSidebar*>(data);
        if (!self || !row) {
            return;
        }
        const char* uri = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "place-uri"));
        if (uri && self->onPlaceSelected) {
            self->onPlaceSelected(std::string(uri));
        }
    }), this);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scrolledWindow), m_listBox);
    gtk_box_append(GTK_BOX(m_container), m_scrolledWindow);

    // Populate standard XDG places
    addStandardPlaces();

    // Setup dynamic mount monitoring
    m_volumeMonitor = g_volume_monitor_get();
    if (m_volumeMonitor) {
        auto onMonitorChanged = +[](GVolumeMonitor*, gpointer, gpointer data) {
            auto* self = static_cast<PlacesSidebar*>(data);
            if (self) {
                self->scheduleRefreshMounts();
            }
        };

        m_mountAddedId = g_signal_connect(m_volumeMonitor, "mount-added", G_CALLBACK(onMonitorChanged), this);
        m_mountRemovedId = g_signal_connect(m_volumeMonitor, "mount-removed", G_CALLBACK(onMonitorChanged), this);
        m_mountChangedId = g_signal_connect(m_volumeMonitor, "mount-changed", G_CALLBACK(onMonitorChanged), this);
        m_mountPreUnmountId = g_signal_connect(m_volumeMonitor, "mount-pre-unmount", G_CALLBACK(onMonitorChanged), this);
        m_volumeAddedId = g_signal_connect(m_volumeMonitor, "volume-added", G_CALLBACK(onMonitorChanged), this);
        m_volumeRemovedId = g_signal_connect(m_volumeMonitor, "volume-removed", G_CALLBACK(onMonitorChanged), this);
        m_volumeChangedId = g_signal_connect(m_volumeMonitor, "volume-changed", G_CALLBACK(onMonitorChanged), this);
        m_driveConnectedId = g_signal_connect(m_volumeMonitor, "drive-connected", G_CALLBACK(onMonitorChanged), this);
        m_driveDisconnectedId = g_signal_connect(m_volumeMonitor, "drive-disconnected", G_CALLBACK(onMonitorChanged), this);
        m_driveChangedId = g_signal_connect(m_volumeMonitor, "drive-changed", G_CALLBACK(onMonitorChanged), this);

        // Initial scan of connected mounts
        refreshMounts();
    }
}

void PlacesSidebar::scheduleRefreshMounts() {
    if (m_refreshIdleId > 0) {
        return; // Already scheduled
    }
    m_refreshIdleId = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer data) -> gboolean {
        auto* self = static_cast<PlacesSidebar*>(data);
        if (self) {
            self->m_refreshIdleId = 0;
            self->refreshMounts();
        }
        return G_SOURCE_REMOVE;
    }, this, nullptr);
}

void PlacesSidebar::addStandardPlaces() {
    // 1. Home
    const char* home = g_get_home_dir();
    if (home && *home != '\0' && g_file_test(home, G_FILE_TEST_IS_DIR)) {
        g_autoptr(GError) error = nullptr;
        g_autofree char* uri = g_filename_to_uri(home, nullptr, &error);
        if (uri && g_uri_is_valid(uri, G_URI_FLAGS_NONE, nullptr)) {
            addPlaceRow("Home", uri, "user-home-symbolic");
        } else if (error) {
            g_debug("PlacesSidebar: Failed to convert home '%s' to URI: %s", home, error->message);
        }
    }

    // 2-7. XDG standard directories
    struct XdgPlace {
        const char* name;
        GUserDirectory directory;
        const char* iconName;
    };

    const XdgPlace places[] = {
        {"Desktop", G_USER_DIRECTORY_DESKTOP, "user-desktop-symbolic"},
        {"Documents", G_USER_DIRECTORY_DOCUMENTS, "folder-documents-symbolic"},
        {"Downloads", G_USER_DIRECTORY_DOWNLOAD, "folder-download-symbolic"},
        {"Music", G_USER_DIRECTORY_MUSIC, "folder-music-symbolic"},
        {"Pictures", G_USER_DIRECTORY_PICTURES, "folder-pictures-symbolic"},
        {"Videos", G_USER_DIRECTORY_VIDEOS, "folder-videos-symbolic"},
    };

    for (const auto& place : places) {
        const char* path = g_get_user_special_dir(place.directory);
        if (!path || *path == '\0') {
            continue;
        }
        if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
            continue;
        }
        // Avoid duplicate row if Desktop points to $HOME
        if (place.directory == G_USER_DIRECTORY_DESKTOP && home && g_strcmp0(path, home) == 0) {
            continue;
        }

        g_autoptr(GError) error = nullptr;
        g_autofree char* uri = g_filename_to_uri(path, nullptr, &error);
        if (uri && g_uri_is_valid(uri, G_URI_FLAGS_NONE, nullptr)) {
            addPlaceRow(place.name, uri, place.iconName);
        } else if (error) {
            g_debug("PlacesSidebar: Failed to convert '%s' to URI: %s", path, error->message);
        }
    }

    // 8. Root (/)
    if (g_file_test("/", G_FILE_TEST_IS_DIR)) {
        addPlaceRow("File System", "file:///", "drive-harddisk-root-symbolic");
    }

    // 9. Trash
    addPlaceRow("Trash", "trash:///", "user-trash-symbolic");

    // Section header for dynamic Drives/Mounts
    m_drivesHeaderRow = gtk_list_box_row_new();
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(m_drivesHeaderRow), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(m_drivesHeaderRow), FALSE);

    GtkWidget* headerLabel = gtk_label_new("Drives");
    gtk_label_set_xalign(GTK_LABEL(headerLabel), 0.0f);
    gtk_widget_add_css_class(headerLabel, "heading");
    gtk_widget_add_css_class(headerLabel, "dim-label");
    gtk_widget_set_margin_start(headerLabel, 12);
    gtk_widget_set_margin_top(headerLabel, 14);
    gtk_widget_set_margin_bottom(headerLabel, 4);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(m_drivesHeaderRow), headerLabel);
    g_object_set_data(G_OBJECT(m_drivesHeaderRow), "is-header", GINT_TO_POINTER(1));
    gtk_list_box_append(GTK_LIST_BOX(m_listBox), m_drivesHeaderRow);
}

void PlacesSidebar::addPlaceRow(const std::string& name, const std::string& uri, const std::string& iconName) {
    if (name.empty() || uri.empty()) {
        return;
    }

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    const char* actualIcon = iconName.empty() ? "folder-symbolic" : iconName.c_str();
    GtkWidget* image = gtk_image_new_from_icon_name(actualIcon);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 16);

    GtkWidget* label = gtk_label_new(name.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    g_object_set_data_full(G_OBJECT(row), "place-uri", g_strdup(uri.c_str()), g_free);
    gtk_list_box_append(GTK_LIST_BOX(m_listBox), row);
}

void PlacesSidebar::addMountRow(const std::string& name, const std::string& uri, GIcon* icon) {
    if (uri.empty()) {
        return;
    }
    const std::string displayName = name.empty() ? "Drive" : name;

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget* image = nullptr;
    if (icon) {
        image = gtk_image_new_from_gicon(icon);
    } else {
        image = gtk_image_new_from_icon_name("drive-removable-media-symbolic");
    }
    gtk_image_set_pixel_size(GTK_IMAGE(image), 16);

    GtkWidget* label = gtk_label_new(displayName.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    g_object_set_data(G_OBJECT(row), "is-mount", GINT_TO_POINTER(1));
    g_object_set_data_full(G_OBJECT(row), "place-uri", g_strdup(uri.c_str()), g_free);
    gtk_list_box_append(GTK_LIST_BOX(m_listBox), row);
}

void PlacesSidebar::refreshMounts() {
    if (!m_listBox || !m_volumeMonitor) {
        return;
    }

    // Thread safety: ensure widget modification happens strictly on the GTK main loop
    if (!g_main_context_is_owner(g_main_context_default())) {
        scheduleRefreshMounts();
        return;
    }

    if (m_refreshIdleId > 0) {
        g_source_remove(m_refreshIdleId);
        m_refreshIdleId = 0;
    }

    // REMED-4: Record the currently selected place URI before removing mount rows
    std::string previouslySelectedUri;
    GtkListBoxRow* selectedRow = gtk_list_box_get_selected_row(GTK_LIST_BOX(m_listBox));
    if (selectedRow) {
        const char* uri = static_cast<const char*>(g_object_get_data(G_OBJECT(selectedRow), "place-uri"));
        if (uri) {
            previouslySelectedUri = uri;
        }
    }

    // Remove all existing mount rows
    GtkWidget* child = gtk_widget_get_first_child(m_listBox);
    while (child != nullptr) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        if (g_object_get_data(G_OBJECT(child), "is-mount") != nullptr) {
            gtk_list_box_remove(GTK_LIST_BOX(m_listBox), child);
        }
        child = next;
    }

    // REMED-4: Track URIs to avoid duplicates, pre-seed with standard places (file:/// and $HOME)
    std::unordered_set<std::string> seenUris;
    seenUris.insert("file:///");
    seenUris.insert("file://");

    const char* homeDir = g_get_home_dir();
    if (homeDir && *homeDir != '\0') {
        g_autofree char* homeUri = g_filename_to_uri(homeDir, nullptr, nullptr);
        if (homeUri) {
            seenUris.insert(homeUri);
            std::string hStr(homeUri);
            if (hStr.back() == '/') {
                seenUris.insert(hStr.substr(0, hStr.size() - 1));
            } else {
                seenUris.insert(hStr + "/");
            }
        }
    }

    size_t dynamicMountCount = 0;

    // 1. Query active mounts
    GList* mounts = g_volume_monitor_get_mounts(m_volumeMonitor);
    for (GList* l = mounts; l != nullptr; l = l->next) {
        auto* mount = G_MOUNT(l->data);
        if (!mount) {
            continue;
        }

        g_autoptr(GVolume) volume = g_mount_get_volume(mount);

        char* rawName = g_mount_get_name(mount);
        if ((!rawName || *rawName == '\0') && volume) {
            g_free(rawName);
            rawName = g_volume_get_name(volume);
        }
        g_autofree char* name = rawName;

        GFile* rawRoot = g_mount_get_root(mount);
        if (!rawRoot && volume) {
            rawRoot = g_volume_get_activation_root(volume);
        }
        g_autoptr(GFile) root = rawRoot;
        if (!root) {
            continue;
        }

        g_autofree char* uri = g_file_get_uri(root);
        if (!uri || !g_uri_is_valid(uri, G_URI_FLAGS_NONE, nullptr)) {
            continue;
        }

        if (!seenUris.insert(uri).second) {
            continue;
        }

        GIcon* rawIcon = g_mount_get_symbolic_icon(mount);
        if (!rawIcon) {
            rawIcon = g_mount_get_icon(mount);
        }
        if (!rawIcon && volume) {
            rawIcon = g_volume_get_symbolic_icon(volume);
            if (!rawIcon) {
                rawIcon = g_volume_get_icon(volume);
            }
        }
        g_autoptr(GIcon) icon = rawIcon;

        const std::string displayName = (name && *name != '\0') ? name : "Volume";
        addMountRow(displayName, uri, icon);
        ++dynamicMountCount;
    }
    g_list_free_full(mounts, g_object_unref);

    // 2. Query connected volumes without active mount
    GList* volumes = g_volume_monitor_get_volumes(m_volumeMonitor);
    for (GList* v = volumes; v != nullptr; v = v->next) {
        auto* volume = G_VOLUME(v->data);
        if (!volume) {
            continue;
        }

        g_autoptr(GMount) mount = g_volume_get_mount(volume);
        if (mount) {
            // Already handled in mounts list
            continue;
        }

        g_autoptr(GFile) root = g_volume_get_activation_root(volume);
        if (!root) {
            continue;
        }

        g_autofree char* uri = g_file_get_uri(root);
        if (!uri || !g_uri_is_valid(uri, G_URI_FLAGS_NONE, nullptr)) {
            continue;
        }

        if (!seenUris.insert(uri).second) {
            continue;
        }

        g_autofree char* name = g_volume_get_name(volume);

        GIcon* rawIcon = g_volume_get_symbolic_icon(volume);
        if (!rawIcon) {
            rawIcon = g_volume_get_icon(volume);
        }
        g_autoptr(GIcon) icon = rawIcon;

        const std::string displayName = (name && *name != '\0') ? name : "Drive";
        addMountRow(displayName, uri, icon);
        ++dynamicMountCount;
    }
    g_list_free_full(volumes, g_object_unref);

    // REMED-4: Update "Drives" header visibility based on dynamic mounts count
    if (m_drivesHeaderRow) {
        gtk_widget_set_visible(m_drivesHeaderRow, dynamicMountCount > 0 ? TRUE : FALSE);
    }

    // REMED-4: Restore previously selected place URI if recorded
    if (!previouslySelectedUri.empty()) {
        selectPlaceByUri(previouslySelectedUri);
    }
}

void PlacesSidebar::selectPlaceByUri(const std::string& targetUri) {
    if (!m_listBox || targetUri.empty()) {
        return;
    }

    GtkWidget* child = gtk_widget_get_first_child(m_listBox);
    while (child != nullptr) {
        if (GTK_IS_LIST_BOX_ROW(child)) {
            const char* uri = static_cast<const char*>(g_object_get_data(G_OBJECT(child), "place-uri"));
            if (uri && targetUri == uri) {
                gtk_list_box_select_row(GTK_LIST_BOX(m_listBox), GTK_LIST_BOX_ROW(child));
                return;
            }
        }
        child = gtk_widget_get_next_sibling(child);
    }

    gtk_list_box_unselect_all(GTK_LIST_BOX(m_listBox));
}

} // namespace dfiles::ui
