#include "operations/file_ops.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <unordered_set>

namespace dfiles::operations {

namespace {

struct CompletionDispatch {
    std::function<void(bool, const std::string&)> callback;
    bool success{false};
    std::string message;
    std::weak_ptr<std::atomic<bool>> aliveToken;
    std::shared_ptr<std::atomic<bool>> runningFlag;
};

struct ProgressDispatch {
    std::function<void(const OperationProgress&)> callback;
    OperationProgress progress;
    std::weak_ptr<std::atomic<bool>> aliveToken;
};

bool fileOrLinkExists(GFile* file) {
    if (!file) return false;
    g_autoptr(GFileInfo) info = g_file_query_info(
        file,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        nullptr,
        nullptr
    );
    return (info != nullptr);
}

} // anonymous namespace

FileOperations::FileOperations()
    : m_aliveToken(std::make_shared<std::atomic<bool>>(true)),
      m_running(std::make_shared<std::atomic<bool>>(false)) {
    initClipboardMonitoring();
}

FileOperations::~FileOperations() {
    cancelCurrent();
    if (m_clipboard) {
        if (m_clipboardChangedId > 0 && g_signal_handler_is_connected(m_clipboard, m_clipboardChangedId)) {
            g_signal_handler_disconnect(m_clipboard, m_clipboardChangedId);
        }
        m_clipboardChangedId = 0;
        g_object_remove_weak_pointer(G_OBJECT(m_clipboard), reinterpret_cast<gpointer*>(&m_clipboard));
        m_clipboard = nullptr;
    }
}

void FileOperations::initClipboardMonitoring() {
    if (m_clipboard) {
        return;
    }
    GdkDisplay* display = gdk_display_get_default();
    if (!display) {
        return;
    }
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    if (!clipboard) {
        return;
    }
    m_clipboard = clipboard;
    g_object_add_weak_pointer(G_OBJECT(m_clipboard), reinterpret_cast<gpointer*>(&m_clipboard));
    m_clipboardChangedId = g_signal_connect(
        m_clipboard,
        "changed",
        G_CALLBACK(+[](GdkClipboard* clip, gpointer userData) {
            auto* self = static_cast<FileOperations*>(userData);
            if (self && !self->m_isSettingClipboard && !gdk_clipboard_is_local(clip)) {
                self->m_clipboardUris.clear();
                self->m_clipboardIsCut = false;
            }
        }),
        this
    );
}

void FileOperations::cancelCurrent() {
    if (m_running) {
        m_running->store(false);
    }
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_clear_object(&m_cancellable);
    }
    if (m_aliveToken) {
        m_aliveToken->store(false);
        m_aliveToken.reset();
    }
    if (m_workerThread.joinable()) {
        m_workerThread.detach();
    }
}

void FileOperations::splitBasename(const std::string& filename, std::string& stem, std::string& ext) {
    if (filename.empty()) {
        stem.clear();
        ext.clear();
        return;
    }

    std::string cleanName = filename;
    while (cleanName.size() > 1 && cleanName.back() == '/') {
        cleanName.pop_back();
    }
    if (cleanName == "/") {
        stem = "/";
        ext.clear();
        return;
    }

    bool matchedDouble = false;
    // Check for well-known double extensions
    static const std::vector<std::string> doubleExts = {
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz4", ".tar.sz"
    };
    for (const auto& dext : doubleExts) {
        if (cleanName.size() > dext.size() &&
            cleanName.compare(cleanName.size() - dext.size(), dext.size(), dext) == 0) {
            stem = cleanName.substr(0, cleanName.size() - dext.size());
            ext = dext;
            matchedDouble = true;
            break;
        }
    }

    if (!matchedDouble) {
        size_t lastDot = cleanName.find_last_of('.');
        if (lastDot == std::string::npos || lastDot == 0) {
            // No extension or dotfile without further dots (e.g. .bashrc)
            stem = cleanName;
            ext.clear();
        } else {
            stem = cleanName.substr(0, lastDot);
            ext = cleanName.substr(lastDot);
        }
    }

    // If stem already ends with " (copy \d+)", strip it so we increment N cleanly
    size_t copyPos = stem.rfind(" (copy ");
    if (copyPos != std::string::npos && copyPos > 0) {
        size_t numStart = copyPos + 7;
        size_t closingParen = stem.find(')', numStart);
        if (closingParen == stem.size() - 1 && closingParen > numStart) {
            bool allDigits = true;
            for (size_t idx = numStart; idx < closingParen; ++idx) {
                if (!std::isdigit(static_cast<unsigned char>(stem[idx]))) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                stem = stem.substr(0, copyPos);
            }
        }
    }
}

std::string FileOperations::getNonCollidingBasename(GFile* targetDir, const std::string& originalName) {
    if (!targetDir || originalName.empty()) {
        return originalName;
    }

    std::string cleanName = originalName;
    while (cleanName.size() > 1 && cleanName.back() == '/') {
        cleanName.pop_back();
    }
    if (cleanName.empty() || cleanName == "/") {
        return originalName;
    }

    g_autoptr(GFile) initialChild = g_file_get_child(targetDir, cleanName.c_str());
    if (!fileOrLinkExists(initialChild)) {
        return cleanName;
    }

    std::string stem;
    std::string ext;
    splitBasename(cleanName, stem, ext);

    for (int i = 1; ; ++i) {
        std::string candidate = stem + " (copy " + std::to_string(i) + ")" + ext;
        g_autoptr(GFile) child = g_file_get_child(targetDir, candidate.c_str());
        if (!fileOrLinkExists(child)) {
            return candidate;
        }
    }
}

