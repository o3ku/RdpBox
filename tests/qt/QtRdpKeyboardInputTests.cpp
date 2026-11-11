#include <cassert>

#include "qtui/QtRdpKeyboardInput.h"
#include "common/rdp/RdpWinKeyCodes.h"

int main()
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a", false);
    QKeyEvent repeatPress(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a", true);
    QKeyEvent repeatRelease(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a", true);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a", false);
    assert(rdpbox::shouldForwardQtKeyEvent(press, true));
    assert(rdpbox::shouldForwardQtKeyEvent(repeatPress, true));
    assert(!rdpbox::shouldForwardQtKeyEvent(repeatRelease, false));
    assert(rdpbox::shouldForwardQtKeyEvent(release, false));

#ifdef _WIN32
    // Windows gives Qt aggregate VKs; the scan code preserves the actual side.
    const unsigned int keys[][3] = {
        {VK_SHIFT, 0x36, 0}, {VK_CONTROL, 0x11D, 1}, {VK_MENU, 0x138, 1},
        {VK_SHIFT, 0x2A, 0}, {VK_CONTROL, 0x1D, 0}, {VK_MENU, 0x38, 0},
        {VK_RETURN, 0x11C, 1}, {VK_LWIN, 0x15B, 1}, {VK_RWIN, 0x15C, 1}
    };
    for (const auto &entry : keys) {
        for (QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
            QKeyEvent event(type, Qt::Key_unknown, Qt::NoModifier, entry[1], entry[0], 0);
            const auto key = rdpbox::keyIdentifierFromQtEvent(event);
            assert(key);
            assert(key->scanCode == (entry[1] & 0xFFu));
            assert(key->extended == (entry[2] != 0));
        }
    }
    QKeyEvent fallback(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier, 0, VK_RCONTROL, 0);
    assert(rdpbox::keyIdentifierFromQtEvent(fallback) == keyIdentifierFromVirtualKey(VK_RCONTROL));
#endif
    return 0;
}
