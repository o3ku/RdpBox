#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace updater
{
struct ReleaseAsset
{
    std::wstring tagName;
    std::wstring assetName;
    std::wstring downloadUrl;
};
// Cross-thread cancellation for the WinHTTP update requests. cancel() closes
// the in-flight request handle, which unblocks a stalled socket that every
// per-operation timeout would otherwise keep alive (a slow-drip mirror). The
// worker thread simply sees a failed request. Off Windows no handles exist
// and cancel() only sets the flag.
class DownloadCancellation
{
public:
    // Internal bookkeeping shared with the winhttp layer: exactly one closer.
    struct RequestSlot
    {
        void *handle = nullptr;
        std::atomic_bool closed{false};
    };

    void cancel();
    bool cancelled() const { return m_cancelled.load(std::memory_order_acquire); }
    void adoptRequestSlot(const std::shared_ptr<RequestSlot> &slot);

private:
    std::atomic_bool m_cancelled{false};
    std::mutex m_mutex;
    std::vector<std::shared_ptr<RequestSlot>> m_slots;
};

bool isNewerReleaseTag(const std::wstring &currentTag, const std::wstring &candidateTag);
// Network proxy policy for update traffic. Held in memory; each front end
// loads/persists its own setting (Qt General tab / MFC Logo menu) and pushes
// it here, so both UIs share one policy without sharing a settings store.
enum class ProxyMode
{
    System,  // WinHTTP auto proxy (WPAD / system settings) - the default
    None,    // direct connections only
    Manual,  // fixed "host:port" proxy
};

struct ProxyConfig
{
    ProxyMode mode = ProxyMode::System;
    std::wstring manualProxy;  // "host:port", used only in Manual mode
};

const ProxyConfig &currentProxyConfig();
void setCurrentProxyConfig(const ProxyConfig &config);

// WinHttpOpen access type (WINHTTP_ACCESS_TYPE_*) for a proxy config.
unsigned long winhttpAccessTypeFor(const ProxyConfig &proxy);

bool fetchLatestRelease(const std::wstring &owner,
                        const std::wstring &repository,
                        const std::wstring &assetName,
                        ReleaseAsset &asset,
                        std::wstring &errorMessage,
                        DownloadCancellation *cancellation = nullptr);
using DownloadProgressCallback = std::function<void(std::uint64_t bytesReceived, std::uint64_t totalBytes)>;
bool downloadReleaseAsset(const ReleaseAsset &asset,
                          const std::wstring &targetPath,
                          std::wstring &errorMessage,
                          DownloadProgressCallback progressCallback = {},
                          DownloadCancellation *cancellation = nullptr);

// Verifies the downloaded copy of the release asset named `assetName` against
// the same release's SHA256SUMS.txt asset. Returns false when the sums asset
// is missing, does not list the asset, or the digest does not match. Callers
// must refuse to execute a download that fails this check.
bool verifyDownloadedAssetSha256(const std::wstring &owner,
                                 const std::wstring &repository,
                                 const std::wstring &assetName,
                                 const std::wstring &downloadedPath,
                                 std::wstring &errorMessage);

// Writes the apply-update PowerShell script and spawns it. Returns true once the
// helper process is running; it waits for this process to exit, replaces the exe
// and relaunches it with the given connection names restored.
bool applyDownloadedUpdate(const std::wstring &downloadedPath,
                           const std::wstring &currentExePath,
                           const std::vector<std::wstring> &connectionNames);
}
