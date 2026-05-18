#include "rdp/RdpFocusNotification.h"

namespace rdp
{
bool shouldSendFocusIn(bool hadWindowFocus,
                       bool hadSystemKeyTarget,
                       bool hasWindowFocusAfterEnsure)
{
    return hasWindowFocusAfterEnsure && (!hadWindowFocus || !hadSystemKeyTarget);
}
}
