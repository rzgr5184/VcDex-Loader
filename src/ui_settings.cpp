#include "ui_pages.hpp"

#include "app.hpp"
#include "config.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace settings_page {
namespace {

using ui::Alpha;
using ui::AlphaF;
using ui::Approach;
using ui::Clamp01;
using ui::EaseOutCubic;
using ui::Measure;
using ui::Mix;
using ui::Spaced;
using ui::TextAt;
using ui::TextCentered;

bool g_dirty = false;
float g_themeHover[8] = {};
float g_lastCheckAt = -1.0f;
bool g_wasChecking = false;

float Scale() {
    return ui::Scale();
}

void MarkDirty() {
    g_dirty = true;
}

void OpenPath(const std::filesystem::path& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DrawCard(ImDrawList* draw, ImVec2 pos, ImVec2 size, float alpha = 1.0f) {
    ui::GlassPanel(draw, pos, ImVec2(pos.x + size.x, pos.y + size.y), 18.0f * Scale(), 1.1f,
                   true, alpha);
}

void SectionHeader(ImDrawList* draw, ui::Fonts& fonts, float x, float y, const char* title,
                   const char* subtitle, ImU32 iconColor, int iconKind) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();

    draw->AddCircleFilled(ImVec2(x + 13.0f * scale, y + 12.0f * scale), 13.0f * scale,
                          AlphaF(iconColor, 0.14f), 24);
    switch (iconKind) {
    case 0: ui::IconSpark(draw, ImVec2(x + 13.0f * scale, y + 12.0f * scale), 13.0f * scale, iconColor); break;
    case 1: ui::IconRefresh(draw, ImVec2(x + 13.0f * scale, y + 12.0f * scale), 13.0f * scale, iconColor); break;
    case 2: ui::IconFolder(draw, ImVec2(x + 13.0f * scale, y + 12.0f * scale), 13.0f * scale, iconColor); break;
    default: ui::IconGlobe(draw, ImVec2(x + 13.0f * scale, y + 12.0f * scale), 13.0f * scale, iconColor); break;
    }

    TextAt(draw, fonts.title, 14.5f * scale, ImVec2(x + 32.0f * scale, y + 2.0f * scale),
           colors.text, title);
    TextAt(draw, fonts.caption, 9.5f * scale, ImVec2(x + 32.0f * scale, y + 22.0f * scale),
           colors.textFaint, subtitle);
}

void DrawThemeCard(const theme::Definition& definition, ImVec2 pos, ImVec2 size,
                   bool selected, bool enabled, float dt, int index) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    const theme::Colors& preview = definition.colors;
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    char id[48];
    std::snprintf(id, sizeof(id), "##theme_%d", index);

    const bool clicked = ui::HitBox(id, pos, size, enabled);
    const bool hovered = enabled && ImGui::IsItemHovered();
    g_themeHover[index] = Approach(g_themeHover[index], hovered ? 1.0f : 0.0f, 14.0f, dt);
    const float hover = g_themeHover[index];
    const float lift = hover * 2.0f * scale;

    const ImVec2 min(pos.x, pos.y - lift);
    const ImVec2 max(pos.x + size.x, pos.y + size.y - lift);

    draw->AddRectFilledMultiColor(min, max, preview.bg0, preview.bg1, preview.bg2,
                                  Mix(preview.bg1, preview.blob1, 0.25f));
    draw->AddCircleFilled(ImVec2(max.x - 18.0f * scale, min.y + 15.0f * scale),
                          size.y * 0.55f, AlphaF(preview.blob1, 0.20f), 32);
    draw->AddCircleFilled(ImVec2(min.x + 20.0f * scale, max.y - 8.0f * scale),
                          size.y * 0.48f, AlphaF(preview.blob2, 0.16f), 32);
    draw->AddRectFilled(ImVec2(min.x + 10.0f * scale, max.y - 22.0f * scale),
                        ImVec2(max.x - 10.0f * scale, max.y - 9.0f * scale),
                        AlphaF(preview.panelTint, 0.9f), 3.0f * scale);
    draw->AddRectFilled(ImVec2(min.x + 10.0f * scale, max.y - 22.0f * scale),
                        ImVec2(min.x + 40.0f * scale, max.y - 20.0f * scale),
                        preview.primary, 1.0f * scale);

