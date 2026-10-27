#include "FreeRdpProcess.h"

#include "common/Win32String.h"
#include "common/rdp/FrameBufferMemory.h"
#include "common/rdp/FreeRdpProcessNative.h"
#include "common/rdp/RdpInputEventUtil.h"
#include "common/rdp/RdpSessionViewBehavior.h"
#include "RdpClipboardBridge.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <freerdp/autodetect.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/codecs.h>
#include <freerdp/constants.h>
#include <freerdp/input.h>
#include <freerdp/settings_types.h>
#include <freerdp/update.h>

#ifdef _WIN32
#include <windows.h>
#else
// winpr provides the Win32 synchronization surface (CreateEventW, Sleep,
// WaitForMultipleObjects, DWORD...) on POSIX.
#include <winpr/winpr.h>
#include <netdb.h>
#include <cstdio>
#endif

namespace
{
constexpr DWORD kEventWaitTimeoutMs = 10;

// FreeRDP resolves the host twice before connecting (a resolvability pre-check
// plus the real resolve) and aborts the whole connection on a single
// getaddrinfo failure, reporting "DNS host name was not found" even for
// numeric IPs when the Windows resolver stalls transiently (sleep/resume,
// adapter switch). Gate the connection on our own bounded retry instead.
int resolveWithRetry(const char *host, UINT32 port)
{
    char portStr[16] = {};
    std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int lastError = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        addrinfo *result = nullptr;
        const int rc = ::getaddrinfo(host, portStr, &hints, &result);
        if (rc == 0) {
            ::freeaddrinfo(result);
            return 0;
        }
#ifdef _WIN32
        lastError = ::WSAGetLastError();
#else
        lastError = rc;  // getaddrinfo error code (no Winsock layer off Windows)
#endif
        if (attempt + 1 < 8)
            ::Sleep(300);
    }
    return lastError;
}
}

// One connection attempt, owned by its worker. Everything a worker that
// outlives the process can still touch lives here: the freerdp context, the
// input queue, the certificate handshake. The owning process reads it through
// the mutex and the state it publishes through publishFromWorker().
class FreeRdpBackend
{
public:
    FreeRdpBackend(FreeRdpProcess *owner,
                   rdpContext *context,
                   HANDLE certDecidedEvent,
                   HANDLE certAbortEvent)
        : certDecidedEvent(certDecidedEvent)
        , certAbortEvent(certAbortEvent)
        , alive(std::make_shared<FreeRdpProcess::Abandoned>())
        , m_owner(owner)
        , m_context(context)
    {
    }

    FreeRdpProcess *owner() const { return m_owner.load(); }

    // Called by the process once it gives up on this worker: from then on the
    // worker publishes nothing and never reaches back into the process.
    void abandon()
    {
        m_owner.store(nullptr);
    }

    // The worker takes the session from here: it is the one who ends up
    // releasing the context.
    void claim(FreeRdpProcess *owner) { m_owner.store(owner); }

    // The worker is done with the process. The context is released before
    // this, so no path from the worker can still reach `owner`.
    void release() { m_owner.store(nullptr); }

    rdpContext *context() const
    {
        std::scoped_lock lock(mutex);
        return m_context;
    }

    // Called by the process before it aborts, so the abort is ordered against
    // whatever the worker is doing with the same context.
    rdpContext *lockedContext(std::unique_lock<std::mutex> &lock) const
    {
        lock = std::unique_lock<std::mutex>(mutex);
        return m_context;
    }

    // Releases the freerdp context exactly once, whichever side gets here
    // first: the worker in its own unwinding, or stop() when the worker
    // reported that it let go. A null context means someone already did.
    void freeContext()
    {
        rdpContext *owned = nullptr;
        {
            std::scoped_lock lock(mutex);
            owned = std::exchange(m_context, nullptr);
            stopRequested = true;
            appliers.clear();

            // Detach the callbacks before the context unwinds; the worker keeps
            // its own state from here on. Under the mutex so it cannot race
            // the owner clearing stop() does on the abandon path.
            if (owned) {
                if (NativeRdpContext *nativeContext = toNativeContext(owned))
                    nativeContext->owner = nullptr;
            }
        }

        if (!owned)
            return;

        freerdp_client_stop(owned);
        freerdp_client_context_free(owned);
    }

    // The certificate handshake is answered on the worker thread, so a worker
    // that outlives the process still has events to wait on.
    HANDLE certDecidedEvent;
    HANDLE certAbortEvent;
    std::atomic_bool certDecisionAccepted{false};
    // Guarded by `mutex`: read and set together with the context while the
    // worker is still applying queued requests through it.
    bool stopRequested = false;
    // Cleared by the worker once it has released everything it holds.
    std::shared_ptr<FreeRdpProcess::Abandoned> alive;

