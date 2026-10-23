#pragma once

#include "common/NativeTypes.h"
#include "common/rdp/RdpInputEventUtil.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <functional>
#include <memory>
#include <string>

#include <winpr/wtypes.h>

// The worker thread runs a FreeRdpBackend, not the object the UI holds. A
// connection that stop() had to abandon (a wedged freerdp_disconnect must not
// freeze the window) keeps running against its own backend and releases the
// freerdp context on its way out. The process object is reached only through a
// guarded owner pointer, never after that pointer was cleared.
class FreeRdpBackend;

class FreeRdpProcess
{
public:
    enum class State { Idle, Starting, Running, Finished };

    // Hand-over token: set by the worker once it has released the freerdp
    // context, so the UI side knows it no longer has to wait for it (nor free
    // it a second time).
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

private:
    struct Private;

    // Runs one connection on its own thread. The worker owns the backend (and
    // with it the freerdp context) from the moment it starts: it releases both
    // on its way out, whatever stop() decided in the meantime.
    void runWorker(std::shared_ptr<FreeRdpBackend> backend);

    static void publishFromWorkerBackend(FreeRdpBackend &backend,
                                         State state, const std::string &error = {});
    static void applyPendingRequests(const std::shared_ptr<FreeRdpBackend> &backend);

    void publishFromWorker(FreeRdpBackend &backend, State state, const std::string &error = {});
    void submit(std::function<void(void *, FreeRdpBackend &)> apply);
    void sendMousePointer(PointI pos, SizeI viewSize, std::uint16_t extraFlags, bool extended);
    std::unique_ptr<Private> m_d;
};
