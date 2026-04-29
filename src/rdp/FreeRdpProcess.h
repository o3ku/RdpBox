#pragma once

#include "common/NativeTypes.h"

#include <cstdint>
#include <optional>
#include <functional>
#include <memory>
#include <string>

class FreeRdpProcess
{
public:
    enum class State { Idle, Starting, Running, Finished };

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
    FrameBuffer frame() const;
    void writeFrameFromContext(void *rdpContext);
    std::optional<FrameBuffer> frameIfNewer(uint64_t &lastSeenGeneration);
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
    void sendKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);
    void sendMouseMove(PointI pos, SizeI viewSize);
    void sendMouseButton(MouseButton button, bool down, PointI pos, SizeI viewSize);
    void sendWheel(PointI angleDelta, PointI pos, SizeI viewSize);
    void requestResize(SizeI size);

    ConnectionInfo connectionInfo() const;

    void updateStateFromBackend(State state);
    void updateCursorFromBackend(const CursorInfo &cursor);
    void resetCursorFromBackend();
    void attachClipboardChannel(void *channelContext);
    void detachClipboardChannel();
    bool challengeCertificate(const CertificateChallenge &challenge);

private:
    struct Private;

    std::unique_ptr<Private> m_d;
};
