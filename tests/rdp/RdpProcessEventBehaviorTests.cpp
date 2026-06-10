#include <cassert>

#include "rdp/RdpProcessEventBehavior.h"

int main()
{
    assert(rdp::process_event::isCurrentGeneration(2, 2));
    assert(!rdp::process_event::isCurrentGeneration(2, 1));

    assert(rdp::process_event::shouldHandleProcessEvent(7, 7));
    assert(!rdp::process_event::shouldHandleProcessEvent(7, 6));

    assert(rdp::process_event::shouldHandleFrameEvent(true, 7, 7));
    assert(!rdp::process_event::shouldHandleFrameEvent(false, 7, 7));
    assert(!rdp::process_event::shouldHandleFrameEvent(true, 7, 6));

    assert(rdp::process_event::shouldClearPendingCertificateRequest(true, 3, 3));
    assert(!rdp::process_event::shouldClearPendingCertificateRequest(false, 3, 3));
    assert(!rdp::process_event::shouldClearPendingCertificateRequest(true, 3, 4));

    assert(rdp::process_event::shouldShowCertificatePrompt(5, 5, true, true, true, 5));
    assert(!rdp::process_event::shouldShowCertificatePrompt(5, 4, true, true, true, 4));
    assert(!rdp::process_event::shouldShowCertificatePrompt(5, 5, false, true, true, 5));
    assert(!rdp::process_event::shouldShowCertificatePrompt(5, 5, true, false, true, 5));
    assert(!rdp::process_event::shouldShowCertificatePrompt(5, 5, true, true, false, 5));
    assert(!rdp::process_event::shouldShowCertificatePrompt(5, 5, true, true, true, 4));

    return 0;
}
