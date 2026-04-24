#pragma once

struct s_cliprdr_client_context;
using CliprdrClientContext = s_cliprdr_client_context;

class PlatformClipboardBackend
{
public:
    virtual ~PlatformClipboardBackend() = default;

    virtual bool attach(CliprdrClientContext *context) = 0;
    virtual void detach() = 0;
};
