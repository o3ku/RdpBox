#include "RdpClipboardBridge.h"

#include "PlatformClipboardBackend.h"
#ifdef _WIN32
#include "WindowsClipboardBackend.h"
#else
#include <memory>

namespace
{
// ponytail: no clipboard redirection off Windows yet; a null backend keeps the
// bridge functional and the channel simply does not attach.
class NullClipboardBackend : public PlatformClipboardBackend
{
public:
    bool attach(CliprdrClientContext *) override { return false; }
    void detach() override {}
};
}  // namespace
using ActiveClipboardBackend = NullClipboardBackend;
#endif

#ifdef _WIN32
RdpClipboardBridge::RdpClipboardBridge()
    : m_backend(std::make_unique<WindowsClipboardBackend>())
{
}
#else
RdpClipboardBridge::RdpClipboardBridge()
    : m_backend(std::make_unique<NullClipboardBackend>())
{
}
#endif

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
