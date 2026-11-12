#pragma once

#include "core/types.hpp"
#include "model/file_item.hpp"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>

namespace dfiles::model {

/**
 * @brief Natural alphanumeric string comparison function (e.g., "file2" < "file10").
 * Exposed for deterministic file item comparison and unit verification (REMED-5).
 */
int naturalCompare(const char* orig_s1, const char* orig_s2);

/**
 * @brief High-performance asynchronous GListModel provider with live sorting,
 * filtering, and filesystem change monitoring.
 *
 * Encapsulates:
 * - GListStore storing DfilesFileItem objects
 * - GtkFilterListModel for hidden files and search query matching
 * - GtkSortListModel for natural sorting with folders placed first
 * - Async GFileEnumerator pipeline with batching (100 items)
 * - GFileMonitor tracking created, deleted, moved, and changed files
 */
class DirectoryModel {
public:
    DirectoryModel();
    ~DirectoryModel();

    DirectoryModel(const DirectoryModel&) = delete;
    DirectoryModel& operator=(const DirectoryModel&) = delete;
    DirectoryModel(DirectoryModel&&) = delete;
    DirectoryModel& operator=(DirectoryModel&&) = delete;

    /**
     * @brief Asynchronously enumerates and loads the specified directory.
     * Cancels any ongoing load operation and updates the model in batches.
     */
    void loadDirectory(GFile* directory);

    /**
     * @brief Gets the sorted and filtered GListModel suitable for GTK4 views.
     */
    [[nodiscard]] GListModel* getModel() const noexcept;

    /**
     * @brief Gets the currently active directory.
     */
    [[nodiscard]] GFile* getCurrentDirectory() const noexcept;

    /**
     * @brief Returns true if an asynchronous directory load is in progress.
     */
    [[nodiscard]] bool isLoading() const noexcept { return m_isLoading; }

    /**
     * @brief Hidden file visibility state.
     */
    [[nodiscard]] bool getShowHidden() const noexcept { return m_showHidden; }
    void setShowHidden(bool show);

    /**
     * @brief Sorting configuration.
     */
    [[nodiscard]] core::SortColumn getSortColumn() const noexcept { return m_sortColumn; }
    [[nodiscard]] core::SortOrder getSortOrder() const noexcept { return m_sortOrder; }
    void setSorting(core::SortColumn col, core::SortOrder order);

    /**
     * @brief Search filter query string.
     */
    [[nodiscard]] std::string getSearchFilter() const { return m_searchFilter; }
    void setSearchFilter(const std::string& query);

    /**
     * @brief Item count and size statistics.
     */
    [[nodiscard]] size_t getItemCount() const;
    [[nodiscard]] size_t getDirectoryCount() const noexcept { return m_dirCount; }
    [[nodiscard]] size_t getFileCount() const noexcept { return m_fileCount; }
    [[nodiscard]] uint64_t getTotalSize() const noexcept { return m_totalSize; }

    /**
     * @brief Item retrieval helpers.
     */
    [[nodiscard]] std::shared_ptr<FileItem> getItemAt(size_t index) const;
    [[nodiscard]] std::shared_ptr<FileItem> findItemByFile(GFile* file) const;

    // Signals / Callbacks
    std::function<void()> onLoadingStarted;
    std::function<void(size_t totalItems, size_t dirCount, size_t fileCount, uint64_t totalSize)> onLoadingFinished;
    std::function<void(const std::string& errorMsg)> onError;

private:
    void setupFileMonitor();
    int findItemIndex(GFile* target) const;

    // Async enumerator handlers
    static void onEnumerateChildrenReadyTrampoline(GObject* source, GAsyncResult* res, gpointer userData);
    static void onNextFilesReadyTrampoline(GObject* source, GAsyncResult* res, gpointer userData);
    struct EnumerateContext;
    struct BatchContext;
    void handleEnumerateChildrenReady(GObject* source, GAsyncResult* res, EnumerateContext* ctx);
    void handleNextFilesReady(GObject* source, GAsyncResult* res, BatchContext* ctx);

    // GFileMonitor async handler (REMED-1)
    struct MonitorQueryContext;
    static void onMonitorQueryInfoReadyTrampoline(GObject* source, GAsyncResult* res, gpointer userData);
    void handleMonitorQueryInfoReady(GObject* source, GAsyncResult* res, MonitorQueryContext* ctx);

    // GFileMonitor handler
    static void onMonitorChangedTrampoline(
        GFileMonitor* monitor,
        GFile* file,
        GFile* otherFile,
        GFileMonitorEvent eventType,
        gpointer userData
    );
    void handleMonitorChanged(
        GFileMonitor* monitor,
        GFile* file,
        GFile* otherFile,
        GFileMonitorEvent eventType
    );

    // Filter & Sorter callbacks
    static gboolean filterFuncTrampoline(gpointer item, gpointer userData);
    static int compareFuncTrampoline(gconstpointer a, gconstpointer b, gpointer userData);
    gboolean filterItem(DfilesFileItem* item) const;
    int compareItems(DfilesFileItem* a, DfilesFileItem* b) const;

    GListStore* m_store{nullptr};
    GtkCustomFilter* m_customFilter{nullptr};
    GtkFilterListModel* m_filterModel{nullptr};
    GtkCustomSorter* m_customSorter{nullptr};
    GtkSortListModel* m_sortModel{nullptr};

    GFile* m_currentDirectory{nullptr};
    GCancellable* m_cancellable{nullptr};
    GFileMonitor* m_monitor{nullptr};
    gulong m_monitorChangedId{0};

    core::SortColumn m_sortColumn{core::SortColumn::Name};
    core::SortOrder m_sortOrder{core::SortOrder::Ascending};
    bool m_showHidden{false};
    bool m_isLoading{false};

    std::string m_searchFilter;
    char* m_searchFilterCasefolded{nullptr};

    size_t m_totalItems{0};
    size_t m_dirCount{0};
    size_t m_fileCount{0};
    uint64_t m_totalSize{0};

    std::shared_ptr<bool> m_aliveToken;
};

} // namespace dfiles::model
