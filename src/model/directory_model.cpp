#include "model/directory_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace dfiles::model {

/**
 * @brief Natural alphanumeric string comparison function (e.g., "file2" < "file10").
 * Compares multi-digit number sequences numerically and characters case-insensitively.
 * Validates UTF-8 defensively to prevent buffer overruns or invalid character loops.
 */
int naturalCompare(const char* orig_s1, const char* orig_s2) {
    if (!orig_s1 && !orig_s2) return 0;
    if (!orig_s1) return -1;
    if (!orig_s2) return 1;

    const char* s1 = orig_s1;
    const char* s2 = orig_s2;

    while (*s1 && *s2) {
        if (g_ascii_isdigit(*s1) && g_ascii_isdigit(*s2)) {
            char* end1 = nullptr;
            char* end2 = nullptr;
            unsigned long long n1 = strtoull(s1, &end1, 10);
            unsigned long long n2 = strtoull(s2, &end2, 10);
            if (n1 != n2) {
                return (n1 < n2) ? -1 : 1;
            }
            if (s1 == end1 || s2 == end2) {
                break;
            }
            ptrdiff_t len1 = end1 - s1;
            ptrdiff_t len2 = end2 - s2;
            if (len1 != len2) {
                // REMED-5: Tie-break identical numeric value by digit sequence length / leading zeros
                return (len1 < len2) ? -1 : 1;
            }
            s1 = end1;
            s2 = end2;
        } else {
            gunichar c1 = g_utf8_get_char_validated(s1, -1);
            gunichar c2 = g_utf8_get_char_validated(s2, -1);

            if (c1 == static_cast<gunichar>(-1) || c1 == static_cast<gunichar>(-2) ||
                c2 == static_cast<gunichar>(-1) || c2 == static_cast<gunichar>(-2)) {
                // Defensive fallback for non-UTF8 or malformed sequences
                unsigned char b1 = static_cast<unsigned char>(*s1);
                unsigned char b2 = static_cast<unsigned char>(*s2);
                if (b1 != b2) {
                    return (b1 < b2) ? -1 : 1;
                }
                s1++;
                s2++;
                continue;
            }

            gunichar lower1 = g_unichar_tolower(c1);
            gunichar lower2 = g_unichar_tolower(c2);

            if (lower1 != lower2) {
                return (lower1 < lower2) ? -1 : 1;
            }
            s1 = g_utf8_next_char(s1);
            s2 = g_utf8_next_char(s2);
        }
    }

    if (*s1) return 1;
    if (*s2) return -1;

    // REMED-5: Fallback to byte-identical strcmp so distinct filenames never return 0
    return strcmp(orig_s1, orig_s2);
}

struct DirectoryModel::EnumerateContext {
    DirectoryModel* model{nullptr};
    GFile* targetDirectory{nullptr};
    std::weak_ptr<bool> aliveToken;

    ~EnumerateContext() {
        if (targetDirectory) {
            g_object_unref(targetDirectory);
        }
    }
};

struct DirectoryModel::BatchContext {
    DirectoryModel* model{nullptr};
    GFileEnumerator* enumerator{nullptr};
    GCancellable* cancellable{nullptr};
    GFile* targetDirectory{nullptr};
    std::weak_ptr<bool> aliveToken;

    ~BatchContext() {
        if (enumerator) {
            g_file_enumerator_close_async(enumerator, G_PRIORITY_DEFAULT, nullptr, nullptr, nullptr);
            g_clear_object(&enumerator);
        }
        if (cancellable) {
            g_clear_object(&cancellable);
        }
        if (targetDirectory) {
            g_clear_object(&targetDirectory);
        }
    }
};

struct DirectoryModel::MonitorQueryContext {
    DirectoryModel* model{nullptr};
    GFile* file{nullptr};
    GFileMonitorEvent eventType{};
    std::weak_ptr<bool> aliveToken;

    ~MonitorQueryContext() {
        if (file) {
            g_object_unref(file);
        }
    }
};

