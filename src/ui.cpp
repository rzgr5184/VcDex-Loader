#include "ui.hpp"

#include "backdrop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace ui {
namespace {

Fonts g_fonts;
float g_scale = 1.0f;

std::unordered_map<std::string, float> g_toggleAnim;
std::unordered_map<std::string, float> g_rippleStart;

ImU32 White() {
    return IM_COL32(255, 255, 255, 255);
}

ImU32 Black() {
    return IM_COL32(0, 0, 0, 255);
}

ImU32 DarkInk() {
    return IM_COL32(6, 24, 27, 255);
}

ImTextureID Texture(const ID3D11ShaderResourceView* srv) {
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(srv));
}

void DrawRipple(ImDrawList* draw, const std::string& id, ImVec2 min, ImVec2 max,
                ImU32 color) {
    const auto it = g_rippleStart.find(id);
    if (it == g_rippleStart.end()) return;

    const float elapsed = static_cast<float>(ImGui::GetTime()) - it->second;
    if (elapsed < 0.0f || elapsed > 0.7f) return;

    const float t = elapsed / 0.7f;
    const float radius = 12.0f * g_scale + t * std::min(max.x - min.x, max.y - min.y) * 0.55f;
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

    draw->PushClipRect(min, max, true);
    draw->AddCircle(center, radius, AlphaF(color, (1.0f - t) * (1.0f - t) * 0.55f),
                    48, 2.0f * g_scale);
    draw->PopClipRect();
}

} // namespace

void SetFonts(const Fonts& fonts) {
    g_fonts = fonts;
}

Fonts& GetFonts() {
    return g_fonts;
}

void SetScale(float scale) {
    g_scale = scale > 0.0f ? scale : 1.0f;
}

float Scale() {
    return g_scale;
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

ImU32 Alpha(ImU32 color, int alpha) {
    return (color & 0x00FFFFFFu) |
           (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT);
}

ImU32 AlphaF(ImU32 color, float factor) {
    const int base = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFFu);
    return Alpha(color, static_cast<int>(static_cast<float>(base) * factor));
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = Clamp01(t);
    auto channel = [a, b, t](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFFu);
        const float cb = static_cast<float>((b >> shift) & 0xFFu);
        return static_cast<ImU32>(ca + (cb - ca) * t + 0.5f);
    };
    return IM_COL32(channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT),
                    channel(IM_COL32_B_SHIFT), channel(IM_COL32_A_SHIFT));
}

float Approach(float current, float target, float rate, float dt) {
    return current + (target - current) * (1.0f - std::exp(-rate * std::max(0.0f, dt)));
}

float EaseOutCubic(float value) {
    value = Clamp01(value);
    const float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
}

float EaseOutBack(float value) {
    value = Clamp01(value);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    const float p = value - 1.0f;
    return 1.0f + c3 * p * p * p + c1 * p * p;
}

ImVec2 Measure(ImFont* font, float size, const std::string& text) {
    if (!font) return ImVec2(0, 0);
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
}

void TextAt(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
            const std::string& text) {
    if (!draw || !font) return;
    draw->AddText(font, size, pos, color, text.c_str());
}

void TextCentered(ImDrawList* draw, ImFont* font, float size, float centerX, float y,
                  ImU32 color, const std::string& text) {
    const ImVec2 measure = Measure(font, size, text);
    TextAt(draw, font, size, ImVec2(centerX - measure.x * 0.5f, y), color, text);
}

float Spaced(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
             const std::string& text, float spacing) {
    float x = pos.x;
    for (char c : text) {
        const std::string glyph(1, c);
        TextAt(draw, font, size, ImVec2(x, pos.y), color, glyph);
        x += Measure(font, size, glyph).x + spacing;
    }
    return x - pos.x - spacing;
}

void TextEllipsized(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
                    const std::string& text, float maxWidth) {
    if (!font) return;
    if (Measure(font, size, text).x <= maxWidth) {
        TextAt(draw, font, size, pos, color, text);
        return;
    }

    const std::string ellipsis = "...";
    std::string cut = text;
    while (!cut.empty() && Measure(font, size, cut + ellipsis).x > maxWidth) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
            cut.pop_back();
    }
    TextAt(draw, font, size, pos, color, cut + ellipsis);
}

