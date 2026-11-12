#include "views/file_view.hpp"

namespace dfiles::views {

FileView::FileView() {
    setupUI();
}

FileView::~FileView() {
    onItemActivated = nullptr;
    onSelectionChanged = nullptr;

    if (m_columnView) {
        if (m_colActivateId > 0) {
            if (g_signal_handler_is_connected(m_columnView, m_colActivateId)) {
                g_signal_handler_disconnect(m_columnView, m_colActivateId);
            }
            m_colActivateId = 0;
        }
        GtkSorter* viewSorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(m_columnView));
        if (viewSorter && m_sorterChangedId > 0) {
            if (g_signal_handler_is_connected(viewSorter, m_sorterChangedId)) {
                g_signal_handler_disconnect(viewSorter, m_sorterChangedId);
            }
            m_sorterChangedId = 0;
        }
        gtk_column_view_set_model(GTK_COLUMN_VIEW(m_columnView), nullptr);
        g_clear_object(&m_columnView);
    }

    if (m_gridView) {
        if (m_gridActivateId > 0) {
            if (g_signal_handler_is_connected(m_gridView, m_gridActivateId)) {
                g_signal_handler_disconnect(m_gridView, m_gridActivateId);
            }
            m_gridActivateId = 0;
        }
        gtk_grid_view_set_model(GTK_GRID_VIEW(m_gridView), nullptr);
        g_clear_object(&m_gridView);
    }

    if (m_selectionModel) {
        if (m_selectionChangedHandlerId > 0) {
            if (g_signal_handler_is_connected(m_selectionModel, m_selectionChangedHandlerId)) {
                g_signal_handler_disconnect(m_selectionModel, m_selectionChangedHandlerId);
            }
            m_selectionChangedHandlerId = 0;
        }
        g_clear_object(&m_selectionModel);
    }
}

void FileView::grabFocus() {
    GtkWidget* target = (m_mode == core::ViewMode::List) ? m_columnView : m_gridView;
    if (target) {
        gtk_widget_grab_focus(target);
    } else if (m_scrolledWindow) {
        gtk_widget_grab_focus(m_scrolledWindow);
    }
}

GtkWidget* FileView::getWidget() const noexcept {
    return m_scrolledWindow;
}

void FileView::setViewMode(core::ViewMode mode) {
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;

    if (m_mode == core::ViewMode::List) {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scrolledWindow), m_columnView);
    } else {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scrolledWindow), m_gridView);
    }
}

void FileView::setModel(model::DirectoryModel* model) {
    if (m_selectionModel) {
        if (m_selectionChangedHandlerId > 0) {
            if (g_signal_handler_is_connected(m_selectionModel, m_selectionChangedHandlerId)) {
                g_signal_handler_disconnect(m_selectionModel, m_selectionChangedHandlerId);
            }
            m_selectionChangedHandlerId = 0;
        }
        g_clear_object(&m_selectionModel);
    }

    m_model = model;

    if (m_model) {
        GListModel* gmodel = m_model->getModel();
        // gtk_multi_selection_new transfers full ownership (consumes one reference of model).
        // Passing g_object_ref(gmodel) preserves DirectoryModel's ownership of m_sortModel.
        m_selectionModel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(gmodel)));
        m_selectionChangedHandlerId = g_signal_connect(
            m_selectionModel,
            "selection-changed",
            G_CALLBACK(onSelectionChangedTrampoline),
            this
        );

        gtk_column_view_set_model(GTK_COLUMN_VIEW(m_columnView), GTK_SELECTION_MODEL(m_selectionModel));
        gtk_grid_view_set_model(GTK_GRID_VIEW(m_gridView), GTK_SELECTION_MODEL(m_selectionModel));
    } else {
        gtk_column_view_set_model(GTK_COLUMN_VIEW(m_columnView), nullptr);
        gtk_grid_view_set_model(GTK_GRID_VIEW(m_gridView), nullptr);
    }
}