    draw->AddRectFilled(min, max, AlphaF(preview.bg0, 0.12f), 12.0f * scale);
    draw->AddRect(min, max,
                  selected ? AlphaF(colors.primary, 0.95f)
                           : AlphaF(colors.panelBorder, 0.45f + 0.4f * hover),
                  12.0f * scale, 0, selected ? 2.0f * scale : 1.0f * scale);

    TextAt(draw, fonts.medium, 10.5f * scale, ImVec2(min.x + 11.0f * scale, max.y - 38.0f * scale),
           preview.text, definition.name);
    TextAt(draw, fonts.caption, 8.0f * scale, ImVec2(min.x + 11.0f * scale, max.y - 25.0f * scale),
           AlphaF(preview.textDim, 0.95f), definition.blurb);

    if (selected) {
        const ImVec2 check(max.x - 13.0f * scale, min.y + 13.0f * scale);
        draw->AddCircleFilled(check, 8.5f * scale, colors.primary, 20);
        ui::IconCheck(draw, check, 11.0f * scale, IM_COL32(8, 26, 26, 255));
    }

    if (!clicked) return;

    settings::Get().theme = definition.id;
    theme::SetTheme(definition.id);
    MarkDirty();
}

void DrawToggleRow(ImDrawList* draw, ui::Fonts& fonts, float x, float y, float width,
                   const char* id, const char* label, const char* hint, bool* value,
                   bool enabled) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();

    TextAt(draw, fonts.medium, 11.5f * scale, ImVec2(x, y), colors.text, label);
    TextAt(draw, fonts.caption, 8.8f * scale, ImVec2(x, y + 17.0f * scale), colors.textFaint,
           hint);

    if (ui::Toggle(id, ImVec2(x + width - 44.0f * scale, y + 1.0f * scale), *value, enabled)) {
        *value = !*value;
        MarkDirty();
    }
}

std::string CheckAge() {
    if (g_lastCheckAt < 0.0f) return "Not checked yet";
    const float age = static_cast<float>(ImGui::GetTime()) - g_lastCheckAt;
    if (age < 12.0f) return "Just now";
    if (age < 90.0f) return std::to_string(static_cast<int>(age / 60.0f)) + "m ago";
    return std::to_string(static_cast<int>(age / 60.0f)) + " minutes ago";
}

