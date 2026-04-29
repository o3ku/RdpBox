#pragma once

#include <string>

namespace AppPaths
{
bool isPortableMode();
std::wstring appRootPath();
std::wstring dataRootPath();
std::wstring profilesFilePath();
std::wstring frameCaptureRootPath();
bool enablePortableMode();

std::string readFileContent(const std::wstring &filePath);
bool writeFileContent(const std::wstring &filePath, const std::string &contents);
}