    // Last desktop size the worker published; input mapping runs on the worker
    // thread and must use the size its own frame came from.
    SizeI desktopSize;

    // Requests from the UI thread, applied by the worker on its own thread.
    // Each entry closes over what it needs, so the queue holds no pointers
    // beyond the context the worker already owns.
    struct Applier
    {
        rdpContext *context;
        std::function<void(void *, FreeRdpBackend &)> apply;
    };
    mutable std::mutex mutex;
    std::vector<Applier> appliers;

private:
    std::atomic<FreeRdpProcess *> m_owner;
    // Written under `mutex` and dropped by freeContext(), so a process that
    // stops before its worker starts still frees the context exactly once.
    rdpContext *m_context;
};

struct FreeRdpProcess::Private
{
    // The freerdp context is not held here: it belongs to the worker's
    // FreeRdpBackend from start() on, so stop() can never free a context the
    // worker is still using.
    std::shared_ptr<FreeRdpBackend> backend;
    // Joined (or detached) by stop(); stop() is also called from start(), so
    // a start/stop cycle never leaves a thread behind.
    std::thread worker;
    mutable std::mutex mutex;
    FrameBuffer frame;
    uint64_t frameGeneration = 0;
    SizeI desktopSize;
    CursorInfo cursor;
    bool hasFirstFrame = false;
    std::unique_ptr<RdpClipboardBridge> clipboard;
    // The clipboard channel is attached on the worker thread and detached by
    // stop(), possibly after the session was abandoned.
    std::mutex clipboardMutex;
    State state = State::Idle;
    std::string lastDisconnectError;

    std::function<void(State)> stateChanged;
    std::function<void()> frameUpdated;
    std::function<void(const SizeI &)> desktopResized;
    std::function<void()> cursorUpdated;
    std::function<void(const CertificateChallenge &)> certificateChallenge;

    // Signalled by resolveCertificateChallenge() and by stop(); the worker
    // waits on both, so the handshake needs no timeout and no polling.
    HANDLE certDecidedEvent = nullptr;
    HANDLE certAbortEvent = nullptr;
};

