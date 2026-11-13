#include "inspector/checksum_helper.hpp"

namespace dfiles::inspector {

ChecksumHelper::ChecksumHelper()
    : m_aliveToken(std::make_shared<bool>(true)),
      m_running(std::make_shared<std::atomic<bool>>(false)) {
}

ChecksumHelper::~ChecksumHelper() {
    cancel();
}

void ChecksumHelper::computeAsync(GFile* file, std::function<void(const ChecksumResult&)> callback) {
    cancel();

    if (!file || !callback) {
        return;
    }

    // Guard against blocking UI on non-regular files (FIFOs, sockets, device nodes)
    GFileType ftype = g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr);
    if (ftype != G_FILE_TYPE_REGULAR && ftype != G_FILE_TYPE_SYMBOLIC_LINK) {
        ChecksumResult res;
        res.success = false;
        res.error = "Checksums only supported for regular files";
        callback(res);
        return;
    }

    m_cancellable = g_cancellable_new();
    m_aliveToken = std::make_shared<bool>(true);
    m_running = std::make_shared<std::atomic<bool>>(true);

    std::weak_ptr<bool> weakAlive = m_aliveToken;
    auto sharedRunning = m_running;
    auto* cancellable = G_CANCELLABLE(g_object_ref(m_cancellable));
    auto* targetFile = G_FILE(g_object_ref(file));

    m_workerThread = std::jthread([weakAlive, cancellable, targetFile, sharedRunning, cb = std::move(callback)]() {
        ChecksumResult result;
        GError* error = nullptr;

        // Verify resolved target is regular file before opening stream
        GFileType targetType = g_file_query_file_type(targetFile, G_FILE_QUERY_INFO_NONE, cancellable);
        if (targetType != G_FILE_TYPE_REGULAR) {
            result.success = false;
            result.error = "Not a regular file";
            g_object_unref(cancellable);
            g_object_unref(targetFile);
            sharedRunning->store(false);

            auto* ctx = new DispatchContext{
                std::move(cb),
                std::move(result),
                weakAlive
            };
            g_idle_add(onIdleDispatch, ctx);
            return;
        }

        GFileInputStream* stream = g_file_read(targetFile, cancellable, &error);
        if (error) {
            if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                g_error_free(error);
                g_object_unref(cancellable);
                g_object_unref(targetFile);
                sharedRunning->store(false);
                return;
            }
            result.success = false;
            result.error = error->message ? error->message : "Failed to open file for hashing";
            g_error_free(error);
        } else {
            GChecksum* md5 = g_checksum_new(G_CHECKSUM_MD5);
            GChecksum* sha256 = g_checksum_new(G_CHECKSUM_SHA256);

            constexpr size_t BUFFER_SIZE = 64 * 1024;
            auto buffer = std::make_unique<uint8_t[]>(BUFFER_SIZE);

            bool cancelled = false;
            bool readError = false;

            while (true) {
                if (g_cancellable_is_cancelled(cancellable)) {
                    cancelled = true;
                    break;
                }
                auto token = weakAlive.lock();
                if (!token || !*token) {
                    cancelled = true;
                    break;
                }

                GError* readErr = nullptr;
                gssize bytesRead = g_input_stream_read(
                    G_INPUT_STREAM(stream),
                    buffer.get(),
                    BUFFER_SIZE,
                    cancellable,
                    &readErr
                );

                if (readErr) {
                    if (g_error_matches(readErr, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                        cancelled = true;
                    } else {
                        result.success = false;
                        result.error = readErr->message ? readErr->message : "Error reading file data";
                        readError = true;
                    }
                    g_error_free(readErr);
                    break;
                }

                if (bytesRead <= 0) {
                    // Reached EOF
                    break;
                }

                g_checksum_update(md5, buffer.get(), static_cast<gssize>(bytesRead));
                g_checksum_update(sha256, buffer.get(), static_cast<gssize>(bytesRead));
            }

            g_input_stream_close(G_INPUT_STREAM(stream), nullptr, nullptr);
            g_object_unref(stream);

            if (cancelled) {
                g_checksum_free(md5);
                g_checksum_free(sha256);
                g_object_unref(cancellable);
                g_object_unref(targetFile);
                sharedRunning->store(false);
                return;
            }

            if (!readError) {
                const char* md5Str = g_checksum_get_string(md5);
                const char* sha256Str = g_checksum_get_string(sha256);
                result.md5 = md5Str ? md5Str : "";
                result.sha256 = sha256Str ? sha256Str : "";
                result.success = true;
            }

            g_checksum_free(md5);
            g_checksum_free(sha256);
        }

        g_object_unref(cancellable);
        g_object_unref(targetFile);
        sharedRunning->store(false);

        // Schedule callback delivery on the GTK main loop
        auto* ctx = new DispatchContext{
            std::move(cb),
            std::move(result),
            weakAlive
        };
        g_idle_add(onIdleDispatch, ctx);
    });
}

void ChecksumHelper::cancel() {
    if (m_running) {
        m_running->store(false);
    }
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_clear_object(&m_cancellable);
    }
    if (m_aliveToken) {
        *m_aliveToken = false;
        m_aliveToken.reset();
    }
    if (m_workerThread.joinable()) {
        m_workerThread.detach();
    }
}

gboolean ChecksumHelper::onIdleDispatch(gpointer userData) {
    auto* ctx = static_cast<DispatchContext*>(userData);
    if (ctx) {
        auto token = ctx->aliveToken.lock();
        if (token && *token) {
            if (ctx->callback) {
                ctx->callback(ctx->result);
            }
        }
        delete ctx;
    }
    return G_SOURCE_REMOVE;
}

} // namespace dfiles::inspector