uint64_t FileOperations::calculateTotalBytes(
    const std::vector<GFile*>& sources,
    size_t& totalFiles,
    GCancellable* cancellable
) {
    uint64_t totalBytes = 0;
    totalFiles = 0;

    std::vector<GFile*> queue;
    queue.reserve(sources.size());
    for (auto* src : sources) {
        if (src) {
            queue.push_back(G_FILE(g_object_ref(src)));
        }
    }

    std::unordered_set<std::string> visitedDirs;

    while (!queue.empty()) {
        if (cancellable && g_cancellable_is_cancelled(cancellable)) {
            break;
        }

        g_autoptr(GFile) current = queue.back();
        queue.pop_back();

        g_autoptr(GFileInfo) info = g_file_query_info(
            current,
            G_FILE_ATTRIBUTE_STANDARD_TYPE ","
            G_FILE_ATTRIBUTE_STANDARD_SIZE ","
            G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancellable,
            nullptr
        );

        if (!info) {
            continue;
        }

        totalFiles++;
        GFileType ftype = g_file_info_get_file_type(info);
        bool isSymlink = g_file_info_get_is_symlink(info);

        if (ftype == G_FILE_TYPE_REGULAR) {
            totalBytes += g_file_info_get_size(info);
        } else if (ftype == G_FILE_TYPE_DIRECTORY && !isSymlink) {
            g_autofree char* path = g_file_get_path(current);
            if (path) {
                if (!visitedDirs.insert(path).second) {
                    continue; // Skip already visited directory (cyclic symlink guard)
                }
            }

            GError* enumErr = nullptr;
            g_autoptr(GFileEnumerator) enumerator = g_file_enumerate_children(
                current,
                G_FILE_ATTRIBUTE_STANDARD_NAME,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                cancellable,
                &enumErr
            );

            if (enumerator) {
                while (true) {
                    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                        break;
                    }
                    GFileInfo* childInfo = nullptr;
                    GFile* childFile = nullptr;
                    if (!g_file_enumerator_iterate(enumerator, &childInfo, &childFile, cancellable, nullptr) || !childInfo) {
                        break;
                    }
                    if (childFile) {
                        queue.push_back(G_FILE(g_object_ref(childFile)));
                    }
                }
            }
            if (enumErr) {
                g_error_free(enumErr);
            }
        }
    }

    for (auto* rem : queue) {
        g_object_unref(rem);
    }

    return totalBytes;
}

bool FileOperations::copyRecursive(
    GFile* src,
    GFile* dst,
    bool overwrite,
    GCancellable* cancellable,
    const std::shared_ptr<std::atomic<bool>>& aliveToken,
    std::function<void(const OperationProgress&)> onProgress,
    OperationProgress& progress,
    std::string& errorMsg,
    std::vector<GFile*>* createdFilesTracker
) {
    if (!src || !dst) {
        errorMsg = "Invalid source or destination";
        return false;
    }
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        errorMsg = "Operation cancelled";
        return false;
    }
    if (aliveToken && !aliveToken->load()) {
        errorMsg = "Operation cancelled";
        return false;
    }
    if (g_file_equal(src, dst)) {
        errorMsg = "Source and destination are identical";
        return false;
    }

    GError* err = nullptr;
    g_autoptr(GFileInfo) info = g_file_query_info(
        src,
        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
        G_FILE_ATTRIBUTE_UNIX_MODE ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        cancellable,
        &err
    );

    if (!info) {
        errorMsg = err ? err->message : "Failed to query source file";
        if (err) g_error_free(err);
        return false;
    }

    GFileType ftype = g_file_info_get_file_type(info);
    bool isSymlink = g_file_info_get_is_symlink(info);

    if (ftype == G_FILE_TYPE_DIRECTORY && !isSymlink) {
        if (g_file_has_prefix(dst, src)) {
            errorMsg = "Cannot copy directory into itself";
            return false;
        }

        bool createdThisDir = false;
        GError* mkdirErr = nullptr;
        if (!g_file_make_directory(dst, cancellable, &mkdirErr)) {
            if (g_error_matches(mkdirErr, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                g_error_free(mkdirErr);
                mkdirErr = nullptr;
                GFileType dstType = g_file_query_file_type(dst, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable);
                if (dstType != G_FILE_TYPE_DIRECTORY) {
                    if (overwrite) {
                        g_file_delete(dst, cancellable, &mkdirErr);
                        if (mkdirErr) {
                            errorMsg = mkdirErr->message ? mkdirErr->message : "Failed to overwrite destination";
                            g_error_free(mkdirErr);
                            return false;
                        }
                        if (!g_file_make_directory(dst, cancellable, &mkdirErr)) {
                            errorMsg = mkdirErr ? mkdirErr->message : "Failed to create directory";
                            g_error_free(mkdirErr);
                            return false;
                        }
                        createdThisDir = true;
                    } else {
                        errorMsg = "Destination already exists and is not a directory";
                        return false;
                    }
                }
            } else {
                errorMsg = mkdirErr ? mkdirErr->message : "Failed to create directory";
                g_error_free(mkdirErr);
                return false;
            }
        } else {
            createdThisDir = true;
        }

        if (createdThisDir && createdFilesTracker) {
            createdFilesTracker->push_back(G_FILE(g_object_ref(dst)));
        }

        // Copy POSIX mode permissions to directory
        if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_MODE)) {
            guint32 mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
            g_file_set_attribute_uint32(dst, G_FILE_ATTRIBUTE_UNIX_MODE, mode, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);
        }

        progress.currentFiles++;
        g_autofree char* srcName = g_file_get_basename(src);
        progress.currentFileName = srcName ? srcName : "";
        if (onProgress) {
            onProgress(progress);
        }

        g_autoptr(GFileEnumerator) enumerator = g_file_enumerate_children(
            src,
            G_FILE_ATTRIBUTE_STANDARD_NAME,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancellable,
            &err
        );

        if (!enumerator) {
            errorMsg = err ? err->message : "Failed to enumerate directory";
            if (err) g_error_free(err);
            return false;
        }

        while (true) {
            if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                errorMsg = "Operation cancelled";
                return false;
            }
            if (aliveToken && !aliveToken->load()) {
                errorMsg = "Operation cancelled";
                return false;
            }

            GFileInfo* childInfo = nullptr;
            GFile* childSrc = nullptr;
            if (!g_file_enumerator_iterate(enumerator, &childInfo, &childSrc, cancellable, &err) || !childInfo) {
                break;
            }
            if (err) {
                errorMsg = err->message;
                g_error_free(err);
                return false;
            }

            const char* cName = g_file_info_get_name(childInfo);
            if (!cName) continue;

            g_autoptr(GFile) childDst = g_file_get_child(dst, cName);
            if (!copyRecursive(childSrc, childDst, overwrite, cancellable, aliveToken, onProgress, progress, errorMsg, createdFilesTracker)) {
                return false;
            }
        }

        // Preserve modification time on directory after populating children
        if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED)) {
            guint64 mtime = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
            g_file_set_attribute_uint64(dst, G_FILE_ATTRIBUTE_TIME_MODIFIED, mtime, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);
            if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC)) {
                guint32 usec = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
                g_file_set_attribute_uint32(dst, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC, usec, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);
            }
        }

        return true;
    }

    // Regular file or symlink
    g_autofree char* srcName = g_file_get_basename(src);
    progress.currentFileName = srcName ? srcName : "";
    uint64_t fileBaseBytes = progress.currentBytes;
    uint64_t fileSize = g_file_info_get_size(info);

    struct CopyCbData {
        OperationProgress* prog;
        uint64_t base;
        std::function<void(const OperationProgress&)>* onProg;
        const std::shared_ptr<std::atomic<bool>>* token;
    } cbData{&progress, fileBaseBytes, &onProgress, &aliveToken};

    auto progressCb = [](goffset current, goffset total, gpointer userData) {
        (void)total;
        auto* d = static_cast<CopyCbData*>(userData);
        if (d->token && *(d->token) && !(*(d->token))->load()) {
            return;
        }
        d->prog->currentBytes = d->base + static_cast<uint64_t>(current);
        if (d->onProg && *(d->onProg)) {
            (*(d->onProg))(*(d->prog));
        }
    };

    GFileCopyFlags flags = static_cast<GFileCopyFlags>(G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA);
    if (overwrite) {
        flags = static_cast<GFileCopyFlags>(flags | G_FILE_COPY_OVERWRITE);
    }

    bool dstExisted = fileOrLinkExists(dst);
    GError* copyErr = nullptr;
    gboolean ok = g_file_copy(
        src,
        dst,
        flags,
        cancellable,
        onProgress ? progressCb : nullptr,
        onProgress ? &cbData : nullptr,
        &copyErr
    );

    if (!ok) {
        if (!dstExisted && fileOrLinkExists(dst)) {
            g_file_delete(dst, nullptr, nullptr);
        }
        errorMsg = copyErr ? copyErr->message : "Failed to copy file";
        if (copyErr) g_error_free(copyErr);
        return false;
    }

    if (!dstExisted && createdFilesTracker) {
        createdFilesTracker->push_back(G_FILE(g_object_ref(dst)));
    }

    // Preserve POSIX mode permissions and modification time on copied file
    if (!isSymlink) {
        if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_MODE)) {
            guint32 mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
            g_file_set_attribute_uint32(dst, G_FILE_ATTRIBUTE_UNIX_MODE, mode, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);
        }
        if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED)) {
            guint64 mtime = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
            g_file_set_attribute_uint64(dst, G_FILE_ATTRIBUTE_TIME_MODIFIED, mtime, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);
            if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC)) {
                guint32 usec = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
                g_file_set_attribute_uint32(dst, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC, usec, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);
            }
        }
    }

    progress.currentFiles++;
    progress.currentBytes = fileBaseBytes + fileSize;
    if (onProgress) {
        onProgress(progress);
    }

    return true;
}