DirectoryModel::DirectoryModel() {
    m_aliveToken = std::make_shared<bool>(true);

    // 1. Underlying GListStore storing DfilesFileItem objects
    m_store = g_list_store_new(DFILES_TYPE_FILE_ITEM);

    // 2. Custom filter for hidden files and search queries
    m_customFilter = gtk_custom_filter_new(filterFuncTrampoline, this, nullptr);
    m_filterModel = gtk_filter_list_model_new(G_LIST_MODEL(m_store), GTK_FILTER(m_customFilter));
    gtk_filter_list_model_set_incremental(m_filterModel, FALSE);

    // 3. Custom sorter for columns, order, and natural sorting with folders first
    m_customSorter = gtk_custom_sorter_new(compareFuncTrampoline, this, nullptr);
    m_sortModel = gtk_sort_list_model_new(G_LIST_MODEL(m_filterModel), GTK_SORTER(m_customSorter));
    gtk_sort_list_model_set_incremental(m_sortModel, FALSE);
}

DirectoryModel::~DirectoryModel() {
    *m_aliveToken = false;

    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_clear_object(&m_cancellable);
    }

    if (m_monitor) {
        if (m_monitorChangedId > 0) {
            if (g_signal_handler_is_connected(m_monitor, m_monitorChangedId)) {
                g_signal_handler_disconnect(m_monitor, m_monitorChangedId);
            }
            m_monitorChangedId = 0;
        }
        g_file_monitor_cancel(m_monitor);
        g_clear_object(&m_monitor);
    }

    if (m_currentDirectory) {
        g_clear_object(&m_currentDirectory);
    }

    g_free(m_searchFilterCasefolded);
    m_searchFilterCasefolded = nullptr;

    // Releasing m_sortModel cascades through the GTK4 list model pipeline,
    // safely unreferencing m_filterModel, m_customSorter, m_customFilter, and m_store.
    if (m_sortModel) {
        g_clear_object(&m_sortModel);
    }
    m_filterModel = nullptr;
    m_customSorter = nullptr;
    m_customFilter = nullptr;
    m_store = nullptr;
}

GListModel* DirectoryModel::getModel() const noexcept {
    return G_LIST_MODEL(m_sortModel);
}

GFile* DirectoryModel::getCurrentDirectory() const noexcept {
    return m_currentDirectory;
}

void DirectoryModel::loadDirectory(GFile* directory) {
    if (!directory) {
        return;
    }

    // Thread safety: Ensure directory loading executes on the GTK main loop
    if (!g_main_context_is_owner(g_main_context_default())) {
        struct LoadPayload {
            DirectoryModel* model;
            GFile* dir;
            std::weak_ptr<bool> aliveToken;
        };
        auto* payload = new LoadPayload{this, G_FILE(g_object_ref(directory)), m_aliveToken};
        g_idle_add(+[](gpointer p) -> gboolean {
            auto* lp = static_cast<LoadPayload*>(p);
            if (!lp) return G_SOURCE_REMOVE;
            auto token = lp->aliveToken.lock();
            if (!token) {
                if (lp->dir) {
                    g_object_unref(lp->dir);
                }
                delete lp;
                return G_SOURCE_REMOVE;
            }
            if (lp->model && lp->dir) {
                lp->model->loadDirectory(lp->dir);
            }
            if (lp->dir) {
                g_object_unref(lp->dir);
            }
            delete lp;
            return G_SOURCE_REMOVE;
        }, payload);
        return;
    }

    // Cancel any previous in-flight operation
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_clear_object(&m_cancellable);
    }

    // Stop, disconnect, and clear previous directory monitor
    if (m_monitor) {
        if (m_monitorChangedId > 0) {
            if (g_signal_handler_is_connected(m_monitor, m_monitorChangedId)) {
                g_signal_handler_disconnect(m_monitor, m_monitorChangedId);
            }
            m_monitorChangedId = 0;
        }
        g_file_monitor_cancel(m_monitor);
        g_clear_object(&m_monitor);
    }

    m_cancellable = g_cancellable_new();

    if (m_currentDirectory) {
        g_clear_object(&m_currentDirectory);
    }
    m_currentDirectory = G_FILE(g_object_ref(directory));

    // Clear current store items
    g_list_store_remove_all(m_store);

    // Reset statistics
    m_totalItems = 0;
    m_dirCount = 0;
    m_fileCount = 0;
    m_totalSize = 0;
    m_isLoading = true;

    if (onLoadingStarted) {
        onLoadingStarted();
    }

    auto* enumCtx = new EnumerateContext{this, G_FILE(g_object_ref(directory)), m_aliveToken};

    g_file_enumerate_children_async(
        directory,
        "standard::*,time::*,unix::*",
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        G_PRIORITY_DEFAULT,
        m_cancellable,
        onEnumerateChildrenReadyTrampoline,
        enumCtx
    );
}

