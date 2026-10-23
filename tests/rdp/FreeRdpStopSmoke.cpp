// Acceptance test for the stop()/worker hand-over: a connection that fails
// fast, followed by the destructor calling stop(), must not touch the freerdp
// context after the worker released it.
//
// _CRTDBG_DELAY_FREE_MEM_DF keeps freed blocks around (filled with 0xDD)
// instead of handing them back to the allocator, so a stale dereference
// crashes here instead of silently reading recycled memory.

#undef NDEBUG
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

#include <crtdbg.h>

#include "common/rdp/FreeRdpProcess.h"

int main()
{
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG)
                   | _CRTDBG_ALLOC_MEM_DF | _CRTDBG_DELAY_FREE_MEM_DF);

    for (int round = 0; round < 3; ++round) {
        FreeRdpProcess process;
        // The callback runs on the worker thread.
        std::atomic_bool finished{false};
        process.setStateChangedCallback([&finished](FreeRdpProcess::State state) {
            if (state == FreeRdpProcess::State::Finished)
                finished.store(true);
        });
        // Port 1 on loopback refuses immediately: the worker reports Finished
        // before stop() ever runs, which is the window this test is about.
        process.start(L"127.0.0.1", 1, {}, {}, {}, 1280, 720, true, true);
        for (int waited = 0; !finished.load() && waited < 30000; waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(finished.load());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::puts("FreeRdpStopSmoke passed");
    return 0;
}