bool FileOperations::deleteRecursive(
    GFile* target,
    GCancellable* cancellable,
    const std::shared_ptr<std::atomic<bool>>& aliveToken,
    std::string& errorMsg
) {
    if (!target) {
        errorMsg = "Invalid target";
        return false;
    }
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        errorMsg = "Operation cancelled";
        return false;
    }
    if (aliveToken && !aliveToken->load()) {
        errorMsg = "Operation cancelled";
        return false;
    }

    GError* err = nullptr;
    g_autoptr(GFileInfo) info = g_file_query_info(
        target,
        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        cancellable,
        &err
    );

    if (!info) {
        if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            g_error_free(err);
            return true;
        }
        errorMsg = err ? err->message : "Failed to query target";
        if (err) g_error_free(err);
        return false;
    }

    GFileType ftype = g_file_info_get_file_type(info);
    bool isSymlink = g_file_info_get_is_symlink(info);

    if (ftype == G_FILE_TYPE_DIRECTORY && !isSymlink) {
        g_autoptr(GFileEnumerator) enumerator = g_file_enumerate_children(
            target,
            G_FILE_ATTRIBUTE_STANDARD_NAME,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancellable,
            &err
        );

        if (!enumerator) {
            errorMsg = err ? err->message : "Failed to enumerate directory for deletion";
            if (err) g_error_free(err);
            return false;
        }

        while (true) {
            if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                errorMsg = "Operation cancelled";
                return false;
            }
            if (aliveToken && !aliveToken->load()) {
                errorMsg = "Operation cancelled";
                return false;
            }

            GFileInfo* childInfo = nullptr;
            GFile* childFile = nullptr;
            if (!g_file_enumerator_iterate(enumerator, &childInfo, &childFile, cancellable, &err) || !childInfo) {
                break;
            }
            if (err) {
                errorMsg = err->message;
                g_error_free(err);
                return false;
            }

            if (!deleteRecursive(childFile, cancellable, aliveToken, errorMsg)) {
                return false;
            }
        }
    }

    if (!g_file_delete(target, cancellable, &err)) {
        if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            g_error_free(err);
            return true;
        }
        errorMsg = err ? err->message : "Failed to delete item";
        if (err) g_error_free(err);
        return false;
    }

    return true;
}

