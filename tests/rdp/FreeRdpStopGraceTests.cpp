#include <cassert>

#include "common/rdp/FreeRdpStopGrace.h"

int main()
{
    using namespace rdp::stop_grace;

    // An attempt that never got a handshake through is released quickly and
    // without a line in the log, whether or not it published a state first.
    assert(plan(false, false) == Plan::Failed);
    assert(plan(true, false) == Plan::Starting);
    assert(quiet(Plan::Failed));
    assert(quiet(Plan::Starting));
    assert(pollBudget(Plan::Failed) == kQuietGrace);
    assert(pollBudget(Plan::Starting) == kQuietGrace);
    assert(!report(Plan::Failed));
    assert(!report(Plan::Starting));

    // A session that connected has to be torn down properly: wait for it, and
    // say so if it refuses.
    assert(plan(false, true) == Plan::Connected);
    assert(!quiet(Plan::Connected));
    assert(pollBudget(Plan::Connected) == kStopGrace);
    assert(report(Plan::Connected));

    return 0;
}