void DirectoryModel::onEnumerateChildrenReadyTrampoline(GObject* source, GAsyncResult* res, gpointer userData) {
    auto* ctx = static_cast<EnumerateContext*>(userData);
    if (!ctx) {
        return;
    }
    if (auto token = ctx->aliveToken.lock()) {
        ctx->model->handleEnumerateChildrenReady(source, res, ctx);
    }
    delete ctx;
}

void DirectoryModel::handleEnumerateChildrenReady(GObject* source, GAsyncResult* res, EnumerateContext* enumCtx) {
    GError* error = nullptr;
    GFileEnumerator* enumerator = g_file_enumerate_children_finish(G_FILE(source), res, &error);

    if (error) {
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_error_free(error);
            return;
        }
        std::string msg = error->message ? error->message : "Failed to load directory";
        g_error_free(error);
        m_isLoading = false;
        if (onError) {
            onError(msg);
        }
        return;
    }

    if (!enumerator) {
        m_isLoading = false;
        return;
    }

    // REMED-2: Discard batch cleanly if target directory has changed
    if (!m_currentDirectory || !enumCtx->targetDirectory || !g_file_equal(m_currentDirectory, enumCtx->targetDirectory)) {
        g_file_enumerator_close_async(enumerator, G_PRIORITY_DEFAULT, nullptr, nullptr, nullptr);
        g_object_unref(enumerator);
        return;
    }

    auto* ctx = new BatchContext();
    ctx->model = this;
    ctx->enumerator = enumerator;
    ctx->cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    ctx->targetDirectory = G_FILE(g_object_ref(enumCtx->targetDirectory));
    ctx->aliveToken = m_aliveToken;

    // Begin reading entries in batches of 100
    g_file_enumerator_next_files_async(
        enumerator,
        100,
        G_PRIORITY_DEFAULT,
        m_cancellable,
        onNextFilesReadyTrampoline,
        ctx
    );
}

void DirectoryModel::onNextFilesReadyTrampoline(GObject* source, GAsyncResult* res, gpointer userData) {
    auto* ctx = static_cast<BatchContext*>(userData);
    if (!ctx) {
        return;
    }

    if (auto token = ctx->aliveToken.lock()) {
        ctx->model->handleNextFilesReady(source, res, ctx);
    } else {
        delete ctx;
    }
}

