#pragma once

#include <string>

namespace AppPaths
{
bool isPortableMode();
std::wstring appRootPath();
std::wstring dataRootPath();
std::wstring profilesFilePath();
std::wstring windowStateFilePath();
std::wstring frameCaptureRootPath();
bool enablePortableMode();
}
