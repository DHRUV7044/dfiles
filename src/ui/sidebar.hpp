#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <functional>
#include <string>

namespace dfiles::ui {

/**
 * @brief Places sidebar for quick navigation to XDG directories, root, trash,
 * and dynamically attached storage mounts.
 *
 * Encapsulates a GtkBox containing a GtkScrolledWindow with a GtkListBox
 * styled with the "navigation-sidebar" CSS class.
 */
class PlacesSidebar {
public:
    PlacesSidebar();
    ~PlacesSidebar();

    PlacesSidebar(const PlacesSidebar&) = delete;
    PlacesSidebar& operator=(const PlacesSidebar&) = delete;
    PlacesSidebar(PlacesSidebar&&) noexcept = default;
    PlacesSidebar& operator=(PlacesSidebar&&) noexcept = default;

    [[nodiscard]] GtkWidget* getWidget() const;
    [[nodiscard]] GtkListBox* getListBox() const;

    void refreshMounts();
    void scheduleRefreshMounts();
    void selectPlaceByUri(const std::string& uri);

    // Navigation callback
    std::function<void(const std::string& uri)> onPlaceSelected;

private:
    void setupUI();
    void addStandardPlaces();
    void addPlaceRow(const std::string& name, const std::string& uri, const std::string& iconName);
    void addMountRow(const std::string& name, const std::string& uri, GIcon* icon);

    GtkWidget* m_container{nullptr};
    GtkWidget* m_scrolledWindow{nullptr};
    GtkWidget* m_listBox{nullptr};
    GtkWidget* m_drivesHeaderRow{nullptr};

    GVolumeMonitor* m_volumeMonitor{nullptr};
    gulong m_mountAddedId{0};
    gulong m_mountRemovedId{0};
    gulong m_mountChangedId{0};
    gulong m_mountPreUnmountId{0};
    gulong m_volumeAddedId{0};
    gulong m_volumeRemovedId{0};
    gulong m_volumeChangedId{0};
    gulong m_driveConnectedId{0};
    gulong m_driveDisconnectedId{0};
    gulong m_driveChangedId{0};
    gulong m_rowActivatedId{0};
    guint m_refreshIdleId{0};
};

} // namespace dfiles::ui
