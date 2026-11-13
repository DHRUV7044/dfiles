#pragma once

#include <gio/gio.h>
#include <gdk/gdk.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dfiles::operations {

/**
 * @brief Progress reporting structure for file operations.
 */
struct OperationProgress {
    uint64_t currentBytes{0};
    uint64_t totalBytes{0};
    size_t currentFiles{0};
    size_t totalFiles{0};
    std::string currentFileName;
};

/**
 * @brief Asynchronous file operations manager (Copy, Move, Trash, Delete, Rename, CreateDir).
 *
 * Provides non-blocking execution for filesystem mutations, full recursive directory handling,
 * cross-filesystem fallback move, collision resolution, clipboard integration (copy/cut/paste),
 * and thread-safe UI callback dispatching.
 */
class FileOperations {
public:
    FileOperations();
    ~FileOperations();

    FileOperations(const FileOperations&) = delete;
    FileOperations& operator=(const FileOperations&) = delete;
    FileOperations(FileOperations&&) = delete;
    FileOperations& operator=(FileOperations&&) = delete;

    /**
     * @brief Asynchronously copies source files and directories to target directory.
     * @param sources Vector of source GFile objects.
     * @param targetDir Destination directory GFile.
     * @param overwrite If true, existing files are overwritten; otherwise non-colliding names are generated.
     * @param onComplete Callback invoked upon completion on the GLib main context.
     * @param onProgress Optional callback invoked with progress updates.
     */
    void copyAsync(
        const std::vector<GFile*>& sources,
        GFile* targetDir,
        bool overwrite,
        std::function<void(bool success, const std::string& msg)> onComplete,
        std::function<void(const OperationProgress& progress)> onProgress = nullptr
    );

    /**
     * @brief Asynchronously moves source files and directories to target directory.
     * Falls back to copy + delete on cross-filesystem moves.
     */
    void moveAsync(
        const std::vector<GFile*>& sources,
        GFile* targetDir,
        bool overwrite,
        std::function<void(bool success, const std::string& msg)> onComplete
    );

    /**
     * @brief Asynchronously moves files or directories to user Trash (XDG Trash spec).
     */
    void trashAsync(
        const std::vector<GFile*>& files,
        std::function<void(bool success, const std::string& msg)> onComplete
    );

    /**
     * @brief Asynchronously permanently deletes files or directories recursively.
     */
    void deleteAsync(
        const std::vector<GFile*>& files,
        std::function<void(bool success, const std::string& msg)> onComplete
    );

    /**
     * @brief Asynchronously renames a file or directory.
     */
    void renameAsync(
        GFile* file,
        const std::string& newName,
        std::function<void(bool success, const std::string& msg)> onComplete
    );

    /**
     * @brief Asynchronously creates a new directory inside parentDir.
     */
    void createDirectoryAsync(
        GFile* parentDir,
        const std::string& dirName,
        std::function<void(bool success, const std::string& msg)> onComplete
    );

    /**
     * @brief Cancels current in-flight operation.
     */
    void cancelCurrent();

    /**
     * @brief Returns true if a background file operation is active.
     */
    [[nodiscard]] bool isRunning() const noexcept { return m_running && m_running->load(); }

    // Clipboard Integration
    void copyToClipboard(const std::vector<GFile*>& files, bool isCut);
    void pasteFromClipboard(GFile* targetDir, std::function<void(bool success, const std::string& msg)> onComplete);
    [[nodiscard]] bool canPaste() const;
    [[nodiscard]] bool isClipboardCut() const noexcept { return m_clipboardIsCut; }

    /**
     * @brief Resolves collisions by generating "name (copy N).ext" format if target exists.
     */
    static std::string getNonCollidingBasename(GFile* targetDir, const std::string& originalName);

private:
    static void splitBasename(const std::string& filename, std::string& stem, std::string& ext);

    static bool copyRecursive(
        GFile* src,
        GFile* dst,
        bool overwrite,
        GCancellable* cancellable,
        const std::shared_ptr<std::atomic<bool>>& aliveToken,
        std::function<void(const OperationProgress&)> onProgress,
        OperationProgress& progress,
        std::string& errorMsg,
        std::vector<GFile*>* createdFilesTracker = nullptr
    );

    static bool deleteRecursive(
        GFile* target,
        GCancellable* cancellable,
        const std::shared_ptr<std::atomic<bool>>& aliveToken,
        std::string& errorMsg
    );

    static uint64_t calculateTotalBytes(
        const std::vector<GFile*>& sources,
        size_t& totalFiles,
        GCancellable* cancellable
    );

    void initClipboardMonitoring();

    GCancellable* m_cancellable{nullptr};
    std::shared_ptr<std::atomic<bool>> m_aliveToken;
    std::jthread m_workerThread;
    std::shared_ptr<std::atomic<bool>> m_running;

    GdkClipboard* m_clipboard{nullptr};
    gulong m_clipboardChangedId{0};
    bool m_isSettingClipboard{false};
    std::vector<std::string> m_clipboardUris;
    bool m_clipboardIsCut{false};
};

} // namespace dfiles::operations
