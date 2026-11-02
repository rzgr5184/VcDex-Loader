#include "app.hpp"
#include "config.hpp"
#include "http.hpp"
#include "inject.hpp"
#include "json.hpp"
#include "sha256.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace app {
namespace {

std::mutex g_mutex;
std::mutex g_logMutex;
AppUiState g_state;
std::atomic<bool> g_busy{false};
std::atomic<bool> g_background{false};
std::atomic<bool> g_exitRequested{false};

std::filesystem::path g_baseDir;
std::filesystem::path g_exePath;
std::filesystem::path g_dataDir;
std::string g_loaderUrl;
std::string g_loaderSha;
std::string g_localExeSha;
float g_autoCheckElapsed = 0.0f;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring result(static_cast<std::size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), needed);
    return result;
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), needed, nullptr, nullptr);
    return result;
}

std::string CacheBust();

std::string Timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_dataDir.empty()) return;

    std::error_code ec;
    const auto folder = g_dataDir / "logs";
    std::filesystem::create_directories(folder, ec);

    const auto path = folder / "update.log";
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 1024 * 1024) {
        std::filesystem::remove(folder / "update.old.log", ec);
        std::filesystem::rename(path, folder / "update.old.log", ec);
    }

    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file.is_open()) return;
    file << Timestamp() << "  " << message << "\r\n";
}

bool EnsureWritableDir(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) return false;

    const auto probe = folder / ".write-test";
    HANDLE handle = CreateFileW(probe.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    CloseHandle(handle);
    std::filesystem::remove(probe, ec);
    return true;
}

// Portable installs keep data next to the exe. When that folder is read-only
// (Program Files, USB media) the loader transparently falls back to LOCALAPPDATA
// so automatic updates keep working anyway.
std::filesystem::path ResolveDataDir(const std::filesystem::path& baseDir) {
    const auto portable = baseDir / "data";
    if (EnsureWritableDir(portable)) return portable;

    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer,
                                                 static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        const auto local = std::filesystem::path(buffer) / "VcDex";
        if (EnsureWritableDir(local)) return local;
    }
    return portable;
}

const std::string& LocalExeHash() {
    if (g_localExeSha.empty()) {
        if (g_exePath.empty()) {
            wchar_t modulePath[MAX_PATH * 4]{};
            GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
            g_exePath = modulePath;
        }
        g_localExeSha = crypto::Sha256File(g_exePath);
    }
    return g_localExeSha;
}

std::vector<std::string> MirrorUrls(const std::string& url) {
    std::vector<std::string> urls;
    if (url.empty()) return urls;
    urls.push_back(url);

    const auto bases = config::BaseUrls();
    for (const auto& base : bases) {
        if (url.rfind(base, 0) != 0) continue;

        const std::string suffix = url.substr(base.size());
        for (const auto& other : bases) {
            if (other == base) continue;
            const std::string candidate = other + suffix;
            if (std::find(urls.begin(), urls.end(), candidate) == urls.end())
                urls.push_back(candidate);
        }
        break;
    }
    return urls;
}

bool FetchManifest(std::string& body, std::string& usedBase, std::wstring& error) {
    const auto bases = config::BaseUrls();
    if (bases.empty()) {
        error = L"Guncelleme sunucusu yapilandirilmamis.";
        return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        for (const auto& base : bases) {
            std::string candidate;
            std::wstring candidateError;
            const std::string url = base + config::kManifestName + CacheBust();

            if (net::Get(Utf8ToWide(url), candidate, candidateError, 8) && !candidate.empty()) {
                body = std::move(candidate);
                usedBase = base;
                Log("Manifest fetched from " + base);
                return true;
            }

            error = candidateError;
            Log("Manifest request failed (" + base + "): " + Narrow(candidateError));
            Sleep(static_cast<DWORD>(250 + attempt * 400));
        }
    }
    return false;
}

std::filesystem::path LocalFilePath(const RemoteFile& file) {
    return g_dataDir / Utf8ToWide(file.path);
}

std::filesystem::path GameFolder(const GameEntry& game) {
    return g_dataDir / Utf8ToWide(game.id);
}

int ParseVersionPart(const std::string& value, std::size_t& cursor) {
    int number = 0;
    while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
        number = number * 10 + (value[cursor] - '0');
        ++cursor;
    }
    if (cursor < value.size() && value[cursor] == '.') ++cursor;
    return number;
}

bool VersionGreater(const std::string& candidate, const std::string& baseline) {
    if (candidate.empty()) return false;

    std::size_t a = 0;
    std::size_t b = 0;
    for (int i = 0; i < 4; ++i) {
        const int left = ParseVersionPart(candidate, a);
        const int right = ParseVersionPart(baseline, b);
        if (left != right) return left > right;
    }
    return false;
}

std::string CacheBust() {
    return "?t=" + std::to_string(GetTickCount64());
}

