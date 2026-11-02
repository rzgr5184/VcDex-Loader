#pragma once

#include <windows.h>
#include <d3d11.h>

#include <filesystem>

namespace images {

// Resource ids defined in res/resources.rc
enum Id {
    VcLogo          = 201,
    Redmatch2Cover  = 202,
    OneArmedCover   = 203,
};

bool Init(ID3D11Device* device);
void Shutdown();

// Loads an up-to-date cover from disk (data/covers/...). Keeps the embedded
// fallback when the file does not exist or cannot be decoded. Cheap to call
// every frame: the file timestamp is cached.
bool LoadFile(Id id, const std::filesystem::path& path);

ID3D11ShaderResourceView* Srv(Id id);
int Width(Id id);
int Height(Id id);

} // namespace images
