#include "inspector/inspector_sidebar.hpp"

#include <cstring>
#include <inttypes.h>

namespace dfiles::inspector {

namespace {

GtkWidget* createPropertyRow(const char* title, GtkWidget** outLabel, bool monospace = false) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(row, 6);
    gtk_widget_set_margin_end(row, 6);
    gtk_widget_set_margin_top(row, 3);
    gtk_widget_set_margin_bottom(row, 3);

    GtkWidget* titleWidget = gtk_label_new(title);
    gtk_widget_add_css_class(titleWidget, "dim-label");
    gtk_widget_add_css_class(titleWidget, "caption");
    gtk_widget_set_size_request(titleWidget, 72, -1);
    gtk_label_set_xalign(GTK_LABEL(titleWidget), 0.0f);

    GtkWidget* valueWidget = gtk_label_new("—");
    gtk_label_set_xalign(GTK_LABEL(valueWidget), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(valueWidget), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(valueWidget), TRUE);
    gtk_widget_set_hexpand(valueWidget, TRUE);

    if (monospace) {
        gtk_widget_add_css_class(valueWidget, "monospace");
    }

    gtk_box_append(GTK_BOX(row), titleWidget);
    gtk_box_append(GTK_BOX(row), valueWidget);

    if (outLabel) {
        *outLabel = valueWidget;
    }
    return row;
}

void copyLabelToClipboard(GtkLabel* label) {
    if (!label) return;
    const char* text = gtk_label_get_text(label);
    if (!text || text[0] == '\0' || std::strcmp(text, "—") == 0 || std::strcmp(text, "Calculating...") == 0) {
        return;
    }
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GdkClipboard* clipboard = gdk_display_get_clipboard(display);
        if (clipboard) {
            gdk_clipboard_set_text(clipboard, text);
        }
    }
}

} // namespace

InspectorSidebar::InspectorSidebar()
    : m_checksumHelper(std::make_unique<ChecksumHelper>()) {
    setupUI();
    showEmptyState();
}

InspectorSidebar::~InspectorSidebar() {
    onClosed = nullptr;

    if (m_checksumHelper) {
        m_checksumHelper->cancel();
    }

    if (m_closeBtnSignalId > 0 && m_closeButton) {
        if (g_signal_handler_is_connected(m_closeButton, m_closeBtnSignalId)) {
            g_signal_handler_disconnect(m_closeButton, m_closeBtnSignalId);
        }
        m_closeBtnSignalId = 0;
    }
    if (m_calcBtnSignalId > 0 && m_calcChecksumBtn) {
        if (g_signal_handler_is_connected(m_calcChecksumBtn, m_calcBtnSignalId)) {
            g_signal_handler_disconnect(m_calcChecksumBtn, m_calcBtnSignalId);
        }
        m_calcBtnSignalId = 0;
    }
    if (m_md5CopySignalId > 0 && m_md5CopyBtn) {
        if (g_signal_handler_is_connected(m_md5CopyBtn, m_md5CopySignalId)) {
            g_signal_handler_disconnect(m_md5CopyBtn, m_md5CopySignalId);
        }
        m_md5CopySignalId = 0;
    }
    if (m_sha256CopySignalId > 0 && m_sha256CopyBtn) {
        if (g_signal_handler_is_connected(m_sha256CopyBtn, m_sha256CopySignalId)) {
            g_signal_handler_disconnect(m_sha256CopyBtn, m_sha256CopySignalId);
        }
        m_sha256CopySignalId = 0;
    }

    if (m_currentFile) {
        g_clear_object(&m_currentFile);
    }
}

void InspectorSidebar::toggleVisibility() {
    setVisible(!isVisible());
}

bool InspectorSidebar::isVisible() const {
    return gtk_widget_get_visible(m_rootBox) == TRUE;
}

void InspectorSidebar::setVisible(bool visible) {
    gtk_widget_set_visible(m_rootBox, visible ? TRUE : FALSE);
    if (!visible && m_checksumHelper) {
        m_checksumHelper->cancel();
    }
}

void InspectorSidebar::inspectItems(const std::vector<model::FileItem>& items) {
    if (items.empty()) {
        showEmptyState();
    } else if (items.size() == 1) {
        showSingleFile(items.front());
    } else {
        showMultipleFiles(items);
    }
}

void InspectorSidebar::showEmptyState() {
    if (m_checksumHelper) {
        m_checksumHelper->cancel();
    }
    if (m_currentFile) {
        g_clear_object(&m_currentFile);
    }
    if (m_previewPicture) {
        gtk_picture_set_paintable(GTK_PICTURE(m_previewPicture), nullptr);
    }
    if (m_previewIcon) {
        gtk_image_clear(GTK_IMAGE(m_previewIcon));
    }
    gtk_stack_set_visible_child_name(GTK_STACK(m_stack), "empty");
}