FreeRdpProcess::FreeRdpProcess()
    : m_d(std::make_unique<Private>())
{
    m_d->cursor = defaultCursorInfo();
    m_d->certDecidedEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_d->certAbortEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

FreeRdpProcess::~FreeRdpProcess()
{
    stop();

    std::scoped_lock lock(m_d->mutex);
    destroyCursorInfo(m_d->cursor);
    if (m_d->certDecidedEvent) {
        ::CloseHandle(m_d->certDecidedEvent);
        m_d->certDecidedEvent = nullptr;
    }
    if (m_d->certAbortEvent) {
        ::CloseHandle(m_d->certAbortEvent);
        m_d->certAbortEvent = nullptr;
    }
}

void FreeRdpProcess::start(const std::wstring &host,
                           int port,
                           const std::wstring &username,
                           const std::wstring &password,
                           const std::wstring &domain,
                           int width,
                           int height,
                           bool clipboardEnabled,
                           bool ignoreCertificate)
{
    stop();

    RDP_CLIENT_ENTRY_POINTS entryPoints = {};
    entryPoints.Size = sizeof(entryPoints);
    entryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
    entryPoints.GlobalInit = nativefreerdp_global_init;
    entryPoints.GlobalUninit = nativefreerdp_global_uninit;
    entryPoints.ContextSize = sizeof(NativeRdpContext);
    entryPoints.ClientNew = nativefreerdp_client_new;
    entryPoints.ClientFree = nativefreerdp_client_free;
    entryPoints.ClientStart = nativefreerdp_client_start;
    entryPoints.ClientStop = nativefreerdp_client_stop;

    rdpContext *context = freerdp_client_context_new(&entryPoints);
    if (!context) {
        updateStateFromBackend(State::Finished);
        return;
    }

    NativeRdpContext *nativeContext = toNativeContext(context);
    // `locked` is the callback route's gate: every freerdp callback into this
    // object takes it, and stop() takes it to cut the route when a worker is
    // abandoned. Leaving it null makes every callback a silent no-op.
    nativeContext->locked = std::make_shared<NativeRdpContext::InvokeLock>();
    nativeContext->owner = this;
    nativeContext->ignoreCertificate = ignoreCertificate;

    rdpSettings *settings = context->settings;
    const SizeI desktopSize = rdp::session_view::clampedDesktopSize(SizeI{width, height});
    const UINT32 desktopWidth = static_cast<UINT32>(desktopSize.width);
    const UINT32 desktopHeight = static_cast<UINT32>(desktopSize.height);
    const std::string hostUtf8 = utf8FromWide(host);
    const std::string usernameUtf8 = utf8FromWide(username);
    const std::string passwordUtf8 = utf8FromWide(password);
    const std::string domainUtf8 = utf8FromWide(domain);
    const UINT32 serverPort = static_cast<UINT32>(std::clamp(port, 1, 65535));

    const bool configured =
        freerdp_settings_set_string(settings, FreeRDP_ServerHostname, hostUtf8.c_str()) &&
        freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, serverPort) &&
        freerdp_settings_set_string(settings, FreeRDP_Username, usernameUtf8.c_str()) &&
        freerdp_settings_set_string(settings, FreeRDP_Password, passwordUtf8.c_str()) &&
        freerdp_settings_set_string(settings, FreeRDP_Domain, domainUtf8.c_str()) &&
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, desktopWidth) &&
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, desktopHeight) &&
        freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32) &&
        freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, ignoreCertificate) &&
        freerdp_settings_set_bool(settings, FreeRDP_SoftwareGdi, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, clipboardEnabled);

    if (configured) {
        freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_JpegCodec, TRUE);
        freerdp_settings_set_uint32(settings, FreeRDP_JpegQuality, 75);
        freerdp_settings_set_bool(settings, FreeRDP_GfxH264, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_NetworkAutoDetect, TRUE);
    }

    if (!configured || freerdp_client_start(context) != 0) {
        freerdp_client_context_free(context);
        updateStateFromBackend(State::Finished);
        return;
    }

    CursorInfo oldCursor;
    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, defaultCursorInfo());
        FrameBufferMemory::release(m_d->frame);
        m_d->desktopSize = SizeI{static_cast<int>(desktopWidth), static_cast<int>(desktopHeight)};
        m_d->hasFirstFrame = false;
    }
    destroyCursorInfo(oldCursor);

    // The worker may publish a state change before this call returns (a
    // refused connection reports Finished immediately). It only reaches this
    // object while `owner` is set, and that cannot happen before the last
    // statement below, so the caller's reference is enough: nothing here
    // ref-counts the process itself.
    //
    // Last chance to give up: from here on the context belongs to the worker,
    // which releases it exactly once whether or not a stop() races this call.
    std::shared_ptr<FreeRdpBackend> backend = std::make_shared<FreeRdpBackend>(
        this, context, m_d->certDecidedEvent, m_d->certAbortEvent);
    {
        std::scoped_lock lock(m_d->mutex);
        backend->desktopSize = m_d->desktopSize;
        m_d->backend = backend;
    }

    std::thread worker(&FreeRdpProcess::runWorker, this, backend);
    {
        std::scoped_lock lock(m_d->mutex);
        m_d->worker = std::move(worker);
    }
    updateStateFromBackend(State::Starting);
}

void FreeRdpProcess::runWorker(std::shared_ptr<FreeRdpBackend> backend)
{
    std::shared_ptr<FreeRdpProcess::Abandoned> aliveFlag = backend->alive;

    // The freerdp context is released here, exactly once, on the thread that
    // was using it: stop() hands the session over instead of freeing it
    // behind our back, and by the time this runs stop() may already be gone.
    struct Guard
    {
        std::shared_ptr<FreeRdpBackend> backend;
        std::shared_ptr<FreeRdpProcess::Abandoned> alive;

        ~Guard()
        {
            // Stop the channels before the context goes: they are torn down
            // here, on the worker thread, not from a stop() that gave up.
            if (rdpContext *owned = backend->context()) {
                if (owned->instance)
                    freerdp_disconnect(owned->instance);
                backend->freeContext();
            }
            // Cleared last: only now may the owner destroy itself, with
            // every path back to the process already closed.
            backend->release();
            alive->released.store(true);
        }
    } guard{backend, aliveFlag};

    rdpContext *context = backend->context();
    if (!context || !context->instance) {
        FreeRdpProcess::publishFromWorkerBackend(*backend, FreeRdpProcess::State::Finished);
        return;
    }

    // Take the session over. It was handed out before this thread existed, so
    // the process may have stopped it already: then the guard releases
    // everything and nothing is published.
    {
        std::scoped_lock lock(backend->mutex);
        if (backend->stopRequested)
            return;
        // Re-arm the callback route for this connection; stop() clears it on
        // the abandon path, which only covers the window before this point.
        if (NativeRdpContext *nativeContext = toNativeContext(context)) {
            nativeContext->owner = this;
        }
        backend->claim(this);
    }

    freerdp *instance = context->instance;

    const char *host = freerdp_settings_get_string(context->settings, FreeRDP_ServerHostname);
    const UINT32 port = freerdp_settings_get_uint32(context->settings, FreeRDP_ServerPort);
    const int resolveError = host ? resolveWithRetry(host, port) : 0;
    if (resolveError != 0) {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "Could not resolve %.64s after retries (resolver error %d).",
                      host, resolveError);
        FreeRdpProcess::publishFromWorkerBackend(*backend, FreeRdpProcess::State::Finished, message);
        return;
    }

    const BOOL connected = freerdp_connect(instance);

    if (connected) {
        for (;;) {
            bool stopRequested = false;
            {
                std::scoped_lock lock(backend->mutex);
                stopRequested = backend->stopRequested;
            }
            if (stopRequested || freerdp_shall_disconnect_context(context))
                break;

            HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
            const DWORD handleCount = freerdp_get_event_handles(context, handles, MAXIMUM_WAIT_OBJECTS);
            if (handleCount == 0)
                break;

            const DWORD waitResult = WaitForMultipleObjects(handleCount, handles, FALSE, kEventWaitTimeoutMs);
            if (waitResult == WAIT_TIMEOUT) {
                applyPendingRequests(backend);
                continue;
            }
            if (waitResult == WAIT_FAILED)
                break;
            if (!freerdp_check_event_handles(context))
                break;

            applyPendingRequests(backend);
        }
    }

    freerdp_abort_connect_context(context);
    freerdp_disconnect(instance);

    std::string lastErrorText;
    const UINT32 lastError = freerdp_get_last_error(context);
    if (lastError != FREERDP_ERROR_SUCCESS) {
        const char *errorStr = freerdp_get_last_error_string(lastError);
        if (errorStr && *errorStr)
            lastErrorText = errorStr;
    }
    FreeRdpProcess::publishFromWorkerBackend(*backend, FreeRdpProcess::State::Finished, lastErrorText);
}

