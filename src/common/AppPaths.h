#pragma once

#include <string>

namespace AppPaths
{
bool isPortableMode();
std::wstring executablePath();
// Directory holding the executable, so callers can name it in a message
// (portable mode failures). Empty when the module path has no separator.
std::wstring executableDirectory();
std::wstring dataRootPath();
std::wstring profilesFilePath();
std::wstring updatesDirectoryPath();
// Machine-local window geometry file (never inside dataRootPath: a synced
// portable folder must not be rewritten for per-monitor window state).
std::wstring windowStateFilePath();
// Writes the portable flag next to the executable. Returns false when that
// directory is not writable (Program Files, read-only media); the caller must
// surface that, because isPortableMode() then silently falls back to APPDATA.
bool enablePortableMode();

// True when --portable was passed but the flag could not be persisted
// (read-only install directory), so the app is running against APPDATA.
bool portableModeEnableFailed();

std::string readFileContent(const std::wstring &filePath);
bool writeFileContent(const std::wstring &filePath, const std::string &contents);
}
