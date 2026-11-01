#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace win {

// Finds a running process by executable name (case-insensitive). Returns 0 if not found.
DWORD FindProcess(const std::wstring& exeName);

bool IsProcessRunning(const std::wstring& exeName);

// Resolves the full image path of a running process.
bool GetProcessImagePath(DWORD pid, std::filesystem::path& out);

// Finds a game executable using the Steam registry and library folders.
bool FindGameExecutable(const std::wstring& appId,
                        const std::wstring& installDirHint,
                        const std::wstring& exeRelativePath,
                        std::filesystem::path& out);

// Launches the game through Steam when an app id is known, otherwise from `fallbackExe`.
bool LaunchGame(const std::wstring& appId, const std::filesystem::path& fallbackExe,
                std::wstring& error);

// Waits for a process to appear. Returns 0 on timeout.
DWORD WaitForProcess(const std::wstring& exeName, int timeoutMs);

// Classic LoadLibraryW remote-thread injection.
bool InjectDll(DWORD pid, const std::filesystem::path& dll, std::wstring& error);

// Runs a command line without a console and waits for it to finish.
bool RunAndWait(const std::wstring& commandLine, DWORD timeoutMs, DWORD& exitCode,
                std::wstring& error);

} // namespace win
