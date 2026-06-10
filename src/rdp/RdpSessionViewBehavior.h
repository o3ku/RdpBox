#pragma once

#include "common/NativeTypes.h"

#include <string>

namespace rdp::session_view
{
struct FrameGateState
{
    bool active = false;
    int remaining = 0;
    bool waitingForFirstContentFrame = false;
    bool resolutionUpdatePending = false;
};

struct FrameArrivalDecision
{
    FrameGateState state;
    bool renderFrame = true;
    bool hideOverlay = false;
    bool resolutionFrameProgress = false;
};

SizeI normalizedViewSize(int width, int height);

int initialFrameDiscardCount(bool codecAvailable);

std::wstring startOverlayText(bool reconnecting);

std::wstring finishedOverlayText(const std::string &disconnectError);

bool shouldFlushPendingResize(bool hasPendingResize, bool connected, bool hasProcess);

FrameArrivalDecision frameArrivalDecision(FrameGateState state);
}