std::vector<model::FileItem> FileView::getSelectedItems() const {
    std::vector<model::FileItem> result;
    if (!m_selectionModel) {
        return result;
    }

    GtkBitset* selection = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(m_selectionModel));
    if (!selection) {
        return result;
    }

    GtkBitsetIter iter;
    guint index = 0;
    if (gtk_bitset_iter_init_first(&iter, selection, &index)) {
        do {
            g_autoptr(GObject) obj = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(m_selectionModel), index));
            if (obj && DFILES_IS_FILE_ITEM(obj)) {
                result.emplace_back(DFILES_FILE_ITEM(obj));
            }
        } while (gtk_bitset_iter_next(&iter, &index));
    }

    gtk_bitset_unref(selection);
    return result;
}

void FileView::selectAll() {
    if (m_selectionModel) {
        gtk_selection_model_select_all(GTK_SELECTION_MODEL(m_selectionModel));
    }
}

void FileView::unselectAll() {
    if (m_selectionModel) {
        gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m_selectionModel));
    }
}

void FileView::setupUI() {
    m_scrolledWindow = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(m_scrolledWindow),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );
    gtk_widget_set_vexpand(m_scrolledWindow, TRUE);
    gtk_widget_set_hexpand(m_scrolledWindow, TRUE);

    setupColumnView();
    setupGridView();

    // Default to List View
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scrolledWindow), m_columnView);
}