void DrawAppearance(const AppUiState& state, float x, float y, float width, float height,
                    bool enabled, float dt) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    DrawCard(draw, ImVec2(x, y), ImVec2(width, height));
    const float pad = 18.0f * scale;
    const float innerX = x + pad;
    const float innerW = width - pad * 2.0f;

    SectionHeader(draw, fonts, innerX, y + pad,
                  "Appearance", "Themes and motion for the whole interface",
                  colors.primary, 0);

    const float themesTop = y + pad + 54.0f * scale;
    const float gap = 9.0f * scale;
    const int columns = 3;
    const float cardW = (innerW - gap * static_cast<float>(columns - 1)) /
                        static_cast<float>(columns);
    const float cardH = 64.0f * scale;

    for (int i = 0; i < theme::Count(); ++i) {
        const int column = i % columns;
        const int row = i / columns;
        const theme::Definition& definition = theme::At(i);
        DrawThemeCard(definition,
                      ImVec2(innerX + static_cast<float>(column) * (cardW + gap),
                             themesTop + static_cast<float>(row) * (cardH + gap)),
                      ImVec2(cardW, cardH), definition.id == theme::CurrentId(), enabled, dt,
                      i);
    }

    float cursor = themesTop + 2.0f * cardH + gap + 24.0f * scale;

    TextAt(draw, fonts.medium, 9.0f * scale, ImVec2(innerX, cursor), colors.textFaint,
           "EFFECTS");
    cursor += 20.0f * scale;

    DrawToggleRow(draw, fonts, innerX, cursor, innerW, "##toggle_anim", "Animations",
                  "Liquid backdrop, fades and page transitions",
                  &settings::Get().animations, enabled);
    cursor += 44.0f * scale;
    DrawToggleRow(draw, fonts, innerX, cursor, innerW, "##toggle_particles", "Particles",
                  "Floating light motes over the background",
                  &settings::Get().particles, enabled);
    cursor += 44.0f * scale;
    DrawToggleRow(draw, fonts, innerX, cursor, innerW, "##toggle_parallax", "Parallax",
                  "Cover art follows the mouse pointer", &settings::Get().parallax, enabled);
    cursor += 48.0f * scale;

    TextAt(draw, fonts.medium, 10.5f * scale, ImVec2(innerX, cursor), colors.textDim,
           "Background intensity");
    const std::string percent = std::to_string(settings::Get().intensity) + "%";
    const ImVec2 percentSize = Measure(fonts.medium, 10.5f * scale, percent);
    TextAt(draw, fonts.medium, 10.5f * scale,
           ImVec2(innerX + innerW - percentSize.x, cursor), colors.primary, percent);
    cursor += 22.0f * scale;

    if (ui::SliderInt("##intensity", ImVec2(innerX, cursor), ImVec2(innerW, 20.0f * scale),
                      &settings::Get().intensity, 0, 100, enabled)) {
        MarkDirty();
    }
    cursor += 34.0f * scale;

    TextAt(draw, fonts.medium, 10.5f * scale, ImVec2(innerX, cursor), colors.textDim,
           "Glass blur");
    cursor += 22.0f * scale;

    ui::ButtonOptions soft;
    soft.kind = ui::ButtonKind::Soft;
    soft.font = fonts.medium;
    soft.fontSize = 9.0f * scale;
    soft.rounding = 8.0f * scale;

    ui::ButtonOptions ghost = soft;
    ghost.kind = ui::ButtonKind::Ghost;

    const float segW = 84.0f * scale;
    if (ui::Button("##blur_low", ImVec2(innerX, cursor), ImVec2(segW, 28.0f * scale), "LOW",
                   enabled, settings::Get().blurHigh ? ghost : soft)) {
        if (settings::Get().blurHigh) {
            settings::Get().blurHigh = false;
            MarkDirty();
        }
    }
    if (ui::Button("##blur_high", ImVec2(innerX + segW + 8.0f * scale, cursor),
                   ImVec2(segW, 28.0f * scale), "HIGH", enabled,
                   settings::Get().blurHigh ? soft : ghost)) {
        if (!settings::Get().blurHigh) {
            settings::Get().blurHigh = true;
            MarkDirty();
        }
    }
    cursor += 42.0f * scale;

    DrawToggleRow(draw, fonts, innerX, cursor, innerW, "##toggle_reduce", "Reduce motion",
                  "Keeps the interface calm and static", &settings::Get().reduceMotion,
                  enabled);

    (void)state;
}

