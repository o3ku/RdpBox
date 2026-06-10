#include "RdpSessionViewBehavior.h"

#include <algorithm>

namespace rdp::session_view
{
namespace
{
constexpr int kInitialFrameDiscardWithCodec = 3;
constexpr int kInitialFrameDiscardNoCodec = 5;
}

SizeI normalizedViewSize(int width, int height)
{
    return SizeI{std::max(1, width), std::max(1, height)};
}

int initialFrameDiscardCount(bool codecAvailable)
{
    return codecAvailable ? kInitialFrameDiscardWithCodec : kInitialFrameDiscardNoCodec;
}

std::wstring startOverlayText(bool reconnecting)
{
    return reconnecting ? L"Reconnecting..." : L"Connecting...";
}

std::wstring finishedOverlayText(const std::string &disconnectError)
{
    if (!disconnectError.empty())
        return std::wstring(disconnectError.begin(), disconnectError.end()) + L"\r\nClick to Reconnect";

    return L"Disconnected - Click to Reconnect";
}

bool shouldFlushPendingResize(bool hasPendingResize, bool connected, bool hasProcess)
{
    return hasPendingResize && connected && hasProcess;
}

FrameArrivalDecision frameArrivalDecision(FrameGateState state)
{
    FrameArrivalDecision decision;
    decision.state = state;

    if (decision.state.active) {
        if (decision.state.remaining > 0)
            --decision.state.remaining;

        if (decision.state.remaining == 0) {
            decision.state.active = false;
            decision.state.waitingForFirstContentFrame = false;
            decision.state.resolutionUpdatePending = false;
            decision.hideOverlay = true;
        } else {
            decision.renderFrame = false;
        }
    } else if (decision.state.waitingForFirstContentFrame) {
        decision.state.waitingForFirstContentFrame = false;
        if (!decision.state.resolutionUpdatePending)
            decision.hideOverlay = true;
    }

    decision.resolutionFrameProgress =
        !decision.state.resolutionUpdatePending && !decision.state.active;
    return decision;
}
}