void FileView::setupColumnView() {
    m_columnView = gtk_column_view_new(nullptr);
    g_object_ref_sink(m_columnView);

    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(m_columnView), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(m_columnView), FALSE);
    gtk_column_view_set_enable_rubberband(GTK_COLUMN_VIEW(m_columnView), TRUE);

    m_colActivateId = g_signal_connect(m_columnView, "activate", G_CALLBACK(onItemActivatedTrampoline), this);

    // 1. Column: Icon + Name
    {
        GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_margin_start(box, 6);
            gtk_widget_set_margin_end(box, 6);
            gtk_widget_set_margin_top(box, 4);
            gtk_widget_set_margin_bottom(box, 4);

            GtkWidget* image = gtk_image_new();
            gtk_image_set_pixel_size(GTK_IMAGE(image), 16);

            GtkWidget* label = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_widget_set_hexpand(label, TRUE);

            gtk_box_append(GTK_BOX(box), image);
            gtk_box_append(GTK_BOX(box), label);
            gtk_list_item_set_child(item, box);
        }), nullptr);

        g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            DfilesFileItem* fileItem = DFILES_FILE_ITEM(gtk_list_item_get_item(item));
            GtkWidget* box = gtk_list_item_get_child(item);
            if (!fileItem || !box) return;

            GtkWidget* image = gtk_widget_get_first_child(box);
            GtkWidget* label = gtk_widget_get_next_sibling(image);

            GIcon* icon = dfiles_file_item_get_icon(fileItem);
            if (icon) {
                gtk_image_set_from_gicon(GTK_IMAGE(image), icon);
            } else {
                gtk_image_clear(GTK_IMAGE(image));
            }
            gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_display_name(fileItem));
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* box = gtk_list_item_get_child(item);
            if (box) {
                GtkWidget* image = gtk_widget_get_first_child(box);
                GtkWidget* label = gtk_widget_get_next_sibling(image);
                if (image) {
                    gtk_image_clear(GTK_IMAGE(image));
                }
                if (label) {
                    gtk_label_set_text(GTK_LABEL(label), "");
                }
            }
        }), nullptr);

        g_signal_connect(factory, "teardown", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            gtk_list_item_set_child(item, nullptr);
        }), nullptr);

        GtkColumnViewColumn* col = gtk_column_view_column_new("Name", GTK_LIST_ITEM_FACTORY(g_object_ref(factory)));
        g_object_unref(factory);
        GtkCustomSorter* nameSorter = gtk_custom_sorter_new(+[](gconstpointer a, gconstpointer b, gpointer) -> int {
            auto* itemA = DFILES_FILE_ITEM(const_cast<gpointer>(a));
            auto* itemB = DFILES_FILE_ITEM(const_cast<gpointer>(b));
            if (!itemA || !itemB) return 0;
            const char* nameA = dfiles_file_item_get_name(itemA);
            const char* nameB = dfiles_file_item_get_name(itemB);
            return g_strcmp0(nameA, nameB);
        }, nullptr, nullptr);
        gtk_column_view_column_set_sorter(col, GTK_SORTER(nameSorter));
        g_object_unref(nameSorter);
        m_colName = col;

        gtk_column_view_column_set_expand(col, TRUE);
        gtk_column_view_column_set_resizable(col, TRUE);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(m_columnView), col);
        g_object_unref(col);
    }

    // 2. Column: Size
    {
        GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(label), 1.0f);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_widget_set_margin_start(label, 6);
            gtk_widget_set_margin_end(label, 12);
            gtk_widget_add_css_class(label, "numeric");
            gtk_list_item_set_child(item, label);
        }), nullptr);

        g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            DfilesFileItem* fileItem = DFILES_FILE_ITEM(gtk_list_item_get_item(item));
            GtkWidget* label = gtk_list_item_get_child(item);
            if (fileItem && label) {
                gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_formatted_size(fileItem));
            }
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_list_item_get_child(item);
            if (label) {
                gtk_label_set_text(GTK_LABEL(label), "");
            }
        }), nullptr);

        g_signal_connect(factory, "teardown", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            gtk_list_item_set_child(item, nullptr);
        }), nullptr);

        GtkColumnViewColumn* col = gtk_column_view_column_new("Size", GTK_LIST_ITEM_FACTORY(g_object_ref(factory)));
        g_object_unref(factory);
        GtkCustomSorter* sizeSorter = gtk_custom_sorter_new(+[](gconstpointer a, gconstpointer b, gpointer) -> int {
            auto* itemA = DFILES_FILE_ITEM(const_cast<gpointer>(a));
            auto* itemB = DFILES_FILE_ITEM(const_cast<gpointer>(b));
            if (!itemA || !itemB) return 0;
            uint64_t sA = dfiles_file_item_get_size(itemA);
            uint64_t sB = dfiles_file_item_get_size(itemB);
            if (sA < sB) return -1;
            if (sA > sB) return 1;
            return 0;
        }, nullptr, nullptr);
        gtk_column_view_column_set_sorter(col, GTK_SORTER(sizeSorter));
        g_object_unref(sizeSorter);
        m_colSize = col;

        gtk_column_view_column_set_expand(col, FALSE);
        gtk_column_view_column_set_resizable(col, TRUE);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(m_columnView), col);
        g_object_unref(col);
    }

    // 3. Column: Modified Date
    {
        GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_set_margin_start(label, 6);
            gtk_widget_set_margin_end(label, 6);
            gtk_widget_add_css_class(label, "dim-label");
            gtk_list_item_set_child(item, label);
        }), nullptr);

        g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            DfilesFileItem* fileItem = DFILES_FILE_ITEM(gtk_list_item_get_item(item));
            GtkWidget* label = gtk_list_item_get_child(item);
            if (fileItem && label) {
                gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_formatted_time(fileItem));
            }
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_list_item_get_child(item);
            if (label) {
                gtk_label_set_text(GTK_LABEL(label), "");
            }
        }), nullptr);

        g_signal_connect(factory, "teardown", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            gtk_list_item_set_child(item, nullptr);
        }), nullptr);

        GtkColumnViewColumn* col = gtk_column_view_column_new("Modified", GTK_LIST_ITEM_FACTORY(g_object_ref(factory)));
        g_object_unref(factory);
        GtkCustomSorter* modSorter = gtk_custom_sorter_new(+[](gconstpointer a, gconstpointer b, gpointer) -> int {
            auto* itemA = DFILES_FILE_ITEM(const_cast<gpointer>(a));
            auto* itemB = DFILES_FILE_ITEM(const_cast<gpointer>(b));
            if (!itemA || !itemB) return 0;
            int64_t tA = dfiles_file_item_get_modified_time(itemA);
            int64_t tB = dfiles_file_item_get_modified_time(itemB);
            if (tA < tB) return -1;
            if (tA > tB) return 1;
            return 0;
        }, nullptr, nullptr);
        gtk_column_view_column_set_sorter(col, GTK_SORTER(modSorter));
        g_object_unref(modSorter);
        m_colModified = col;

        gtk_column_view_column_set_expand(col, FALSE);
        gtk_column_view_column_set_resizable(col, TRUE);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(m_columnView), col);
        g_object_unref(col);
    }

    // 4. Column: Permissions
    {
        GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_set_margin_start(label, 6);
            gtk_widget_set_margin_end(label, 6);
            gtk_widget_add_css_class(label, "monospace");
            gtk_widget_add_css_class(label, "dim-label");
            gtk_list_item_set_child(item, label);
        }), nullptr);

        g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            DfilesFileItem* fileItem = DFILES_FILE_ITEM(gtk_list_item_get_item(item));
            GtkWidget* label = gtk_list_item_get_child(item);
            if (fileItem && label) {
                gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_permissions_string(fileItem));
            }
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_list_item_get_child(item);
            if (label) {
                gtk_label_set_text(GTK_LABEL(label), "");
            }
        }), nullptr);

        g_signal_connect(factory, "teardown", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            gtk_list_item_set_child(item, nullptr);
        }), nullptr);

        GtkColumnViewColumn* col = gtk_column_view_column_new("Permissions", GTK_LIST_ITEM_FACTORY(g_object_ref(factory)));
        g_object_unref(factory);
        GtkCustomSorter* permSorter = gtk_custom_sorter_new(+[](gconstpointer a, gconstpointer b, gpointer) -> int {
            auto* itemA = DFILES_FILE_ITEM(const_cast<gpointer>(a));
            auto* itemB = DFILES_FILE_ITEM(const_cast<gpointer>(b));
            if (!itemA || !itemB) return 0;
            uint32_t mA = dfiles_file_item_get_unix_mode(itemA);
            uint32_t mB = dfiles_file_item_get_unix_mode(itemB);
            if (mA < mB) return -1;
            if (mA > mB) return 1;
            return 0;
        }, nullptr, nullptr);
        gtk_column_view_column_set_sorter(col, GTK_SORTER(permSorter));
        g_object_unref(permSorter);
        m_colPermissions = col;

        gtk_column_view_column_set_expand(col, FALSE);
        gtk_column_view_column_set_resizable(col, TRUE);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(m_columnView), col);
        g_object_unref(col);
    }

    GtkSorter* viewSorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(m_columnView));
    if (viewSorter) {
        m_sorterChangedId = g_signal_connect(viewSorter, "changed", G_CALLBACK(onSorterChangedTrampoline), this);
    }
}