void InspectorSidebar::showSingleFile(const model::FileItem& item) {
    if (m_checksumHelper) {
        m_checksumHelper->cancel();
    }
    if (m_currentFile) {
        g_clear_object(&m_currentFile);
    }

    m_currentFile = item.getGFile();
    if (m_currentFile) {
        g_object_ref(m_currentFile);
    }

    // 1. Name & MIME type
    gtk_label_set_text(GTK_LABEL(m_nameLabel), item.getDisplayName().c_str());
    gtk_label_set_text(GTK_LABEL(m_typeLabel), item.getMimeType().c_str());

    // 2. Parent Location
    if (m_currentFile) {
        g_autoptr(GFile) parent = g_file_get_parent(m_currentFile);
        if (parent) {
            g_autofree char* path = g_file_get_parse_name(parent);
            gtk_label_set_text(GTK_LABEL(m_locationLabel), path ? path : "—");
        } else {
            gtk_label_set_text(GTK_LABEL(m_locationLabel), "—");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(m_locationLabel), "—");
    }

    // 3. Size
    if (item.isDirectory()) {
        gtk_label_set_text(GTK_LABEL(m_sizeLabel), "Folder");
    } else {
        std::string sizeStr = item.getFormattedSize() + " (" + std::to_string(item.getSize()) + " bytes)";
        gtk_label_set_text(GTK_LABEL(m_sizeLabel), sizeStr.c_str());
    }

    // 4. Permissions (octal + POSIX)
    uint32_t mode = item.getUnixMode();
    g_autofree char* octal = g_strdup_printf("%04o", mode & 07777);
    std::string permsStr = item.getPermissionsString() + " (" + (octal ? octal : "") + ")";
    gtk_label_set_text(GTK_LABEL(m_permsLabel), permsStr.c_str());

    // 5. Owner / Group
    GFileInfo* info = item.getGFileInfo();
    const char* owner = info ? g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_OWNER_USER) : nullptr;
    const char* group = info ? g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_OWNER_GROUP) : nullptr;
    std::string ownerStr = (owner ? owner : "—") + std::string(" / ") + (group ? group : "—");
    gtk_label_set_text(GTK_LABEL(m_ownerLabel), ownerStr.c_str());

    // 6. Modified timestamp
    gtk_label_set_text(GTK_LABEL(m_modifiedLabel), item.getFormattedTime().c_str());

    // 7. Preview image / icon
    constexpr uint64_t MAX_PREVIEW_IMAGE_SIZE = 15 * 1024 * 1024; // 15MB cap
    const std::string mime = item.getMimeType();
    if (mime.rfind("image/", 0) == 0 && m_currentFile && item.getSize() <= MAX_PREVIEW_IMAGE_SIZE) {
        gtk_picture_set_file(GTK_PICTURE(m_previewPicture), m_currentFile);
        gtk_widget_set_visible(m_previewPicture, TRUE);
        gtk_widget_set_visible(m_previewIcon, FALSE);
    } else {
        gtk_picture_set_paintable(GTK_PICTURE(m_previewPicture), nullptr);
        gtk_widget_set_visible(m_previewPicture, FALSE);
        GIcon* icon = item.getIcon();
        if (icon) {
            gtk_image_set_from_gicon(GTK_IMAGE(m_previewIcon), icon);
        }
        gtk_widget_set_visible(m_previewIcon, TRUE);
    }

    // 8. Reset Checksum Section
    const bool isRegularFile = info && (g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR);
    if (!isRegularFile || item.isDirectory()) {
        gtk_widget_set_visible(m_checksumCard, FALSE);
    } else {
        gtk_widget_set_visible(m_checksumCard, TRUE);
        gtk_spinner_stop(GTK_SPINNER(m_checksumSpinner));
        gtk_widget_set_visible(m_checksumSpinner, FALSE);
        gtk_widget_set_sensitive(m_calcChecksumBtn, TRUE);
        gtk_label_set_text(GTK_LABEL(m_md5Label), "—");
        gtk_label_set_text(GTK_LABEL(m_sha256Label), "—");
        gtk_widget_set_sensitive(m_md5CopyBtn, FALSE);
        gtk_widget_set_sensitive(m_sha256CopyBtn, FALSE);
    }

    gtk_stack_set_visible_child_name(GTK_STACK(m_stack), "single");
}