void GlassPanel(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float tint,
                bool border, float borderAlpha) {
    const theme::Colors& colors = theme::Current();
    ID3D11ShaderResourceView* blur = backdrop::BlurSrv();
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    if (blur && display.x > 0.0f && display.y > 0.0f) {
        const ImVec2 uv0(min.x / display.x, min.y / display.y);
        const ImVec2 uv1(max.x / display.x, max.y / display.y);
        draw->AddImageRounded(Texture(blur), min, max, uv0, uv1, IM_COL32_WHITE, rounding);
        draw->AddRectFilled(min, max, AlphaF(colors.bg0, 0.30f * tint), rounding);
        draw->AddRectFilled(min, max, AlphaF(colors.panelTint, tint), rounding);
    } else {
        draw->AddRectFilled(min, max, AlphaF(colors.bg1, 0.88f * tint), rounding);
    }

    if (border) {
        draw->AddRect(min, max, AlphaF(colors.panelBorder, borderAlpha), rounding, 0, 1.0f);
    }

    const float inset = std::max(0.0f, rounding * 0.75f);
    if (max.x - inset > min.x + inset) {
        draw->AddLine(ImVec2(min.x + inset, min.y + 0.5f),
                      ImVec2(max.x - inset, min.y + 0.5f),
                      AlphaF(White(), 0.10f), 1.0f);
    }
}

void GlowRect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 color,
              float strength) {
    for (int i = 0; i < 3; ++i) {
        const float expand = strength * static_cast<float>(i + 1);
        draw->AddRect(ImVec2(min.x - expand, min.y - expand),
                      ImVec2(max.x + expand, max.y + expand),
                      AlphaF(color, 0.35f / static_cast<float>(i + 1)), rounding + expand,
                      0, 1.0f);
    }
}

bool Button(const char* id, ImVec2 pos, ImVec2 size, const std::string& label,
            bool enabled, const ButtonOptions& options) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);

    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);

    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) g_rippleStart[id] = static_cast<float>(ImGui::GetTime());

    const theme::Colors& colors = theme::Current();
    const float rounding = options.rounding > 0.0f ? options.rounding : 12.0f * g_scale;

    ImVec2 drawMin = pos;
    ImVec2 drawMax(pos.x + size.x, pos.y + size.y);
    if (held) {
        drawMin.x += 1.0f;
        drawMin.y += 1.5f;
        drawMax.x -= 1.0f;
        drawMax.y -= 0.5f;
    }

    ImU32 fill = 0;
    ImU32 fillHover = 0;
    ImU32 textColor = colors.text;
    ImU32 borderColor = 0;

    switch (options.kind) {
    case ButtonKind::Primary:
        fill = colors.primary;
        fillHover = Mix(colors.primary, White(), 0.20f);
        textColor = DarkInk();
        borderColor = AlphaF(White(), 0.28f);
        break;
    case ButtonKind::Soft:
        fill = Alpha(colors.primary, 28);
        fillHover = Alpha(colors.primary, 46);
        textColor = colors.primarySoft;
        borderColor = Alpha(colors.primary, 78);
        break;
    case ButtonKind::Danger:
        fill = Alpha(colors.error, 30);
        fillHover = Alpha(colors.error, 52);
        textColor = colors.error;
        borderColor = Alpha(colors.error, 92);
        break;
    case ButtonKind::Ghost:
    default:
        fill = AlphaF(White(), 0.06f);
        fillHover = AlphaF(White(), 0.13f);
        textColor = colors.textDim;
        borderColor = AlphaF(White(), 0.14f);
        break;
    }

    if (!enabled) {
        fill = AlphaF(White(), 0.04f);
        fillHover = fill;
        textColor = colors.textFaint;
        borderColor = AlphaF(White(), 0.08f);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (options.kind == ButtonKind::Primary && options.glow && enabled &&
        (hovered || ImGui::IsItemActive())) {
        GlowRect(draw, drawMin, drawMax, rounding, colors.primary, 4.0f * g_scale);
    }

    draw->AddRectFilled(drawMin, drawMax, hovered && enabled ? fillHover : fill, rounding);

    // A soft top shine keeps buttons from looking flat.
    if (options.kind == ButtonKind::Primary) {
        draw->PushClipRect(drawMin, drawMax, true);
        draw->AddRectFilledMultiColor(
            ImVec2(drawMin.x, drawMin.y), ImVec2(drawMax.x, drawMin.y + size.y * 0.55f),
            AlphaF(White(), 0.26f), AlphaF(White(), 0.26f), AlphaF(White(), 0.0f),
            AlphaF(White(), 0.0f));
        draw->PopClipRect();
    }

    if (borderColor) {
        draw->AddRect(drawMin, drawMax, borderColor, rounding, 0, 1.0f);
    }

    if (options.kind == ButtonKind::Primary && hovered && enabled) {
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.72f, 1.25f);
        const float x = drawMin.x + (size.x + 70.0f * g_scale) * phase - 35.0f * g_scale;
        draw->PushClipRect(drawMin, drawMax, true);
        draw->AddLine(ImVec2(x, drawMax.y), ImVec2(x + 34.0f * g_scale, drawMin.y),
                      AlphaF(White(), 0.30f), 14.0f * g_scale);
        draw->PopClipRect();
    }

    DrawRipple(draw, id, drawMin, drawMax, textColor);

    ImFont* font = options.font ? options.font : g_fonts.medium;
    const float fontSize = options.fontSize > 0.0f ? options.fontSize : 12.0f * g_scale;
    const ImVec2 measure = Measure(font, fontSize, label);
    TextAt(draw, font, fontSize,
           ImVec2((drawMin.x + drawMax.x - measure.x) * 0.5f,
                  (drawMin.y + drawMax.y - measure.y) * 0.5f - fontSize * 0.06f),
           textColor, label);

    return clicked;
}

