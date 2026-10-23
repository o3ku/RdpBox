#pragma once

#include "common/NativeTypes.h"
#include "common/rdp/RdpInputEventUtil.h"

#include <atomic>
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>

#include <winpr/wtypes.h>

// The worker thread runs a FreeRdpBackend, not the object the UI holds: a
// session that has to be abandoned (stop() gives up waiting for a wedged
// freerdp_disconnect) keeps publishing into its own backend, which lives until
// the thread ends. The process is only reached through the owner pointer, and
// only while the alive flag says it is still there.
class FreeRdpBackend;

// Forwarding a pointer type freerdp declares is cheaper than pulling its headers
// into every consumer of this class. Write the pointer as void * and convert.
class FreeRdpProcess
{
public:
    enum class State { Idle, Starting, Running, Finished };

    // Set once the freerdp context is gone, so the owner knows it no longer
    // has to wait for (or release) anything the worker holds.
    struct Abandoned
    {
        std::atomic_bool released{false};
    };

    struct ConnectionInfo
    {
        std::string codecName;
        std::uint32_t rtt = 0;
        bool rttAvailable = false;
    };

    struct CertificateChallenge
    {
        std::wstring host;
        int port = 0;
        std::wstring commonName;
        std::wstring subject;
        std::wstring issuer;
        std::wstring fingerprint;
        bool changed = false;
    };

    FreeRdpProcess();
    ~FreeRdpProcess();

    void start(const std::wstring &host,
               int port,
               const std::wstring &username,
               const std::wstring &password,
               const std::wstring &domain = {},
               int width = 0,
               int height = 0,
               bool clipboardEnabled = true,
               bool ignoreCertificate = true);
    void stop();

    State state() const;
    void writeFrameFromContext(void *rdpContext);
    bool consumeFrameIfNewer(uint64_t &lastSeenGeneration, FrameBuffer &target);
    SizeI desktopSize() const;
    CursorInfo cursor() const;

    // Callbacks may be invoked from the FreeRDP worker thread after start()
    // returns. Consumers must marshal to their own thread before touching
    // thread-affine UI objects. No UI-thread marshalling is performed here.
    void setStateChangedCallback(std::function<void(State)> callback);
    void setFrameUpdatedCallback(std::function<void()> callback);
    void setDesktopResizedCallback(std::function<void(const SizeI &)> callback);
    void setCursorUpdatedCallback(std::function<void()> callback);
    void setCertificateChallengeCallback(std::function<void(const CertificateChallenge &)> callback);
    void resolveCertificateChallenge(bool accept);

    void sendFocusIn();
    void sendKey(const KeyIdentifier &key, bool down, bool wasDown = false);
    void sendKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);
    void sendMouseMove(PointI pos, SizeI viewSize);
    void sendMouseButton(MouseButton button, bool down, PointI pos, SizeI viewSize);
    void sendWheel(PointI angleDelta, PointI pos, SizeI viewSize);
    void requestResize(SizeI size);
    void requestRefresh();

    ConnectionInfo connectionInfo() const;
    std::string lastDisconnectError() const;

    void updateStateFromBackend(State state);
    void updateCursorFromBackend(const CursorInfo &cursor);
    void resetCursorFromBackend();
    void attachClipboardChannel(void *channelContext);
    void detachClipboardChannel();
    bool challengeCertificate(const CertificateChallenge &challenge);

    // Worker-side entry points. They take the session they belong to, so a
    // worker that outlives this object never has to look this way.
    static void publishFromWorkerBackend(FreeRdpBackend &backend,
                                         State state,
                                         const std::string &lastError = {});
    static void applyPendingRequests(const std::shared_ptr<FreeRdpBackend> &backend);

private:
    struct Private;

    // Hands the connection over to a worker that owns its own state. Called
    // with the context already configured and started.
    std::shared_ptr<FreeRdpBackend> startWorker(void *context);

    void publishFromWorker(FreeRdpBackend &backend, State state, const std::string &lastError = {});
    // Queues one input/resize request for the worker thread. Nothing in here
    // touches freerdp directly: a detached worker must not have the UI thread
    // call into a context it may already be tearing down.
    void submit(std::function<void(void *, FreeRdpBackend &)> apply);
    void sendMousePointer(PointI pos, SizeI viewSize, std::uint16_t flags, bool extended);

    std::unique_ptr<Private> m_d;
};