void InspectorSidebar::showMultipleFiles(const std::vector<model::FileItem>& items) {
    if (m_checksumHelper) {
        m_checksumHelper->cancel();
    }
    if (m_currentFile) {
        g_clear_object(&m_currentFile);
    }
    if (m_previewPicture) {
        gtk_picture_set_paintable(GTK_PICTURE(m_previewPicture), nullptr);
    }
    if (m_previewIcon) {
        gtk_image_clear(GTK_IMAGE(m_previewIcon));
    }

    uint64_t totalSize = 0;
    size_t dirCount = 0;
    size_t fileCount = 0;
    for (const auto& item : items) {
        if (item.isDirectory()) {
            dirCount++;
        } else {
            fileCount++;
            totalSize += item.getSize();
        }
    }

    g_autofree char* formattedSize = g_format_size(totalSize);
    std::string countStr = std::to_string(items.size()) + " items selected";
    gtk_label_set_text(GTK_LABEL(m_multiCountLabel), countStr.c_str());

    std::string sizeStr = std::string(formattedSize ? formattedSize : "0 bytes") +
                          " (" + std::to_string(totalSize) + " bytes)";
    gtk_label_set_text(GTK_LABEL(m_multiSizeLabel), sizeStr.c_str());

    std::string detailsStr = std::to_string(fileCount) + " files, " + std::to_string(dirCount) + " folders";
    gtk_label_set_text(GTK_LABEL(m_multiDetailsLabel), detailsStr.c_str());

    gtk_stack_set_visible_child_name(GTK_STACK(m_stack), "multi");
}

void InspectorSidebar::startChecksumComputation() {
    if (!m_currentFile || !m_checksumHelper) {
        return;
    }

    gtk_widget_set_visible(m_checksumSpinner, TRUE);
    gtk_spinner_start(GTK_SPINNER(m_checksumSpinner));
    gtk_widget_set_sensitive(m_calcChecksumBtn, FALSE);
    gtk_label_set_text(GTK_LABEL(m_md5Label), "Calculating...");
    gtk_label_set_text(GTK_LABEL(m_sha256Label), "Calculating...");

    m_checksumHelper->computeAsync(m_currentFile, [this](const ChecksumResult& res) {
        gtk_spinner_stop(GTK_SPINNER(m_checksumSpinner));
        gtk_widget_set_visible(m_checksumSpinner, FALSE);
        gtk_widget_set_sensitive(m_calcChecksumBtn, TRUE);

        if (res.success) {
            gtk_label_set_text(GTK_LABEL(m_md5Label), res.md5.c_str());
            gtk_label_set_text(GTK_LABEL(m_sha256Label), res.sha256.c_str());
            gtk_widget_set_sensitive(m_md5CopyBtn, TRUE);
            gtk_widget_set_sensitive(m_sha256CopyBtn, TRUE);
        } else {
            gtk_label_set_text(GTK_LABEL(m_md5Label), res.error.empty() ? "Failed" : res.error.c_str());
            gtk_label_set_text(GTK_LABEL(m_sha256Label), res.error.empty() ? "Failed" : res.error.c_str());
            gtk_widget_set_sensitive(m_md5CopyBtn, FALSE);
            gtk_widget_set_sensitive(m_sha256CopyBtn, FALSE);
        }
    });
}

