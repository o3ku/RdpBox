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

    {
        const SizeI degenerate = rdp::session_view::clampedDesktopSize(SizeI{1, 1});
        assert(degenerate.width == 1280);
        assert(degenerate.height == 900);

        const SizeI narrow = rdp::session_view::clampedDesktopSize(SizeI{100, 1080});
        assert(narrow.width == 1280);
        assert(narrow.height == 1080);

        const SizeI shortHeight = rdp::session_view::clampedDesktopSize(SizeI{1920, 100});
        assert(shortHeight.width == 1920);
        assert(shortHeight.height == 900);

        const SizeI atFloor = rdp::session_view::clampedDesktopSize(SizeI{1280, 900});
        assert(atFloor.width == 1280);
        assert(atFloor.height == 900);

        const SizeI full = rdp::session_view::clampedDesktopSize(SizeI{1920, 1080});
        assert(full.width == 1920);
        assert(full.height == 1080);
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
