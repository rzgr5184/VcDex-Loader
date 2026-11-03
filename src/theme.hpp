#pragma once

#include "imgui.h"

#include <string>

// Central colour system for the VcDex Loader interface.
//
// Every UI colour is read from theme::Current(). Switching a theme kicks off a
// short colour crossfade; the renderer and the liquid backdrop both read the
// interpolated values, so the whole window changes atmosphere at once.
namespace theme {

struct Colors {
    ImU32 bg0 = 0;
    ImU32 bg1 = 0;
    ImU32 bg2 = 0;

    ImU32 primary = 0;
    ImU32 primarySoft = 0;
    ImU32 secondary = 0;
    ImU32 accent = 0;
    ImU32 glow = 0;

    ImU32 blob0 = 0;
    ImU32 blob1 = 0;
    ImU32 blob2 = 0;

    ImU32 text = 0;
    ImU32 textDim = 0;
    ImU32 textFaint = 0;

    ImU32 panelTint = 0;
    ImU32 panelBorder = 0;

    ImU32 success = 0;
    ImU32 warning = 0;
    ImU32 error = 0;
};

enum class Id {
    Lagoon = 0,
    Aurora,
    Ember,
    Matcha,
    Ice,
    Count
};

struct Definition {
    Id id;
    const char* key;
    const char* name;
    const char* blurb;
    Colors colors;
};

int Count();
const Definition& At(int index);
int IndexOf(Id id);
Id IdFromKey(const std::string& key);
const char* Key(Id id);
const char* Name(Id id);

// Starts a colour crossfade (or applies instantly when instant is true).
void SetTheme(Id id, bool instant = false);
Id CurrentId();

// Advances the crossfade. Call once per frame before drawing.
void Update(float dt);

// Interpolated palette for the current frame.
const Colors& Current();

bool Transitioning();
float TransitionAmount(); // 0..1, 1 = fully settled on the target theme

} // namespace theme
