#pragma once

#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "inspector/checksum_helper.hpp"
#include "model/file_item.hpp"

namespace dfiles::inspector {

/**
 * @brief Live file inspection and metadata sidebar (toggled via F11).
 *
 * Displays detailed file properties (name, size, MIME type, permissions, timestamps),
 * live thumbnail/icon preview, multiple selection summary, and asynchronous MD5/SHA-256
 * checksum computation.
 */
class InspectorSidebar {
public:
    explicit InspectorSidebar();
    ~InspectorSidebar();

    InspectorSidebar(const InspectorSidebar&) = delete;
    InspectorSidebar& operator=(const InspectorSidebar&) = delete;
    InspectorSidebar(InspectorSidebar&&) = delete;
    InspectorSidebar& operator=(InspectorSidebar&&) = delete;

    /**
     * @brief Access the root widget container (vertical GtkBox) for window layout embedding.
     */
    [[nodiscard]] GtkWidget* getWidget() const noexcept { return m_rootBox; }

    /**
     * @brief Toggle sidebar visibility.
     */
    void toggleVisibility();

    /**
     * @brief Returns true if the inspector sidebar is currently visible.
     */
    [[nodiscard]] bool isVisible() const;

    /**
     * @brief Explicitly show or hide the inspector sidebar.
     */
    void setVisible(bool visible);

    /**
     * @brief Update the inspector sidebar with the current file selection.
     * @param items List of selected FileItems from the active pane.
     */
    void inspectItems(const std::vector<model::FileItem>& items);

    /**
     * @brief Callback invoked when the user clicks the close button in the sidebar header.
     */
    std::function<void()> onClosed;

private:
    void setupUI();
    void showEmptyState();
    void showSingleFile(const model::FileItem& item);
    void showMultipleFiles(const std::vector<model::FileItem>& items);
    void startChecksumComputation();

    GtkWidget* m_rootBox{nullptr};
    GtkWidget* m_headerBox{nullptr};
    GtkWidget* m_scrolledWindow{nullptr};
    GtkWidget* m_stack{nullptr};

    // Pages
    GtkWidget* m_emptyPage{nullptr};
    GtkWidget* m_singleBox{nullptr};
    GtkWidget* m_multiBox{nullptr};

    // Single item preview & details
    GtkWidget* m_previewPicture{nullptr};
    GtkWidget* m_previewIcon{nullptr};
    GtkWidget* m_nameLabel{nullptr};
    GtkWidget* m_typeLabel{nullptr};
    GtkWidget* m_locationLabel{nullptr};
    GtkWidget* m_sizeLabel{nullptr};
    GtkWidget* m_permsLabel{nullptr};
    GtkWidget* m_ownerLabel{nullptr};
    GtkWidget* m_modifiedLabel{nullptr};

    // Checksums
    GtkWidget* m_checksumCard{nullptr};
    GtkWidget* m_calcChecksumBtn{nullptr};
    GtkWidget* m_checksumSpinner{nullptr};
    GtkWidget* m_md5Label{nullptr};
    GtkWidget* m_sha256Label{nullptr};
    GtkWidget* m_md5CopyBtn{nullptr};
    GtkWidget* m_sha256CopyBtn{nullptr};

    // Multi-selection summary
    GtkWidget* m_multiCountLabel{nullptr};
    GtkWidget* m_multiSizeLabel{nullptr};
    GtkWidget* m_multiDetailsLabel{nullptr};

    GtkWidget* m_closeButton{nullptr};
    gulong m_closeBtnSignalId{0};
    gulong m_calcBtnSignalId{0};
    gulong m_md5CopySignalId{0};
    gulong m_sha256CopySignalId{0};

    GFile* m_currentFile{nullptr};
    std::unique_ptr<ChecksumHelper> m_checksumHelper;
};

} // namespace dfiles::inspector