void FileOperations::copyAsync(
    const std::vector<GFile*>& sources,
    GFile* targetDir,
    bool overwrite,
    std::function<void(bool success, const std::string& msg)> onComplete,
    std::function<void(const OperationProgress& progress)> onProgress
) {
    cancelCurrent();

    if (sources.empty() || !targetDir) {
        if (onComplete) {
            auto* dispatch = new CompletionDispatch{
                std::move(onComplete),
                false,
                "Invalid sources or target directory",
                {},
                nullptr
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
                if (ctx->callback) {
                    ctx->callback(ctx->success, ctx->message);
                }
                return G_SOURCE_REMOVE;
            }, dispatch);
        }
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    if (m_running) {
        m_running->store(true);
    }

    std::vector<GFile*> srcRefs;
    srcRefs.reserve(sources.size());
    for (auto* s : sources) {
        if (s) {
            srcRefs.push_back(G_FILE(g_object_ref(s)));
        }
    }

    if (srcRefs.empty()) {
        if (m_running) {
            m_running->store(false);
        }
        if (onComplete) {
            onComplete(false, "No valid source files specified");
        }
        return;
    }

    auto* targetRef = G_FILE(g_object_ref(targetDir));
    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    std::weak_ptr<std::atomic<bool>> weakAlive = m_aliveToken;
    auto sharedToken = m_aliveToken;
    auto sharedRunning = m_running;

    m_workerThread = std::jthread([
        srcRefs = std::move(srcRefs),
        targetRef,
        cancellable,
        overwrite,
        weakAlive,
        sharedToken,
        sharedRunning,
        cb = std::move(onComplete),
        progCb = std::move(onProgress)
    ]() mutable {
        struct WorkerGuard {
            std::vector<GFile*>& files;
            GFile*& target;
            GCancellable*& canc;
            ~WorkerGuard() {
                for (auto* f : files) {
                    if (f) g_object_unref(f);
                }
                files.clear();
                if (target) {
                    g_object_unref(target);
                    target = nullptr;
                }
                if (canc) {
                    g_object_unref(canc);
                    canc = nullptr;
                }
            }
        } guard{srcRefs, targetRef, cancellable};

        size_t totalFiles = 0;
        uint64_t totalBytes = calculateTotalBytes(srcRefs, totalFiles, cancellable);

        OperationProgress progress;
        progress.totalBytes = totalBytes;
        progress.totalFiles = totalFiles;
        progress.currentBytes = 0;
        progress.currentFiles = 0;

        auto lastReport = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
        auto throttledProgress = [progCb, weakAlive, lastReport](const OperationProgress& p) {
            if (!progCb) return;
            auto now = std::chrono::steady_clock::now();
            bool isComplete = (p.totalBytes > 0 ? (p.currentBytes >= p.totalBytes)
                                               : (p.totalFiles > 0 && p.currentFiles >= p.totalFiles));
            if (!isComplete && (now - *lastReport) < std::chrono::milliseconds(50)) {
                return;
            }
            *lastReport = now;

            auto* pDispatch = new ProgressDispatch{
                progCb,
                p,
                weakAlive
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<ProgressDispatch> ctx(static_cast<ProgressDispatch*>(data));
                if (auto token = ctx->aliveToken.lock()) {
                    if (token->load() && ctx->callback) {
                        ctx->callback(ctx->progress);
                    }
                }
                return G_SOURCE_REMOVE;
            }, pDispatch);
        };

        bool success = true;
        std::string errorMsg;
        size_t copiedCount = 0;

        for (auto* src : srcRefs) {
            if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }
            auto token = weakAlive.lock();
            if (!token || !token->load()) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }

            g_autoptr(GFileInfo) srcInfo = g_file_query_info(
                src,
                G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                cancellable,
                nullptr
            );
            if (srcInfo) {
                GFileType sType = g_file_info_get_file_type(srcInfo);
                bool sSymlink = g_file_info_get_is_symlink(srcInfo);
                if (sType == G_FILE_TYPE_DIRECTORY && !sSymlink) {
                    if (g_file_equal(targetRef, src) || g_file_has_prefix(targetRef, src)) {
                        success = false;
                        errorMsg = "Cannot copy directory into itself or a subdirectory";
                        break;
                    }
                }
            }

            g_autofree char* srcBasename = g_file_get_basename(src);
            if (!srcBasename) continue;

            std::string destName = overwrite ? srcBasename : getNonCollidingBasename(targetRef, srcBasename);
            g_autoptr(GFile) dst = g_file_get_child(targetRef, destName.c_str());

            if (!copyRecursive(src, dst, overwrite, cancellable, token, throttledProgress, progress, errorMsg)) {
                success = false;
                break;
            }
            copiedCount++;
        }

        if (progCb && success) {
            progress.currentBytes = progress.totalBytes;
            progress.currentFiles = progress.totalFiles;
            throttledProgress(progress);
        }

        if (sharedToken->load() && sharedRunning) {
            sharedRunning->store(false);
        }

        std::string finalMsg;
        if (success) {
            finalMsg = (copiedCount == 1) ? "Successfully copied 1 item"
                                          : ("Successfully copied " + std::to_string(copiedCount) + " items");
        } else {
            finalMsg = errorMsg.empty() ? "Copy operation failed" : errorMsg;
        }

        auto* cDispatch = new CompletionDispatch{
            std::move(cb),
            success,
            std::move(finalMsg),
            weakAlive,
            sharedRunning
        };
        g_idle_add([](gpointer data) -> gboolean {
            std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
            if (auto token = ctx->aliveToken.lock()) {
                if (token->load()) {
                    if (ctx->runningFlag) {
                        ctx->runningFlag->store(false);
                    }
                    if (ctx->callback) {
                        ctx->callback(ctx->success, ctx->message);
                    }
                }
            }
            return G_SOURCE_REMOVE;
        }, cDispatch);
    });
}

