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
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace app {
namespace {

std::mutex g_mutex;
AppUiState g_state;
std::atomic<bool> g_busy{false};
std::atomic<bool> g_exitRequested{false};

std::filesystem::path g_baseDir;
std::filesystem::path g_dataDir;
std::string g_loaderUrl;
std::string g_loaderSha;

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

std::string ManifestUrl() {
    const std::string base = config::BaseUrl();
    if (base.empty()) return {};
    return base + config::kManifestName;
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
    if (file.sha256.empty()) return false;

    const std::string hash = crypto::Sha256File(local);
    return hash.empty() || hash != file.sha256;
}

bool EnsureFile(const RemoteFile& file, std::wstring& error) {
    if (!FileNeedsUpdate(file)) return true;

    const auto local = LocalFilePath(file);
    auto temporary = local;
    temporary += L".tmp";

    std::error_code ec;
    std::filesystem::create_directories(local.parent_path(), ec);

    if (!net::Download(Utf8ToWide(file.url), temporary, error, 120)) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    if (!file.sha256.empty()) {
        const std::string hash = crypto::Sha256File(temporary);
        if (hash != file.sha256) {
            error = L"Dosya dogrulamasi basarisiz: " + Utf8ToWide(file.path);
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    if (!MoveFileExW(temporary.wstring().c_str(), local.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"Dosya guncellenemedi (hata " + std::to_wstring(GetLastError()) + L"): " +
                local.wstring();
        std::filesystem::remove(temporary, ec);
        return false;
    }

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

        if (!EnsureFile(file, error)) return false;
        ++done;
    }
    return true;
}

bool SyncAllFiles() {
    for (const auto& game : Snapshot().games) {
        std::wstring error;
        if (!SyncGameFiles(game, error, true)) {
            SetToast("Dosya guncellenemedi: " + Narrow(error), true);
            return false;
        }
    }
    RefreshReady();
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
        game.accent = 0;
        game.files = {
            MakeRemoteFile(game.dllRel),
            MakeRemoteFile(game.injectorRel),
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
        game.accent = 1;
        game.files = {
            MakeRemoteFile(game.dllRel),
        };
        games.push_back(std::move(game));
    }

    return games;
}

std::string ResolveUrl(const std::string& url) {
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return url;
    const std::string base = config::BaseUrl();
    if (!url.empty() && url.front() == '/') return base + url.substr(1);
    return base + url;
}

bool ParseManifest(const std::string& body, std::vector<GameEntry>& games,
                   std::string& loaderVersion, std::string& loaderUrl,
                   std::string& loaderSha) {
    json::Value root;
    std::string parseError;
    if (!json::Parse(body, root, &parseError) || !root.IsObject()) return false;

    loaderVersion = root.At("loader_version").AsString();
    loaderUrl = ResolveUrl(root.At("loader_url").AsString());
    loaderSha = root.At("loader_sha256").AsString();

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
        game.accent = static_cast<int>(item.At("accent").AsNumber(i));

        const json::Value& filesValue = item.At("files");
        if (filesValue.IsArray()) {
            for (std::size_t f = 0; f < filesValue.Size(); ++f) {
                const json::Value& fileValue = filesValue[f];
                if (!fileValue.IsObject()) continue;

                RemoteFile file;
                file.path = fileValue.At("path").AsString();
                file.url = ResolveUrl(fileValue.At("url").AsString());
                file.sha256 = fileValue.At("sha256").AsString();
                if (!file.path.empty() && !file.url.empty())
                    game.files.push_back(std::move(file));
            }
        }

        parsed.push_back(std::move(game));
    }

    if (parsed.empty()) return false;
    games = std::move(parsed);
    return true;
}

void SaveCache(const std::string& body) {
    std::error_code ec;
    std::filesystem::create_directories(g_dataDir, ec);
    std::ofstream file(g_dataDir / "manifest.json", std::ios::binary | std::ios::trunc);
    if (file.is_open()) file.write(body.data(), static_cast<std::streamsize>(body.size()));
}

bool LoadCache() {
    std::ifstream file(g_dataDir / "manifest.json", std::ios::binary);
    if (!file.is_open()) return false;

    const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<GameEntry> games;
    std::string version, url, sha;
    if (!ParseManifest(body, games, version, url, sha)) return false;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.games = std::move(games);
    g_state.remoteVersion = version;
    g_loaderUrl = url;
    g_loaderSha = sha;
    return true;
}

void StartupWorker() {
    SetPhase("Checking for updates", "Connecting to VcDex servers...", 0.08f, true);

    std::string body;
    std::wstring error;
    std::vector<GameEntry> remoteGames;
    std::string remoteVersion;
    std::string loaderUrl;
    std::string loaderSha;

    const std::string manifestUrl = ManifestUrl();
    const bool configured = !manifestUrl.empty();
    bool syncOk = true;
    const bool fetched = configured &&
        net::Get(Utf8ToWide(manifestUrl + CacheBust()), body, error, 15) &&
        ParseManifest(body, remoteGames, remoteVersion, loaderUrl, loaderSha);

    if (fetched) {
        SaveCache(body);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_state.games = remoteGames;
            g_state.online = true;
            g_state.remoteVersion = remoteVersion;
            g_state.updateAvailable = VersionGreater(remoteVersion, config::kVersion);
            g_loaderUrl = loaderUrl;
            g_loaderSha = loaderSha;
        }

        RefreshReady();

        SetPhase("Syncing files", "Checking installed game files...", 0.25f, true);
        syncOk = SyncAllFiles();
    } else {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_state.online = false;
        }
        RefreshReady();
    }

    RefreshReady();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.checking = false;
    g_state.busy = false;
    g_state.ready = true;
    g_state.progress = 1.0f;
    g_state.indeterminate = false;
    g_state.phase = "Ready";
    g_state.detail = !syncOk ? "Some files could not be downloaded"
                             : (g_state.online ? "All files are up to date"
                                               : "Offline mode - using locally installed files");
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
                       const std::filesystem::path& targetExe) {
    std::wofstream batch(batchPath, std::ios::trunc);
    if (!batch.is_open()) return false;

    batch << L"@echo off\r\n"
          << L"setlocal\r\n"
          << L"ping 127.0.0.1 -n 3 >nul\r\n"
          << L"set /a tries=0\r\n"
          << L":retry\r\n"
          << L"move /y \"" << newExe.wstring() << L"\" \"" << targetExe.wstring() << L"\" >nul 2>&1\r\n"
          << L"if errorlevel 1 (\r\n"
          << L"  set /a tries+=1\r\n"
          << L"  if %tries% lss 40 (\r\n"
          << L"    ping 127.0.0.1 -n 1 >nul\r\n"
          << L"    goto retry\r\n"
          << L"  )\r\n"
          << L"  exit /b 1\r\n"
          << L")\r\n"
          << L"start \"\" \"" << targetExe.wstring() << L"\"\r\n"
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

        const auto newExe = g_baseDir / "VcDexLoader.update.exe";
        const auto targetExe = g_baseDir / "VcDexLoader.exe";

        SetPhase("Downloading update", "Downloading the latest VcDex Loader...", 0.2f, true);

        std::wstring error;
        if (!net::Download(Utf8ToWide(url) + Utf8ToWide(CacheBust()), newExe, error, 180)) {
            FailLoad("Guncelleme indirilemedi.");
            g_busy = false;
            return;
        }

        if (!sha.empty() && crypto::Sha256File(newExe) != sha) {
            std::error_code ec;
            std::filesystem::remove(newExe, ec);
            FailLoad("Guncelleme dogrulanamadi (hash uyusmuyor).");
            g_busy = false;
            return;
        }

        SetPhase("Restarting", "Applying update, VcDex will restart...", 0.9f, true);
        Sleep(400);

        const auto batchPath = std::filesystem::temp_directory_path() / "vcdex_self_update.bat";
        if (!WriteUpdaterBatch(batchPath, newExe, targetExe)) {
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
            g_exitRequested = true;
        } else {
            FailLoad("Guncelleme uygulanamadi.");
        }
    } catch (...) {
        FailLoad("Guncelleme sirasinda hata olustu.");
    }

    g_busy = false;
}

} // namespace

void Init(HWND hwnd) {
    (void)hwnd;

    wchar_t modulePath[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));

    g_baseDir = std::filesystem::path(modulePath).parent_path();
    g_dataDir = g_baseDir / "data";

    std::error_code ec;
    std::filesystem::create_directories(g_dataDir, ec);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.localVersion = config::kVersion;
        g_state.phase = "Connecting";
        g_state.detail = "Preparing VcDex...";
    }

    if (!LoadCache()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.games = DefaultGames();
    }

    RefreshReady();

    std::thread(StartupWorker).detach();
}

AppUiState Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state;
}

void Tick(float deltaSeconds) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.toastTimer > 0.0f) {
        g_state.toastTimer -= deltaSeconds;
        if (g_state.toastTimer <= 0.0f) {
            g_state.toastTimer = 0.0f;
            g_state.toast.clear();
        }
    }
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
    if (g_busy.exchange(true)) return;
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