void FreeRdpProcess::applyPendingRequests(const std::shared_ptr<FreeRdpBackend> &backend)
{
    std::vector<FreeRdpBackend::Applier> pending;
    {
        std::scoped_lock lock(backend->mutex);
        if (backend->appliers.empty())
            return;
        pending.swap(backend->appliers);
    }

    for (auto &request : pending) {
        if (!request.context || !request.apply)
            continue;
        request.apply(request.context, *backend);
    }
}

void FreeRdpProcess::stop()
{
    std::shared_ptr<FreeRdpBackend> backend;
    std::thread worker;
    {
        std::scoped_lock lock(m_d->mutex);
        backend = std::exchange(m_d->backend, nullptr);
        worker = std::move(m_d->worker);
    }

    if (backend) {
        {
            // Order the abort against the worker's own use of the context:
            // the same lock guards the pointer freeContext() clears.
            std::unique_lock<std::mutex> lock;
            if (rdpContext *context = backend->lockedContext(lock)) {
                // Set before aborting, so the worker sees it as soon as
                // freerdp_connect() returns.
                backend->stopRequested = true;
                freerdp_abort_connect_context(context);
            } else {
                backend->stopRequested = true;
            }
        }

        if (backend->certAbortEvent)
            ::SetEvent(backend->certAbortEvent);

        // The worker honours stopRequested within one event wait, unless
        // freerdp_disconnect() itself wedges (hung server, half-open TCP):
        // stop() runs on the UI thread (destructors, tab switches, resume
        // handling) and waiting for it there would freeze the whole window.
        using namespace std::chrono_literals;
        constexpr auto kStopGrace = 5s;
        constexpr auto kPollInterval = 10ms;
        const auto deadline = std::chrono::steady_clock::now() + kStopGrace;
        while (!backend->alive->released.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(kPollInterval);

        if (backend->alive->released.load()) {
            // It let go and released the context itself, so the thread is on
            // its last few instructions; join it so a start/stop cycle cannot
            // pile threads up.
            if (worker.joinable()) {
                worker.join();
            } else {
                // No thread to wait for (start() failed, or the worker was
                // already detached): release what it never got to.
                backend->freeContext();
                backend->abandon();
            }
        } else {
            // It still owns the context. Hand the session over: the worker
            // keeps publishing into its own object and releases the freerdp
            // context when it finally unwinds. Leaking one dead session beats
            // freezing the window or freeing memory a live thread still uses.
            std::fprintf(stderr, "RdpBox: FreeRDP worker still running after %lldms grace; "
                                 "abandoning this session.\n",
                         static_cast<long long>(kStopGrace / 1ms));
            backend->abandon();

            // Close the last path back: the worker outlives this object, so
            // its callbacks must stop reaching it before stop() returns. The
            // context stays alive while the backend mutex is held, and the
            // lock waits out a callback that is already inside.
            {
                std::unique_lock<std::mutex> lock;
                if (rdpContext *context = backend->lockedContext(lock))
                    nativefreerdp_detach_owner(toNativeContext(context));
            }

            if (worker.joinable())
                worker.detach();
        }
    } else if (worker.joinable()) {
        worker.join();
    }

    if (m_d->certAbortEvent)
        ::ResetEvent(m_d->certAbortEvent);

    CursorInfo oldCursor;
    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, defaultCursorInfo());
        FrameBufferMemory::release(m_d->frame);
        m_d->desktopSize = {};
        m_d->hasFirstFrame = false;
        m_d->frameGeneration = 0;
    }
    destroyCursorInfo(oldCursor);

    {
        std::lock_guard<std::mutex> lock(m_d->clipboardMutex);
        if (m_d->clipboard)
            m_d->clipboard->detach();
    }
}