void FileOperations::moveAsync(
    const std::vector<GFile*>& sources,
    GFile* targetDir,
    bool overwrite,
    std::function<void(bool success, const std::string& msg)> onComplete
) {
    cancelCurrent();

    if (sources.empty() || !targetDir) {
        if (onComplete) {
            auto* dispatch = new CompletionDispatch{
                std::move(onComplete),
                false,
                "Invalid sources or target directory",
                {},
                nullptr
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
                if (ctx->callback) {
                    ctx->callback(ctx->success, ctx->message);
                }
                return G_SOURCE_REMOVE;
            }, dispatch);
        }
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    if (m_running) {
        m_running->store(true);
    }

    std::vector<GFile*> srcRefs;
    srcRefs.reserve(sources.size());
    for (auto* s : sources) {
        if (s) {
            srcRefs.push_back(G_FILE(g_object_ref(s)));
        }
    }

    if (srcRefs.empty()) {
        if (m_running) {
            m_running->store(false);
        }
        if (onComplete) {
            onComplete(false, "No valid source files specified");
        }
        return;
    }

    auto* targetRef = G_FILE(g_object_ref(targetDir));
    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    std::weak_ptr<std::atomic<bool>> weakAlive = m_aliveToken;
    auto sharedToken = m_aliveToken;
    auto sharedRunning = m_running;

    m_workerThread = std::jthread([
        srcRefs = std::move(srcRefs),
        targetRef,
        cancellable,
        overwrite,
        weakAlive,
        sharedToken,
        sharedRunning,
        cb = std::move(onComplete)
    ]() mutable {
        struct WorkerGuard {
            std::vector<GFile*>& files;
            GFile*& target;
            GCancellable*& canc;
            ~WorkerGuard() {
                for (auto* f : files) {
                    if (f) g_object_unref(f);
                }
                files.clear();
                if (target) {
                    g_object_unref(target);
                    target = nullptr;
                }
                if (canc) {
                    g_object_unref(canc);
                    canc = nullptr;
                }
            }
        } guard{srcRefs, targetRef, cancellable};

        bool success = true;
        std::string errorMsg;
        size_t movedCount = 0;

        for (auto* src : srcRefs) {
            if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }
            auto token = weakAlive.lock();
            if (!token || !token->load()) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }

            g_autoptr(GFileInfo) srcInfo = g_file_query_info(
                src,
                G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                cancellable,
                nullptr
            );
            if (srcInfo) {
                GFileType sType = g_file_info_get_file_type(srcInfo);
                bool sSymlink = g_file_info_get_is_symlink(srcInfo);
                if (sType == G_FILE_TYPE_DIRECTORY && !sSymlink) {
                    if (g_file_equal(targetRef, src) || g_file_has_prefix(targetRef, src)) {
                        success = false;
                        errorMsg = "Cannot move directory into itself or a subdirectory";
                        break;
                    }
                }
            }

            g_autofree char* srcBasename = g_file_get_basename(src);
            if (!srcBasename) continue;

            std::string destName = overwrite ? srcBasename : getNonCollidingBasename(targetRef, srcBasename);
            g_autoptr(GFile) dst = g_file_get_child(targetRef, destName.c_str());

            if (g_file_equal(src, dst)) {
                movedCount++;
                continue;
            }

            if (g_file_equal(dst, src) || g_file_has_prefix(dst, src)) {
                success = false;
                errorMsg = "Cannot move directory into itself";
                break;
            }

            GFileCopyFlags flags = static_cast<GFileCopyFlags>(G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA);
            if (overwrite) {
                flags = static_cast<GFileCopyFlags>(flags | G_FILE_COPY_OVERWRITE);
            }

            GError* moveErr = nullptr;
            gboolean ok = g_file_move(src, dst, flags, cancellable, nullptr, nullptr, &moveErr);

            if (!ok) {
                if (g_error_matches(moveErr, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED) ||
                    g_error_matches(moveErr, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE) ||
                    g_error_matches(moveErr, G_IO_ERROR, G_IO_ERROR_WOULD_MERGE)) {
                    std::vector<GFile*> createdFiles;
                    OperationProgress copyProg;
                    std::string copyErrMsg;
                    bool copyOk = copyRecursive(src, dst, overwrite, cancellable, token, nullptr, copyProg, copyErrMsg, &createdFiles);
                    if (!copyOk) {
                        success = false;
                        errorMsg = copyErrMsg.empty() ? "Failed during cross-filesystem move copy" : copyErrMsg;
                        for (auto it = createdFiles.rbegin(); it != createdFiles.rend(); ++it) {
                            GFile* f = *it;
                            if (f) {
                                g_file_delete(f, nullptr, nullptr);
                                g_object_unref(f);
                            }
                        }
                        createdFiles.clear();
                        break;
                    }

                    for (auto* f : createdFiles) {
                        if (f) g_object_unref(f);
                    }
                    createdFiles.clear();

                    std::string delErrMsg;
                    bool delOk = deleteRecursive(src, cancellable, token, delErrMsg);
                    if (!delOk) {
                        success = false;
                        errorMsg = delErrMsg.empty() ? "Failed to delete source during cross-filesystem move" : delErrMsg;
                        break;
                    }
                } else {
                    success = false;
                    errorMsg = moveErr ? moveErr->message : "Failed to move file";
                    g_error_free(moveErr);
                    break;
                }
            }

            movedCount++;
        }

        if (sharedToken->load() && sharedRunning) {
            sharedRunning->store(false);
        }

        std::string finalMsg;
        if (success) {
            finalMsg = (movedCount == 1) ? "Successfully moved 1 item"
                                         : ("Successfully moved " + std::to_string(movedCount) + " items");
        } else {
            finalMsg = errorMsg.empty() ? "Move operation failed" : errorMsg;
        }

        auto* cDispatch = new CompletionDispatch{
            std::move(cb),
            success,
            std::move(finalMsg),
            weakAlive,
            sharedRunning
        };
        g_idle_add([](gpointer data) -> gboolean {
            std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
            if (auto token = ctx->aliveToken.lock()) {
                if (token->load()) {
                    if (ctx->runningFlag) {
                        ctx->runningFlag->store(false);
                    }
                    if (ctx->callback) {
                        ctx->callback(ctx->success, ctx->message);
                    }
                }
            }
            return G_SOURCE_REMOVE;
        }, cDispatch);
    });
}

void FileOperations::trashAsync(
    const std::vector<GFile*>& files,
    std::function<void(bool success, const std::string& msg)> onComplete
) {
    cancelCurrent();

    if (files.empty()) {
        if (onComplete) {
            auto* dispatch = new CompletionDispatch{
                std::move(onComplete),
                true,
                "Nothing to trash",
                {},
                nullptr
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
                if (ctx->callback) {
                    ctx->callback(ctx->success, ctx->message);
                }
                return G_SOURCE_REMOVE;
            }, dispatch);
        }
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    if (m_running) {
        m_running->store(true);
    }

    std::vector<GFile*> fileRefs;
    fileRefs.reserve(files.size());
    for (auto* f : files) {
        if (f) {
            fileRefs.push_back(G_FILE(g_object_ref(f)));
        }
    }

    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    std::weak_ptr<std::atomic<bool>> weakAlive = m_aliveToken;
    auto sharedToken = m_aliveToken;
    auto sharedRunning = m_running;

    m_workerThread = std::jthread([
        fileRefs = std::move(fileRefs),
        cancellable,
        weakAlive,
        sharedToken,
        sharedRunning,
        cb = std::move(onComplete)
    ]() mutable {
        struct FileGuard {
            std::vector<GFile*>& flist;
            GCancellable*& canc;
            ~FileGuard() {
                for (auto* f : flist) {
                    if (f) g_object_unref(f);
                }
                flist.clear();
                if (canc) {
                    g_object_unref(canc);
                    canc = nullptr;
                }
            }
        } guard{fileRefs, cancellable};

        bool success = true;
        std::string errorMsg;
        size_t trashedCount = 0;

        for (auto* f : fileRefs) {
            if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }
            auto token = weakAlive.lock();
            if (!token || !token->load()) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }

            GError* err = nullptr;
            if (!g_file_trash(f, cancellable, &err)) {
                success = false;
                errorMsg = err ? err->message : "Failed to move file to trash";
                if (err) g_error_free(err);
                break;
            }
            trashedCount++;
        }

        if (sharedToken->load() && sharedRunning) {
            sharedRunning->store(false);
        }

        std::string finalMsg;
        if (success) {
            finalMsg = (trashedCount == 1) ? "Moved 1 item to trash"
                                           : ("Moved " + std::to_string(trashedCount) + " items to trash");
        } else {
            finalMsg = errorMsg.empty() ? "Trash operation failed" : errorMsg;
        }

        auto* cDispatch = new CompletionDispatch{
            std::move(cb),
            success,
            std::move(finalMsg),
            weakAlive,
            sharedRunning
        };
        g_idle_add([](gpointer data) -> gboolean {
            std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
            if (auto token = ctx->aliveToken.lock()) {
                if (token->load()) {
                    if (ctx->runningFlag) {
                        ctx->runningFlag->store(false);
                    }
                    if (ctx->callback) {
                        ctx->callback(ctx->success, ctx->message);
                    }
                }
            }
            return G_SOURCE_REMOVE;
        }, cDispatch);
    });
}

