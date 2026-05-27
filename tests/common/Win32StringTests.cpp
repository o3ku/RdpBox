#include <cassert>
#include <cctype>
#include <regex>
#include <string>

#include "common/Win32String.h"

int main()
{
    {
        const std::wstring wide = L"RdpBox-你好-Remote";
        const std::string utf8 = utf8FromWide(wide);
        assert(!utf8.empty());
        assert(wideFromUtf8(utf8) == wide);
    }

    {
        assert(utf8FromWide(L"").empty());
        assert(wideFromUtf8("").empty());
    }

    {
        const std::string guid = createGuidString();
        assert(guid.size() == 36);
        assert(guid.find('{') == std::string::npos);
        assert(guid.find('}') == std::string::npos);
        assert(guid[8] == '-');
        assert(guid[13] == '-');
        assert(guid[18] == '-');
        assert(guid[23] == '-');
        for (size_t i = 0; i < guid.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23)
                continue;
            assert(std::isxdigit(static_cast<unsigned char>(guid[i])) != 0);
        }
    }

    {
        const std::string timestamp = currentUtcIso8601();
        const std::regex isoPattern(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$)");
        assert(std::regex_match(timestamp, isoPattern));
    }

    return 0;
}
