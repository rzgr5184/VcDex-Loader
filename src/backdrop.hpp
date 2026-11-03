#pragma once

#include "theme.hpp"

#include <windows.h>
#include <d3d11.h>

#include "imgui.h"

// GPU liquid backdrop + separable blur used by the glass panels.
//
// The backdrop is rendered into a half resolution target with a fullscreen
// pixel shader, then blurred (two ping-pong passes) into a quarter resolution
// target that UI panels sample through ImDrawList::AddImageRounded.
//
// Everything degrades gracefully: if shaders or targets cannot be created,
// Available() returns false and the UI falls back to gradient fills.
namespace backdrop {

bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
void Shutdown();

// Recreates the offscreen targets. Cheap when the size is unchanged.
void Resize(int width, int height);

bool Available();

struct Params {
    float time = 0.0f;
    ImVec2 mouse{0.5f, 0.5f};
    float intensity = 0.7f;   // 0..1
    float caustic = 0.7f;     // 0..1
    float grain = 0.03f;      // 0..1 (already scaled)
    bool highQuality = true;
    bool reduceMotion = false;
    ImVec2 ripple{0.5f, 0.5f};
    float rippleAge = -1.0f;  // negative = inactive
};

void Render(const Params& params, const theme::Colors& colors);

ID3D11ShaderResourceView* BackgroundSrv();
ID3D11ShaderResourceView* BlurSrv();

} // namespace backdrop
