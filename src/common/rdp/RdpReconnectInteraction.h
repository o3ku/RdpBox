#pragma once

inline bool shouldReconnectOnPointerDown(bool profileValid,
                                         bool connected,
                                         bool hasProcess,
                                         bool processFinished,
                                         bool resolutionUpdatePending)
{
    if (!profileValid)
        return false;

    if (resolutionUpdatePending)
        return true;

    if (connected)
        return false;

    if (!hasProcess)
        return true;

    return processFinished;
}