void DirectoryModel::handleNextFilesReady(GObject* source, GAsyncResult* res, BatchContext* ctx) {
    if (g_cancellable_is_cancelled(ctx->cancellable)) {
        delete ctx;
        return;
    }

    // REMED-2: Discard batch cleanly if target directory has changed
    if (!m_currentDirectory || !ctx->targetDirectory || !g_file_equal(m_currentDirectory, ctx->targetDirectory)) {
        delete ctx;
        return;
    }

    GError* error = nullptr;
    GList* files = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source), res, &error);

    if (error) {
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_error_free(error);
            delete ctx;
            return;
        }
        std::string msg = error->message ? error->message : "Error reading directory entries";
        g_error_free(error);
        m_isLoading = false;
        delete ctx;
        if (onError) {
            onError(msg);
        }
        return;
    }

    if (files != nullptr) {
        guint rawCount = g_list_length(files);
        if (rawCount > 0) {
            auto** additions = g_new(gpointer, rawCount);
            guint validCount = 0;

            for (GList* l = files; l != nullptr; l = l->next) {
                auto* info = G_FILE_INFO(l->data);
                const char* name = g_file_info_get_name(info);
                // REMED-2: Construct child file using ctx->targetDirectory
                GFile* childFile = g_file_get_child(ctx->targetDirectory, name ? name : "");
                DfilesFileItem* item = dfiles_file_item_new(childFile, info);

                if (item) {
                    m_totalItems++;
                    if (dfiles_file_item_is_directory(item)) {
                        m_dirCount++;
                    } else {
                        m_fileCount++;
                        m_totalSize += dfiles_file_item_get_size(item);
                    }
                    additions[validCount++] = item;
                }
                g_object_unref(childFile);
            }

            if (validCount > 0) {
                guint currentStoreCount = g_list_model_get_n_items(G_LIST_MODEL(m_store));
                g_list_store_splice(m_store, currentStoreCount, 0, additions, validCount);

                for (guint j = 0; j < validCount; ++j) {
                    g_object_unref(additions[j]);
                }
            }
            g_free(additions);
        }

        g_list_free_full(files, g_object_unref);

        // Fetch subsequent batch
        g_file_enumerator_next_files_async(
            ctx->enumerator,
            100,
            G_PRIORITY_DEFAULT,
            ctx->cancellable,
            onNextFilesReadyTrampoline,
            ctx
        );
    } else {
        // EOF reached: enumeration is complete
        m_isLoading = false;
        setupFileMonitor();

        if (onLoadingFinished) {
            onLoadingFinished(m_totalItems, m_dirCount, m_fileCount, m_totalSize);
        }

        delete ctx;
    }
}

void DirectoryModel::setupFileMonitor() {
    if (m_monitor) {
        if (m_monitorChangedId > 0) {
            if (g_signal_handler_is_connected(m_monitor, m_monitorChangedId)) {
                g_signal_handler_disconnect(m_monitor, m_monitorChangedId);
            }
            m_monitorChangedId = 0;
        }
        g_file_monitor_cancel(m_monitor);
        g_clear_object(&m_monitor);
    }
    if (!m_currentDirectory) {
        return;
    }

    GError* error = nullptr;
    m_monitor = g_file_monitor_directory(
        m_currentDirectory,
        G_FILE_MONITOR_WATCH_MOVES,
        nullptr,
        &error
    );

    if (error) {
        g_clear_error(&error);
        return;
    }

    if (m_monitor) {
        m_monitorChangedId = g_signal_connect(m_monitor, "changed", G_CALLBACK(onMonitorChangedTrampoline), this);
    }
}