bool HitBox(const char* id, ImVec2 pos, ImVec2 size, bool enabled) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool hovered = enabled && ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

bool Toggle(const char* id, ImVec2 pos, bool value, bool enabled) {
    const float width = 44.0f * g_scale;
    const float height = 24.0f * g_scale;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    float& anim = g_toggleAnim[id];
    anim = Approach(anim, value ? 1.0f : 0.0f, 18.0f, ImGui::GetIO().DeltaTime);

    const theme::Colors& colors = theme::Current();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = height * 0.5f;

    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                        Mix(AlphaF(White(), 0.10f), Alpha(colors.primary, 210), anim),
                        rounding);
    draw->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                  hovered ? AlphaF(White(), 0.34f) : AlphaF(White(), 0.18f), rounding, 0,
                  1.0f);

    const float knobRadius = height * 0.5f - 3.0f * g_scale;
    const float knobX = pos.x + knobRadius + 3.0f * g_scale +
                        anim * (width - (knobRadius + 3.0f * g_scale) * 2.0f);
    const ImVec2 knob(knobX, pos.y + height * 0.5f);
    draw->AddCircleFilled(knob, knobRadius + 2.0f * g_scale, AlphaF(White(), 0.16f), 20);
    draw->AddCircleFilled(knob, knobRadius,
                          anim > 0.5f ? White() : AlphaF(White(), 0.86f), 20);

    return clicked;
}

bool SliderInt(const char* id, ImVec2 pos, ImVec2 size, int* value, int minimum,
               int maximum, bool enabled) {
    if (!value || maximum <= minimum) return false;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool active = enabled && ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    bool changed = false;
    if (active) {
        const float t = Clamp01((ImGui::GetMousePos().x - pos.x) / std::max(1.0f, size.x));
        const int next = minimum + static_cast<int>(t * static_cast<float>(maximum - minimum) + 0.5f);
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }

    const theme::Colors& colors = theme::Current();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float fraction = static_cast<float>(*value - minimum) /
                           static_cast<float>(maximum - minimum);

    const float trackY = pos.y + size.y * 0.5f;
    const float trackH = 6.0f * g_scale;
    const ImVec2 trackMin(pos.x, trackY - trackH * 0.5f);
    const ImVec2 trackMax(pos.x + size.x, trackY + trackH * 0.5f);

    draw->AddRectFilled(trackMin, trackMax, AlphaF(White(), 0.10f), trackH * 0.5f);
    if (fraction > 0.0f) {
        draw->AddRectFilled(trackMin, ImVec2(pos.x + size.x * fraction, trackMax.y),
                            Alpha(colors.primary, 220), trackH * 0.5f);
    }

    const ImVec2 knob(pos.x + size.x * fraction, trackY);
    draw->AddCircleFilled(knob, 10.0f * g_scale, AlphaF(colors.primary, 0.20f), 24);
    draw->AddCircleFilled(knob, 6.5f * g_scale,
                          (hovered || active) ? White() : colors.primarySoft, 24);

    return changed;
}

void Spinner(ImDrawList* draw, ImVec2 center, float radius, float thickness, ImU32 color,
             float time) {
    const float start = time * 3.4f;
    for (int i = 0; i < 3; ++i) {
        const float offset = static_cast<float>(i) * 2.094f;
        draw->PathArcTo(center, radius, start + offset, start + offset + 1.45f, 32);
        draw->PathStroke(Alpha(color, 190 - i * 45), 0, thickness);
    }
    draw->AddCircle(center, radius, Alpha(color, 28), 48, thickness);
}