void FreeRdpProcess::publishFromWorkerBackend(FreeRdpBackend &backend,
                                             State state,
                                             const std::string &lastError)
{
    // Read the owner exactly once: it can turn null under us, and a stale read
    // is the difference between publishing a state change and writing into a
    // destroyed object.
    FreeRdpProcess *owner = backend.owner();
    if (!owner)
        return;

    // No lifetime dance here: `owner` is only non-null while the process still
    // knows this session. A process destroyed mid-publish is a bug in the
    // caller (stop() clears the owner before it lets go), not something to
    // paper over with a self-reference.
    owner->publishFromWorker(backend, state, lastError);
}

void FreeRdpProcess::publishFromWorker(FreeRdpBackend &backend, State state, const std::string &lastError)
{
    static_cast<void>(backend);
    std::function<void(State)> callback;
    {
        std::scoped_lock lock(m_d->mutex);
        if (!lastError.empty())
            m_d->lastDisconnectError = lastError;
        if (m_d->state == state)
            return;
        m_d->state = state;
        callback = m_d->stateChanged;
    }

    if (callback)
        callback(state);
}

FreeRdpProcess::State FreeRdpProcess::state() const
{
    std::scoped_lock lock(m_d->mutex);
    return m_d->state;
}

std::string FreeRdpProcess::lastDisconnectError() const
{
    std::scoped_lock lock(m_d->mutex);
    return m_d->lastDisconnectError;
}

bool FreeRdpProcess::consumeFrameIfNewer(uint64_t &lastSeenGeneration, FrameBuffer &target)
{
    std::scoped_lock lock(m_d->mutex);
    if (m_d->frameGeneration == lastSeenGeneration || m_d->frame.empty())
        return false;

    lastSeenGeneration = m_d->frameGeneration;
    target = std::move(m_d->frame);
    return true;
}

SizeI FreeRdpProcess::desktopSize() const
{
    std::scoped_lock lock(m_d->mutex);
    return m_d->desktopSize;
}

CursorInfo FreeRdpProcess::cursor() const
{
    std::scoped_lock lock(m_d->mutex);
    return duplicateCursorInfo(m_d->cursor);
}

void FreeRdpProcess::setStateChangedCallback(std::function<void(State)> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->stateChanged = std::move(callback);
}

void FreeRdpProcess::setFrameUpdatedCallback(std::function<void()> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->frameUpdated = std::move(callback);
}

void FreeRdpProcess::setDesktopResizedCallback(std::function<void(const SizeI &)> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->desktopResized = std::move(callback);
}

void FreeRdpProcess::setCursorUpdatedCallback(std::function<void()> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->cursorUpdated = std::move(callback);
}

void FreeRdpProcess::setCertificateChallengeCallback(std::function<void(const CertificateChallenge &)> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->certificateChallenge = std::move(callback);
}

void FreeRdpProcess::resolveCertificateChallenge(bool accept)
{
    std::shared_ptr<FreeRdpBackend> backend;
    {
        std::scoped_lock lock(m_d->mutex);
        backend = m_d->backend;
    }

    if (!backend)
        return;

    backend->certDecisionAccepted = accept;
    if (backend->certDecidedEvent)
        ::SetEvent(backend->certDecidedEvent);
}

bool FreeRdpProcess::challengeCertificate(const CertificateChallenge &challenge)
{
    std::shared_ptr<FreeRdpBackend> backend;
    std::function<void(const CertificateChallenge &)> callback;
    {
        std::scoped_lock lock(m_d->mutex);
        backend = m_d->backend;
        callback = m_d->certificateChallenge;
    }

    if (!backend || !callback || !backend->certDecidedEvent || !backend->certAbortEvent)
        return false;

    backend->certDecisionAccepted = false;
    ::ResetEvent(backend->certDecidedEvent);

    callback(challenge);

    HANDLE handles[2] = { backend->certDecidedEvent, backend->certAbortEvent };
    const DWORD waitResult = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (waitResult != WAIT_OBJECT_0)
        return false;

    return backend->certDecisionAccepted;
}

