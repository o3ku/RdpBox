#pragma once

struct s_cliprdr_client_context;
using CliprdrClientContext = s_cliprdr_client_context;

class PlatformClipboardBackend
{
public:
    virtual ~PlatformClipboardBackend() = default;

    virtual bool attach(CliprdrClientContext *context) = 0;
    virtual void detach() = 0;
    // Teardown for a session whose worker was abandoned: cut the callback
    // route and stop the helper thread, but keep the context allocated (the
    // worker may still be running callbacks into it). A bounded leak instead
    // of a use-after-free, mirroring how stop() abandons the session itself.
    virtual void park() = 0;
};