void FileOperations::deleteAsync(
    const std::vector<GFile*>& files,
    std::function<void(bool success, const std::string& msg)> onComplete
) {
    cancelCurrent();

    if (files.empty()) {
        if (onComplete) {
            auto* dispatch = new CompletionDispatch{
                std::move(onComplete),
                true,
                "Nothing to delete",
                {},
                nullptr
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
                if (ctx->callback) {
                    ctx->callback(ctx->success, ctx->message);
                }
                return G_SOURCE_REMOVE;
            }, dispatch);
        }
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    if (m_running) {
        m_running->store(true);
    }

    std::vector<GFile*> fileRefs;
    fileRefs.reserve(files.size());
    for (auto* f : files) {
        if (f) {
            fileRefs.push_back(G_FILE(g_object_ref(f)));
        }
    }

    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    std::weak_ptr<std::atomic<bool>> weakAlive = m_aliveToken;
    auto sharedToken = m_aliveToken;
    auto sharedRunning = m_running;

    m_workerThread = std::jthread([
        fileRefs = std::move(fileRefs),
        cancellable,
        weakAlive,
        sharedToken,
        sharedRunning,
        cb = std::move(onComplete)
    ]() mutable {
        struct FileGuard {
            std::vector<GFile*>& flist;
            GCancellable*& canc;
            ~FileGuard() {
                for (auto* f : flist) {
                    if (f) g_object_unref(f);
                }
                flist.clear();
                if (canc) {
                    g_object_unref(canc);
                    canc = nullptr;
                }
            }
        } guard{fileRefs, cancellable};

        bool success = true;
        std::string errorMsg;
        size_t deletedCount = 0;

        for (auto* f : fileRefs) {
            if (cancellable && g_cancellable_is_cancelled(cancellable)) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }
            auto token = weakAlive.lock();
            if (!token || !token->load()) {
                success = false;
                errorMsg = "Operation cancelled";
                break;
            }

            if (!deleteRecursive(f, cancellable, token, errorMsg)) {
                success = false;
                break;
            }
            deletedCount++;
        }

        if (sharedToken->load() && sharedRunning) {
            sharedRunning->store(false);
        }

        std::string finalMsg;
        if (success) {
            finalMsg = (deletedCount == 1) ? "Permanently deleted 1 item"
                                           : ("Permanently deleted " + std::to_string(deletedCount) + " items");
        } else {
            finalMsg = errorMsg.empty() ? "Delete operation failed" : errorMsg;
        }

        auto* cDispatch = new CompletionDispatch{
            std::move(cb),
            success,
            std::move(finalMsg),
            weakAlive,
            sharedRunning
        };
        g_idle_add([](gpointer data) -> gboolean {
            std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
            if (auto token = ctx->aliveToken.lock()) {
                if (token->load()) {
                    if (ctx->runningFlag) {
                        ctx->runningFlag->store(false);
                    }
                    if (ctx->callback) {
                        ctx->callback(ctx->success, ctx->message);
                    }
                }
            }
            return G_SOURCE_REMOVE;
        }, cDispatch);
    });
}

void FileOperations::renameAsync(
    GFile* file,
    const std::string& newName,
    std::function<void(bool success, const std::string& msg)> onComplete
) {
    cancelCurrent();

    if (!file || newName.empty()) {
        if (onComplete) {
            auto* dispatch = new CompletionDispatch{
                std::move(onComplete),
                false,
                "Invalid file or target name",
                {},
                nullptr
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
                if (ctx->callback) {
                    ctx->callback(ctx->success, ctx->message);
                }
                return G_SOURCE_REMOVE;
            }, dispatch);
        }
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    if (m_running) {
        m_running->store(true);
    }

    auto* fileRef = G_FILE(g_object_ref(file));
    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    std::weak_ptr<std::atomic<bool>> weakAlive = m_aliveToken;
    auto sharedToken = m_aliveToken;
    auto sharedRunning = m_running;

    m_workerThread = std::jthread([
        fileRef,
        newName,
        cancellable,
        weakAlive,
        sharedToken,
        sharedRunning,
        cb = std::move(onComplete)
    ]() mutable {
        struct FileGuard {
            GFile*& f;
            GCancellable*& canc;
            ~FileGuard() {
                if (f) {
                    g_object_unref(f);
                    f = nullptr;
                }
                if (canc) {
                    g_object_unref(canc);
                    canc = nullptr;
                }
            }
        } guard{fileRef, cancellable};

        bool success = true;
        std::string finalMsg;

        GError* err = nullptr;
        GFile* renamed = g_file_set_display_name(fileRef, newName.c_str(), cancellable, &err);
        if (renamed) {
            g_object_unref(renamed);
            success = true;
            finalMsg = "Renamed to " + newName;
        } else {
            success = false;
            finalMsg = err ? err->message : "Failed to rename file";
            if (err) g_error_free(err);
        }

        if (sharedToken->load() && sharedRunning) {
            sharedRunning->store(false);
        }

        auto* cDispatch = new CompletionDispatch{
            std::move(cb),
            success,
            std::move(finalMsg),
            weakAlive,
            sharedRunning
        };
        g_idle_add([](gpointer data) -> gboolean {
            std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
            if (auto token = ctx->aliveToken.lock()) {
                if (token->load()) {
                    if (ctx->runningFlag) {
                        ctx->runningFlag->store(false);
                    }
                    if (ctx->callback) {
                        ctx->callback(ctx->success, ctx->message);
                    }
                }
            }
            return G_SOURCE_REMOVE;
        }, cDispatch);
    });
}