void DrawUpdates(const AppUiState& state, float x, float y, float width, float height,
                 bool enabled) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    DrawCard(draw, ImVec2(x, y), ImVec2(width, height));
    const float pad = 18.0f * scale;
    const float innerX = x + pad;
    const float innerW = width - pad * 2.0f;

    SectionHeader(draw, fonts, innerX, y + pad, "Updates",
                  "Files, hashes and self repair", colors.secondary, 1);

    float cursor = y + pad + 58.0f * scale;

    TextAt(draw, fonts.caption, 9.0f * scale, ImVec2(innerX, cursor), colors.textFaint,
           "INSTALLED");
    TextAt(draw, fonts.caption, 9.0f * scale,
           ImVec2(innerX + innerW * 0.5f, cursor), colors.textFaint, "LATEST");
    cursor += 15.0f * scale;
    TextAt(draw, fonts.title, 16.0f * scale, ImVec2(innerX, cursor), colors.text,
           "v" + (state.localVersion.empty() ? std::string(config::kVersion)
                                             : state.localVersion));
    TextAt(draw, fonts.title, 16.0f * scale, ImVec2(innerX + innerW * 0.5f, cursor),
           state.remoteVersion.empty() ? colors.textFaint : colors.primary,
           state.remoteVersion.empty() ? "unknown" : "v" + state.remoteVersion);

    cursor += 30.0f * scale;
    TextAt(draw, fonts.caption, 9.0f * scale, ImVec2(innerX, cursor), colors.textFaint,
           "Last check: " + CheckAge());

    cursor += 24.0f * scale;
    const float buttonH = 30.0f * scale;
    const float third = (innerW - 16.0f * scale) / 3.0f;

    ui::ButtonOptions ghost;
    ghost.kind = ui::ButtonKind::Ghost;
    ghost.font = fonts.medium;
    ghost.fontSize = 8.5f * scale;
    ghost.rounding = 8.0f * scale;

    if (ui::Button("##check_now", ImVec2(innerX, cursor), ImVec2(third, buttonH),
                   "CHECK NOW", enabled, ghost)) {
        app::CheckFiles();
    }
    if (ui::Button("##repair_files", ImVec2(innerX + third + 8.0f * scale, cursor),
                   ImVec2(third, buttonH), "REPAIR FILES", enabled, ghost)) {
        app::CheckFiles();
    }
    if (ui::Button("##open_log", ImVec2(innerX + (third + 8.0f * scale) * 2.0f, cursor),
                   ImVec2(third, buttonH), "OPEN LOG", true, ghost)) {
        OpenPath(app::DataDir() / "logs" / "update.log");
    }
}

void DrawPaths(float x, float y, float width, float height) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    DrawCard(draw, ImVec2(x, y), ImVec2(width, height));
    const float pad = 18.0f * scale;
    const float innerX = x + pad;
    const float innerW = width - pad * 2.0f;

    SectionHeader(draw, fonts, innerX, y + pad, "Paths", "Where VcDex keeps its files",
                  colors.textDim, 2);

    float cursor = y + pad + 58.0f * scale;

    TextAt(draw, fonts.caption, 8.5f * scale, ImVec2(innerX, cursor), colors.textFaint,
           "GAME DATA");
    cursor += 14.0f * scale;
    ui::TextEllipsized(draw, fonts.body, 10.5f * scale, ImVec2(innerX, cursor), colors.textDim,
                       app::DataDir().string(), innerW);
    cursor += 24.0f * scale;

    TextAt(draw, fonts.caption, 8.5f * scale, ImVec2(innerX, cursor), colors.textFaint,
           "LOADER");
    cursor += 14.0f * scale;
    ui::TextEllipsized(draw, fonts.body, 10.5f * scale, ImVec2(innerX, cursor), colors.textDim,
                       app::BaseDir().string(), innerW);
    cursor += 26.0f * scale;

    ui::ButtonOptions ghost;
    ghost.kind = ui::ButtonKind::Ghost;
    ghost.font = fonts.medium;
    ghost.fontSize = 8.5f * scale;
    ghost.rounding = 8.0f * scale;

    if (ui::Button("##open_data", ImVec2(innerX, cursor), ImVec2(120.0f * scale, 28.0f * scale),
                   "OPEN DATA FOLDER", true, ghost)) {
        OpenPath(app::DataDir());
    }
    if (ui::Button("##copy_data", ImVec2(innerX + 128.0f * scale, cursor),
                   ImVec2(96.0f * scale, 28.0f * scale), "COPY PATH", true, ghost)) {
        ImGui::SetClipboardText(app::DataDir().string().c_str());
    }
}

