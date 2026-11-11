#pragma once

#include "common/rdp/RdpInputEventUtil.h"

#include <QKeyEvent>

namespace rdpbox
{
inline bool shouldForwardQtKeyEvent(const QKeyEvent &event, bool down)
{
    // Qt synthesizes a release before every repeated press.
    return down || !event.isAutoRepeat();
}

#ifdef _WIN32
inline std::optional<KeyIdentifier> keyIdentifierFromQtEvent(const QKeyEvent &event)
{
    const quint32 scanCode = event.nativeScanCode();
    if ((scanCode & 0xFFu) == 0)
        return keyIdentifierFromVirtualKey(static_cast<unsigned int>(event.nativeVirtualKey()));
    return KeyIdentifier{static_cast<std::uint16_t>(scanCode & 0xFFu), (scanCode & 0x100u) != 0};
}
#endif
}
