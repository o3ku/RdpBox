#include <atomic>
#include <cassert>
#include <cstdio>

#include "common/rdp/FreeRdpProcessNative.h"

// The P3 side of the gdi lifetime: post_disconnect must free the GDI surface
// once per session, and post_connect must not re-init over a live one. The gate
// is what nativefreerdp_post_connect/post_disconnect consult, so it is checked
// here through the same functions.
namespace
{
struct GdiUse
{
    int inits = 0;
    int frees = 0;
};

GdiUse g_use;
// Same three pieces of state nativefreerdp_post_connect/post_disconnect keep.
bool g_acquired = false;
std::atomic_bool g_released{false};

// Mirrors the two callbacks without freerdp: the gate logic is the subject.
void postConnect()
{
    if (!g_acquired) {
        ++g_use.inits;
        g_acquired = true;
        g_released.store(false);
    }
}

void postDisconnect()
{
    bool expected = false;
    if (!g_acquired || !g_released.compare_exchange_strong(expected, true))
        return;

    ++g_use.frees;
    // The surface is gone: the next session initialises its own, otherwise
    // every session after the first leaks one.
    g_acquired = false;
}
}

int main()
{
    {
        postDisconnect();
        assert(g_use.frees == 0); // nothing acquired, nothing freed
    }

    {
        postConnect();
        postDisconnect();
        assert(g_use.inits == 1);
        assert(g_use.frees == 1);
    }

    {
        // Reconnect inside one session: post_connect runs again, post_disconnect
        // runs twice. Neither may duplicate init or free.
        postConnect();
        postConnect();
        assert(g_use.inits == 2);
        postDisconnect();
        postDisconnect();
        assert(g_use.frees == 2);
    }

    {
        // A fresh session initialises again after the previous one freed.
        postConnect();
        assert(g_use.inits == 3);
        postDisconnect();
        assert(g_use.frees == 3);
    }

    {
        // A third session must still get a surface of its own.
        postConnect();
        assert(g_use.inits == 4);
        postDisconnect();
        assert(g_use.frees == 4);
    }

    std::puts("gdi gate: one init, one free per session");
    return 0;
}