void FreeRdpProcess::submit(std::function<void(void *, FreeRdpBackend &)> apply)
{
    std::shared_ptr<FreeRdpBackend> backend;
    {
        std::scoped_lock lock(m_d->mutex);
        backend = m_d->backend;
    }

    if (!backend)
        return;

    // The queue is drained by the worker between event waits, so an input
    // request never runs freerdp on this thread. Taking the process lock first
    // and the session lock second keeps that ordering one-way; the context is
    // read under that same lock, since context() would take it again.
    try {
        std::unique_lock<std::mutex> lock;
        rdpContext *context = backend->lockedContext(lock);
        if (!context || backend->stopRequested)
            return;
        backend->appliers.push_back({context, std::move(apply)});
    } catch (const std::system_error &) {
        // The session is being torn down behind this call (stop() frees the
        // backend while an input event still reaches it through a nested
        // message pump). Dropping one input beat beats crashing the process.
    }
}

void FreeRdpProcess::sendFocusIn()
{
    submit([](void *rawContext, FreeRdpBackend &) {
        auto *context = static_cast<rdpContext *>(rawContext);
        if (!context->input)
            return;
        freerdp_input_send_focus_in_event(context->input, currentToggleState());
    });
}

void FreeRdpProcess::sendKey(const KeyIdentifier &key, bool down, bool wasDown)
{
    if (key.scanCode == 0)
        return;

    submit([key, down, wasDown](void *rawContext, FreeRdpBackend &) {
        auto *context = static_cast<rdpContext *>(rawContext);
        if (!context->input)
            return;

        UINT32 rdpScancode = MAKE_RDP_SCANCODE(static_cast<BYTE>(key.scanCode), key.extended);

        if (rdpScancode == RDP_SCANCODE_NUMLOCK_EXTENDED) {
            rdpScancode = RDP_SCANCODE_NUMLOCK;
        } else if (rdpScancode == RDP_SCANCODE_NUMLOCK) {
            if (down)
                freerdp_input_send_keyboard_pause_event(context->input);
            return;
        } else if (rdpScancode == RDP_SCANCODE_RSHIFT_EXTENDED) {
            rdpScancode = RDP_SCANCODE_RSHIFT;
        }

        freerdp_input_send_keyboard_event_ex(context->input, down, wasDown, rdpScancode);
    });
}

void FreeRdpProcess::sendKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam)
{
    const auto event = keyEventInfoFromMessage(message, wParam, lParam);
    if (!event)
        return;

    sendKey(event->key, event->down, event->wasDown);
}

void FreeRdpProcess::sendMousePointer(PointI pos, SizeI viewSize, std::uint16_t extraFlags, bool extended)
{
    submit([pos, viewSize, extraFlags, extended](void *rawContext, FreeRdpBackend &backend) {
        auto *context = static_cast<rdpContext *>(rawContext);
        const SizeI desktop = backend.desktopSize;
        if (desktop.width <= 0 || desktop.height <= 0 || viewSize.width <= 0 || viewSize.height <= 0)
            return;

        const PointI mapped = clampToDesktop(PointI{
            pos.x * desktop.width / viewSize.width,
            pos.y * desktop.height / viewSize.height
        }, desktop);

        if (extended) {
            freerdp_client_send_extended_button_event(&toNativeContext(context)->common,
                                                      FALSE, extraFlags, mapped.x, mapped.y);
            return;
        }

        freerdp_client_send_button_event(&toNativeContext(context)->common,
                                         FALSE, extraFlags, mapped.x, mapped.y);
    });
}

void FreeRdpProcess::sendMouseMove(PointI pos, SizeI viewSize)
{
    sendMousePointer(pos, viewSize, PTR_FLAGS_MOVE, false);
}

void FreeRdpProcess::sendMouseButton(MouseButton button, bool down, PointI pos, SizeI viewSize)
{
    if (button == MouseButton::Back || button == MouseButton::Forward) {
        UINT16 flags = (button == MouseButton::Back) ? PTR_XFLAGS_BUTTON1 : PTR_XFLAGS_BUTTON2;
        if (down)
            flags |= PTR_XFLAGS_DOWN;

        sendMousePointer(pos, viewSize, flags, true);
        return;
    }

    UINT16 flags = 0;
    switch (button) {
    case MouseButton::Left:
        flags |= PTR_FLAGS_BUTTON1;
        break;
    case MouseButton::Right:
        flags |= PTR_FLAGS_BUTTON2;
        break;
    case MouseButton::Middle:
        flags |= PTR_FLAGS_BUTTON3;
        break;
    default:
        return;
    }

    if (down)
        flags |= PTR_FLAGS_DOWN;

    sendMousePointer(pos, viewSize, flags, false);
}

