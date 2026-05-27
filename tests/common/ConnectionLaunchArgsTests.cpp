#include <cassert>

#include "common/ConnectionLaunchArgs.h"

int main()
{
    {
        const std::vector<std::wstring> names =
            launch::parseConnectionsArgumentValue(L"alpha,beta,gamma");
        assert(names.size() == 3);
        assert(names[0] == L"alpha");
        assert(names[1] == L"beta");
        assert(names[2] == L"gamma");
    }

    {
        const std::vector<std::wstring> names =
            launch::parseConnectionsArgumentValue(L" alpha ,  beta  ,, gamma ");
        assert(names.size() == 3);
        assert(names[0] == L"alpha");
        assert(names[1] == L"beta");
        assert(names[2] == L"gamma");
    }

    {
        std::vector<std::wstring> names;
        assert(launch::tryParseConnectionsArgument(L"--connections=left,right", names));
        assert(names.size() == 2);
        assert(names[0] == L"left");
        assert(names[1] == L"right");
        assert(!launch::tryParseConnectionsArgument(L"--portable", names));
    }

    {
        const std::wstring value = launch::buildConnectionsArgumentValue({
            L" alpha ",
            L"",
            L"beta",
            L" gamma"
        });
        assert(value == L"alpha,beta,gamma");
    }

    return 0;
}
