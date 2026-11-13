#pragma once

#include <gio/gio.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dfiles::inspector {

/**
 * @brief Result structure containing MD5 and SHA-256 digest outputs.
 */
struct ChecksumResult {
    std::string md5;
    std::string sha256;
    bool success{false};
    std::string error;
};

/**
 * @brief Asynchronous file checksum generator for MD5 and SHA-256.
 *
 * Computes hashes asynchronously in 64KB blocks without blocking the GTK UI thread.
 * Supports clean cancellation and guarantees thread-safe dispatch of results back to
 * the GTK main loop via g_idle_add.
 */
class ChecksumHelper {
public:
    ChecksumHelper();
    ~ChecksumHelper();

    ChecksumHelper(const ChecksumHelper&) = delete;
    ChecksumHelper& operator=(const ChecksumHelper&) = delete;
    ChecksumHelper(ChecksumHelper&&) = delete;
    ChecksumHelper& operator=(ChecksumHelper&&) = delete;

    /**
     * @brief Asynchronously computes MD5 and SHA-256 checksums for the given GFile.
     * @param file Target file to compute hashes for.
     * @param callback Completion callback invoked on the GTK main thread.
     */
    void computeAsync(GFile* file, std::function<void(const ChecksumResult&)> callback);

    /**
     * @brief Cancels any in-flight hashing operation.
     */
    void cancel();

    /**
     * @brief Returns true if a checksum operation is currently active.
     */
    [[nodiscard]] bool isRunning() const noexcept {
        return m_running && m_running->load();
    }

private:
    struct DispatchContext {
        std::function<void(const ChecksumResult&)> callback;
        ChecksumResult result;
        std::weak_ptr<bool> aliveToken;
    };

    static gboolean onIdleDispatch(gpointer userData);

    GCancellable* m_cancellable{nullptr};
    std::shared_ptr<bool> m_aliveToken;
    std::shared_ptr<std::atomic<bool>> m_running;
    std::jthread m_workerThread;
};

} // namespace dfiles::inspector