void ProgressBar(ImDrawList* draw, ImVec2 pos, ImVec2 size, float fill,
                 bool indeterminate, ImU32 track, ImU32 color, float time) {
    const float rounding = size.y * 0.5f;
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), track, rounding);

    const float clamped = Clamp01(fill);
    float width = size.x * (indeterminate ? 0.32f : clamped);
    float x = pos.x;
    if (indeterminate) {
        const float cycle = std::fmod(time * 0.55f, 1.0f);
        x = pos.x + (size.x - width) * cycle;
    }

    if (width <= 0.5f) return;
    const ImVec2 fillMin(x, pos.y);
    const ImVec2 fillMax(x + width, pos.y + size.y);

    draw->AddRectFilled(fillMin, fillMax, color, rounding);

    const float shimmer = std::fmod(time * 0.9f, 1.0f);
    const float shimmerX = fillMin.x + width * shimmer;
    draw->PushClipRect(fillMin, fillMax, true);
    draw->AddLine(ImVec2(shimmerX - 10.0f * g_scale, fillMax.y),
                  ImVec2(shimmerX + 10.0f * g_scale, fillMin.y),
                  AlphaF(White(), 0.30f), size.y);
    draw->PopClipRect();
}

// ---------------------------------------------------------------- icons

void IconGrid(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float box = size * 0.34f;
    const float gap = size * 0.12f;
    const float thickness = std::max(1.0f, size * 0.09f);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const ImVec2 min(center.x - box - gap * 0.5f + x * (box + gap),
                             center.y - box - gap * 0.5f + y * (box + gap));
            draw->AddRectFilled(min, ImVec2(min.x + box, min.y + box), color,
                                size * 0.10f);
        }
    }
    (void)thickness;
}

void IconSliders(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float half = size * 0.42f;
    const float thickness = std::max(1.0f, size * 0.09f);
    const float offsets[3] = {-size * 0.28f, 0.0f, size * 0.28f};
    const float knobs[3] = {0.25f, -0.30f, 0.42f};

    for (int i = 0; i < 3; ++i) {
        const float y = center.y + offsets[i];
        draw->AddLine(ImVec2(center.x - half, y), ImVec2(center.x + half, y), color,
                      thickness);
        const ImVec2 knob(center.x + half * knobs[i], y);
        draw->AddCircleFilled(knob, size * 0.10f, color, 16);
    }
}

void IconSearch(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float radius = size * 0.30f;
    const ImVec2 c(center.x - size * 0.06f, center.y - size * 0.06f);
    draw->AddCircle(c, radius, color, 24, std::max(1.0f, size * 0.09f));
    draw->AddLine(ImVec2(c.x + radius * 0.75f, c.y + radius * 0.75f),
                  ImVec2(center.x + size * 0.36f, center.y + size * 0.36f), color,
                  std::max(1.0f, size * 0.11f));
}

void IconCheck(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float thickness = std::max(1.5f, size * 0.11f);
    draw->AddLine(ImVec2(center.x - size * 0.30f, center.y),
                  ImVec2(center.x - size * 0.06f, center.y + size * 0.24f), color,
                  thickness);
    draw->AddLine(ImVec2(center.x - size * 0.06f, center.y + size * 0.24f),
                  ImVec2(center.x + size * 0.34f, center.y - size * 0.24f), color,
                  thickness);
}

void IconRefresh(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float radius = size * 0.33f;
    const float thickness = std::max(1.2f, size * 0.09f);
    draw->PathArcTo(center, radius, 0.55f, 5.25f, 32);
    draw->PathStroke(color, 0, thickness);
    const ImVec2 tip(center.x + radius * std::cos(5.45f), center.y + radius * std::sin(5.45f));
    draw->AddTriangleFilled(ImVec2(tip.x - size * 0.16f, tip.y - size * 0.02f),
                            ImVec2(tip.x + size * 0.12f, tip.y - size * 0.12f),
                            ImVec2(tip.x + size * 0.02f, tip.y + size * 0.16f), color);
}

void IconFolder(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float w = size * 0.74f;
    const float h = size * 0.56f;
    const ImVec2 min(center.x - w * 0.5f, center.y - h * 0.5f + size * 0.04f);
    const ImVec2 max(min.x + w, min.y + h);
    draw->AddRectFilled(ImVec2(min.x, min.y - size * 0.12f),
                        ImVec2(min.x + w * 0.45f, min.y + size * 0.02f), color,
                        size * 0.06f);
    draw->AddRect(min, max, color, size * 0.08f, 0, std::max(1.2f, size * 0.08f));
}

