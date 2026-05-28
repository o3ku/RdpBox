#pragma once

#include <string>

namespace AppPaths
{
bool isPortableMode();
std::wstring executablePath();
std::wstring dataRootPath();
std::wstring profilesFilePath();
std::wstring frameCaptureRootPath();
std::wstring updatesDirectoryPath();
bool enablePortableMode();

std::string readFileContent(const std::wstring &filePath);
bool writeFileContent(const std::wstring &filePath, const std::string &contents);
}
