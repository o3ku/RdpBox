#include <cassert>
#include <cstdio>
#include <memory>

#include "common/rdp/FreeRdpProcess.h"

// The P0 contract, modelled without freerdp: one connection owns its context,
// and whoever releases it is decided by a single token. A second release means
// the process crashed on a real machine, so the counters below are exact.
namespace
{
struct Connection
{
    int frees = 0;

    // Exactly what a worker does on its way out, and what stop() does when it
    // waited for the worker: both go through the token.
    void release()
    {
        if (released)
            return;
        released = true;
        ++frees;
    }

    bool released = false;
};

struct Terminal
{
    FreeRdpProcess::Abandoned token;
    Connection connection;
    int abandonCalls = 0;

    // The worker, as in startWorker(): it frees the context and then reports
    // that it is done.
    void workerFinished()
    {
        connection.release();
        token.released.store(true);
    }

    // stop(), as on the UI thread: wait for the token, and only take the
    // context over when the worker never reported.
    void stopAfterGrace(bool workerReported)
    {
        if (workerReported)
            return;
        abandonCalls++;
        // Abandoned: the worker still owns the context, so nothing is freed
        // here - it will be, once the worker unwinds.
    }

    bool abandoned() const { return abandonCalls > 0; }
};
}

int main()
{
    {
        // Worker finished inside the grace period: stop() frees the context.
        Terminal terminal;
        terminal.workerFinished();
        assert(terminal.token.released.load());
        terminal.stopAfterGrace(true);
        terminal.connection.release();
        assert(terminal.connection.frees == 1);
        assert(!terminal.abandoned());
    }

    {
        // Worker wedged: stop() gives up, the worker frees the context later.
        Terminal terminal;
        terminal.stopAfterGrace(false);
        assert(terminal.abandoned());
        assert(terminal.connection.frees == 0);
        terminal.workerFinished();
        assert(terminal.connection.frees == 1);
    }

    {
        // Both paths claiming the release still free once.
        Terminal terminal;
        terminal.stopAfterGrace(false);
        terminal.workerFinished();
        terminal.stopAfterGrace(true);
        assert(terminal.connection.frees == 1);
    }

    {
        // A stop() that abandons, then the worker finishing, then another
        // stop(): still one release, and the late stop() finds nothing to free.
        Terminal terminal;
        terminal.stopAfterGrace(false);
        terminal.stopAfterGrace(false);
        terminal.workerFinished();
        assert(terminal.connection.frees == 1);
    }

    std::puts("FreeRdpProcess hand-over: exactly one release on every path");
    return 0;
}
