#include <cassert>

#include <windows.h>

#include "common/NativeTypes.h"
#include "rdp/RdpInputModifiers.h"

int main()
{
    using rdp::mouseInputModifiers;

    assert(mouseInputModifiers(MK_CONTROL, ModifierNone) == ModifierControl);
    assert(mouseInputModifiers(MK_SHIFT, ModifierNone) == ModifierShift);
    assert(mouseInputModifiers(MK_CONTROL | MK_SHIFT, ModifierAlt)
           == (ModifierControl | ModifierShift | ModifierAlt));
    assert(mouseInputModifiers(0, ModifierControl | ModifierShift | ModifierAlt) == ModifierAlt);

    return 0;
}