void SetPhase(const std::string& phase, const std::string& detail, float progress, bool indeterminate) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.phase = phase;
    g_state.detail = detail;
    g_state.progress = std::clamp(progress, 0.0f, 1.0f);
    g_state.indeterminate = indeterminate;
    g_state.busy = true;
}

void FinishPhase(const std::string& phase, const std::string& detail) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.phase = phase;
    g_state.detail = detail;
    g_state.progress = 1.0f;
    g_state.indeterminate = false;
    g_state.busy = false;
}

void SetToast(const std::string& message, bool error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.toast = message;
    g_state.toastError = error;
    g_state.toastTimer = 5.5f;
}

void RefreshReady() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& game : g_state.games) {
        bool ready = true;

        if (game.dllRel.empty()) {
            ready = false;
        } else {
            std::error_code ec;
            if (!std::filesystem::exists(g_dataDir / Utf8ToWide(game.dllRel), ec)) ready = false;
        }

        if (ready && !game.injectorRel.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(g_dataDir / Utf8ToWide(game.injectorRel), ec)) ready = false;
        }

        game.ready = ready;
    }
}

bool FileNeedsUpdate(const RemoteFile& file) {
    std::error_code ec;
    const auto local = LocalFilePath(file);
    if (!std::filesystem::exists(local, ec)) return true;

    if (file.size > 0) {
        const auto size = std::filesystem::file_size(local, ec);
        if (ec || size != file.size) return true;
    }

    if (file.sha256.empty()) return false;

    const std::string hash = crypto::Sha256File(local);
    return hash.empty() || hash != file.sha256;
}

bool ReplaceWithRetry(const std::filesystem::path& source,
                      const std::filesystem::path& target,
                      std::wstring& error) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (std::filesystem::exists(target)) {
            SetFileAttributesW(target.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
        }

        if (MoveFileExW(source.wstring().c_str(), target.wstring().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }

        const DWORD code = GetLastError();
        const bool transient = code == ERROR_SHARING_VIOLATION ||
                               code == ERROR_ACCESS_DENIED ||
                               code == ERROR_LOCK_VIOLATION ||
                               code == ERROR_USER_MAPPED_FILE;
        if (!transient) {
            error = L"Dosya guncellenemedi (hata " + std::to_wstring(code) + L"): " +
                    target.wstring();
            return false;
        }

        Sleep(static_cast<DWORD>(150 + attempt * 120));
    }

    error = L"Dosya guncellenemedi (kilitli): " + target.wstring();
    return false;
}

bool DownloadVerified(const RemoteFile& file, const std::filesystem::path& destination,
                      const std::function<void(float)>& onProgress, std::wstring& error) {
    const auto urls = MirrorUrls(file.url);
    if (urls.empty()) {
        error = L"Indirme adresi bulunamadi: " + Utf8ToWide(file.path);
        return false;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string& url = urls[static_cast<std::size_t>(attempt) % urls.size()];

        std::error_code ec;
        std::filesystem::remove(destination, ec);

        std::wstring downloadError;
        const bool downloaded = net::Download(
            Utf8ToWide(url), destination, downloadError, 180,
            [&](std::uint64_t received, std::uint64_t total) {
                if (!onProgress) return;
                const std::uint64_t expected = total > 0 ? total : file.size;
                if (expected == 0) return;
                const float value = static_cast<float>(
                    static_cast<double>(received) / static_cast<double>(expected));
                onProgress(std::clamp(value, 0.0f, 0.98f));
            });

        if (!downloaded) {
            error = downloadError;
            Log("Download failed (" + std::to_string(attempt + 1) + "/3) " + file.path +
                " from " + url + ": " + Narrow(downloadError));
            Sleep(static_cast<DWORD>(300 * (attempt + 1)));
            continue;
        }

        if (!file.sha256.empty()) {
            const std::string hash = crypto::Sha256File(destination);
            if (hash != file.sha256) {
                error = L"Dosya dogrulamasi basarisiz: " + Utf8ToWide(file.path);
                Log("Hash mismatch (" + std::to_string(attempt + 1) + "/3) " + file.path);
                std::filesystem::remove(destination, ec);
                Sleep(static_cast<DWORD>(300 * (attempt + 1)));
                continue;
            }
        }

        if (file.size > 0) {
            const auto actual = std::filesystem::file_size(destination, ec);
            if (!ec && actual != file.size) {
                error = L"Dosya boyutu uyusmuyor: " + Utf8ToWide(file.path);
                Log("Size mismatch " + file.path + " expected=" + std::to_string(file.size) +
                    " actual=" + std::to_string(actual));
                std::filesystem::remove(destination, ec);
                Sleep(static_cast<DWORD>(300 * (attempt + 1)));
                continue;
            }
        }

        if (onProgress) onProgress(1.0f);
        return true;
    }

    return false;
}