void DrawAbout(float x, float y, float width, float height) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    DrawCard(draw, ImVec2(x, y), ImVec2(width, height));
    const float pad = 18.0f * scale;
    const float innerX = x + pad;
    const float innerW = width - pad * 2.0f;

    TextAt(draw, fonts.title, 13.5f * scale, ImVec2(innerX, y + pad + 2.0f * scale),
           colors.text, "VcDex Loader");
    TextAt(draw, fonts.caption, 9.0f * scale, ImVec2(innerX, y + pad + 22.0f * scale),
           colors.textFaint, "Liquid interface for your game library");

    TextAt(draw, fonts.body, 10.0f * scale, ImVec2(innerX, y + pad + 46.0f * scale),
           colors.textDim, "Version " + std::string(config::kVersion) + "  ·  by " +
                               std::string(config::kAuthor));

    ui::ButtonOptions ghost;
    ghost.kind = ui::ButtonKind::Ghost;
    ghost.font = fonts.medium;
    ghost.fontSize = 8.5f * scale;
    ghost.rounding = 8.0f * scale;

    if (ui::Button("##about_github", ImVec2(innerX + innerW - 92.0f * scale,
                                            y + pad + 42.0f * scale),
                   ImVec2(92.0f * scale, 26.0f * scale), "GITHUB", true, ghost)) {
        ShellExecuteW(nullptr, L"open", L"https://github.com/rzgr5184/VcDex-Loader", nullptr,
                      nullptr, SW_SHOWNORMAL);
    }

    TextAt(draw, fonts.caption, 8.0f * scale, ImVec2(innerX, y + height - 24.0f * scale),
           colors.textFaint, "Dear ImGui · D3D11 · single executable");
}

} // namespace

void Draw(const AppUiState& state, float x, float y, float width, float height) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool enabled = !state.busy && !state.checking;

    if (g_wasChecking && !state.checking) g_lastCheckAt = static_cast<float>(ImGui::GetTime());
    g_wasChecking = state.checking;

    TextAt(draw, fonts.title, 27.0f * scale, ImVec2(x, y), colors.text, "Settings");
    TextAt(draw, fonts.body, 11.5f * scale, ImVec2(x + 1.0f * scale, y + 36.0f * scale),
           colors.textDim, "Make the loader feel like yours. Changes save automatically.");

    ui::ButtonOptions back;
    back.kind = ui::ButtonKind::Ghost;
    back.font = fonts.medium;
    back.fontSize = 9.5f * scale;
    back.rounding = 9.0f * scale;

    if (ui::Button("##back_library", ImVec2(x + width - 142.0f * scale, y + 6.0f * scale),
                   ImVec2(142.0f * scale, 34.0f * scale), "BACK TO LIBRARY", enabled, back)) {
        pages::ShowLibrary();
    }

    const float top = y + 78.0f * scale;
    const float bottom = y + height - 50.0f * scale;
    const float gap = 18.0f * scale;
    const bool twoColumns = width >= 760.0f * scale;

    if (twoColumns) {
        const float columnW = (width - gap) * 0.5f;
        const float leftH = bottom - top;
        DrawAppearance(state, x, top, columnW, leftH, enabled, ImGui::GetIO().DeltaTime);

        float cursor = top;
        const float updatesH = 196.0f * scale;
        DrawUpdates(state, x + columnW + gap, cursor, columnW, updatesH, enabled);
        cursor += updatesH + 14.0f * scale;

        const float pathsH = 172.0f * scale;
        DrawPaths(x + columnW + gap, cursor, columnW, pathsH);
        cursor += pathsH + 14.0f * scale;

        const float aboutH = std::max(104.0f * scale, bottom - cursor);
        DrawAbout(x + columnW + gap, cursor, columnW, aboutH);
    } else {
        const float leftH = 512.0f * scale;
        DrawAppearance(state, x, top, width, leftH, enabled, ImGui::GetIO().DeltaTime);

        float cursor = top + leftH + gap;
        DrawUpdates(state, x, cursor, width, 196.0f * scale, enabled);
        cursor += 196.0f * scale + gap;
        DrawPaths(x, cursor, width, 172.0f * scale);
        cursor += 172.0f * scale + gap;
        DrawAbout(x, cursor, width, 110.0f * scale);
    }

    if (g_dirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        settings::Save();
        g_dirty = false;
    }
}

} // namespace settings_page