void FileView::setupGridView() {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();

    g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_size_request(card, 110, 100);
        gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_start(card, 6);
        gtk_widget_set_margin_end(card, 6);
        gtk_widget_set_margin_top(card, 6);
        gtk_widget_set_margin_bottom(card, 6);

        GtkWidget* image = gtk_image_new();
        gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
        gtk_widget_set_halign(image, GTK_ALIGN_CENTER);

        GtkWidget* label = gtk_label_new(nullptr);
        gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_lines(GTK_LABEL(label), 2);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 14);

        gtk_box_append(GTK_BOX(card), image);
        gtk_box_append(GTK_BOX(card), label);
        gtk_list_item_set_child(item, card);
    }), nullptr);

    g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        DfilesFileItem* fileItem = DFILES_FILE_ITEM(gtk_list_item_get_item(item));
        GtkWidget* card = gtk_list_item_get_child(item);
        if (!fileItem || !card) return;

        GtkWidget* image = gtk_widget_get_first_child(card);
        GtkWidget* label = gtk_widget_get_next_sibling(image);

        GIcon* icon = dfiles_file_item_get_icon(fileItem);
        if (icon) {
            gtk_image_set_from_gicon(GTK_IMAGE(image), icon);
        } else {
            gtk_image_clear(GTK_IMAGE(image));
        }
        gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_display_name(fileItem));
    }), nullptr);

    g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        GtkWidget* card = gtk_list_item_get_child(item);
        if (card) {
            GtkWidget* image = gtk_widget_get_first_child(card);
            GtkWidget* label = gtk_widget_get_next_sibling(image);
            if (image) {
                gtk_image_clear(GTK_IMAGE(image));
            }
            if (label) {
                gtk_label_set_text(GTK_LABEL(label), "");
            }
        }
    }), nullptr);

    g_signal_connect(factory, "teardown", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        gtk_list_item_set_child(item, nullptr);
    }), nullptr);

    m_gridView = gtk_grid_view_new(nullptr, GTK_LIST_ITEM_FACTORY(g_object_ref(factory)));
    g_object_unref(factory);
    g_object_ref_sink(m_gridView);

    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(m_gridView), 12);
    gtk_grid_view_set_min_columns(GTK_GRID_VIEW(m_gridView), 2);
    gtk_grid_view_set_enable_rubberband(GTK_GRID_VIEW(m_gridView), TRUE);

    m_gridActivateId = g_signal_connect(m_gridView, "activate", G_CALLBACK(onItemActivatedTrampoline), this);
}