bool EnsureFile(const RemoteFile& file, std::wstring& error,
                const std::function<void(float)>& onProgress = {}) {
    if (!FileNeedsUpdate(file)) {
        if (onProgress) onProgress(1.0f);
        return true;
    }

    const auto local = LocalFilePath(file);
    auto temporary = local;
    temporary += L".part";

    std::error_code ec;
    std::filesystem::create_directories(local.parent_path(), ec);

    if (!DownloadVerified(file, temporary, onProgress, error)) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    if (!ReplaceWithRetry(temporary, local, error)) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    Log("Updated " + file.path + " (" + std::to_string(file.size) + " bytes)");
    return true;
}

bool SyncGameFiles(const GameEntry& game, std::wstring& error, bool reportProgress) {
    if (game.files.empty()) return true;

    int total = 0;
    for (const auto& file : game.files)
        if (FileNeedsUpdate(file)) ++total;

    if (total == 0) return true;

    int done = 0;
    for (const auto& file : game.files) {
        if (!FileNeedsUpdate(file)) continue;

        if (reportProgress) {
            std::filesystem::path relative(file.path);
            SetPhase("Downloading files",
                     relative.filename().string() + " indiriliyor...",
                     static_cast<float>(done) / static_cast<float>(total),
                     false);
        }

        if (!EnsureFile(file, error, [&](float value) {
                if (!reportProgress) return;
                std::filesystem::path relative(file.path);
                SetPhase("Downloading files",
                         relative.filename().string() + " indiriliyor... (" +
                             std::to_string(static_cast<int>(value * 100.0f)) + "%)",
                         (static_cast<float>(done) + value) / static_cast<float>(total),
                         false);
            })) {
            return false;
        }
        ++done;
    }
    return true;
}

std::filesystem::path ManagedListPath() {
    return g_dataDir / ".managed";
}

std::set<std::string> ReadManagedList() {
    std::set<std::string> files;
    std::ifstream file(ManagedListPath(), std::ios::binary);
    if (!file.is_open()) return files;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) files.insert(line);
    }
    return files;
}

std::set<std::string> CurrentManagedFiles() {
    std::set<std::string> files;
    for (const auto& game : Snapshot().games)
        for (const auto& file : game.files)
            if (!file.path.empty()) files.insert(file.path);
    return files;
}

void WriteManagedList(const std::set<std::string>& files) {
    const auto path = ManagedListPath();
    auto temporary = path;
    temporary += L".tmp";

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;
        for (const auto& file : files) out << file << "\n";
    }

    std::error_code ec;
    if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
    }
}

