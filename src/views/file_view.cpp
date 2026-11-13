#include "views/file_view.hpp"

namespace dfiles::views {

FileView::FileView() {
    setupUI();
}

FileView::~FileView() {
    onItemActivated = nullptr;
    onSelectionChanged = nullptr;

    if (m_rightClickGesture) {
        if (m_rightClickPressedId > 0 && g_signal_handler_is_connected(m_rightClickGesture, m_rightClickPressedId)) {
            g_signal_handler_disconnect(m_rightClickGesture, m_rightClickPressedId);
        }
        m_rightClickPressedId = 0;
        g_object_remove_weak_pointer(G_OBJECT(m_rightClickGesture), reinterpret_cast<gpointer*>(&m_rightClickGesture));
        if (m_scrolledWindow) {
            gtk_widget_remove_controller(m_scrolledWindow, GTK_EVENT_CONTROLLER(m_rightClickGesture));
        }
        m_rightClickGesture = nullptr;
    }
    if (m_itemPopover) {
        g_object_remove_weak_pointer(G_OBJECT(m_itemPopover), reinterpret_cast<gpointer*>(&m_itemPopover));
        if (gtk_widget_get_parent(m_itemPopover)) {
            gtk_widget_unparent(m_itemPopover);
        }
        m_itemPopover = nullptr;
    }
    if (m_bgPopover) {
        g_object_remove_weak_pointer(G_OBJECT(m_bgPopover), reinterpret_cast<gpointer*>(&m_bgPopover));
        if (gtk_widget_get_parent(m_bgPopover)) {
            gtk_widget_unparent(m_bgPopover);
        }
        m_bgPopover = nullptr;
    }

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

    if (m_scrolledWindow) {
        g_object_remove_weak_pointer(G_OBJECT(m_scrolledWindow), reinterpret_cast<gpointer*>(&m_scrolledWindow));
        g_clear_object(&m_scrolledWindow);
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
    g_object_ref_sink(m_scrolledWindow);
    g_object_add_weak_pointer(G_OBJECT(m_scrolledWindow), reinterpret_cast<gpointer*>(&m_scrolledWindow));

    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(m_scrolledWindow),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );
    gtk_widget_set_vexpand(m_scrolledWindow, TRUE);
    gtk_widget_set_hexpand(m_scrolledWindow, TRUE);

    setupColumnView();
    setupGridView();
    setupContextMenus();

    // Keyboard controller for Shift+F10 / Menu context menu accelerator
    GtkEventController* keyCtrl = gtk_event_controller_key_new();
    g_signal_connect(keyCtrl, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer userData) -> gboolean {
        auto* self = static_cast<FileView*>(userData);
        if (!self) return FALSE;

        const bool isShiftF10 = (keyval == GDK_KEY_F10 && (state & GDK_SHIFT_MASK) != 0);
        const bool isMenu = (keyval == GDK_KEY_Menu);

        if (isShiftF10 || isMenu) {
            self->showContextMenu();
            return TRUE;
        }
        return FALSE;
    }), this);
    gtk_widget_add_controller(m_scrolledWindow, keyCtrl);

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

            guint pos = gtk_list_item_get_position(item);
            g_object_set_data(G_OBJECT(box), "item_pos", GUINT_TO_POINTER(pos + 1));

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
                g_object_set_data(G_OBJECT(box), "item_pos", nullptr);
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
                guint pos = gtk_list_item_get_position(item);
                g_object_set_data(G_OBJECT(label), "item_pos", GUINT_TO_POINTER(pos + 1));
                gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_formatted_size(fileItem));
            }
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_list_item_get_child(item);
            if (label) {
                g_object_set_data(G_OBJECT(label), "item_pos", nullptr);
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
                guint pos = gtk_list_item_get_position(item);
                g_object_set_data(G_OBJECT(label), "item_pos", GUINT_TO_POINTER(pos + 1));
                gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_formatted_time(fileItem));
            }
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_list_item_get_child(item);
            if (label) {
                g_object_set_data(G_OBJECT(label), "item_pos", nullptr);
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
                guint pos = gtk_list_item_get_position(item);
                g_object_set_data(G_OBJECT(label), "item_pos", GUINT_TO_POINTER(pos + 1));
                gtk_label_set_text(GTK_LABEL(label), dfiles_file_item_get_permissions_string(fileItem));
            }
        }), nullptr);

        g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* label = gtk_list_item_get_child(item);
            if (label) {
                g_object_set_data(G_OBJECT(label), "item_pos", nullptr);
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

        guint pos = gtk_list_item_get_position(item);
        g_object_set_data(G_OBJECT(card), "item_pos", GUINT_TO_POINTER(pos + 1));

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
            g_object_set_data(G_OBJECT(card), "item_pos", nullptr);
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

void FileView::setupContextMenus() {
    // 1. Item Context Menu
    GMenu* itemMenu = g_menu_new();

    // Section 1: Navigation / Open
    GMenu* s1 = g_menu_new();
    g_menu_append(s1, "Open", "win.open-item");
    g_menu_append(s1, "Open in Terminal", "win.open-terminal");
    g_menu_append(s1, "Copy Path", "win.copy-path");
    g_menu_append_section(itemMenu, nullptr, G_MENU_MODEL(s1));
    g_object_unref(s1);

    // Section 2: Clipboard
    GMenu* s2 = g_menu_new();
    g_menu_append(s2, "Cut", "win.cut");
    g_menu_append(s2, "Copy", "win.copy");
    g_menu_append(s2, "Paste", "win.paste");
    g_menu_append_section(itemMenu, nullptr, G_MENU_MODEL(s2));
    g_object_unref(s2);

    // Section 3: File mutations
    GMenu* s3 = g_menu_new();
    g_menu_append(s3, "Rename…", "win.rename");
    g_menu_append(s3, "Move to Trash", "win.trash");
    g_menu_append(s3, "Delete Permanently", "win.delete-permanent");
    g_menu_append_section(itemMenu, nullptr, G_MENU_MODEL(s3));
    g_object_unref(s3);

    // Section 4: Properties
    GMenu* s4 = g_menu_new();
    g_menu_append(s4, "Properties", "win.toggle-inspector");
    g_menu_append_section(itemMenu, nullptr, G_MENU_MODEL(s4));
    g_object_unref(s4);

    m_itemPopover = gtk_popover_menu_new_from_model(G_MENU_MODEL(itemMenu));
    gtk_widget_set_parent(m_itemPopover, m_scrolledWindow);
    gtk_popover_set_has_arrow(GTK_POPOVER(m_itemPopover), FALSE);
    g_object_unref(itemMenu);
    g_object_add_weak_pointer(G_OBJECT(m_itemPopover), reinterpret_cast<gpointer*>(&m_itemPopover));

    // 2. Background Context Menu
    GMenu* bgMenu = g_menu_new();

    GMenu* bgs1 = g_menu_new();
    g_menu_append(bgs1, "New Folder…", "win.new-folder");
    g_menu_append(bgs1, "Paste", "win.paste");
    g_menu_append(bgs1, "Open in Terminal", "win.open-terminal");
    g_menu_append_section(bgMenu, nullptr, G_MENU_MODEL(bgs1));
    g_object_unref(bgs1);

    GMenu* bgs2 = g_menu_new();
    g_menu_append(bgs2, "Select All", "win.select-all");
    g_menu_append_section(bgMenu, nullptr, G_MENU_MODEL(bgs2));
    g_object_unref(bgs2);

    m_bgPopover = gtk_popover_menu_new_from_model(G_MENU_MODEL(bgMenu));
    gtk_widget_set_parent(m_bgPopover, m_scrolledWindow);
    gtk_popover_set_has_arrow(GTK_POPOVER(m_bgPopover), FALSE);
    g_object_unref(bgMenu);
    g_object_add_weak_pointer(G_OBJECT(m_bgPopover), reinterpret_cast<gpointer*>(&m_bgPopover));

    // 3. Right-Click Gesture
    m_rightClickGesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(m_rightClickGesture), GDK_BUTTON_SECONDARY);
    m_rightClickPressedId = g_signal_connect(
        m_rightClickGesture,
        "pressed",
        G_CALLBACK(+[](GtkGestureClick*, int, double x, double y, gpointer userData) {
            auto* self = static_cast<FileView*>(userData);
            if (self) {
                self->onRightClick(x, y);
            }
        }),
        this
    );
    gtk_widget_add_controller(m_scrolledWindow, GTK_EVENT_CONTROLLER(m_rightClickGesture));
    g_object_add_weak_pointer(G_OBJECT(m_rightClickGesture), reinterpret_cast<gpointer*>(&m_rightClickGesture));
}

