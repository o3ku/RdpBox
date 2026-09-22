#pragma once

#include <memory>

struct s_cliprdr_client_context;
using CliprdrClientContext = s_cliprdr_client_context;

class PlatformClipboardBackend;

class RdpClipboardBridge
{
public:
    RdpClipboardBridge();
    ~RdpClipboardBridge();

    bool attach(CliprdrClientContext *context);
    void detach();

private:
    std::unique_ptr<PlatformClipboardBackend> m_backend;
};