void InspectorSidebar::setupUI() {
    m_rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(m_rootBox, 280, -1);
    gtk_widget_add_css_class(m_rootBox, "background");
    gtk_widget_set_visible(m_rootBox, FALSE);

    // 1. Header Toolbar
    m_headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(m_headerBox, "view");
    gtk_widget_set_margin_start(m_headerBox, 8);
    gtk_widget_set_margin_end(m_headerBox, 8);
    gtk_widget_set_margin_top(m_headerBox, 6);
    gtk_widget_set_margin_bottom(m_headerBox, 6);

    GtkWidget* titleLabel = gtk_label_new("Properties");
    gtk_widget_add_css_class(titleLabel, "heading");
    gtk_widget_set_hexpand(titleLabel, TRUE);
    gtk_label_set_xalign(GTK_LABEL(titleLabel), 0.0f);
    gtk_box_append(GTK_BOX(m_headerBox), titleLabel);

    m_closeButton = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(m_closeButton, "Hide properties (F11)");
    gtk_widget_add_css_class(m_closeButton, "flat");
    m_closeBtnSignalId = g_signal_connect_swapped(
        m_closeButton,
        "clicked",
        G_CALLBACK(+[](InspectorSidebar* self) {
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

    // 2. Scrolled Container
    m_scrolledWindow = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scrolledWindow), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(m_scrolledWindow, TRUE);
    gtk_widget_set_hexpand(m_scrolledWindow, TRUE);

    // 3. Stack View Switcher
    m_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(m_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    // Page: Empty State
    m_emptyPage = adw_status_page_new();
    adw_status_page_set_title(ADW_STATUS_PAGE(m_emptyPage), "No Selection");
    adw_status_page_set_description(ADW_STATUS_PAGE(m_emptyPage), "Select an item to view properties");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(m_emptyPage), "preferences-system-details-symbolic");
    gtk_stack_add_named(GTK_STACK(m_stack), m_emptyPage, "empty");

    // Page: Single File Details
    m_singleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(m_singleBox, 10);
    gtk_widget_set_margin_end(m_singleBox, 10);
    gtk_widget_set_margin_top(m_singleBox, 10);
    gtk_widget_set_margin_bottom(m_singleBox, 10);

    // Thumbnail Preview Frame
    GtkWidget* previewBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(previewBox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(previewBox, 4);

    m_previewPicture = gtk_picture_new();
    gtk_widget_set_size_request(m_previewPicture, 220, 140);
    gtk_picture_set_content_fit(GTK_PICTURE(m_previewPicture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_visible(m_previewPicture, FALSE);

    m_previewIcon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(m_previewIcon), 64);
    gtk_widget_set_size_request(m_previewIcon, 64, 64);

    gtk_box_append(GTK_BOX(previewBox), m_previewPicture);
    gtk_box_append(GTK_BOX(previewBox), m_previewIcon);
    gtk_box_append(GTK_BOX(m_singleBox), previewBox);

    // Details Card
    GtkWidget* detailsCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(detailsCard, "card");
    gtk_widget_set_margin_bottom(detailsCard, 6);

    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Name", &m_nameLabel));
    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Type", &m_typeLabel));
    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Location", &m_locationLabel));
    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Size", &m_sizeLabel));
    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Permissions", &m_permsLabel, true));
    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Owner", &m_ownerLabel));
    gtk_box_append(GTK_BOX(detailsCard), createPropertyRow("Modified", &m_modifiedLabel));

    gtk_box_append(GTK_BOX(m_singleBox), detailsCard);

    // Checksums Card
    m_checksumCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(m_checksumCard, "card");
    gtk_widget_set_margin_bottom(m_checksumCard, 6);

    GtkWidget* checksumHeaderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(checksumHeaderBox, 6);
    gtk_widget_set_margin_end(checksumHeaderBox, 6);
    gtk_widget_set_margin_top(checksumHeaderBox, 6);

    GtkWidget* checksumTitle = gtk_label_new("Checksums");
    gtk_widget_add_css_class(checksumTitle, "heading");
    gtk_widget_set_hexpand(checksumTitle, TRUE);
    gtk_label_set_xalign(GTK_LABEL(checksumTitle), 0.0f);
    gtk_box_append(GTK_BOX(checksumHeaderBox), checksumTitle);

    m_calcChecksumBtn = gtk_button_new_with_label("Compute");
    gtk_widget_add_css_class(m_calcChecksumBtn, "suggested-action");
    gtk_widget_add_css_class(m_calcChecksumBtn, "pill");
    m_calcBtnSignalId = g_signal_connect_swapped(
        m_calcChecksumBtn,
        "clicked",
        G_CALLBACK(+[](InspectorSidebar* self) {
            if (self) {
                self->startChecksumComputation();
            }
        }),
        this
    );
    gtk_box_append(GTK_BOX(checksumHeaderBox), m_calcChecksumBtn);

    m_checksumSpinner = gtk_spinner_new();
    gtk_widget_set_visible(m_checksumSpinner, FALSE);
    gtk_box_append(GTK_BOX(checksumHeaderBox), m_checksumSpinner);

    gtk_box_append(GTK_BOX(m_checksumCard), checksumHeaderBox);

    // MD5 Row
    GtkWidget* md5Row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(md5Row, 6);
    gtk_widget_set_margin_end(md5Row, 6);
    gtk_widget_set_margin_bottom(md5Row, 2);

    GtkWidget* md5Title = gtk_label_new("MD5");
    gtk_widget_add_css_class(md5Title, "dim-label");
    gtk_widget_add_css_class(md5Title, "caption");
    gtk_widget_set_size_request(md5Title, 56, -1);
    gtk_label_set_xalign(GTK_LABEL(md5Title), 0.0f);
    gtk_box_append(GTK_BOX(md5Row), md5Title);

    m_md5Label = gtk_label_new("—");
    gtk_widget_add_css_class(m_md5Label, "monospace");
    gtk_widget_add_css_class(m_md5Label, "caption");
    gtk_label_set_xalign(GTK_LABEL(m_md5Label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(m_md5Label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(m_md5Label), TRUE);
    gtk_widget_set_hexpand(m_md5Label, TRUE);
    gtk_box_append(GTK_BOX(md5Row), m_md5Label);

    m_md5CopyBtn = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_add_css_class(m_md5CopyBtn, "flat");
    gtk_widget_set_tooltip_text(m_md5CopyBtn, "Copy MD5");
    gtk_widget_set_sensitive(m_md5CopyBtn, FALSE);
    m_md5CopySignalId = g_signal_connect_swapped(
        m_md5CopyBtn,
        "clicked",
        G_CALLBACK(+[](InspectorSidebar* self) {
            if (self) {
                copyLabelToClipboard(GTK_LABEL(self->m_md5Label));
            }
        }),
        this
    );
    gtk_box_append(GTK_BOX(md5Row), m_md5CopyBtn);
    gtk_box_append(GTK_BOX(m_checksumCard), md5Row);

    // SHA-256 Row
    GtkWidget* shaRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(shaRow, 6);
    gtk_widget_set_margin_end(shaRow, 6);
    gtk_widget_set_margin_bottom(shaRow, 6);

    GtkWidget* shaTitle = gtk_label_new("SHA-256");
    gtk_widget_add_css_class(shaTitle, "dim-label");
    gtk_widget_add_css_class(shaTitle, "caption");
    gtk_widget_set_size_request(shaTitle, 56, -1);
    gtk_label_set_xalign(GTK_LABEL(shaTitle), 0.0f);
    gtk_box_append(GTK_BOX(shaRow), shaTitle);

    m_sha256Label = gtk_label_new("—");
    gtk_widget_add_css_class(m_sha256Label, "monospace");
    gtk_widget_add_css_class(m_sha256Label, "caption");
    gtk_label_set_xalign(GTK_LABEL(m_sha256Label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(m_sha256Label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(m_sha256Label), TRUE);
    gtk_widget_set_hexpand(m_sha256Label, TRUE);
    gtk_box_append(GTK_BOX(shaRow), m_sha256Label);

    m_sha256CopyBtn = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_add_css_class(m_sha256CopyBtn, "flat");
    gtk_widget_set_tooltip_text(m_sha256CopyBtn, "Copy SHA-256");
    gtk_widget_set_sensitive(m_sha256CopyBtn, FALSE);
    m_sha256CopySignalId = g_signal_connect_swapped(
        m_sha256CopyBtn,
        "clicked",
        G_CALLBACK(+[](InspectorSidebar* self) {
            if (self) {
                copyLabelToClipboard(GTK_LABEL(self->m_sha256Label));
            }
        }),
        this
    );
    gtk_box_append(GTK_BOX(shaRow), m_sha256CopyBtn);
    gtk_box_append(GTK_BOX(m_checksumCard), shaRow);

    gtk_box_append(GTK_BOX(m_singleBox), m_checksumCard);
    gtk_stack_add_named(GTK_STACK(m_stack), m_singleBox, "single");

    // Page: Multiple Selection
    m_multiBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(m_multiBox, 12);
    gtk_widget_set_margin_end(m_multiBox, 12);
    gtk_widget_set_margin_top(m_multiBox, 20);
    gtk_widget_set_margin_bottom(m_multiBox, 12);

    GtkWidget* multiIcon = gtk_image_new_from_icon_name("emblem-documents-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(multiIcon), 64);
    gtk_widget_set_halign(multiIcon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(m_multiBox), multiIcon);

    m_multiCountLabel = gtk_label_new("0 items selected");
    gtk_widget_add_css_class(m_multiCountLabel, "title-3");
    gtk_widget_set_halign(m_multiCountLabel, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(m_multiBox), m_multiCountLabel);

    GtkWidget* multiCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(multiCard, "card");
    gtk_widget_set_margin_top(multiCard, 8);

    gtk_box_append(GTK_BOX(multiCard), createPropertyRow("Total Size", &m_multiSizeLabel));
    gtk_box_append(GTK_BOX(multiCard), createPropertyRow("Contents", &m_multiDetailsLabel));

    gtk_box_append(GTK_BOX(m_multiBox), multiCard);
    gtk_stack_add_named(GTK_STACK(m_stack), m_multiBox, "multi");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scrolledWindow), m_stack);
    gtk_box_append(GTK_BOX(m_rootBox), m_scrolledWindow);
}

} // namespace dfiles::inspector
