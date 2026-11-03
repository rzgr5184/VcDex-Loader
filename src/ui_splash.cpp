#include "ui_splash.hpp"

#include "images.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace splash {
namespace {

using ui::AlphaF;
using ui::Approach;
using ui::Clamp01;
using ui::EaseOutCubic;
using ui::Measure;
using ui::TextAt;
using ui::TextCentered;

std::string Shorten(const std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    std::string out = text.substr(0, maxBytes);
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
        out.pop_back();
    return out + "...";
}

void DrawBackdropWash(ImDrawList* draw, float width, float height, float alpha) {
    const theme::Colors& colors = theme::Current();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height),
                        AlphaF(colors.bg0, 0.78f * alpha));

    const float time = static_cast<float>(ImGui::GetTime());
    const float driftX = std::sin(time * 0.22f) * width * 0.07f;
    const float driftY = std::cos(time * 0.18f) * height * 0.06f;
    draw->AddCircleFilled(ImVec2(width * 0.68f + driftX, height * 0.18f + driftY),
                          width * 0.36f, AlphaF(colors.blob1, 0.10f * alpha), 96);
    draw->AddCircleFilled(ImVec2(width * 0.24f - driftX, height * 0.84f - driftY),
                          width * 0.42f, AlphaF(colors.blob2, 0.09f * alpha), 96);
    draw->AddCircleFilled(ImVec2(width * 0.52f, height * 0.48f), width * 0.30f,
                          AlphaF(colors.primary, 0.05f * alpha), 96);
}

} // namespace

void Draw(const AppUiState& state, float alpha, float bootElapsed) {
    if (alpha <= 0.003f) return;

    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float a = Clamp01(alpha);
    const float scale = ui::Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float time = static_cast<float>(ImGui::GetTime());
    const float t = bootElapsed;

    DrawBackdropWash(draw, width, height, a);

    const float centerY = height * 0.42f;
    const ImVec2 center(width * 0.5f, centerY);
    const float base = 58.0f * scale;
    const float pulse = 0.5f + 0.5f * std::sin(t * 2.1f);

    draw->AddCircleFilled(center, (base + 74.0f * scale),
                          AlphaF(colors.primary, (0.055f + 0.035f * pulse) * a), 96);
    draw->AddCircle(center, base + 30.0f * scale,
                    AlphaF(colors.primary, (0.16f + 0.12f * pulse) * a), 96,
                    1.4f * scale);
    draw->AddCircle(center, base + 46.0f * scale, AlphaF(colors.secondary, 0.13f * a), 96,
                    1.0f * scale);

    for (int i = 0; i < 3; ++i) {
        const float start = t * 1.35f + static_cast<float>(i) * 2.094f;
        draw->PathArcTo(center, base + 30.0f * scale, start, start + 0.95f, 48);
        draw->PathStroke(
            AlphaF(i == 0 ? colors.primary : colors.secondary, (0.55f - i * 0.13f) * a), 0,
            2.1f * scale);
    }

    ID3D11ShaderResourceView* logo = images::Srv(images::VcLogo);
    if (logo) {
        const float angle = t * 1.15f;
        const float half = base * 0.92f;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        auto corner = [&](float dx, float dy) {
            return ImVec2(center.x + dx * c - dy * s, center.y + dx * s + dy * c);
        };
        draw->AddImageQuad(static_cast<ImTextureID>(
                               reinterpret_cast<uintptr_t>(logo)),
                           corner(-half, -half), corner(half, -half),
                           corner(half, half), corner(-half, half), ImVec2(0, 0),
                           ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                           AlphaF(IM_COL32_WHITE, a));
    } else {
        TextCentered(draw, fonts.display, 54.0f * scale, width * 0.5f,
                     center.y - 30.0f * scale, AlphaF(colors.primary, a), "V");
    }

    TextCentered(draw, fonts.display, 36.0f * scale, width * 0.5f,
                 center.y + base + 52.0f * scale, AlphaF(colors.text, a), "VcDex");

    const std::string sub = "LOADER";
    float subWidth = 0.0f;
    for (char ch : sub)
        subWidth += Measure(fonts.medium, 10.0f * scale, std::string(1, ch)).x + 4.0f * scale;
    subWidth -= 4.0f * scale;
    ui::Spaced(draw, fonts.medium, 10.0f * scale,
               ImVec2(width * 0.5f - subWidth * 0.5f, center.y + base + 100.0f * scale),
               AlphaF(colors.textFaint, a), sub, 4.0f * scale);

    const std::string detail =
        Shorten(state.detail.empty() ? "Preparing VcDex..." : state.detail, 86);
    TextCentered(draw, fonts.body, 11.5f * scale, width * 0.5f,
                 center.y + base + 130.0f * scale, AlphaF(colors.textDim, a), detail);

    const float barW = 300.0f * scale;
    const float barH = 5.0f * scale;
    const ImVec2 barPos((width - barW) * 0.5f, center.y + base + 160.0f * scale);
    ui::ProgressBar(draw, barPos, ImVec2(barW, barH), state.progress, state.indeterminate,
                    AlphaF(colors.panelBorder, 0.45f * a), AlphaF(colors.primary, a), t);

    TextCentered(draw, fonts.medium, 9.0f * scale, width * 0.5f, height - 58.0f * scale,
                 AlphaF(colors.textFaint, a), "VCDEX LOADER   v" + state.localVersion);
    TextCentered(draw, fonts.body, 9.5f * scale, width * 0.5f, height - 38.0f * scale,
                 AlphaF(colors.textFaint, 0.65f * a), "made by Laynex");
}