// Removes files that belonged to an older manifest and cleans partial downloads.
void CleanupStaleFiles() {
    const auto current = CurrentManagedFiles();
    const auto previous = ReadManagedList();
    std::error_code ec;

    for (const auto& relative : previous) {
        if (current.count(relative)) continue;

        const auto path = g_dataDir / Utf8ToWide(relative);
        if (std::filesystem::exists(path, ec)) {
            std::filesystem::remove(path, ec);
            Log("Removed stale file " + relative);
        }
    }

    std::vector<std::filesystem::path> partial;
    for (auto it = std::filesystem::recursive_directory_iterator(g_dataDir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;

        const std::wstring name = it->path().filename().wstring();
        const bool part = name.size() > 5 && name.compare(name.size() - 5, 5, L".part") == 0;
        const bool tmp = name.size() > 4 && name.compare(name.size() - 4, 4, L".tmp") == 0;
        if (part || tmp) partial.push_back(it->path());
    }
    for (const auto& path : partial) {
        std::filesystem::remove(path, ec);
        Log("Removed partial file " + Narrow(path.filename().wstring()));
    }

    WriteManagedList(current);

    for (const auto& game : Snapshot().games) {
        const auto folder = g_dataDir / Utf8ToWide(game.id);
        std::filesystem::remove(folder, ec); // only succeeds when empty
    }
    std::filesystem::remove(g_dataDir / "covers", ec);
}

bool SyncAllFiles(bool reportProgress, int* updatedOut = nullptr,
                  std::uint64_t* bytesOut = nullptr) {
    std::vector<RemoteFile> needed;
    std::uint64_t totalBytes = 0;

    for (const auto& game : Snapshot().games) {
        for (const auto& file : game.files) {
            if (!FileNeedsUpdate(file)) continue;
            needed.push_back(file);
            totalBytes += file.size > 0 ? file.size : (1024ull * 1024ull);
        }
    }

    int updated = 0;
    std::uint64_t doneBytes = 0;

    for (std::size_t i = 0; i < needed.size(); ++i) {
        const RemoteFile& file = needed[i];
        const std::uint64_t weight = file.size > 0 ? file.size : (1024ull * 1024ull);

        if (reportProgress) {
            SetPhase("Updating files",
                     file.path + " (" + std::to_string(i + 1) + "/" +
                         std::to_string(needed.size()) + ")",
                     totalBytes > 0 ? static_cast<float>(
                         static_cast<double>(doneBytes) / static_cast<double>(totalBytes))
                                    : 0.0f,
                     false);
        }

        std::wstring error;
        const bool ok = EnsureFile(file, error, [&](float value) {
            if (!reportProgress) return;
            const float base = totalBytes > 0 ? static_cast<float>(
                static_cast<double>(doneBytes) / static_cast<double>(totalBytes)) : 0.0f;
            const float span = totalBytes > 0 ? static_cast<float>(
                static_cast<double>(weight) / static_cast<double>(totalBytes)) : 0.0f;
            SetPhase("Updating files",
                     file.path + " indiriliyor... (" +
                         std::to_string(static_cast<int>(value * 100.0f)) + "%)",
                     base + span * value, false);
        });

        if (!ok) {
            if (reportProgress) SetToast("Dosya guncellenemedi: " + Narrow(error), true);
            Log("Sync failed on " + file.path + ": " + Narrow(error));
            return false;
        }

        ++updated;
        doneBytes += weight;
    }

    if (updated > 0) {
        CleanupStaleFiles();
        Log("Sync completed: " + std::to_string(updated) + " file(s) updated");
    } else {
        WriteManagedList(CurrentManagedFiles());
    }

    RefreshReady();
    if (updatedOut) *updatedOut = updated;
    if (bytesOut) *bytesOut = doneBytes;
    return true;
}

RemoteFile MakeRemoteFile(const std::string& relativePath) {
    RemoteFile file;
    file.path = relativePath;
    file.url = config::BaseUrl() + "dist/" + relativePath;
    return file;
}

std::vector<GameEntry> DefaultGames() {
    std::vector<GameEntry> games;

    {
        GameEntry game;
        game.id = "redmatch2";
        game.name = "Redmatch 2";
        game.subtitle = "Unity Engine  •  FPS";
        game.status = "Undetected";
        game.tag = "Bypass Injector";
        game.process = "Redmatch 2.exe";
        game.appid = "1280770";
        game.installDir = "Redmatch 2";
        game.exeRel = "Redmatch 2.exe";
        game.dllRel = "redmatch2/LaynexRm2.dll";
        game.injectorRel = "redmatch2/LaynexRm2Injector.exe";
        game.coverRel = "covers/rm2.png";
        game.accent = 0;
        game.files = {
            MakeRemoteFile(game.dllRel),
            MakeRemoteFile(game.injectorRel),
            MakeRemoteFile(game.coverRel),
        };
        games.push_back(std::move(game));
    }

    {
        GameEntry game;
        game.id = "onearmedrobber";
        game.name = "One Armed Robber";
        game.subtitle = "Unreal Engine 4  •  FPS";
        game.status = "Undetected";
        game.tag = "Internal DLL";
        game.process = "OAR-Win64-Shipping.exe";
        game.appid = "2551020";
        game.installDir = "One-armed robber";
        game.exeRel = "OAR\\Binaries\\Win64\\OAR-Win64-Shipping.exe";
        game.dllRel = "onearmedrobber/one_armed_robber_int.dll";
        game.injectorRel = "";
        game.coverRel = "covers/oar.png";
        game.accent = 1;
        game.files = {
            MakeRemoteFile(game.dllRel),
            MakeRemoteFile(game.coverRel),
        };
        games.push_back(std::move(game));
    }

    return games;
}

struct RemoteManifest {
    std::vector<GameEntry> games;
    std::string version;
    std::string loaderUrl;
    std::string loaderSha;
};

std::string ResolveUrl(const std::string& url, const std::string& base) {
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return url;
    if (!url.empty() && url.front() == '/') return base + url.substr(1);
    return base + url;
}

bool ParseManifest(const std::string& body, const std::string& base,
                   RemoteManifest& manifest) {
    json::Value root;
    std::string parseError;
    if (!json::Parse(body, root, &parseError) || !root.IsObject()) return false;

    manifest.version = root.At("loader_version").AsString();
    manifest.loaderUrl = ResolveUrl(root.At("loader_url").AsString(), base);
    manifest.loaderSha = root.At("loader_sha256").AsString();

    const json::Value& gamesValue = root.At("games");
    if (!gamesValue.IsArray() || gamesValue.Size() == 0) return false;

    std::vector<GameEntry> parsed;
    for (std::size_t i = 0; i < gamesValue.Size(); ++i) {
        const json::Value& item = gamesValue[i];
        if (!item.IsObject()) continue;

        GameEntry game;
        game.id = item.At("id").AsString();
        if (game.id.empty()) continue;

        game.name = item.At("name").AsString(game.id);
        game.subtitle = item.At("subtitle").AsString();
        game.status = item.At("status").AsString("Undetected");
        game.tag = item.At("tag").AsString();
        game.process = item.At("process").AsString();
        game.appid = item.At("appid").AsString();
        game.installDir = item.At("installdir").AsString();
        game.exeRel = item.At("exe_rel").AsString();
        game.dllRel = item.At("dll").AsString();
        game.injectorRel = item.At("injector").AsString();
        game.coverRel = item.At("cover").AsString();
        game.accent = static_cast<int>(item.At("accent").AsNumber(i));

        const json::Value& filesValue = item.At("files");
        if (filesValue.IsArray()) {
            for (std::size_t f = 0; f < filesValue.Size(); ++f) {
                const json::Value& fileValue = filesValue[f];
                if (!fileValue.IsObject()) continue;

                RemoteFile file;
                file.path = fileValue.At("path").AsString();
                file.url = ResolveUrl(fileValue.At("url").AsString(), base);
                file.sha256 = fileValue.At("sha256").AsString();
                file.size = static_cast<std::uint64_t>(fileValue.At("size").AsNumber(0.0));
                if (!file.path.empty() && !file.url.empty())
                    game.files.push_back(std::move(file));
            }
        }

        parsed.push_back(std::move(game));
    }

    if (parsed.empty()) return false;
    manifest.games = std::move(parsed);
    return true;
}

bool LoaderOutdated(const RemoteManifest& manifest) {
    const bool remoteNewer = VersionGreater(manifest.version, config::kVersion);
    if (remoteNewer) return true;

    // Never downgrade a local development build that is ahead of the remote.
    if (VersionGreater(config::kVersion, manifest.version)) return false;

    if (manifest.loaderSha.empty()) return false;
    const std::string& local = LocalExeHash();
    return !local.empty() && local != manifest.loaderSha;
}

void ApplyManifest(RemoteManifest manifest) {
    const bool update = LoaderOutdated(manifest);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.games = std::move(manifest.games);
    g_state.online = true;
    g_state.remoteVersion = manifest.version;
    g_state.updateAvailable = update;
    g_loaderUrl = std::move(manifest.loaderUrl);
    g_loaderSha = std::move(manifest.loaderSha);

    if (update) {
        Log("Loader update queued: local=" + std::string(config::kVersion) +
            " remote=" + g_state.remoteVersion + " exe hash " +
            (g_loaderSha == LocalExeHash() ? "matches" : "differs"));
    }
}

void SaveCache(const std::string& body) {
    std::error_code ec;
    std::filesystem::create_directories(g_dataDir, ec);

    const auto path = g_dataDir / "manifest.json";
    auto temporary = path;
    temporary += ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return;
        file.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!file.good()) return;
    }

    const auto backup = g_dataDir / "manifest.json.bak";
    if (std::filesystem::exists(path, ec))
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);

    if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
    }
}