void FileView::onRightClick(double x, double y) {
    if (!m_scrolledWindow || !gtk_widget_get_root(m_scrolledWindow)) {
        return;
    }
    GdkRectangle rect = {static_cast<int>(x), static_cast<int>(y), 1, 1};

    GtkWidget* picked = gtk_widget_pick(m_scrolledWindow, x, y, GTK_PICK_DEFAULT);
    GtkWidget* cur = picked;
    bool inHeader = false;
    guint itemPos = G_MAXUINT;

    while (cur && cur != m_scrolledWindow) {
        if (GTK_IS_SCROLLBAR(cur)) {
            return;
        }
        const char* tn = G_OBJECT_TYPE_NAME(cur);
        if (g_str_has_prefix(tn, "GtkColumnHeader") || g_str_has_prefix(tn, "GtkColumnViewTitle")) {
            inHeader = true;
            break;
        }
        if (itemPos == G_MAXUINT) {
            gpointer data = g_object_get_data(G_OBJECT(cur), "item_pos");
            if (data) {
                itemPos = GPOINTER_TO_UINT(data) - 1;
            }
        }
        cur = gtk_widget_get_parent(cur);
    }

    if (inHeader) {
        return;
    }

    if (itemPos != G_MAXUINT) {
        if (m_selectionModel) {
            if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(m_selectionModel), itemPos)) {
                gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_selectionModel), itemPos, TRUE);
            }
        }
        if (m_bgPopover) {
            gtk_popover_popdown(GTK_POPOVER(m_bgPopover));
        }
        if (m_itemPopover) {
            gtk_popover_set_pointing_to(GTK_POPOVER(m_itemPopover), &rect);
            gtk_popover_popup(GTK_POPOVER(m_itemPopover));
        }
    } else {
        auto selected = getSelectedItems();
        if (!selected.empty()) {
            unselectAll();
        }
        if (m_itemPopover) {
            gtk_popover_popdown(GTK_POPOVER(m_itemPopover));
        }
        if (m_bgPopover) {
            gtk_popover_set_pointing_to(GTK_POPOVER(m_bgPopover), &rect);
            gtk_popover_popup(GTK_POPOVER(m_bgPopover));
        }
    }
}

