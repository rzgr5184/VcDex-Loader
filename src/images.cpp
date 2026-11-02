#include "images.hpp"

#include <fstream>
#include <iterator>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace images {
namespace {

struct Texture {
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
};

Texture g_textures[3];
std::filesystem::file_time_type g_stamps[3];
bool g_hasStamp[3] = {};
ID3D11Device* g_device = nullptr;

int SlotIndex(Id id) {
    return static_cast<int>(id) - static_cast<int>(VcLogo);
}

Texture* Slot(Id id) {
    const int index = SlotIndex(id);
    if (index < 0 || index >= static_cast<int>(std::size(g_textures))) return nullptr;
    return &g_textures[index];
}

bool Upload(ID3D11Device* device, const unsigned char* bytes, std::size_t size,
            Texture& target) {
    if (!device || !bytes || size == 0) return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels;
    initial.SysMemPitch = static_cast<UINT>(width * 4);

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initial, &texture);
    if (SUCCEEDED(hr) && texture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        viewDesc.Format = desc.Format;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* srv = nullptr;
        hr = device->CreateShaderResourceView(texture, &viewDesc, &srv);
        texture->Release();

        if (SUCCEEDED(hr) && srv) {
            if (target.srv) target.srv->Release();
            target.srv = srv;
            target.width = width;
            target.height = height;
            stbi_image_free(pixels);
            return true;
        }
    }

    stbi_image_free(pixels);
    return false;
}

bool ReadResource(int id, std::vector<unsigned char>& out) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!info) return false;

    HGLOBAL handle = LoadResource(module, info);
    if (!handle) return false;

    const DWORD size = SizeofResource(module, info);
    const void* data = LockResource(handle);
    if (!data || size == 0) return false;

    const auto* bytes = static_cast<const unsigned char*>(data);
    out.assign(bytes, bytes + size);
    return true;
}

Texture* LoadEmbedded(Id id) {
    Texture* slot = Slot(id);
    if (!slot || slot->srv) return slot;

    std::vector<unsigned char> bytes;
    if (!ReadResource(static_cast<int>(id), bytes)) return slot;

    Upload(g_device, bytes.data(), bytes.size(), *slot);
    return slot;
}

} // namespace

bool Init(ID3D11Device* device) {
    if (!device) return false;
    g_device = device;

    for (auto& stamp : g_stamps) stamp = std::filesystem::file_time_type{};
    for (auto& has : g_hasStamp) has = false;

    bool ok = true;
    ok = LoadEmbedded(VcLogo) != nullptr && ok;
    ok = LoadEmbedded(Redmatch2Cover) != nullptr && ok;
    ok = LoadEmbedded(OneArmedCover) != nullptr && ok;
    return ok;
}

bool LoadFile(Id id, const std::filesystem::path& path) {
    Texture* slot = Slot(id);
    if (!slot || !g_device) return false;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return false;

    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) return false;

    const int index = SlotIndex(id);
    if (g_hasStamp[index] && g_stamps[index] == stamp && slot->srv) return true;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    const std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;

    if (!Upload(g_device, bytes.data(), bytes.size(), *slot)) return false;

    g_stamps[index] = stamp;
    g_hasStamp[index] = true;
    return true;
}

void Shutdown() {
    for (auto& texture : g_textures) {
        if (texture.srv) {
            texture.srv->Release();
            texture.srv = nullptr;
        }
        texture.width = 0;
        texture.height = 0;
    }
    for (auto& has : g_hasStamp) has = false;
    g_device = nullptr;
}

ID3D11ShaderResourceView* Srv(Id id) {
    Texture* slot = Slot(id);
    return slot ? slot->srv : nullptr;
}

int Width(Id id) {
    Texture* slot = Slot(id);
    return slot ? slot->width : 0;
}

int Height(Id id) {
    Texture* slot = Slot(id);
    return slot ? slot->height : 0;
}

} // namespace images