bool LoadCache() {
    std::ifstream file(g_dataDir / "manifest.json", std::ios::binary);
    if (!file.is_open()) return false;

    const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    RemoteManifest manifest;
    if (!ParseManifest(body, config::BaseUrl(), manifest)) return false;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.games = std::move(manifest.games);
    g_state.remoteVersion = manifest.version;
    g_loaderUrl = std::move(manifest.loaderUrl);
    g_loaderSha = std::move(manifest.loaderSha);
    return true;
}

void StartupWorker() {
    Log("=== VcDex Loader " + std::string(config::kVersion) + " starting ===");
    LocalExeHash();

    SetPhase("Checking for updates", "Connecting to VcDex servers...", 0.08f, true);

    std::string body;
    std::string base;
    std::wstring error;
    RemoteManifest manifest;
    const bool fetched = FetchManifest(body, base, error) && ParseManifest(body, base, manifest);

    bool syncOk = true;
    int updated = 0;
    std::uint64_t bytes = 0;

    if (fetched) {
        SaveCache(body);
        ApplyManifest(std::move(manifest));
        RefreshReady();

        SetPhase("Verifying files", "All installed files are being verified...", 0.2f, true);
        syncOk = SyncAllFiles(true, &updated, &bytes);
    } else {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_state.online = false;
        }
        Log("Offline mode: " + Narrow(error));
        RefreshReady();
    }

    RefreshReady();

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.checking = false;
        g_state.busy = false;
        g_state.ready = true;
        g_state.progress = 1.0f;
        g_state.indeterminate = false;
        g_state.phase = "Ready";
        g_state.filesUpdated = updated;

        if (!syncOk)
            g_state.detail = "Some files could not be downloaded - retrying on next launch";
        else if (fetched)
            g_state.detail = updated > 0
                ? "Automatic update applied (" + std::to_string(updated) + " files)"
                : "All files are up to date";
        else
            g_state.detail = "Offline mode - using locally installed files";
    }

    Log("Startup complete: fetched=" + std::string(fetched ? "yes" : "no") +
        " updated=" + std::to_string(updated) + " bytes=" + std::to_string(bytes));
}

void BackgroundWorker() {
    Log("Automatic background check started");

    std::string body;
    std::string base;
    std::wstring error;
    RemoteManifest manifest;
    int updated = 0;

    const bool fetched = FetchManifest(body, base, error) && ParseManifest(body, base, manifest);
    if (fetched) {
        SaveCache(body);
        ApplyManifest(std::move(manifest));
        RefreshReady();

        if (!SyncAllFiles(false, &updated))
            Log("Background sync failed");

        if (updated > 0) {
            SetToast("Dosyalar otomatik guncellendi (" + std::to_string(updated) + ")", false);
        } else if (Snapshot().updateAvailable) {
            SetToast("Yeni loader surumu hazir - F5 ile guncelle", false);
        }
    } else {
        Log("Background check failed: " + Narrow(error));
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.backgroundSync = false;
        g_state.filesUpdated += updated;
    }

    g_background = false;
    g_busy = false;
}

