#include <cassert>
#include <filesystem>

#include "common/Win32String.h"
#include "profiles/ProfileRepository.h"

int main()
{
    const std::filesystem::path filePath =
        std::filesystem::temp_directory_path()
        / wideFromUtf8("RdpBox-ProfileRepositoryTests-" + createGuidString() + ".json");
    std::filesystem::remove(filePath);

    {
        ProfileRepository repository(filePath.wstring());

        Profile profile = Profile::create();
        profile.name = L"server-a";
        profile.host = L"10.0.0.8";
        profile.username = L"alice";
        profile.password = L"secret";
        repository.addProfile(profile);

        const Profile stored = repository.profileById(profile.id);
        assert(stored.id == profile.id);
        assert(stored.host == L"10.0.0.8");
        assert(stored.clipboardEnabled);

        const std::vector<Profile> searchResults = repository.search(L"SERVER");
        assert(searchResults.size() == 1);
        assert(searchResults[0].id == profile.id);
    }

    {
        ProfileRepository repository(filePath.wstring());
        assert(repository.profiles().size() == 1);
        assert(repository.profiles()[0].name == L"server-a");
    }

    std::filesystem::remove(filePath);
    return 0;
}