void DrawBusy(const AppUiState& state, float overlay) {
    if (overlay <= 0.003f) return;

    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float scale = ui::Scale();
    const float a = Clamp01(overlay);
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float time = static_cast<float>(ImGui::GetTime());

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height),
                        AlphaF(colors.bg0, 0.66f * a));

    const ImVec2 size(410.0f * scale, 224.0f * scale);
    const ImVec2 pos((width - size.x) * 0.5f, (height - size.y) * 0.5f);
    const ImVec2 end(pos.x + size.x, pos.y + size.y);

    ui::GlassPanel(draw, pos, end, 22.0f * scale, 1.25f, true, a);
    ui::GlowRect(draw, pos, end, 22.0f * scale, colors.primary, 3.0f * scale);

    const ImVec2 center(pos.x + size.x * 0.5f, pos.y + 58.0f * scale);
    ui::Spinner(draw, center, 22.0f * scale, 3.0f * scale, AlphaF(colors.primary, a), time);
    draw->AddCircleFilled(center, 6.0f * scale, AlphaF(colors.secondary, 0.9f * a), 20);

    const std::string phase = state.phase.empty() ? "Working" : state.phase;
    TextCentered(draw, fonts.title, 19.0f * scale, center.x, pos.y + 96.0f * scale,
                 AlphaF(colors.text, a), phase);

    const std::string detail =
        Shorten(state.detail.empty() ? "Please wait..." : state.detail, 76);
    TextCentered(draw, fonts.body, 11.5f * scale, center.x, pos.y + 126.0f * scale,
                 AlphaF(colors.textDim, a), detail);

    const float barW = 318.0f * scale;
    const float barH = 6.0f * scale;
    const ImVec2 barPos(pos.x + (size.x - barW) * 0.5f, pos.y + 158.0f * scale);
    ui::ProgressBar(draw, barPos, ImVec2(barW, barH), state.progress, state.indeterminate,
                    AlphaF(colors.panelBorder, 0.5f * a), AlphaF(colors.primary, a), time);

    TextCentered(draw, fonts.caption, 10.0f * scale, center.x, pos.y + 184.0f * scale,
                 AlphaF(colors.textFaint, a), "Do not close VcDex while this runs");
}

void DrawToast(const AppUiState& state) {
    static float slide = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float scale = ui::Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();

    const bool visible = !state.toast.empty() && state.toastTimer > 0.0f;
    slide = Approach(slide, visible ? 1.0f : 0.0f, 16.0f, io.DeltaTime);
    if (!visible) return;

    float alpha = 1.0f;
    if (state.toastTimer < 0.6f) alpha = Clamp01(state.toastTimer / 0.6f);

    const ImU32 accent = state.toastError ? colors.error : colors.success;
    const std::string message = Shorten(state.toast, 96);
    const float textWidth = Measure(fonts.body, 12.0f * scale, message).x;

    const float w = std::min(width - 120.0f * scale, textWidth + 84.0f * scale);
    const float h = 54.0f * scale;
    const float offset = (1.0f - EaseOutCubic(slide)) * 24.0f * scale;
    const ImVec2 pos(width - w - 28.0f * scale,
                     height - 40.0f * scale - h - 16.0f * scale + offset);
    const ImVec2 end(pos.x + w, pos.y + h);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ui::GlassPanel(draw, pos, end, 14.0f * scale, 1.2f, true, alpha);
    draw->AddRectFilled(ImVec2(pos.x, pos.y + 10.0f * scale),
                        ImVec2(pos.x + 3.0f * scale, end.y - 10.0f * scale),
                        AlphaF(accent, alpha), 2.0f * scale);

    const ImVec2 icon(pos.x + 26.0f * scale, pos.y + h * 0.5f);
    draw->AddCircleFilled(icon, 10.0f * scale, AlphaF(accent, 0.18f * alpha), 24);
    if (state.toastError)
        ui::IconClose(draw, icon, 15.0f * scale, AlphaF(accent, alpha));
    else
        ui::IconCheck(draw, icon, 15.0f * scale, AlphaF(accent, alpha));

    TextAt(draw, fonts.caption, 9.0f * scale, ImVec2(pos.x + 46.0f * scale, pos.y + 10.0f * scale),
           AlphaF(accent, 0.85f * alpha), state.toastError ? "ERROR" : "NOTICE");

    ui::TextEllipsized(draw, fonts.body, 12.0f * scale,
                       ImVec2(pos.x + 46.0f * scale, pos.y + 26.0f * scale),
                       AlphaF(colors.text, alpha), message, w - 64.0f * scale);
}

} // namespace splash