void FailLoad(const std::string& message) {
    SetToast(message, true);
    FinishPhase("Ready", message);
    g_busy = false;
}

void LoadGameWorker(GameEntry game) {
    try {
        SetPhase("Preparing", "Verifying game files...", 0.05f, true);

        std::wstring error;
        if (!SyncGameFiles(game, error, true)) {
            FailLoad("Dosyalar hazirlanamadi: " + Narrow(error));
            return;
        }
        RefreshReady();

        const auto dllPath = g_dataDir / Utf8ToWide(game.dllRel);
        std::error_code ec;
        if (!std::filesystem::exists(dllPath, ec)) {
            FailLoad("DLL bulunamadi: " + game.dllRel);
            return;
        }

        const std::wstring processName = Utf8ToWide(game.process);
        const std::wstring appId = Utf8ToWide(game.appid);
        const std::wstring installDir = Utf8ToWide(game.installDir);
        const std::wstring exeRel = Utf8ToWide(game.exeRel);

        if (!game.injectorRel.empty()) {
            const auto injectorPath = g_dataDir / Utf8ToWide(game.injectorRel);
            if (!std::filesystem::exists(injectorPath, ec)) {
                FailLoad("Injector bulunamadi: " + game.injectorRel);
                return;
            }

            DWORD pid = win::FindProcess(processName);
            std::filesystem::path gameExe;

            if (pid != 0) win::GetProcessImagePath(pid, gameExe);
            if (gameExe.empty())
                win::FindGameExecutable(appId, installDir, exeRel, gameExe);

            if (gameExe.empty()) {
                FailLoad("Oyun yolu bulunamadi. Oyunu bir kez Steam uzerinden acin.");
                return;
            }

            const bool early = (pid == 0);

            SetPhase(early ? "Launching game" : "Injecting",
                     early ? "Starting game with early bypass injection..."
                           : "Attaching to the running game...",
                     0.45f, true);

            const std::wstring command =
                Quote(injectorPath.wstring()) +
                (early ? L" --auto " : L" ") +
                Quote(gameExe.wstring()) + L" " +
                Quote(std::filesystem::absolute(dllPath, ec).wstring());

            DWORD exitCode = 0;
            if (!win::RunAndWait(command, 300000, exitCode, error)) {
                FailLoad("Injector calistirilamadi: " + Narrow(error));
                return;
            }

            if (exitCode != 0) {
                FailLoad("Enjeksiyon basarisiz (injector kodu " + std::to_string(exitCode) + ").");
                return;
            }
        } else {
            DWORD pid = win::FindProcess(processName);
            std::filesystem::path gameExe;
            win::FindGameExecutable(appId, installDir, exeRel, gameExe);

            if (pid == 0) {
                SetPhase("Launching game", "Starting " + game.name + "...", 0.3f, true);
                if (!win::LaunchGame(appId, gameExe, error)) {
                    FailLoad("Oyun baslatilamadi: " + Narrow(error));
                    return;
                }

                pid = win::WaitForProcess(processName, 150000);
                if (pid == 0) {
                    FailLoad("Oyun process'i bulunamadi (zaman asimi).");
                    return;
                }
            }

            SetPhase("Injecting", "Waiting for the game to initialize...", 0.6f, true);
            Sleep(2500);

            bool injected = false;
            for (int attempt = 0; attempt < 6 && !injected; ++attempt) {
                if (win::InjectDll(pid, dllPath, error)) {
                    injected = true;
                    break;
                }
                Sleep(1200);
            }

            if (!injected) {
                FailLoad("DLL enjekte edilemedi: " + Narrow(error));
                return;
            }
        }

        FinishPhase("Ready", game.name + " loaded successfully");
        SetToast(game.name + " basariyla yuklendi", false);
    } catch (...) {
        FailLoad("Beklenmeyen bir hata olustu.");
    }

    g_busy = false;
}

