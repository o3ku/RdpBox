#include <windows.h>

#include "common/NativeTypes.h"
#include "rdp/RdpInputModifiers.h"

int main()
{
    using rdp::mouseInputModifiers;

    if (mouseInputModifiers(MK_CONTROL, ModifierNone) != ModifierControl)
        return 1;
    if (mouseInputModifiers(MK_SHIFT, ModifierNone) != ModifierShift)
        return 1;
    if (mouseInputModifiers(MK_CONTROL | MK_SHIFT, ModifierAlt)
        != (ModifierControl | ModifierShift | ModifierAlt)) {
        return 1;
    }
    if (mouseInputModifiers(0, ModifierControl | ModifierShift | ModifierAlt)
        != (ModifierControl | ModifierShift | ModifierAlt)) {
        return 1;
    }

    return 0;
}
