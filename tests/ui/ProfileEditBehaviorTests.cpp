#include <cassert>

#include "ui/ProfileEditBehavior.h"

int main()
{
    assert(validateProfileEditFields(L"name", L"host") == ProfileEditValidationResult::Valid);
    assert(validateProfileEditFields(L"", L"host")
           == ProfileEditValidationResult::MissingRequiredField);
    assert(validateProfileEditFields(L"name", L"")
           == ProfileEditValidationResult::MissingRequiredField);
    assert(validateProfileEditFields(L"bad,name", L"host")
           == ProfileEditValidationResult::InvalidNameCharacter);
    assert(validateProfileEditFields(L"bad\"name", L"host")
           == ProfileEditValidationResult::InvalidNameCharacter);

    assert(normalizedProfilePort(1) == 1);
    assert(normalizedProfilePort(3389) == 3389);
    assert(normalizedProfilePort(65535) == 65535);
    assert(normalizedProfilePort(0) == 3389);
    assert(normalizedProfilePort(65536) == 3389);
    assert(normalizedProfilePort(-1) == 3389);

    return 0;
}
