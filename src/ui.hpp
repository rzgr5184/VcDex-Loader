#pragma once

#include "imgui.h"
#include "theme.hpp"

#include <string>

// Small drawing toolkit shared by every screen. Everything is immediate-mode
// ImDrawList work so the visuals match the D3D11 liquid background exactly.
namespace ui {

struct Fonts {
    ImFont* caption = nullptr;
    ImFont* body = nullptr;
    ImFont* medium = nullptr;
    ImFont* title = nullptr;
    ImFont* display = nullptr;
    ImFont* icon = nullptr;
};

void SetFonts(const Fonts& fonts);
Fonts& GetFonts();

void SetScale(float scale);
float Scale();

float Clamp01(float value);
ImU32 Alpha(ImU32 color, int alpha);
ImU32 AlphaF(ImU32 color, float factor);
ImU32 Mix(ImU32 a, ImU32 b, float t);
float Approach(float current, float target, float rate, float dt);
float EaseOutCubic(float value);
float EaseOutBack(float value);

ImVec2 Measure(ImFont* font, float size, const std::string& text);
void TextAt(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
            const std::string& text);
void TextCentered(ImDrawList* draw, ImFont* font, float size, float centerX, float y,
                  ImU32 color, const std::string& text);
float Spaced(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
             const std::string& text, float spacing);
void TextEllipsized(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
                    const std::string& text, float maxWidth);

// Frosted glass panel. Samples the backdrop blur target when available and
// falls back to a tinted gradient otherwise.
void GlassPanel(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding,
                float tint = 1.0f, bool border = true, float borderAlpha = 1.0f);
void GlowRect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 color,
              float strength);

enum class ButtonKind { Primary, Ghost, Soft, Danger };

struct ButtonOptions {
    ButtonKind kind = ButtonKind::Primary;
    ImFont* font = nullptr;
    float fontSize = 12.0f;
    float rounding = -1.0f;
    bool glow = true;
};

bool Button(const char* id, ImVec2 pos, ImVec2 size, const std::string& label,
            bool enabled = true, const ButtonOptions& options = {});

// Bare clickable region used for cards / custom rows.
bool HitBox(const char* id, ImVec2 pos, ImVec2 size, bool enabled = true);

bool Toggle(const char* id, ImVec2 pos, bool value, bool enabled = true);
bool SliderInt(const char* id, ImVec2 pos, ImVec2 size, int* value, int minimum,
               int maximum, bool enabled = true);

void Spinner(ImDrawList* draw, ImVec2 center, float radius, float thickness, ImU32 color,
             float time);
void ProgressBar(ImDrawList* draw, ImVec2 pos, ImVec2 size, float fill,
                 bool indeterminate, ImU32 track, ImU32 color, float time);

// Vector icons, all drawn around `center` with a nominal `size` box.
void IconGrid(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconSliders(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconSearch(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconCheck(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconRefresh(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconFolder(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconPlay(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconDownload(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconChevronRight(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconMinimize(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconClose(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconGlobe(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconSpark(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
void IconExternal(ImDrawList* draw, ImVec2 center, float size, ImU32 color);

} // namespace ui
