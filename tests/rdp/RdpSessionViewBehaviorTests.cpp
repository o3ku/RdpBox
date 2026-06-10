#include <cassert>

#include "rdp/RdpSessionViewBehavior.h"

int main()
{
    using rdp::session_view::FrameGateState;

    {
        const SizeI size = rdp::session_view::normalizedViewSize(0, -5);
        assert(size.width == 1);
        assert(size.height == 1);

        const SizeI normal = rdp::session_view::normalizedViewSize(1280, 720);
        assert(normal.width == 1280);
        assert(normal.height == 720);
    }

    assert(rdp::session_view::initialFrameDiscardCount(true) == 3);
    assert(rdp::session_view::initialFrameDiscardCount(false) == 5);

    assert(rdp::session_view::startOverlayText(false) == L"Connecting...");
    assert(rdp::session_view::startOverlayText(true) == L"Reconnecting...");
    assert(rdp::session_view::finishedOverlayText({}) == L"Disconnected - Click to Reconnect");
    assert(rdp::session_view::finishedOverlayText("ERRCONNECT")
           == L"ERRCONNECT\r\nClick to Reconnect");

    assert(rdp::session_view::shouldFlushPendingResize(true, true, true));
    assert(!rdp::session_view::shouldFlushPendingResize(false, true, true));
    assert(!rdp::session_view::shouldFlushPendingResize(true, false, true));
    assert(!rdp::session_view::shouldFlushPendingResize(true, true, false));

    {
        const auto decision = rdp::session_view::frameArrivalDecision(
            FrameGateState{true, 3, true, true});
        assert(decision.state.active);
        assert(decision.state.remaining == 2);
        assert(decision.state.waitingForFirstContentFrame);
        assert(decision.state.resolutionUpdatePending);
        assert(!decision.renderFrame);
        assert(!decision.hideOverlay);
        assert(!decision.resolutionFrameProgress);
    }

    {
        const auto decision = rdp::session_view::frameArrivalDecision(
            FrameGateState{true, 1, true, true});
        assert(!decision.state.active);
        assert(decision.state.remaining == 0);
        assert(!decision.state.waitingForFirstContentFrame);
        assert(!decision.state.resolutionUpdatePending);
        assert(decision.renderFrame);
        assert(decision.hideOverlay);
        assert(decision.resolutionFrameProgress);
    }

    {
        const auto decision = rdp::session_view::frameArrivalDecision(
            FrameGateState{false, 0, true, false});
        assert(!decision.state.active);
        assert(!decision.state.waitingForFirstContentFrame);
        assert(decision.renderFrame);
        assert(decision.hideOverlay);
        assert(decision.resolutionFrameProgress);
    }

    {
        const auto decision = rdp::session_view::frameArrivalDecision(
            FrameGateState{false, 0, true, true});
        assert(!decision.state.active);
        assert(!decision.state.waitingForFirstContentFrame);
        assert(decision.state.resolutionUpdatePending);
        assert(decision.renderFrame);
        assert(!decision.hideOverlay);
        assert(!decision.resolutionFrameProgress);
    }

    return 0;
}
