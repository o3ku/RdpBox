#include "RdpClipboardBridge.h"

#include "PlatformClipboardBackend.h"
#include "WindowsClipboardBackend.h"

RdpClipboardBridge::RdpClipboardBridge()
    : m_backend(std::make_unique<WindowsClipboardBackend>())
{
}

RdpClipboardBridge::~RdpClipboardBridge() = default;

bool RdpClipboardBridge::attach(CliprdrClientContext *context)
{
    return m_backend && m_backend->attach(context);
}

void RdpClipboardBridge::detach()
{
    if (m_backend)
        m_backend->detach();
}
