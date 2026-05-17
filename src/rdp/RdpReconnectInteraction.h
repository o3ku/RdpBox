#pragma once

inline bool shouldReconnectOnPointerDown(bool profileValid,
                                         bool connected,
                                         bool hasProcess,
                                         bool processFinished)
{
    if (!profileValid || connected)
        return false;

    if (!hasProcess)
        return true;

    return processFinished;
}