void FileView::showItemContextMenu() {
    if (!m_itemPopover || !m_scrolledWindow || !gtk_widget_get_root(m_scrolledWindow)) {
        return;
    }
    GtkNative* native = gtk_widget_get_native(m_scrolledWindow);
    if (!native) {
        return;
    }
    if (m_bgPopover) {
        gtk_popover_popdown(GTK_POPOVER(m_bgPopover));
    }
    int width = gtk_widget_get_width(m_scrolledWindow);
    int height = gtk_widget_get_height(m_scrolledWindow);
    GdkRectangle rect = {
        width > 0 ? width / 3 : 20,
        height > 0 ? std::min(height / 3, 100) : 40,
        1,
        1
    };
    gtk_popover_set_pointing_to(GTK_POPOVER(m_itemPopover), &rect);
    gtk_popover_popup(GTK_POPOVER(m_itemPopover));
}

void FileView::showContextMenu() {
    if (!m_scrolledWindow || !gtk_widget_get_root(m_scrolledWindow)) {
        return;
    }
    GtkNative* native = gtk_widget_get_native(m_scrolledWindow);
    if (!native) {
        return;
    }

    auto selected = getSelectedItems();
    if (selected.empty()) {
        if (m_selectionModel && g_list_model_get_n_items(G_LIST_MODEL(m_selectionModel)) > 0) {
            gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_selectionModel), 0, TRUE);
            selected = getSelectedItems();
        }
    }

    if (!selected.empty()) {
        showItemContextMenu();
    } else if (m_bgPopover) {
        if (m_itemPopover) {
            gtk_popover_popdown(GTK_POPOVER(m_itemPopover));
        }
        int width = gtk_widget_get_width(m_scrolledWindow);
        int height = gtk_widget_get_height(m_scrolledWindow);
        GdkRectangle rect = {
            width > 0 ? width / 2 : 20,
            height > 0 ? height / 2 : 40,
            1,
            1
        };
        gtk_popover_set_pointing_to(GTK_POPOVER(m_bgPopover), &rect);
        gtk_popover_popup(GTK_POPOVER(m_bgPopover));
    }
}

} // namespace dfiles::views
