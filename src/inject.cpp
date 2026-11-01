#include "inject.hpp"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace win {
namespace {

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

bool ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                        std::wstring& out) {
    wchar_t buffer[2048];
    DWORD size = sizeof(buffer);
    const LSTATUS status = RegGetValueW(root, subKey, valueName,
                                        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                                        nullptr, buffer, &size);
    if (status != ERROR_SUCCESS) return false;
    out.assign(buffer);
    return true;
}

std::wstring SteamInstallPath() {
    std::wstring path;
    if (ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", path) && !path.empty())
        return path;
    if (ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", path) && !path.empty())
        return path;
    if (ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", path) && !path.empty())
        return path;

    const wchar_t* fallbacks[] = {
        L"C:\\Program Files (x86)\\Steam",
        L"C:\\Program Files\\Steam",
        L"D:\\Steam",
    };
    for (const wchar_t* candidate : fallbacks) {
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return {};
}

std::vector<std::wstring> ParseLibraryFolders(const std::filesystem::path& vdfPath) {
    std::vector<std::wstring> libraries;
    std::ifstream file(vdfPath);
    if (!file.is_open()) return libraries;

    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string key = "\"path\"";

    std::size_t pos = 0;
    while ((pos = content.find(key, pos)) != std::string::npos) {
        pos += key.size();
        const std::size_t open = content.find('"', pos);
        if (open == std::string::npos) break;
        const std::size_t close = content.find('"', open + 1);
        if (close == std::string::npos) break;

        std::string raw = content.substr(open + 1, close - open - 1);

        // Unescape the double backslashes used by the VDF format.
        std::string unescaped;
        unescaped.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
                unescaped.push_back('\\');
                ++i;
            } else {
                unescaped.push_back(raw[i]);
            }
        }

        const int needed = MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, nullptr, 0);
        if (needed > 0) {
            std::wstring wide(static_cast<std::size_t>(needed - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, wide.data(), needed);
            libraries.push_back(wide);
        }
        pos = close + 1;
    }

    return libraries;
}

std::vector<std::wstring> SteamLibraries() {
    std::vector<std::wstring> result;
    const std::wstring steam = SteamInstallPath();
    if (steam.empty()) return result;

    result.push_back(steam);

    const auto vdf = std::filesystem::path(steam) / "steamapps" / "libraryfolders.vdf";
    for (const auto& library : ParseLibraryFolders(vdf)) {
        if (std::find(result.begin(), result.end(), library) == result.end())
            result.push_back(library);
    }
    return result;
}

bool FindFileRecursively(const std::filesystem::path& root, const std::wstring& fileName,
                         std::filesystem::path& out) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return false;

    const std::wstring wanted = ToLower(fileName);

    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;

    while (it != end) {
        if (ec) { ec.clear(); }

        if (it.depth() > 5) {
            it.disable_recursion_pending();
            it.increment(ec);
            continue;
        }

        if (it->is_regular_file(ec)) {
            if (ToLower(it->path().filename().wstring()) == wanted) {
                out = it->path();
                return true;
            }
        }
        it.increment(ec);
    }
    return false;
}

} // namespace

DWORD FindProcess(const std::wstring& exeName) {
    const std::wstring wanted = ToLower(exeName);
    DWORD pid = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (ToLower(entry.szExeFile) == wanted) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

bool IsProcessRunning(const std::wstring& exeName) {
    return FindProcess(exeName) != 0;
}

bool GetProcessImagePath(DWORD pid, std::filesystem::path& out) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;

    wchar_t buffer[MAX_PATH * 4];
    DWORD size = static_cast<DWORD>(std::size(buffer));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, buffer, &size);
    CloseHandle(process);

    if (!ok) return false;
    out = std::filesystem::path(std::wstring(buffer, size));
    return true;
}