void FreeRdpProcess::sendWheel(PointI angleDelta, PointI pos, SizeI viewSize)
{
    int delta = 0;
    bool horizontal = false;
    if (angleDelta.y != 0) {
        delta = angleDelta.y;
    } else if (angleDelta.x != 0) {
        delta = angleDelta.x;
        horizontal = true;
    }
    if (delta == 0)
        return;

    // One applier, so the pointer move and the wheel cannot be reordered
    // against each other or against another session's input.
    submit([pos, viewSize, delta, horizontal](void *rawContext, FreeRdpBackend &backend) {
        auto *context = static_cast<rdpContext *>(rawContext);
        const SizeI desktop = backend.desktopSize;
        if (desktop.width <= 0 || desktop.height <= 0 || viewSize.width <= 0 || viewSize.height <= 0)
            return;

        const PointI mapped = clampToDesktop(PointI{
            pos.x * desktop.width / viewSize.width,
            pos.y * desktop.height / viewSize.height
        }, desktop);

        freerdp_client_send_button_event(&toNativeContext(context)->common,
                                         FALSE, PTR_FLAGS_MOVE, mapped.x, mapped.y);

        int clamped = std::clamp(delta, -255, 255);
        UINT16 flags = horizontal ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL;
        if (clamped < 0) {
            flags |= PTR_FLAGS_WHEEL_NEGATIVE;
            clamped = 0x100 + clamped;
        }

        flags |= static_cast<UINT16>(clamped);
        freerdp_client_send_wheel_event(&toNativeContext(context)->common, flags);
    });
}

void FreeRdpProcess::requestResize(SizeI size)
{
    if (size.width <= 0 || size.height <= 0)
        return;

    submit([size](void *rawContext, FreeRdpBackend &) {
        auto *context = static_cast<rdpContext *>(rawContext);
        const SizeI clamped = rdp::session_view::clampedDesktopSize(size);

        NativeRdpContext *nativeContext = toNativeContext(context);
        if (!nativeContext || !nativeContext->disp || !nativeContext->disp->SendMonitorLayout)
            return;

        DISPLAY_CONTROL_MONITOR_LAYOUT layout = {};
        layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
        layout.Left = 0;
        layout.Top = 0;
        layout.Width = static_cast<UINT32>((clamped.width + 3) & ~3);
        layout.Height = static_cast<UINT32>(clamped.height);
        layout.Orientation = ORIENTATION_LANDSCAPE;
        layout.DesktopScaleFactor = 100;
        layout.DeviceScaleFactor = 100;
        layout.PhysicalWidth = layout.Width;
        layout.PhysicalHeight = layout.Height;

        nativeContext->disp->SendMonitorLayout(nativeContext->disp, 1, &layout);
    });
}

void FreeRdpProcess::requestRefresh()
{
    submit([](void *rawContext, FreeRdpBackend &backend) {
        auto *context = static_cast<rdpContext *>(rawContext);
        if (!context->update || !context->update->RefreshRect)
            return;

        const SizeI desktop = backend.desktopSize;
        if (desktop.width <= 0 || desktop.height <= 0)
            return;

        RECTANGLE_16 area = {};
        area.left = 0;
        area.top = 0;
        area.right = static_cast<UINT16>(std::clamp(desktop.width, 0, 0xFFFF));
        area.bottom = static_cast<UINT16>(std::clamp(desktop.height, 0, 0xFFFF));
        context->update->RefreshRect(context, 1, &area);
    });
}

void FreeRdpProcess::writeFrameFromContext(void *rawContext)
{
    const rdpContext *context = static_cast<rdpContext *>(rawContext);
    if (!context || !context->gdi || !context->gdi->primary_buffer)
        return;

    const int width = context->gdi->width;
    const int height = context->gdi->height;
    const int stride = static_cast<int>(context->gdi->stride);
    if (width <= 0 || height <= 0 || stride <= 0)
        return;

    const std::size_t requiredBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);

    std::function<void(const SizeI &)> desktopCallback;
    std::function<void()> frameCallback;
    std::function<void()> cursorCallback;
    bool firstFrameArrived = false;
    bool desktopSizeChanged = false;
    SizeI desktopSize{width, height};

    std::shared_ptr<FreeRdpBackend> session;
    {
        std::scoped_lock lock(m_d->mutex);

        // The session this frame belongs to: the worker's own copy of the
        // desktop size only follows a frame from a session still in use.
        session = m_d->backend;

        if (m_d->frame.pixels.size() != requiredBytes
            || m_d->frame.width != width
            || m_d->frame.height != height
            || m_d->frame.stride != stride) {
            FrameBufferMemory::resize(m_d->frame, width, height, stride, requiredBytes);
        }

        std::memcpy(m_d->frame.pixels.data(), context->gdi->primary_buffer, requiredBytes);
        desktopSizeChanged = (m_d->desktopSize.width != width || m_d->desktopSize.height != height);
        m_d->desktopSize = desktopSize;
        // Input mapping runs on this thread and must use the size the frame it
        // is clicking on came from, so the worker's own copy follows the frame.
        // Only this thread writes it while it still holds the session, so no
        // further lock is needed to publish it.
        if (session && m_d->backend == session)
            session->desktopSize = desktopSize;
        ++m_d->frameGeneration;
        firstFrameArrived = !m_d->hasFirstFrame;
        m_d->hasFirstFrame = true;
        if (desktopSizeChanged)
            desktopCallback = m_d->desktopResized;
        frameCallback = m_d->frameUpdated;
        cursorCallback = m_d->cursorUpdated;
    }

    if (desktopCallback)
        desktopCallback(desktopSize);
    if (frameCallback)
        frameCallback();
    if (firstFrameArrived && cursorCallback)
        cursorCallback();
}