bool WriteUpdaterBatch(const std::filesystem::path& batchPath,
                       const std::filesystem::path& newExe,
                       const std::filesystem::path& targetExe,
                       DWORD parentPid,
                       const std::filesystem::path& marker) {
    std::wofstream batch(batchPath, std::ios::trunc);
    if (!batch.is_open()) return false;

    batch << L"@echo off\r\n"
          << L"setlocal\r\n"
          << L"set \"LOG=%TEMP%\\vcdex_self_update.log\"\r\n"
          << L"echo [%date% %time%] self-update started >> \"%LOG%\"\r\n"
          << L"set \"TARGET=" << targetExe.wstring() << L"\"\r\n"
          << L"set \"NEW=" << newExe.wstring() << L"\"\r\n"
          << L"set \"MARK=" << marker.wstring() << L"\"\r\n"
          << L"set \"WAIT_PID=" << parentPid << L"\"\r\n"
          << L"set /a waited=0\r\n"
          << L":waitloop\r\n"
          << L"tasklist /FI \"PID eq %WAIT_PID%\" /NH 2>nul | find \"%WAIT_PID%\" >nul\r\n"
          << L"if errorlevel 1 goto apply\r\n"
          << L"set /a waited+=1\r\n"
          << L"if %waited% geq 120 goto apply\r\n"
          << L"ping 127.0.0.1 -n 2 >nul\r\n"
          << L"goto waitloop\r\n"
          << L":apply\r\n"
          << L"set /a tries=0\r\n"
          << L":retry\r\n"
          << L"if exist \"%TARGET%.old\" del /f /q \"%TARGET%.old\" >nul 2>&1\r\n"
          << L"move /y \"%TARGET%\" \"%TARGET%.old\" >nul 2>&1\r\n"
          << L"move /y \"%NEW%\" \"%TARGET%\" >nul 2>&1\r\n"
          << L"if not errorlevel 1 goto success\r\n"
          << L"if exist \"%TARGET%.old\" move /y \"%TARGET%.old\" \"%TARGET%\" >nul 2>&1\r\n"
          << L"set /a tries+=1\r\n"
          << L"if %tries% lss 80 goto waitmore\r\n"
          << L"echo [%date% %time%] self-update FAILED >> \"%LOG%\"\r\n"
          << L"exit /b 1\r\n"
          << L":waitmore\r\n"
          << L"ping 127.0.0.1 -n 2 >nul\r\n"
          << L"goto retry\r\n"
          << L":success\r\n"
          << L"echo [%date% %time%] self-update OK >> \"%LOG%\"\r\n"
          << L"type nul > \"%MARK%\"\r\n"
          << L"start \"\" \"%TARGET%\"\r\n"
          << L"del \"%~f0\"\r\n";
    return true;
}

void UpdateWorker() {
    try {
        std::string url;
        std::string sha;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            url = g_loaderUrl;
            sha = g_loaderSha;
        }

        if (url.empty()) {
            FailLoad("Guncelleme adresi bulunamadi.");
            g_busy = false;
            return;
        }

        if (!sha.empty() && !LocalExeHash().empty() && LocalExeHash() == sha) {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_state.updateAvailable = false;
            }
            Log("Self-update skipped: binary already matches manifest");
            FinishPhase("Ready", "VcDex Loader is already up to date");
            SetToast("VcDex Loader zaten guncel", false);
            g_busy = false;
            return;
        }

        const auto targetExe = g_exePath.empty() ? (g_baseDir / "VcDexLoader.exe") : g_exePath;
        auto newExe = targetExe;
        newExe.replace_filename(targetExe.stem().wstring() + L".update.exe");

        RemoteFile file;
        file.path = Narrow(targetExe.filename().wstring());
        file.url = url + CacheBust();
        file.sha256 = sha;

        SetPhase("Downloading update", "Downloading the latest VcDex Loader...", 0.1f, true);

        std::wstring error;
        const bool downloaded = DownloadVerified(file, newExe, [&](float value) {
            SetPhase("Downloading update",
                     "VcDex Loader indiriliyor... (" +
                         std::to_string(static_cast<int>(value * 100.0f)) + "%)",
                     0.1f + 0.72f * value, false);
        }, error);

        if (!downloaded) {
            FailLoad("Guncelleme indirilemedi: " + Narrow(error));
            g_busy = false;
            return;
        }

        SetPhase("Restarting", "Applying update, VcDex will restart...", 0.9f, true);
        Sleep(350);

        const auto batchPath = std::filesystem::temp_directory_path() /
            ("vcdex_self_update_" + std::to_string(GetCurrentProcessId()) + ".bat");
        const auto marker = g_dataDir / ".just-updated";

        if (!WriteUpdaterBatch(batchPath, newExe, targetExe, GetCurrentProcessId(), marker)) {
            FailLoad("Guncelleme uygulanamadi (gecici dosya).");
            g_busy = false;
            return;
        }

        std::wstring command = L"/c \"" + batchPath.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", command.data(),
                           nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &info)) {
            CloseHandle(info.hThread);
            CloseHandle(info.hProcess);
            Log("Self-update helper launched, restarting");
            g_exitRequested = true;
        } else {
            FailLoad("Guncelleme uygulanamadi.");
        }
    } catch (...) {
        FailLoad("Guncelleme sirasinda hata olustu.");
    }

    g_busy = false;
}

void StartBackgroundCheck() {
    if (g_busy.exchange(true)) return;

    g_background = true;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.backgroundSync = true;
    }
    std::thread(BackgroundWorker).detach();
}

} // namespace

