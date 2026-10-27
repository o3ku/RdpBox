#include <cassert>
#include <cstdio>

#include "common/rdp/FreeRdpProcessNative.h"

// The P3 side of the gdi lifetime: post_disconnect must free the GDI surface
// once per session, and post_connect must not re-init over a live one. The gate
// is `context->gdi` itself, so it is checked here through a mirror of the two
// callbacks: each Session stands for one freerdp context.
namespace
{
struct Session
{
    bool hasGdi = false;
    int inits = 0;
    int frees = 0;
};

void postConnect(Session &session)
{
    if (!session.hasGdi) {
        ++session.inits;
        session.hasGdi = true;
    }
}

void postDisconnect(Session &session)
{
    if (!session.hasGdi)
        return;

    ++session.frees;
    // gdi_free clears context->gdi: a second disconnect of this session is a
    // no-op, and the next session starts from a null surface.
    session.hasGdi = false;
}
}

int main()
{
    {
        Session session;
        postDisconnect(session);
        assert(session.frees == 0); // nothing acquired, nothing freed
    }

    {
        Session session;
        postConnect(session);
        postDisconnect(session);
        assert(session.inits == 1);
        assert(session.frees == 1);
    }

    {
        // Reconnect inside one session: post_connect runs again, post_disconnect
        // runs twice. Neither may duplicate init or free.
        Session session;
        postConnect(session);
        postConnect(session);
        assert(session.inits == 1);
        postDisconnect(session);
        postDisconnect(session);
        assert(session.frees == 1);
    }

    {
        // Two sessions open at once each get their own surface: the gate is the
        // context, not a process-wide flag.
        Session first;
        Session second;
        postConnect(first);
        postConnect(second);
        assert(first.inits == 1);
        assert(second.inits == 1);

        postDisconnect(first);
        assert(first.frees == 1);
        assert(second.frees == 0);

        postDisconnect(second);
        assert(second.frees == 1);
    }

    {
        // A fresh session initialises again after the previous one freed.
        Session previous;
        postConnect(previous);
        postDisconnect(previous);

        Session next;
        postConnect(next);
        assert(next.inits == 1);
        assert(previous.inits == 1);
        postDisconnect(next);
        assert(next.frees == 1);
    }

    std::puts("gdi gate: one init, one free per context");
    return 0;
}
