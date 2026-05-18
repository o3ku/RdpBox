#pragma once

namespace rdp
{
bool shouldSendFocusIn(bool hadWindowFocus,
                       bool hadSystemKeyTarget,
                       bool hasWindowFocusAfterEnsure);
}
