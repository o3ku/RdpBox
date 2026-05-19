#include <cassert>

#include <windows.h>

#include "rdp/ClipboardWindowReady.h"

int main()
{
    using rdp::didClipboardWindowInitialize;

    assert(didClipboardWindowInitialize(WAIT_OBJECT_0, true));
    assert(!didClipboardWindowInitialize(WAIT_OBJECT_0, false));
    assert(!didClipboardWindowInitialize(WAIT_TIMEOUT, true));
    assert(!didClipboardWindowInitialize(WAIT_FAILED, true));

    return 0;
}