void FileOperations::createDirectoryAsync(
    GFile* parentDir,
    const std::string& dirName,
    std::function<void(bool success, const std::string& msg)> onComplete
) {
    cancelCurrent();

    if (!parentDir || dirName.empty()) {
        if (onComplete) {
            auto* dispatch = new CompletionDispatch{
                std::move(onComplete),
                false,
                "Invalid parent directory or folder name",
                {},
                nullptr
            };
            g_idle_add([](gpointer data) -> gboolean {
                std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
                if (ctx->callback) {
                    ctx->callback(ctx->success, ctx->message);
                }
                return G_SOURCE_REMOVE;
            }, dispatch);
        }
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    if (m_running) {
        m_running->store(true);
    }

    auto* parentRef = G_FILE(g_object_ref(parentDir));
    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    std::weak_ptr<std::atomic<bool>> weakAlive = m_aliveToken;
    auto sharedToken = m_aliveToken;
    auto sharedRunning = m_running;

    m_workerThread = std::jthread([
        parentRef,
        dirName,
        cancellable,
        weakAlive,
        sharedToken,
        sharedRunning,
        cb = std::move(onComplete)
    ]() mutable {
        struct FileGuard {
            GFile*& p;
            GCancellable*& canc;
            ~FileGuard() {
                if (p) {
                    g_object_unref(p);
                    p = nullptr;
                }
                if (canc) {
                    g_object_unref(canc);
                    canc = nullptr;
                }
            }
        } guard{parentRef, cancellable};

        bool success = true;
        std::string finalMsg;

        g_autoptr(GFile) newDir = g_file_get_child(parentRef, dirName.c_str());
        GError* err = nullptr;
        if (g_file_make_directory(newDir, cancellable, &err)) {
            success = true;
            finalMsg = "Created folder " + dirName;
        } else {
            success = false;
            finalMsg = err ? err->message : "Failed to create folder";
            if (err) g_error_free(err);
        }

        if (sharedToken->load() && sharedRunning) {
            sharedRunning->store(false);
        }

        auto* cDispatch = new CompletionDispatch{
            std::move(cb),
            success,
            std::move(finalMsg),
            weakAlive,
            sharedRunning
        };
        g_idle_add([](gpointer data) -> gboolean {
            std::unique_ptr<CompletionDispatch> ctx(static_cast<CompletionDispatch*>(data));
            if (auto token = ctx->aliveToken.lock()) {
                if (token->load()) {
                    if (ctx->runningFlag) {
                        ctx->runningFlag->store(false);
                    }
                    if (ctx->callback) {
                        ctx->callback(ctx->success, ctx->message);
                    }
                }
            }
            return G_SOURCE_REMOVE;
        }, cDispatch);
    });
}

void FileOperations::copyToClipboard(const std::vector<GFile*>& files, bool isCut) {
    initClipboardMonitoring();
    m_clipboardUris.clear();
    m_clipboardIsCut = isCut;

    std::string uriListStr;
    std::string gnomeCopied = (isCut ? "cut\n" : "copy\n");

    for (auto* file : files) {
        if (!file) continue;
        g_autofree char* uri = g_file_get_uri(file);
        if (uri) {
            m_clipboardUris.push_back(uri);
            uriListStr += uri;
            uriListStr += "\r\n";
            gnomeCopied += uri;
            gnomeCopied += "\n";
        }
    }

    if (m_clipboardUris.empty()) {
        return;
    }

    GdkDisplay* display = gdk_display_get_default();
    if (!display) {
        return;
    }

    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    if (!clipboard) {
        return;
    }

    g_autoptr(GBytes) uriBytes = g_bytes_new(uriListStr.data(), uriListStr.size());
    GdkContentProvider* pUri = gdk_content_provider_new_for_bytes("text/uri-list", uriBytes);

    g_autoptr(GBytes) gnomeBytes = g_bytes_new(gnomeCopied.data(), gnomeCopied.size());
    GdkContentProvider* pGnome = gdk_content_provider_new_for_bytes("x-special/gnome-copied-files", gnomeBytes);

    GdkContentProvider* providers[2] = { pUri, pGnome };
    g_autoptr(GdkContentProvider) unionProvider = gdk_content_provider_new_union(providers, 2);

    m_isSettingClipboard = true;
    gdk_clipboard_set_content(clipboard, unionProvider);
    m_isSettingClipboard = false;
}

void FileOperations::pasteFromClipboard(
    GFile* targetDir,
    std::function<void(bool success, const std::string& msg)> onComplete
) {
    if (!targetDir) {
        if (onComplete) {
            onComplete(false, "Invalid target directory");
        }
        return;
    }

    initClipboardMonitoring();

    // 1. If we have internal clipboard URIs from this app instance, use them directly.
    // This avoids self-deadlocks where reading synchronously from our own GdkClipboard GInputStream
    // blocks the main loop from serving the clipboard content request.
    if (!m_clipboardUris.empty()) {
        std::vector<GFile*> sources;
        sources.reserve(m_clipboardUris.size());
        for (const auto& uriStr : m_clipboardUris) {
            GFile* f = g_file_new_for_uri(uriStr.c_str());
            if (f) {
                sources.push_back(f);
            }
        }

        if (sources.empty()) {
            if (onComplete) {
                onComplete(false, "No valid files in clipboard");
            }
            return;
        }

        bool isCut = m_clipboardIsCut;
        if (isCut) {
            m_clipboardUris.clear();
            m_clipboardIsCut = false;
            moveAsync(sources, targetDir, false, std::move(onComplete));
        } else {
            copyAsync(sources, targetDir, false, std::move(onComplete));
        }

        for (auto* f : sources) {
            g_object_unref(f);
        }
        return;
    }

    // 2. Otherwise check system GdkClipboard (e.g. copied from another app)
    GdkDisplay* display = gdk_display_get_default();
    GdkClipboard* clipboard = display ? gdk_display_get_clipboard(display) : nullptr;
    GdkContentFormats* formats = clipboard ? gdk_clipboard_get_formats(clipboard) : nullptr;
    bool hasSystemFiles = formats && (
        gdk_content_formats_contain_mime_type(formats, "x-special/gnome-copied-files") ||
        gdk_content_formats_contain_mime_type(formats, "text/uri-list")
    );

    if (hasSystemFiles) {
        struct PasteContext {
            FileOperations* self{nullptr};
            GFile* target{nullptr};
            std::function<void(bool, const std::string&)> callback;
            std::weak_ptr<std::atomic<bool>> aliveToken;
        };

        auto* ctx = new PasteContext{
            this,
            G_FILE(g_object_ref(targetDir)),
            std::move(onComplete),
            m_aliveToken
        };

        const char* mimeTypes[] = {
            "x-special/gnome-copied-files",
            "text/uri-list",
            nullptr
        };

        gdk_clipboard_read_async(
            clipboard,
            mimeTypes,
            G_PRIORITY_DEFAULT,
            nullptr,
            [](GObject* source, GAsyncResult* res, gpointer userData) {
                std::unique_ptr<PasteContext> c(static_cast<PasteContext*>(userData));
                auto token = c->aliveToken.lock();
                if (!token || !token->load()) {
                    if (c->target) g_object_unref(c->target);
                    return;
                }

                GError* err = nullptr;
                const char* outMime = nullptr;
                GInputStream* stream = gdk_clipboard_read_finish(GDK_CLIPBOARD(source), res, &outMime, &err);

                if (!stream) {
                    if (c->callback) {
                        c->callback(false, err ? err->message : "Failed to read clipboard");
                    }
                    if (err) g_error_free(err);
                    if (c->target) g_object_unref(c->target);
                    return;
                }

                std::string mimeStr = outMime ? outMime : "";
                GFile* targetRef = G_FILE(g_object_ref(c->target));
                std::weak_ptr<std::atomic<bool>> weakAlive = c->aliveToken;
                auto cb = std::move(c->callback);
                FileOperations* self = c->self;

                std::jthread([stream, mimeStr, targetRef, weakAlive, cb = std::move(cb), self]() mutable {
                    struct StreamGuard {
                        GInputStream* s;
                        GFile* t;
                        ~StreamGuard() {
                            if (s) {
                                g_input_stream_close(s, nullptr, nullptr);
                                g_object_unref(s);
                            }
                            if (t) {
                                g_object_unref(t);
                            }
                        }
                    } guard{stream, targetRef};

                    constexpr size_t BUF_SIZE = 64 * 1024;
                    std::vector<char> buffer(BUF_SIZE);
                    gsize bytesRead = 0;
                    std::string content;

                    while (true) {
                        GError* readErr = nullptr;
                        gssize n = g_input_stream_read(stream, buffer.data(), buffer.size(), nullptr, &readErr);
                        if (readErr) {
                            g_error_free(readErr);
                            break;
                        }
                        if (n <= 0) {
                            break;
                        }
                        content.append(buffer.data(), static_cast<size_t>(n));
                        bytesRead += static_cast<size_t>(n);
                        if (bytesRead > 1024 * 1024) break; // Limit to 1MB
                    }

                    std::vector<std::string> uris;
                    bool isCut = false;

                    std::istringstream streamLines(content);
                    std::string line;
                    bool firstLine = true;

                    while (std::getline(streamLines, line)) {
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }
                        if (line.empty()) continue;

                        if (firstLine && mimeStr == "x-special/gnome-copied-files") {
                            firstLine = false;
                            if (line == "cut") {
                                isCut = true;
                                continue;
                            } else if (line == "copy") {
                                isCut = false;
                                continue;
                            }
                        }
                        firstLine = false;

                        if (line[0] == '#') {
                            continue;
                        }

                        uris.push_back(line);
                    }

                    struct ParseDispatch {
                        FileOperations* self;
                        GFile* target;
                        std::vector<std::string> uris;
                        bool isCut;
                        std::function<void(bool, const std::string&)> cb;
                        std::weak_ptr<std::atomic<bool>> aliveToken;
                    };

                    auto* dispatch = new ParseDispatch{
                        self,
                        G_FILE(g_object_ref(targetRef)),
                        std::move(uris),
                        isCut,
                        std::move(cb),
                        weakAlive
                    };

                    g_idle_add([](gpointer data) -> gboolean {
                        std::unique_ptr<ParseDispatch> ctx(static_cast<ParseDispatch*>(data));
                        auto token = ctx->aliveToken.lock();
                        if (!token || !token->load()) {
                            if (ctx->target) g_object_unref(ctx->target);
                            return G_SOURCE_REMOVE;
                        }

                        std::vector<GFile*> sources;
                        sources.reserve(ctx->uris.size());
                        for (const auto& u : ctx->uris) {
                            GFile* f = g_file_new_for_uri(u.c_str());
                            if (f) {
                                sources.push_back(f);
                            }
                        }

                        if (sources.empty()) {
                            if (ctx->cb) {
                                ctx->cb(false, "No valid files found in clipboard");
                            }
                            if (ctx->target) g_object_unref(ctx->target);
                            return G_SOURCE_REMOVE;
                        }

                        if (ctx->isCut) {
                            ctx->self->moveAsync(sources, ctx->target, false, std::move(ctx->cb));
                        } else {
                            ctx->self->copyAsync(sources, ctx->target, false, std::move(ctx->cb));
                        }

                        for (auto* f : sources) {
                            g_object_unref(f);
                        }
                        if (ctx->target) {
                            g_object_unref(ctx->target);
                        }
                        return G_SOURCE_REMOVE;
                    }, dispatch);
                }).detach();

                if (c->target) {
                    g_object_unref(c->target);
                    c->target = nullptr;
                }
            },
            ctx
        );
        return;
    }

    if (onComplete) {
        onComplete(false, "Clipboard is empty");
    }
}

bool FileOperations::canPaste() const {
    const_cast<FileOperations*>(this)->initClipboardMonitoring();
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GdkClipboard* clipboard = gdk_display_get_clipboard(display);
        if (clipboard) {
            GdkContentFormats* formats = gdk_clipboard_get_formats(clipboard);
            if (formats && (gdk_content_formats_contain_mime_type(formats, "text/uri-list") ||
                            gdk_content_formats_contain_mime_type(formats, "x-special/gnome-copied-files"))) {
                return true;
            }
        }
    }
    return !m_clipboardUris.empty();
}

} // namespace dfiles::operations
