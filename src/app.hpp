#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

struct RemoteFile {
    std::string path;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};

struct GameEntry {
    std::string id;
    std::string name;
    std::string subtitle;
    std::string status;
    std::string tag;
    std::string process;
    std::string appid;
    std::string installDir;
    std::string exeRel;
    std::string dllRel;
    std::string injectorRel;
    std::string coverRel;
    int accent = 0;

    std::vector<RemoteFile> files;
    bool ready = false;
};

struct AppUiState {
    std::vector<GameEntry> games;

    int selected = 0;

    bool online = false;
    bool busy = false;
    bool checking = true;
    bool ready = false;
    bool backgroundSync = false;

    float progress = 0.0f;
    bool indeterminate = false;
    std::string phase;
    std::string detail;

    bool updateAvailable = false;
    std::string localVersion;
    std::string remoteVersion;

    int filesUpdated = 0;

    std::string toast;
    bool toastError = false;
    float toastTimer = 0.0f;
};

namespace app {

void InitPaths();
void Init(HWND hwnd);
AppUiState Snapshot();

void Tick(float dt);

void SelectGame(int index);
void LoadSelectedGame();
void StartSelfUpdate();
void CheckFiles();
void OpenSelectedFolder();

void ClearToast();

bool ExitRequested();

std::filesystem::path BaseDir();
std::filesystem::path DataDir();

} // namespace app