void IconPlay(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddTriangleFilled(ImVec2(center.x - size * 0.22f, center.y - size * 0.30f),
                            ImVec2(center.x + size * 0.30f, center.y),
                            ImVec2(center.x - size * 0.22f, center.y + size * 0.30f), color);
}

void IconDownload(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float thickness = std::max(1.2f, size * 0.09f);
    draw->AddLine(ImVec2(center.x, center.y - size * 0.32f),
                  ImVec2(center.x, center.y + size * 0.18f), color, thickness);
    draw->AddLine(ImVec2(center.x - size * 0.18f, center.y + size * 0.02f),
                  ImVec2(center.x, center.y + size * 0.20f), color, thickness);
    draw->AddLine(ImVec2(center.x + size * 0.18f, center.y + size * 0.02f),
                  ImVec2(center.x, center.y + size * 0.20f), color, thickness);
    draw->AddLine(ImVec2(center.x - size * 0.28f, center.y + size * 0.34f),
                  ImVec2(center.x + size * 0.28f, center.y + size * 0.34f), color,
                  thickness);
}

void IconChevronRight(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float thickness = std::max(1.2f, size * 0.10f);
    draw->AddLine(ImVec2(center.x - size * 0.12f, center.y - size * 0.24f),
                  ImVec2(center.x + size * 0.14f, center.y), color, thickness);
    draw->AddLine(ImVec2(center.x + size * 0.14f, center.y),
                  ImVec2(center.x - size * 0.12f, center.y + size * 0.24f), color,
                  thickness);
}

void IconMinimize(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddLine(ImVec2(center.x - size * 0.32f, center.y),
                  ImVec2(center.x + size * 0.32f, center.y), color,
                  std::max(1.2f, size * 0.09f));
}

void IconClose(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float arm = size * 0.30f;
    const float thickness = std::max(1.2f, size * 0.09f);
    draw->AddLine(ImVec2(center.x - arm, center.y - arm),
                  ImVec2(center.x + arm, center.y + arm), color, thickness);
    draw->AddLine(ImVec2(center.x + arm, center.y - arm),
                  ImVec2(center.x - arm, center.y + arm), color, thickness);
}

void IconGlobe(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float radius = size * 0.34f;
    const float thickness = std::max(1.0f, size * 0.08f);
    draw->AddCircle(center, radius, color, 28, thickness);
    draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y),
                  color, thickness);
    draw->PathArcTo(center, radius, -1.5708f, 1.5708f, 16);
    draw->PathStroke(color, 0, thickness);
    draw->PathArcTo(center, radius, 1.5708f, 4.7124f, 16);
    draw->PathStroke(color, 0, thickness);
}

void IconSpark(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float thickness = std::max(1.0f, size * 0.08f);
    draw->AddLine(ImVec2(center.x, center.y - size * 0.38f),
                  ImVec2(center.x, center.y + size * 0.38f), color, thickness);
    draw->AddLine(ImVec2(center.x - size * 0.38f, center.y),
                  ImVec2(center.x + size * 0.38f, center.y), color, thickness);
    draw->AddLine(ImVec2(center.x - size * 0.22f, center.y - size * 0.22f),
                  ImVec2(center.x + size * 0.22f, center.y + size * 0.22f), color,
                  thickness * 0.6f);
    draw->AddLine(ImVec2(center.x + size * 0.22f, center.y - size * 0.22f),
                  ImVec2(center.x - size * 0.22f, center.y + size * 0.22f), color,
                  thickness * 0.6f);
}

void IconExternal(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float thickness = std::max(1.0f, size * 0.08f);
    const ImVec2 min(center.x - size * 0.34f, center.y - size * 0.16f);
    const ImVec2 max(center.x + size * 0.12f, center.y + size * 0.34f);
    draw->AddRect(min, max, color, size * 0.08f, 0, thickness);
    draw->AddLine(ImVec2(center.x - size * 0.02f, center.y + size * 0.06f),
                  ImVec2(center.x + size * 0.34f, center.y - size * 0.30f), color,
                  thickness);
    draw->AddLine(ImVec2(center.x + size * 0.10f, center.y - size * 0.30f),
                  ImVec2(center.x + size * 0.34f, center.y - size * 0.30f), color,
                  thickness);
    draw->AddLine(ImVec2(center.x + size * 0.34f, center.y - size * 0.30f),
                  ImVec2(center.x + size * 0.34f, center.y - size * 0.06f), color,
                  thickness);
}

} // namespace ui