int DirectoryModel::findItemIndex(GFile* target) const {
    if (!m_store || !target) {
        return -1;
    }
    guint n = g_list_model_get_n_items(G_LIST_MODEL(m_store));
    for (guint i = 0; i < n; ++i) {
        auto* item = DFILES_FILE_ITEM(g_list_model_get_item(G_LIST_MODEL(m_store), i));
        if (item) {
            GFile* itemFile = dfiles_file_item_get_gfile(item);
            gboolean equal = itemFile && g_file_equal(itemFile, target);
            g_object_unref(item);
            if (equal) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void DirectoryModel::onMonitorChangedTrampoline(
    GFileMonitor* monitor,
    GFile* file,
    GFile* otherFile,
    GFileMonitorEvent eventType,
    gpointer userData
) {
    auto* self = static_cast<DirectoryModel*>(userData);
    if (!self || !self->m_aliveToken || *self->m_aliveToken == false) {
        return;
    }
    self->handleMonitorChanged(monitor, file, otherFile, eventType);
}

void DirectoryModel::onMonitorQueryInfoReadyTrampoline(GObject* source, GAsyncResult* res, gpointer userData) {
    auto* ctx = static_cast<MonitorQueryContext*>(userData);
    if (!ctx) {
        return;
    }
    if (auto token = ctx->aliveToken.lock()) {
        ctx->model->handleMonitorQueryInfoReady(source, res, ctx);
    }
    delete ctx;
}

void DirectoryModel::handleMonitorQueryInfoReady(GObject* source, GAsyncResult* res, MonitorQueryContext* ctx) {
    GError* error = nullptr;
    GFileInfo* info = g_file_query_info_finish(G_FILE(source), res, &error);
    if (error) {
        g_clear_error(&error);
        return;
    }
    if (!info || !m_store || !ctx->file) {
        if (info) g_object_unref(info);
        return;
    }

    switch (ctx->eventType) {
        case G_FILE_MONITOR_EVENT_CREATED:
        case G_FILE_MONITOR_EVENT_MOVED_IN:
        case G_FILE_MONITOR_EVENT_RENAMED:
        case G_FILE_MONITOR_EVENT_MOVED: {
            int existingIdx = findItemIndex(ctx->file);
            if (existingIdx >= 0) {
                g_object_unref(info);
                return;
            }
            DfilesFileItem* item = dfiles_file_item_new(ctx->file, info);
            if (item) {
                g_list_store_append(m_store, item);
                m_totalItems++;
                if (dfiles_file_item_is_directory(item)) {
                    m_dirCount++;
                } else {
                    m_fileCount++;
                    m_totalSize += dfiles_file_item_get_size(item);
                }
                g_object_unref(item);

                if (onLoadingFinished) {
                    onLoadingFinished(m_totalItems, m_dirCount, m_fileCount, m_totalSize);
                }
            }
            break;
        }

        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
        case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED: {
            int idx = findItemIndex(ctx->file);
            if (idx >= 0) {
                auto* oldItem = DFILES_FILE_ITEM(g_list_model_get_item(G_LIST_MODEL(m_store), idx));
                if (oldItem) {
                    if (!dfiles_file_item_is_directory(oldItem)) {
                        uint64_t sz = dfiles_file_item_get_size(oldItem);
                        m_totalSize = (m_totalSize >= sz) ? (m_totalSize - sz) : 0;
                    }
                    g_object_unref(oldItem);
                }

                DfilesFileItem* newItem = dfiles_file_item_new(ctx->file, info);
                if (newItem) {
                    if (!dfiles_file_item_is_directory(newItem)) {
                        m_totalSize += dfiles_file_item_get_size(newItem);
                    }
                    gpointer added = newItem;
                    g_list_store_splice(m_store, static_cast<guint>(idx), 1, &added, 1);
                    g_object_unref(newItem);

                    if (onLoadingFinished) {
                        onLoadingFinished(m_totalItems, m_dirCount, m_fileCount, m_totalSize);
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    g_object_unref(info);
}

void DirectoryModel::handleMonitorChanged(
    GFileMonitor* /*monitor*/,
    GFile* file,
    GFile* otherFile,
    GFileMonitorEvent eventType
) {
    if (!file || !m_store) {
        return;
    }

    switch (eventType) {
        case G_FILE_MONITOR_EVENT_CREATED:
        case G_FILE_MONITOR_EVENT_MOVED_IN: {
            int existingIdx = findItemIndex(file);
            if (existingIdx >= 0) {
                return;
            }
            auto* qCtx = new MonitorQueryContext();
            qCtx->model = this;
            qCtx->file = G_FILE(g_object_ref(file));
            qCtx->eventType = eventType;
            qCtx->aliveToken = m_aliveToken;

            g_file_query_info_async(
                file,
                "standard::*,time::*,unix::*",
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                G_PRIORITY_DEFAULT,
                m_cancellable,
                onMonitorQueryInfoReadyTrampoline,
                qCtx
            );
            break;
        }

        case G_FILE_MONITOR_EVENT_DELETED:
        case G_FILE_MONITOR_EVENT_MOVED_OUT: {
            int idx = findItemIndex(file);
            if (idx >= 0) {
                auto* item = DFILES_FILE_ITEM(g_list_model_get_item(G_LIST_MODEL(m_store), idx));
                if (item) {
                    if (dfiles_file_item_is_directory(item)) {
                        if (m_dirCount > 0) m_dirCount--;
                    } else {
                        if (m_fileCount > 0) m_fileCount--;
                        uint64_t sz = dfiles_file_item_get_size(item);
                        m_totalSize = (m_totalSize >= sz) ? (m_totalSize - sz) : 0;
                    }
                    if (m_totalItems > 0) m_totalItems--;
                    g_object_unref(item);
                }
                g_list_store_remove(m_store, static_cast<guint>(idx));
                if (onLoadingFinished) {
                    onLoadingFinished(m_totalItems, m_dirCount, m_fileCount, m_totalSize);
                }
            }
            break;
        }

        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
        case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED: {
            int idx = findItemIndex(file);
            if (idx >= 0) {
                auto* qCtx = new MonitorQueryContext();
                qCtx->model = this;
                qCtx->file = G_FILE(g_object_ref(file));
                qCtx->eventType = eventType;
                qCtx->aliveToken = m_aliveToken;

                g_file_query_info_async(
                    file,
                    "standard::*,time::*,unix::*",
                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                    G_PRIORITY_DEFAULT,
                    m_cancellable,
                    onMonitorQueryInfoReadyTrampoline,
                    qCtx
                );
            }
            break;
        }

        case G_FILE_MONITOR_EVENT_RENAMED:
        case G_FILE_MONITOR_EVENT_MOVED: {
            int idx = findItemIndex(file);
            if (idx >= 0) {
                auto* oldItem = DFILES_FILE_ITEM(g_list_model_get_item(G_LIST_MODEL(m_store), idx));
                if (oldItem) {
                    if (dfiles_file_item_is_directory(oldItem)) {
                        if (m_dirCount > 0) m_dirCount--;
                    } else {
                        if (m_fileCount > 0) m_fileCount--;
                        uint64_t sz = dfiles_file_item_get_size(oldItem);
                        m_totalSize = (m_totalSize >= sz) ? (m_totalSize - sz) : 0;
                    }
                    g_object_unref(oldItem);
                }
                g_list_store_remove(m_store, static_cast<guint>(idx));
                if (m_totalItems > 0) m_totalItems--;
                if (onLoadingFinished) {
                    onLoadingFinished(m_totalItems, m_dirCount, m_fileCount, m_totalSize);
                }
            }
            if (otherFile) {
                auto* qCtx = new MonitorQueryContext();
                qCtx->model = this;
                qCtx->file = G_FILE(g_object_ref(otherFile));
                qCtx->eventType = eventType;
                qCtx->aliveToken = m_aliveToken;

                g_file_query_info_async(
                    otherFile,
                    "standard::*,time::*,unix::*",
                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                    G_PRIORITY_DEFAULT,
                    m_cancellable,
                    onMonitorQueryInfoReadyTrampoline,
                    qCtx
                );
            }
            break;
        }

        default:
            break;
    }
}

void DirectoryModel::setShowHidden(bool show) {
    if (m_showHidden == show) {
        return;
    }
    m_showHidden = show;
    if (m_customFilter) {
        gtk_filter_changed(GTK_FILTER(m_customFilter), GTK_FILTER_CHANGE_DIFFERENT);
    }
}

void DirectoryModel::setSorting(core::SortColumn col, core::SortOrder order) {
    if (m_sortColumn == col && m_sortOrder == order) {
        return;
    }
    m_sortColumn = col;
    m_sortOrder = order;
    if (m_customSorter) {
        gtk_sorter_changed(GTK_SORTER(m_customSorter), GTK_SORTER_CHANGE_DIFFERENT);
    }
}

void DirectoryModel::setSearchFilter(const std::string& query) {
    if (m_searchFilter == query) {
        return;
    }
    m_searchFilter = query;

    g_free(m_searchFilterCasefolded);
    m_searchFilterCasefolded = query.empty() ? nullptr : g_utf8_casefold(query.c_str(), -1);

    if (m_customFilter) {
        gtk_filter_changed(GTK_FILTER(m_customFilter), GTK_FILTER_CHANGE_DIFFERENT);
    }
}

size_t DirectoryModel::getItemCount() const {
    if (m_sortModel) {
        return g_list_model_get_n_items(G_LIST_MODEL(m_sortModel));
    }
    return 0;
}

std::shared_ptr<FileItem> DirectoryModel::getItemAt(size_t index) const {
    if (!m_sortModel) {
        return nullptr;
    }
    guint n = g_list_model_get_n_items(G_LIST_MODEL(m_sortModel));
    if (index >= n) {
        return nullptr;
    }
    auto* gItem = DFILES_FILE_ITEM(g_list_model_get_item(G_LIST_MODEL(m_sortModel), static_cast<guint>(index)));
    if (!gItem) {
        return nullptr;
    }
    return std::make_shared<FileItem>(gItem, true);
}

std::shared_ptr<FileItem> DirectoryModel::findItemByFile(GFile* file) const {
    int idx = findItemIndex(file);
    if (idx < 0) {
        return nullptr;
    }
    auto* gItem = DFILES_FILE_ITEM(g_list_model_get_item(G_LIST_MODEL(m_store), static_cast<guint>(idx)));
    if (!gItem) {
        return nullptr;
    }
    return std::make_shared<FileItem>(gItem, true);
}

gboolean DirectoryModel::filterFuncTrampoline(gpointer item, gpointer userData) {
    auto* self = static_cast<DirectoryModel*>(userData);
    if (!item || !self) {
        return FALSE;
    }
    return self->filterItem(DFILES_FILE_ITEM(item));
}

gboolean DirectoryModel::filterItem(DfilesFileItem* item) const {
    // 1. Hidden file filter
    if (!m_showHidden && dfiles_file_item_is_hidden(item)) {
        return FALSE;
    }

    // 2. Query search filter
    if (m_searchFilterCasefolded) {
        const char* name = dfiles_file_item_get_display_name(item);
        if (!name) {
            return FALSE;
        }
        gchar* nameCasefold = g_utf8_casefold(name, -1);
        gboolean matches = (strstr(nameCasefold, m_searchFilterCasefolded) != nullptr);
        g_free(nameCasefold);

        if (!matches) {
            return FALSE;
        }
    }

    return TRUE;
}

int DirectoryModel::compareFuncTrampoline(gconstpointer a, gconstpointer b, gpointer userData) {
    auto* self = static_cast<DirectoryModel*>(userData);
    if (!a || !b || !self) {
        return 0;
    }
    return self->compareItems(DFILES_FILE_ITEM(const_cast<gpointer>(a)), DFILES_FILE_ITEM(const_cast<gpointer>(b)));
}

int DirectoryModel::compareItems(DfilesFileItem* a, DfilesFileItem* b) const {
    // Rule: Directories always appear before files
    gboolean aIsDir = dfiles_file_item_is_directory(a);
    gboolean bIsDir = dfiles_file_item_is_directory(b);
    if (aIsDir != bIsDir) {
        return aIsDir ? -1 : 1;
    }

    int res = 0;
    const char* nameA = dfiles_file_item_get_display_name(a);
    const char* nameB = dfiles_file_item_get_display_name(b);

    switch (m_sortColumn) {
        case core::SortColumn::Name:
            res = naturalCompare(nameA, nameB);
            break;

        case core::SortColumn::Size: {
            uint64_t sizeA = dfiles_file_item_get_size(a);
            uint64_t sizeB = dfiles_file_item_get_size(b);
            if (sizeA != sizeB) {
                res = (sizeA < sizeB) ? -1 : 1;
            } else {
                res = naturalCompare(nameA, nameB);
            }
            break;
        }

        case core::SortColumn::ModifiedTime: {
            int64_t timeA = dfiles_file_item_get_modified_time(a);
            int64_t timeB = dfiles_file_item_get_modified_time(b);
            if (timeA != timeB) {
                res = (timeA < timeB) ? -1 : 1;
            } else {
                res = naturalCompare(nameA, nameB);
            }
            break;
        }

        case core::SortColumn::Type: {
            const char* typeA = dfiles_file_item_get_mime_type(a);
            const char* typeB = dfiles_file_item_get_mime_type(b);
            res = naturalCompare(typeA, typeB);
            if (res == 0) {
                res = naturalCompare(nameA, nameB);
            }
            break;
        }
    }

    if (m_sortOrder == core::SortOrder::Descending) {
        res = -res;
    }

    return res;
}

} // namespace dfiles::model