void FileView::onItemActivatedTrampoline(GtkWidget* /*widget*/, guint position, gpointer userData) {
    auto* self = static_cast<FileView*>(userData);
    if (!self || !self->m_selectionModel) {
        return;
    }

    g_autoptr(GObject) item = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(self->m_selectionModel), position));
    if (item && DFILES_IS_FILE_ITEM(item) && self->onItemActivated) {
        self->onItemActivated(DFILES_FILE_ITEM(item));
    }
}

void FileView::onSelectionChangedTrampoline(GtkSelectionModel* /*model*/, guint /*position*/, guint /*nItems*/, gpointer userData) {
    auto* self = static_cast<FileView*>(userData);
    if (!self || !self->onSelectionChanged) {
        return;
    }

    auto items = self->getSelectedItems();
    self->onSelectionChanged(items);
}

void FileView::onSorterChangedTrampoline(GtkSorter* sorter, int /*change*/, gpointer userData) {
    auto* self = static_cast<FileView*>(userData);
    if (!self || !self->m_model || !GTK_IS_COLUMN_VIEW_SORTER(sorter)) {
        return;
    }
    auto* cvSorter = GTK_COLUMN_VIEW_SORTER(sorter);
    GtkColumnViewColumn* primaryCol = gtk_column_view_sorter_get_primary_sort_column(cvSorter);
    if (!primaryCol) {
        return;
    }
    GtkSortType orderType = gtk_column_view_sorter_get_primary_sort_order(cvSorter);
    core::SortOrder order = (orderType == GTK_SORT_ASCENDING) ? core::SortOrder::Ascending : core::SortOrder::Descending;

    core::SortColumn col = core::SortColumn::Name;
    if (primaryCol == self->m_colName) {
        col = core::SortColumn::Name;
    } else if (primaryCol == self->m_colSize) {
        col = core::SortColumn::Size;
    } else if (primaryCol == self->m_colModified) {
        col = core::SortColumn::ModifiedTime;
    } else if (primaryCol == self->m_colPermissions) {
        col = core::SortColumn::Type;
    }
    self->m_model->setSorting(col, order);
}

} // namespace dfiles::views