bool FindGameExecutable(const std::wstring& appId,
                        const std::wstring& installDirHint,
                        const std::wstring& exeRelativePath,
                        std::filesystem::path& out) {
    const std::wstring exeFileName =
        std::filesystem::path(exeRelativePath).filename().wstring();

    // 1) Uninstall registry entry (fastest and most reliable for Steam games).
    if (!appId.empty()) {
        const std::wstring key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " + appId;
        const std::wstring wowKey = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " + appId;

        for (const std::wstring* registryKey : { &key, &wowKey }) {
            for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
                std::wstring location;
                if (!ReadRegistryString(root, registryKey->c_str(), L"InstallLocation", location) ||
                    location.empty())
                    continue;

                const auto candidate = std::filesystem::path(location) / exeRelativePath;
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) {
                    out = candidate;
                    return true;
                }

                const auto flat = std::filesystem::path(location) / exeFileName;
                if (std::filesystem::exists(flat, ec)) {
                    out = flat;
                    return true;
                }
            }
        }
    }

    // 2) Steam library folders.
    for (const auto& library : SteamLibraries()) {
        const auto common = std::filesystem::path(library) / "steamapps" / "common";

        if (!installDirHint.empty()) {
            const auto installDir = common / installDirHint;

            std::error_code ec;
            const auto candidate = installDir / exeRelativePath;
            if (std::filesystem::exists(candidate, ec)) {
                out = candidate;
                return true;
            }

            if (FindFileRecursively(installDir, exeFileName, out)) return true;
        }

        if (FindFileRecursively(common, exeFileName, out)) return true;
    }

    return false;
}

bool LaunchGame(const std::wstring& appId, const std::filesystem::path& fallbackExe,
                std::wstring& error) {
    if (!appId.empty()) {
        const std::wstring url = L"steam://rungameid/" + appId;
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOW);
        if (reinterpret_cast<INT_PTR>(result) > 32) return true;
    }

    std::error_code ec;
    if (!fallbackExe.empty() && std::filesystem::exists(fallbackExe, ec)) {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", fallbackExe.wstring().c_str(),
                                               nullptr, fallbackExe.parent_path().wstring().c_str(),
                                               SW_SHOW);
        if (reinterpret_cast<INT_PTR>(result) > 32) return true;
        error = L"Oyun baslatilamadi (ShellExecute: " +
                std::to_wstring(reinterpret_cast<INT_PTR>(result)) + L")";
        return false;
    }

    error = L"Oyun calistirilamadi: Steam veya oyun yolu bulunamadi.";
    return false;
}

DWORD WaitForProcess(const std::wstring& exeName, int timeoutMs) {
    const DWORD start = GetTickCount();
    for (;;) {
        const DWORD pid = FindProcess(exeName);
        if (pid != 0) return pid;
        if (static_cast<int>(GetTickCount() - start) >= timeoutMs) return 0;
        Sleep(250);
    }
}

bool InjectDll(DWORD pid, const std::filesystem::path& dll, std::wstring& error) {
    std::error_code ec;
    if (!std::filesystem::exists(dll, ec)) {
        error = L"DLL bulunamadi: " + dll.wstring();
        return false;
    }

    const std::wstring dllPath = std::filesystem::absolute(dll, ec).wstring();
    const std::size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);

    HANDLE process = nullptr;
    for (int attempt = 0; attempt < 20; ++attempt) {
        process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                              PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
        if (process) break;
        Sleep(150);
    }

    if (!process) {
        error = L"OpenProcess basarisiz (hata " + std::to_wstring(GetLastError()) +
                L"). Oyunu yonetici olarak calistirmayi deneyin.";
        return false;
    }

    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        error = L"VirtualAllocEx basarisiz (hata " + std::to_wstring(GetLastError()) + L")";
        CloseHandle(process);
        return false;
    }

    bool ok = false;
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        error = L"WriteProcessMemory basarisiz (hata " + std::to_wstring(GetLastError()) + L")";
    } else {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(kernel32, "LoadLibraryW"));

        if (!loadLibrary) {
            error = L"LoadLibraryW adresi bulunamadi";
        } else {
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
            if (!thread) {
                error = L"CreateRemoteThread basarisiz (hata " + std::to_wstring(GetLastError()) + L")";
            } else {
                const DWORD wait = WaitForSingleObject(thread, 30000);
                if (wait == WAIT_TIMEOUT) {
                    error = L"Uzak thread zaman asimina ugradi";
                } else {
                    DWORD moduleHandle = 0;
                    GetExitCodeThread(thread, &moduleHandle);
                    if (moduleHandle == 0) {
                        error = L"DLL yuklenemedi (LoadLibraryW 0 dondu). DLL mimarisi oyunla uyumlu mu?";
                    } else {
                        ok = true;
                    }
                }
                CloseHandle(thread);
            }
        }
    }

    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return ok;
}

bool RunAndWait(const std::wstring& commandLine, DWORD timeoutMs, DWORD& exitCode,
                std::wstring& error) {
    exitCode = static_cast<DWORD>(-1);

    std::wstring mutableCommand = commandLine;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};

    const BOOL created = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);

    if (!created) {
        error = L"Process baslatilamadi (hata " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    const DWORD wait = WaitForSingleObject(info.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        error = L"Islem zaman asimina ugradi";
        return false;
    }

    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
}

} // namespace win