void FreeRdpProcess::updateStateFromBackend(State state)
{
    std::function<void(State)> callback;

    {
        std::scoped_lock lock(m_d->mutex);
        if (m_d->state == state)
            return;

        m_d->state = state;
        callback = m_d->stateChanged;
    }

    if (callback)
        callback(state);
}

void FreeRdpProcess::updateCursorFromBackend(const CursorInfo &cursor)
{
    CursorInfo oldCursor;
    std::function<void()> callback;
    bool emitNow = false;

    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, duplicateCursorInfo(cursor));
        emitNow = m_d->hasFirstFrame;
        callback = m_d->cursorUpdated;
    }

    destroyCursorInfo(oldCursor);
    if (emitNow && callback)
        callback();
}

void FreeRdpProcess::resetCursorFromBackend()
{
    CursorInfo oldCursor;
    std::function<void()> callback;
    bool emitNow = false;

    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, defaultCursorInfo());
        emitNow = m_d->hasFirstFrame;
        callback = m_d->cursorUpdated;
    }

    destroyCursorInfo(oldCursor);
    if (emitNow && callback)
        callback();
}

void FreeRdpProcess::attachClipboardChannel(void *channelContext)
{
    CliprdrClientContext *cliprdr = reinterpret_cast<CliprdrClientContext *>(channelContext);
    if (!cliprdr)
        return;

    std::lock_guard<std::mutex> lock(m_d->clipboardMutex);
    if (!m_d->clipboard)
        m_d->clipboard = std::make_unique<RdpClipboardBridge>();

    m_d->clipboard->attach(cliprdr);
}

void FreeRdpProcess::detachClipboardChannel()
{
    std::lock_guard<std::mutex> lock(m_d->clipboardMutex);
    if (m_d->clipboard)
        m_d->clipboard->detach();
}

FreeRdpProcess::ConnectionInfo FreeRdpProcess::connectionInfo() const
{
    ConnectionInfo info;
    std::scoped_lock lock(m_d->mutex);

    const std::shared_ptr<FreeRdpBackend> backend = m_d->backend;
    if (!backend)
        return info;

    rdpContext *context = backend->context();
    if (!context)
        return info;

    rdpSettings *settings = context->settings;
    if (settings) {
        if (freerdp_settings_get_bool(settings, FreeRDP_SupportGraphicsPipeline)) {
            if (freerdp_settings_get_bool(settings, FreeRDP_GfxAVC444) ||
                freerdp_settings_get_bool(settings, FreeRDP_GfxAVC444v2))
                info.codecName = "H.264 AVC 444";
            else if (freerdp_settings_get_bool(settings, FreeRDP_GfxH264))
                info.codecName = "H.264 AVC 420";
            else
                info.codecName = "GFX";
        } else {
            const UINT32 codecId = freerdp_settings_get_uint32(
                settings, FreeRDP_BitmapCacheV3CodecId);
            switch (codecId) {
            case 1: info.codecName = "NSCodec"; break;
            case 2: info.codecName = "RemoteFX"; break;
            case 3: info.codecName = "ClearCodec"; break;
            default: info.codecName = "Bitmap"; break;
            }
        }
    }

    NativeRdpContext *nativeCtx = toNativeContext(context);
    if (nativeCtx) {
        UINT32 rtt = nativeCtx->rtt.load(std::memory_order_relaxed);
        if (rtt > 0) {
            info.rtt = rtt;
            info.rttAvailable = true;
        }
    }

    if (!info.rttAvailable) {
        rdpAutoDetect *ad = ::autodetect_get(context);
        if (ad) {
            if (ad->netCharAverageRTT > 0) {
                info.rtt = ad->netCharAverageRTT;
                info.rttAvailable = true;
            } else if (ad->netCharBaseRTT > 0) {
                info.rtt = ad->netCharBaseRTT;
                info.rttAvailable = true;
            }
        }
    }

    return info;
}