void Init(HWND hwnd) {
    (void)hwnd;

    wchar_t modulePath[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));

    g_exePath = modulePath;
    g_baseDir = g_exePath.parent_path();
    g_dataDir = ResolveDataDir(g_baseDir);

    std::error_code ec;
    std::filesystem::create_directories(g_dataDir, ec);

    Log("Runtime: exe=" + Narrow(g_exePath.wstring()) + " data=" + Narrow(g_dataDir.wstring()));

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.localVersion = config::kVersion;
        g_state.phase = "Connecting";
        g_state.detail = "Preparing VcDex...";
    }

    const auto updateMarker = g_dataDir / ".just-updated";
    if (std::filesystem::exists(updateMarker, ec)) {
        std::filesystem::remove(updateMarker, ec);
        SetToast("VcDex Loader basariyla guncellendi", false);
        Log("Self-update verified after restart");
    }

    if (!LoadCache()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.games = DefaultGames();
    } else {
        // The cached manifest can already reveal an update before the network
        // request finishes, so the splash reacts immediately.
        const std::string local = LocalExeHash();
        std::lock_guard<std::mutex> lock(g_mutex);
        const bool versionNewer = VersionGreater(g_state.remoteVersion, config::kVersion);
        const bool hashDiffers = !g_loaderSha.empty() && !local.empty() && local != g_loaderSha;
        g_state.updateAvailable = versionNewer || hashDiffers;
    }

    RefreshReady();

    std::thread(StartupWorker).detach();
}

AppUiState Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state;
}

void Tick(float deltaSeconds) {
    bool autoCheck = false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.toastTimer > 0.0f) {
            g_state.toastTimer -= deltaSeconds;
            if (g_state.toastTimer <= 0.0f) {
                g_state.toastTimer = 0.0f;
                g_state.toast.clear();
            }
        }

        g_autoCheckElapsed += deltaSeconds;
        if (g_autoCheckElapsed >= config::kAutoCheckSeconds && g_state.ready &&
            !g_state.checking && !g_state.busy && !g_state.backgroundSync) {
            g_autoCheckElapsed = 0.0f;
            autoCheck = true;
        }
    }

    if (autoCheck) StartBackgroundCheck();
}

void CheckFiles() {
    if (g_busy.exchange(true)) {
        SetToast("Islem suruyor, lutfen bekleyin.", false);
        return;
    }

    std::thread([] {
        Log("Manual update check started");

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_state.busy = true;
            g_state.phase = "Checking for updates";
            g_state.detail = "Connecting to VcDex servers...";
            g_state.progress = 0.05f;
            g_state.indeterminate = true;
        }

        std::string body;
        std::string base;
        std::wstring error;
        RemoteManifest manifest;
        int updated = 0;
        bool ok = false;

        if (FetchManifest(body, base, error) && ParseManifest(body, base, manifest)) {
            SaveCache(body);
            ApplyManifest(std::move(manifest));
            RefreshReady();

            SetPhase("Verifying files", "All installed files are being verified...", 0.25f, true);
            ok = SyncAllFiles(true, &updated);
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_state.busy = false;
            g_state.checking = false;
            g_state.ready = true;
            g_state.indeterminate = false;
            g_state.progress = 1.0f;
            g_state.phase = "Ready";
            g_state.filesUpdated = updated;
        }

        if (!ok) {
            SetToast("Sunucuya ulasilamadi veya bazi dosyalar indirilemedi", true);
        } else if (Snapshot().updateAvailable) {
            SetToast("Yeni surum hazir - F5 ile guncelle", false);
        } else if (updated > 0) {
            SetToast("Dosyalar guncellendi (" + std::to_string(updated) + ")", false);
        } else {
            SetToast("Tum dosyalar guncel", false);
        }

        g_busy = false;
    }).detach();
}

void ClearToast() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.toast.clear();
    g_state.toastTimer = 0.0f;
}

void SelectGame(int index) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (index < 0 || index >= static_cast<int>(g_state.games.size())) return;
    g_state.selected = index;
}

void LoadSelectedGame() {
    if (g_busy.exchange(true)) return;

    GameEntry selected;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.games.empty()) {
            g_busy = false;
            return;
        }
        const int index = std::clamp(g_state.selected, 0, static_cast<int>(g_state.games.size()) - 1);
        selected = g_state.games[static_cast<std::size_t>(index)];
    }

    std::thread(LoadGameWorker, selected).detach();
}

void StartSelfUpdate() {
    if (g_busy.exchange(true)) {
        SetToast("Islem suruyor, lutfen bekleyin.", false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.busy = true;
        g_state.phase = "Preparing update";
        g_state.detail = "Preparing VcDex Loader update...";
        g_state.progress = 0.03f;
        g_state.indeterminate = true;
    }

    Log("Self-update requested by user");
    std::thread(UpdateWorker).detach();
}

void OpenSelectedFolder() {
    GameEntry selected;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.games.empty()) return;
        const int index = std::clamp(g_state.selected, 0, static_cast<int>(g_state.games.size()) - 1);
        selected = g_state.games[static_cast<std::size_t>(index)];
    }

    const auto folder = GameFolder(selected);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOW);
}

bool ExitRequested() {
    return g_exitRequested.load();
}

std::filesystem::path BaseDir() {
    return g_baseDir;
}

std::filesystem::path DataDir() {
    return g_dataDir;
}

} // namespace app
