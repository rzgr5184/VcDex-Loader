#include "ui_pages.hpp"

#include "backdrop.hpp"
#include "config.hpp"
#include "images.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "ui_splash.hpp"

#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace pages {
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

enum class Page { Library = 0, Settings = 1 };

Page g_page = Page::Library;
float g_pageAnim = 1.0f;
float g_navPill = -1.0f;
float g_navHover[2] = {};
float g_boot = 0.0f;
float g_splash = 1.0f;
float g_open = 0.0f;
float g_detailAnim = 1.0f;
float g_heroHover = 0.0f;
float g_cardHover[32] = {};
float g_cardAnim[32] = {};
int g_lastSelected = -1;
bool g_autoUpdate = false;
bool g_restoredGame = false;
bool g_searchFocus = false;
char g_search[96] = {};

float Scale() {
    return ui::Scale();
}

void OpenUrl(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

ImTextureID Tex(ID3D11ShaderResourceView* srv) {
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(srv));
}

images::Id CoverIdFor(const GameEntry& game) {
    if (game.id == "redmatch2") return images::Redmatch2Cover;
    if (game.id == "onearmedrobber") return images::OneArmedCover;
    return images::VcLogo;
}

ID3D11ShaderResourceView* CoverFor(const GameEntry& game) {
    if (game.id == "redmatch2" || game.id == "onearmedrobber") {
        const images::Id id = CoverIdFor(game);
        return images::Srv(id);
    }
    return nullptr;
}

int CoverW(const GameEntry& game) {
    if (game.id == "redmatch2" || game.id == "onearmedrobber")
        return images::Width(CoverIdFor(game));
    return 0;
}

int CoverH(const GameEntry& game) {
    if (game.id == "redmatch2" || game.id == "onearmedrobber")
        return images::Height(CoverIdFor(game));
    return 0;
}

std::string Initials(const GameEntry& game) {
    std::string out;
    bool newWord = true;
    for (char c : game.name) {
        if (c == ' ') {
            newWord = true;
            continue;
        }
        if (newWord && std::isalpha(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            newWord = false;
        }
    }
    if (out.empty()) out = "VC";
    return out;
}

ImU32 AccentFor(const GameEntry& game) {
    const theme::Colors& colors = theme::Current();
    return game.accent == 1 ? colors.secondary : colors.primary;
}

ImU32 StatusColor(const std::string& status) {
    const theme::Colors& colors = theme::Current();
    if (status == "Undetected" || status == "Safe" || status == "Ready")
        return colors.success;
    if (status == "Updating" || status == "Maintenance") return colors.warning;
    return colors.error;
}

void DrawCover(ImDrawList* draw, ID3D11ShaderResourceView* srv, int srcW, int srcH,
               ImVec2 pos, ImVec2 size, float rounding, ImU32 tint, float parallaxX = 0.0f,
               float parallaxY = 0.0f) {
    if (!srv || srcW <= 0 || srcH <= 0 || size.x <= 0.0f || size.y <= 0.0f) {
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            AlphaF(tint, 0.12f), rounding);
        return;
    }

    const float dstAspect = size.x / size.y;
    const float srcAspect = static_cast<float>(srcW) / static_cast<float>(srcH);

    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);

    if (srcAspect > dstAspect) {
        const float fraction = dstAspect / srcAspect;
        uv0.x = (1.0f - fraction) * 0.5f;
        uv1.x = 1.0f - uv0.x;
    } else {
        const float fraction = srcAspect / dstAspect;
        uv0.y = (1.0f - fraction) * 0.5f;
        uv1.y = 1.0f - uv0.y;
    }

    const float spanX = uv1.x - uv0.x;
    const float spanY = uv1.y - uv0.y;
    uv0.x = std::clamp(uv0.x + parallaxX * spanX, 0.0f, 1.0f - spanX);
    uv0.y = std::clamp(uv0.y + parallaxY * spanY, 0.0f, 1.0f - spanY);
    uv1 = ImVec2(uv0.x + spanX, uv0.y + spanY);

    draw->AddImageRounded(Tex(srv), pos, ImVec2(pos.x + size.x, pos.y + size.y), uv0, uv1,
                          tint, rounding);
}

void DrawPill(ImDrawList* draw, ImVec2 pos, const std::string& text, ImU32 accent,
              bool compact = false) {
    ui::Fonts& fonts = ui::GetFonts();
    const float scale = Scale();
    const float fontSize = (compact ? 9.5f : 11.0f) * scale;
    const ImVec2 measured = Measure(fonts.medium, fontSize, text);
    const ImVec2 size(measured.x + (compact ? 22.0f : 28.0f) * scale,
                      (compact ? 18.0f : 23.0f) * scale);

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), AlphaF(accent, 0.14f),
                        size.y * 0.5f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), AlphaF(accent, 0.34f),
                  size.y * 0.5f, 0, 1.0f);
    draw->AddCircleFilled(
        ImVec2(pos.x + (compact ? 9.0f : 11.0f) * scale, pos.y + size.y * 0.5f),
        (compact ? 2.4f : 3.0f) * scale, accent, 12);
    TextAt(draw, fonts.medium, fontSize,
           ImVec2(pos.x + (compact ? 15.0f : 19.0f) * scale,
                  pos.y + (size.y - measured.y) * 0.5f - 1.0f * scale),
           accent, text);
}

void DrawBackground(ImDrawList* draw, float width, float height, float dt) {
    const theme::Colors& colors = theme::Current();
    const float time = static_cast<float>(ImGui::GetTime());

    if (backdrop::Available() && backdrop::BackgroundSrv()) {
        draw->AddImage(Tex(backdrop::BackgroundSrv()), ImVec2(0, 0), ImVec2(width, height),
                       ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    } else {
        draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, height), colors.bg0,
                                      Mix(colors.bg1, colors.bg2, 0.4f), colors.bg2,
                                      colors.bg1);
        const float driftX = std::sin(time * 0.20f) * width * 0.06f;
        const float driftY = std::cos(time * 0.16f) * height * 0.05f;
        draw->AddCircleFilled(ImVec2(width * 0.78f + driftX, height * 0.12f + driftY),
                              width * 0.36f, AlphaF(colors.blob1, 0.10f), 96);
        draw->AddCircleFilled(ImVec2(width * 0.18f - driftX, height * 0.92f - driftY),
                              width * 0.32f, AlphaF(colors.blob2, 0.09f), 96);
    }

    // Soft light from the top-left corner.
    draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, height * 0.55f),
                                  AlphaF(colors.primary, 0.06f), AlphaF(colors.primary, 0.01f),
                                  AlphaF(colors.primary, 0.0f), AlphaF(colors.primary, 0.0f));

    if (settings::Get().particles && settings::Get().animations &&
        !settings::Get().reduceMotion) {
        for (int i = 0; i < 34; ++i) {
            const float seed = static_cast<float>(i) * 0.6180339f;
            const float x = std::fmod(seed * width * 1.37f +
                                          std::sin(time * 0.09f + i * 1.7f) * 42.0f * Scale(),
                                      width);
            const float y = std::fmod(seed * height * 0.93f +
                                          std::cos(time * 0.07f + i * 2.1f) * 30.0f * Scale(),
                                      height);
            const float radius = (1.0f + static_cast<float>(i % 3) * 0.8f) * Scale();
            const ImU32 color = (i % 4 == 0) ? colors.secondary : colors.primary;
            const float alpha = 0.16f + 0.18f * (0.5f + 0.5f * std::sin(time * 0.6f + i));
            draw->AddCircleFilled(ImVec2(x, y), radius, AlphaF(color, alpha), 8);
        }
    }

    // Vignette edges.
    draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, 90.0f * Scale()),
                                  AlphaF(colors.bg0, 0.55f), AlphaF(colors.bg0, 0.55f),
                                  IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS);
    draw->AddRectFilledMultiColor(ImVec2(0, height - 90.0f * Scale()), ImVec2(width, height),
                                  IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS,
                                  AlphaF(colors.bg0, 0.55f), AlphaF(colors.bg0, 0.55f));
}

// ---------------------------------------------------------------- title bar

void DrawWindowButton(ImDrawList* draw, const char* id, ImVec2 pos, bool close, HWND hwnd) {
    const float scale = Scale();
    const float size = 36.0f * scale;
    const bool clicked = ui::HitBox(id, pos, ImVec2(size, size), true);
    const bool hovered = ImGui::IsItemHovered();
    const theme::Colors& colors = theme::Current();

    if (hovered) {
        const ImU32 fill = close ? AlphaF(colors.error, 0.22f) : AlphaF(IM_COL32_WHITE, 0.09f);
        draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), fill, 10.0f * scale);
    }

    const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
    const ImU32 color = hovered && close ? colors.error : colors.textDim;
    if (close)
        ui::IconClose(draw, center, 16.0f * scale, color);
    else
        ui::IconMinimize(draw, center, 16.0f * scale, color);

    if (!clicked) return;
    if (close)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    else
        ShowWindow(hwnd, SW_MINIMIZE);
}

void StartDrag(HWND hwnd) {
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void DrawTitleBar(const AppUiState& state, HWND hwnd, float width, float railW) {
    const float scale = Scale();
    const float height = 64.0f * scale;
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddLine(ImVec2(railW, height - 1.0f), ImVec2(width, height - 1.0f),
                  AlphaF(colors.panelBorder, 0.45f), 1.0f);

    TextAt(draw, fonts.title, 16.0f * scale, ImVec2(railW + 22.0f * scale, 12.0f * scale),
           colors.text, "VcDex Loader");
    TextAt(draw, fonts.caption, 9.5f * scale, ImVec2(railW + 23.0f * scale, 37.0f * scale),
           colors.textFaint, "by " + std::string(config::kAuthor));

    // Search field.
    const float searchW = 280.0f * scale;
    const float searchH = 36.0f * scale;
    const float searchX = railW + 290.0f * scale;
    const float searchY = (height - searchH) * 0.5f;
    const bool hasRoom = searchX + searchW + 130.0f * scale < width;

    if (hasRoom) {
        // Glass is drawn before the input so the text stays on top of it.
        ui::GlassPanel(draw, ImVec2(searchX, searchY),
                       ImVec2(searchX + searchW, searchY + searchH), 11.0f * scale, 0.75f,
                       false);

        ImGui::SetCursorScreenPos(ImVec2(searchX + 30.0f * scale, searchY + 8.0f * scale));
        ImGui::PushItemWidth(searchW - 84.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 8.0f * scale));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, colors.text);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, AlphaF(colors.textFaint, 0.9f));

        if (g_searchFocus) {
            ImGui::SetKeyboardFocusHere();
            g_searchFocus = false;
        }

        ImGui::InputTextWithHint("##search", "Search games", g_search, sizeof(g_search));
        const bool searchActive = ImGui::IsItemActive();
        const bool searchHovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);

        const bool fieldHovered = searchHovered || searchActive;
        draw->AddRect(ImVec2(searchX, searchY), ImVec2(searchX + searchW, searchY + searchH),
                      searchActive ? AlphaF(colors.primary, 0.75f)
                                   : (fieldHovered ? AlphaF(colors.primary, 0.40f)
                                                   : AlphaF(colors.panelBorder, 0.65f)),
                      11.0f * scale, 0, searchActive ? 1.6f * scale : 1.0f * scale);

        ui::IconSearch(draw, ImVec2(searchX + 16.0f * scale, searchY + searchH * 0.5f),
                       15.0f * scale, AlphaF(colors.textDim, 0.85f));

        if (g_search[0] == '\0') {
            const std::string chip = "CTRL K";
            const ImVec2 measured = Measure(fonts.medium, 8.0f * scale, chip);
            const ImVec2 chipPos(searchX + searchW - measured.x - 20.0f * scale,
                                 searchY + (searchH - 17.0f * scale) * 0.5f);
            draw->AddRectFilled(chipPos,
                                ImVec2(chipPos.x + measured.x + 12.0f * scale,
                                       chipPos.y + 17.0f * scale),
                                AlphaF(colors.panelBorder, 0.25f), 5.0f * scale);
            TextAt(draw, fonts.medium, 8.0f * scale,
                   ImVec2(chipPos.x + 6.0f * scale, chipPos.y + 4.5f * scale),
                   colors.textFaint, chip);
        }
    }

    // Drag regions avoid the search box and the window buttons.
    const float controlsX = width - 14.0f * scale - 76.0f * scale;
    const float dragRight = hasRoom ? searchX - 10.0f * scale
                                    : width - 14.0f * scale - 76.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(railW + 10.0f * scale, 0));
    ImGui::InvisibleButton("##title_drag_left",
                           ImVec2(std::max(1.0f, dragRight - railW - 10.0f * scale), height));
    if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) StartDrag(hwnd);

    if (hasRoom) {
        ImGui::SetCursorScreenPos(ImVec2(searchX + searchW + 10.0f * scale, 0));
        ImGui::InvisibleButton("##title_drag_right",
                               ImVec2(std::max(1.0f, controlsX - searchX - searchW -
                                                           10.0f * scale),
                                      height));
        if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            StartDrag(hwnd);
    }

    const float closeX = width - 14.0f * scale - 36.0f * scale;
    const float minX = closeX - 4.0f * scale - 36.0f * scale;
    DrawWindowButton(draw, "##win_min", ImVec2(minX, 14.0f * scale), false, hwnd);
    DrawWindowButton(draw, "##win_close", ImVec2(closeX, 14.0f * scale), true, hwnd);

    // Small connection indicator next to the window controls.
    const ImU32 dot = state.online ? colors.success : colors.warning;
    draw->AddCircleFilled(
        ImVec2(minX - 20.0f * scale, height * 0.5f), 3.4f * scale,
        AlphaF(dot, 0.9f), 12);
}

// ---------------------------------------------------------------- nav rail

void DrawRail(const AppUiState& state, float height, float railW, float dt, bool interactive) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(railW, height), AlphaF(colors.bg0, 0.52f));
    draw->AddLine(ImVec2(railW - 1.0f, 0), ImVec2(railW - 1.0f, height),
                  AlphaF(colors.panelBorder, 0.55f), 1.0f);

    // Logo mark.
    const float logoSize = 36.0f * scale;
    const ImVec2 logoPos((railW - logoSize) * 0.5f, 18.0f * scale);
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 1.8f);

    draw->AddCircleFilled(ImVec2(logoPos.x + logoSize * 0.5f, logoPos.y + logoSize * 0.5f),
                          logoSize * (0.88f + 0.08f * pulse), AlphaF(colors.primary, 0.10f),
                          40);
    draw->AddRectFilled(ImVec2(logoPos.x - 2.0f * scale, logoPos.y - 2.0f * scale),
                        ImVec2(logoPos.x + logoSize + 2.0f * scale,
                               logoPos.y + logoSize + 2.0f * scale),
                        AlphaF(colors.primary, 0.16f), 12.0f * scale);

    ID3D11ShaderResourceView* logo = images::Srv(images::VcLogo);
    if (logo)
        DrawCover(draw, logo, images::Width(images::VcLogo), images::Height(images::VcLogo),
                  logoPos, ImVec2(logoSize, logoSize), 10.0f * scale, IM_COL32_WHITE);
    else
        TextCentered(draw, fonts.icon, 20.0f * scale, logoPos.x + logoSize * 0.5f,
                     logoPos.y + 7.0f * scale, colors.primary, "V");

    draw->AddRect(ImVec2(logoPos.x - 2.0f * scale, logoPos.y - 2.0f * scale),
                  ImVec2(logoPos.x + logoSize + 2.0f * scale,
                         logoPos.y + logoSize + 2.0f * scale),
                  AlphaF(colors.primary, 0.45f + 0.25f * pulse), 12.0f * scale, 0,
                  1.0f * scale);

    // Navigation items.
    const float itemW = railW - 20.0f * scale;
    const float itemH = 58.0f * scale;
    const float itemX = 10.0f * scale;
    const float firstY = 106.0f * scale;
    const int activeIndex = g_page == Page::Settings ? 1 : 0;

    const float targetPill = firstY + static_cast<float>(activeIndex) * (itemH + 8.0f * scale);
    if (g_navPill < 0.0f) g_navPill = targetPill;
    g_navPill = Approach(g_navPill, targetPill, 16.0f, dt);

    // Animated active background pill.
    draw->AddRectFilled(ImVec2(itemX, g_navPill),
                        ImVec2(itemX + itemW, g_navPill + itemH),
                        AlphaF(colors.primary, 0.10f), 15.0f * scale);
    draw->AddRectFilled(ImVec2(itemX + 1.0f * scale, g_navPill + itemH * 0.24f),
                        ImVec2(itemX + 4.0f * scale, g_navPill + itemH * 0.76f),
                        colors.primary, 1.5f * scale);

    const char* ids[2] = {"##nav_library", "##nav_settings"};
    const char* labels[2] = {"Library", "Settings"};

    for (int i = 0; i < 2; ++i) {
        const float y = firstY + static_cast<float>(i) * (itemH + 8.0f * scale);
        const bool active = i == activeIndex;
        const bool clicked = ui::HitBox(ids[i], ImVec2(itemX, y), ImVec2(itemW, itemH),
                                        interactive);
        const bool hovered = interactive && ImGui::IsItemHovered();

        g_navHover[i] = Approach(g_navHover[i], hovered ? 1.0f : 0.0f, 16.0f, dt);
        const float lift = g_navHover[i] * 1.5f * scale;
        const ImU32 tint = active ? colors.primary
                                  : Mix(colors.textDim, colors.text, g_navHover[i] * 0.7f);

        draw->AddRectFilled(ImVec2(itemX, y), ImVec2(itemX + itemW, y + itemH),
                            AlphaF(IM_COL32_WHITE, 0.05f * g_navHover[i]), 15.0f * scale);

        const ImVec2 iconCenter(itemX + itemW * 0.5f, y + 21.0f * scale - lift);
        if (i == 0)
            ui::IconGrid(draw, iconCenter, 19.0f * scale, tint);
        else
            ui::IconSliders(draw, iconCenter, 19.0f * scale, tint);

        if (active) {
            draw->AddCircleFilled(iconCenter, 16.0f * scale, AlphaF(colors.primary, 0.10f),
                                  24);
        }

        TextCentered(draw, fonts.medium, 8.5f * scale, itemX + itemW * 0.5f,
                     y + 38.0f * scale - lift, active ? colors.text : colors.textDim, labels[i]);

        if (clicked) {
            if (i == 0)
                ShowLibrary();
            else
                ShowSettings();
        }
    }

    // External link (GitHub).
    const float linkSize = 34.0f * scale;
    const ImVec2 linkPos((railW - linkSize) * 0.5f, height - 188.0f * scale);
    const bool linkClicked =
        ui::HitBox("##rail_github", linkPos, ImVec2(linkSize, linkSize), interactive);
    const bool linkHovered = interactive && ImGui::IsItemHovered();
    draw->AddRectFilled(linkPos, ImVec2(linkPos.x + linkSize, linkPos.y + linkSize),
                        AlphaF(IM_COL32_WHITE, linkHovered ? 0.10f : 0.045f),
                        10.0f * scale);
    ui::IconGlobe(draw,
                  ImVec2(linkPos.x + linkSize * 0.5f, linkPos.y + linkSize * 0.5f),
                  16.0f * scale,
                  linkHovered ? colors.primary : colors.textDim);
    TextCentered(draw, fonts.caption, 7.5f * scale, railW * 0.5f,
                 linkPos.y + linkSize + 5.0f * scale, colors.textFaint, "GITHUB");
    if (linkClicked) {
        OpenUrl(L"https://github.com/rzgr5184/VcDex-Loader");
    }

    // Version + credit.
    TextCentered(draw, fonts.caption, 9.5f * scale, railW * 0.5f, height - 62.0f * scale,
                 colors.textFaint, "made by Laynex");
    TextCentered(draw, fonts.medium, 8.5f * scale, railW * 0.5f, height - 44.0f * scale,
                 colors.textDim, "v" + state.localVersion);

    if (state.updateAvailable) {
        const float ping = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
        draw->AddCircleFilled(ImVec2(railW * 0.5f + 28.0f * scale, height - 52.0f * scale),
                              3.0f * scale, AlphaF(colors.primary, 0.5f + 0.5f * ping), 12);
    }
}

// ---------------------------------------------------------------- update banner

void DrawUpdateBanner(const AppUiState& state, float x, float y, float width, bool enabled,
                      float anim) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float height = 56.0f * scale;
    const float slide = (1.0f - EaseOutCubic(anim)) * -12.0f * scale;
    const ImVec2 pos(x, y + slide);
    const ImVec2 end(x + width, y + height + slide);
    const float alpha = Clamp01(anim);

    ui::GlassPanel(draw, pos, end, 14.0f * scale, 1.1f, true, alpha);
    draw->AddRectFilled(ImVec2(pos.x, pos.y + 10.0f * scale),
                        ImVec2(pos.x + 3.0f * scale, end.y - 10.0f * scale),
                        AlphaF(colors.primary, 0.9f * alpha), 2.0f * scale);

    const float ping = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.1f);
    draw->AddCircleFilled(ImVec2(pos.x + 24.0f * scale, pos.y + height * 0.5f),
                          4.0f * scale + 3.0f * scale * ping,
                          AlphaF(colors.primary, 0.18f * alpha), 20);
    draw->AddCircleFilled(ImVec2(pos.x + 24.0f * scale, pos.y + height * 0.5f),
                          4.0f * scale, AlphaF(colors.primary, alpha), 16);

    const bool repair = state.localVersion == state.remoteVersion;
    TextAt(draw, fonts.medium, 12.0f * scale, ImVec2(pos.x + 38.0f * scale, pos.y + 11.0f * scale),
           AlphaF(colors.text, alpha), repair ? "Repair available" : "Update available");
    TextAt(draw, fonts.caption, 9.5f * scale,
           ImVec2(pos.x + 38.0f * scale, pos.y + 31.0f * scale),
           AlphaF(colors.textDim, alpha),
           repair ? "Local executable differs from the official build"
                  : "v" + state.localVersion + "  ->  v" + state.remoteVersion);

    const float buttonW = 158.0f * scale;
    const float buttonH = 34.0f * scale;
    ui::ButtonOptions options;
    options.kind = ui::ButtonKind::Primary;
    options.font = fonts.medium;
    options.fontSize = 9.5f * scale;
    options.rounding = 9.0f * scale;

    const bool clicked = ui::Button("##update_now",
                                    ImVec2(end.x - buttonW - 12.0f * scale,
                                           pos.y + (height - buttonH) * 0.5f),
                                    ImVec2(buttonW, buttonH),
                                    enabled ? "RESTART & UPDATE" : "UPDATING...", enabled,
                                    options);
    if (clicked) app::StartSelfUpdate();

    if (!enabled && state.busy) {
        const ImVec2 barPos(end.x - buttonW - 12.0f * scale, end.y - 3.0f * scale);
        draw->AddRectFilled(barPos, ImVec2(barPos.x + buttonW, barPos.y + 2.0f * scale),
                            AlphaF(colors.primary, 0.30f), 1.0f);
    }
}

// ---------------------------------------------------------------- hero

void DrawHero(const AppUiState& state, const GameEntry* game, float x, float y, float width,
              float height, bool enabled, float dt) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos(x, y);
    const ImVec2 end(x + width, y + height);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = enabled && mouse.x >= pos.x && mouse.x <= end.x && mouse.y >= pos.y &&
                         mouse.y <= end.y;
    g_heroHover = Approach(g_heroHover, hovered ? 1.0f : 0.0f, 10.0f, dt);

    const float grow = 2.5f * scale * g_heroHover;
    const ImVec2 artMin(pos.x - grow, pos.y - grow);
    const ImVec2 artMax(end.x + grow, end.y + grow);

    if (game && CoverFor(*game)) {
        const float depth = settings::Get().parallax && !settings::Get().reduceMotion
                                ? 0.03f
                                : 0.0f;
        const float parallaxX =
            ((mouse.x - pos.x) / std::max(1.0f, width) - 0.5f) * depth;
        const float parallaxY =
            ((mouse.y - pos.y) / std::max(1.0f, height) - 0.5f) * depth;
        DrawCover(draw, CoverFor(*game), CoverW(*game), CoverH(*game), artMin,
                  ImVec2(artMax.x - artMin.x, artMax.y - artMin.y), 22.0f * scale,
                  IM_COL32_WHITE, parallaxX, parallaxY);
    } else {
        const ImU32 accent = game ? AccentFor(*game) : colors.primary;
        draw->AddRectFilledMultiColor(artMin, artMax, AlphaF(colors.bg2, 0.96f),
                                      AlphaF(colors.bg1, 0.96f),
                                      Mix(colors.bg1, accent, 0.18f),
                                      Mix(colors.bg0, accent, 0.12f));
        draw->AddCircleFilled(ImVec2(pos.x + width * 0.72f, pos.y + height * 0.28f),
                              height * 0.55f, AlphaF(accent, 0.10f), 64);
        TextAt(draw, fonts.display, 64.0f * scale,
               ImVec2(pos.x + 28.0f * scale, pos.y + height * 0.30f),
               AlphaF(accent, 0.22f), game ? Initials(*game) : "VC");
    }

    // Readability scrim: solid tint plus a stronger left-to-right gradient.
    draw->AddRectFilled(artMin, artMax, AlphaF(colors.bg0, 0.52f), 22.0f * scale);
    draw->AddRectFilledMultiColor(ImVec2(pos.x + 2.0f * scale, pos.y + 2.0f * scale),
                                  ImVec2(pos.x + width * 0.78f, end.y - 2.0f * scale),
                                  AlphaF(colors.bg0, 0.72f), AlphaF(colors.bg0, 0.0f),
                                  AlphaF(colors.bg0, 0.0f), AlphaF(colors.bg0, 0.72f));
    draw->AddRectFilledMultiColor(ImVec2(pos.x + 2.0f * scale, pos.y + height * 0.45f),
                                  ImVec2(end.x - 2.0f * scale, end.y - 2.0f * scale),
                                  IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS,
                                  AlphaF(colors.bg0, 0.70f), AlphaF(colors.bg0, 0.70f));

    draw->AddRect(artMin, artMax,
                  AlphaF(colors.primary, 0.18f + 0.40f * g_heroHover), 22.0f * scale, 0,
                  (1.0f + g_heroHover) * scale);
    if (g_heroHover > 0.01f)
        ui::GlowRect(draw, pos, end, 22.0f * scale, colors.primary, 3.0f * scale);

    if (!game) {
        TextAt(draw, fonts.title, 22.0f * scale, ImVec2(pos.x + 24.0f * scale,
                                                       pos.y + 24.0f * scale),
               colors.text, "No game selected");
        TextAt(draw, fonts.body, 11.5f * scale,
               ImVec2(pos.x + 24.0f * scale, pos.y + 54.0f * scale), colors.textDim,
               "Pick a title from the list below to get started.");
        return;
    }

    const std::string featured = "FEATURED";
    const ImVec2 featuredSize = Measure(fonts.medium, 8.5f * scale, featured);
    draw->AddRectFilled(ImVec2(pos.x + 22.0f * scale, pos.y + 20.0f * scale),
                        ImVec2(pos.x + 22.0f * scale + featuredSize.x + 18.0f * scale,
                               pos.y + 37.0f * scale),
                        AlphaF(colors.primary, 0.14f), 8.0f * scale);
    Spaced(draw, fonts.medium, 8.5f * scale, ImVec2(pos.x + 31.0f * scale, pos.y + 24.0f * scale),
           colors.primary, featured, 1.4f * scale);

    const float textWidth = std::max(120.0f, width - 330.0f * scale);
    ui::TextEllipsized(draw, fonts.display, 31.0f * scale,
                       ImVec2(pos.x + 24.0f * scale, pos.y + height - 112.0f * scale),
                       colors.text, game->name, textWidth);
    ui::TextEllipsized(draw, fonts.body, 12.0f * scale,
                       ImVec2(pos.x + 25.0f * scale, pos.y + height - 70.0f * scale),
                       colors.textDim, game->subtitle, textWidth);

    DrawPill(draw, ImVec2(pos.x + 24.0f * scale, pos.y + height - 44.0f * scale),
             game->status, StatusColor(game->status));

    const float loadW = 150.0f * scale;
    const float loadH = 42.0f * scale;
    const float openW = 126.0f * scale;
    const float buttonsY = pos.y + height - 24.0f * scale - loadH;

    ui::ButtonOptions loadOptions;
    loadOptions.kind = ui::ButtonKind::Primary;
    loadOptions.font = fonts.medium;
    loadOptions.fontSize = 11.0f * scale;
    loadOptions.rounding = 11.0f * scale;

    ui::ButtonOptions ghostOptions;
    ghostOptions.kind = ui::ButtonKind::Ghost;
    ghostOptions.font = fonts.medium;
    ghostOptions.fontSize = 9.5f * scale;
    ghostOptions.rounding = 10.0f * scale;

    if (ui::Button("##hero_open", ImVec2(end.x - 24.0f * scale - loadW - 10.0f * scale - openW,
                                         buttonsY + 4.0f * scale),
                   ImVec2(openW, 34.0f * scale), "OPEN FOLDER", enabled, ghostOptions)) {
        app::OpenSelectedFolder();
    }

    if (ui::Button("##hero_load", ImVec2(end.x - 24.0f * scale - loadW, buttonsY),
                   ImVec2(loadW, loadH), "LOAD GAME", enabled, loadOptions)) {
        app::LoadSelectedGame();
    }

    draw->AddCircleFilled(ImVec2(pos.x + width - 32.0f * scale, pos.y + 30.0f * scale),
                          3.2f * scale,
                          game->ready ? colors.success : colors.warning, 12);
    TextAt(draw, fonts.caption, 8.5f * scale,
           ImVec2(pos.x + width - 25.0f * scale, pos.y + 26.0f * scale),
           AlphaF(colors.textDim, 0.9f), game->ready ? "READY" : "SETUP");
}

// ---------------------------------------------------------------- cards

void DrawGameCard(const AppUiState& state, const GameEntry& game, int index, ImVec2 pos,
                  ImVec2 size, bool selected, bool enabled, float anim) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float eased = EaseOutCubic(Clamp01(anim));
    const float offset = (1.0f - eased) * 16.0f * scale;
    const ImVec2 drawPos(pos.x, pos.y + offset);
    const ImVec2 end(drawPos.x + size.x, drawPos.y + size.y);

    char id[64];
    std::snprintf(id, sizeof(id), "##game_%d", index);

    const bool clicked = ui::HitBox(id, pos, size, enabled);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    const int slot = std::clamp(index, 0, 31);
    g_cardHover[slot] = Approach(g_cardHover[slot], hovered ? 1.0f : 0.0f, 14.0f, ImGui::GetIO().DeltaTime);
    const float hover = g_cardHover[slot];
    const float lift = hover * 2.0f * scale;

    const ImVec2 cardMin(drawPos.x, drawPos.y - lift);
    const ImVec2 cardMax(end.x, end.y - lift);
    const ImU32 accent = AccentFor(game);

    if (selected) ui::GlowRect(draw, cardMin, cardMax, 16.0f * scale, accent, 3.0f * scale);

    ui::GlassPanel(draw, cardMin, cardMax, 16.0f * scale,
                   selected ? 1.25f : (0.85f + 0.25f * hover), true,
                   0.6f + 0.4f * hover);
    draw->AddRect(cardMin, cardMax,
                  selected ? AlphaF(accent, 0.85f)
                           : AlphaF(colors.panelBorder, 0.55f + 0.35f * hover),
                  16.0f * scale, 0, selected ? 1.8f * scale : 1.0f * scale);

    const float thumb = std::min(size.y - 28.0f * scale, 86.0f * scale);
    const ImVec2 thumbPos(cardMin.x + 14.0f * scale, cardMin.y + (size.y - thumb) * 0.5f);
    const ImVec2 thumbEnd(thumbPos.x + thumb, thumbPos.y + thumb);

    draw->AddRectFilled(ImVec2(thumbPos.x - 2.0f * scale, thumbPos.y - 2.0f * scale),
                        ImVec2(thumbEnd.x + 2.0f * scale, thumbEnd.y + 2.0f * scale),
                        AlphaF(accent, 0.16f + 0.16f * hover), 15.0f * scale);
    if (CoverFor(game)) {
        DrawCover(draw, CoverFor(game), CoverW(game), CoverH(game), thumbPos,
                  ImVec2(thumb, thumb), 13.0f * scale, IM_COL32_WHITE);
    } else {
        draw->AddRectFilled(thumbPos, thumbEnd, AlphaF(accent, 0.16f), 13.0f * scale);
        TextCentered(draw, fonts.icon, 24.0f * scale, thumbPos.x + thumb * 0.5f,
                     thumbPos.y + thumb * 0.5f - 12.0f * scale, accent, Initials(game));
    }
    draw->AddRect(thumbPos, thumbEnd, AlphaF(accent, 0.35f + 0.25f * hover), 13.0f * scale,
                  0, 1.0f * scale);

    const float textX = thumbEnd.x + 14.0f * scale;
    const float textW = cardMax.x - textX - 20.0f * scale;

    ui::TextEllipsized(draw, fonts.title, 14.5f * scale,
                       ImVec2(textX, cardMin.y + 17.0f * scale), colors.text, game.name,
                       textW);
    ui::TextEllipsized(draw, fonts.caption, 10.5f * scale,
                       ImVec2(textX, cardMin.y + 40.0f * scale), colors.textDim,
                       game.subtitle, textW);

    DrawPill(draw, ImVec2(textX, cardMin.y + 62.0f * scale), game.status,
             StatusColor(game.status), true);

    const ImU32 ready = game.ready ? colors.success : colors.warning;
    draw->AddCircleFilled(ImVec2(textX + 3.0f * scale, cardMax.y - 17.0f * scale),
                          2.6f * scale, ready, 12);
    TextAt(draw, fonts.caption, 8.5f * scale,
           ImVec2(textX + 10.0f * scale, cardMax.y - 23.0f * scale), colors.textFaint,
           game.ready ? "Files ready" : "Download required");

    const ImVec2 check(cardMax.x - 22.0f * scale, cardMin.y + 22.0f * scale);
    if (selected) {
        draw->AddCircleFilled(check, 9.5f * scale, accent, 24);
        ui::IconCheck(draw, check, 13.0f * scale,
                      IM_COL32(8, 26, 26, 255));
    } else {
        draw->AddCircle(check, 8.5f * scale, AlphaF(IM_COL32_WHITE, 0.22f), 24, 1.0f * scale);
    }

    if (clicked) app::SelectGame(index);
    if (doubleClicked && enabled) app::LoadSelectedGame();
    (void)state;
}

// ---------------------------------------------------------------- detail panel

void DrawInfoRow(ImDrawList* draw, ui::Fonts& fonts, float x, float y, float width,
                 const char* label, const std::string& value) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    Spaced(draw, fonts.medium, 8.0f * scale, ImVec2(x, y), colors.textFaint, label, 1.2f * scale);
    ui::TextEllipsized(draw, fonts.body, 11.0f * scale, ImVec2(x, y + 15.0f * scale),
                       colors.textDim, value, width);
}

void DrawDetailPanel(const AppUiState& state, const GameEntry* game, float x, float y,
                     float width, float height, bool enabled, float dt) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (g_lastSelected != state.selected) {
        g_lastSelected = state.selected;
        g_detailAnim = 0.0f;
    }
    g_detailAnim = Approach(g_detailAnim, 1.0f, 14.0f, dt);
    const float slide = (1.0f - EaseOutCubic(g_detailAnim)) * 10.0f * scale;
    const ImVec2 pos(x, y + slide);
    const ImVec2 end(x + width, y + height + slide);

    ui::GlassPanel(draw, pos, end, 18.0f * scale, 1.15f, true);
    draw->AddRect(pos, end, AlphaF(colors.panelBorder, 0.7f), 18.0f * scale, 0, 1.0f);

    const float pad = 18.0f * scale;

    if (!game) {
        TextAt(draw, fonts.title, 15.0f * scale, ImVec2(pos.x + pad, pos.y + pad),
               colors.text, "Details");
        TextAt(draw, fonts.body, 10.5f * scale,
               ImVec2(pos.x + pad, pos.y + pad + 28.0f * scale), colors.textFaint,
               "Select a game to see its details.");
        return;
    }

    const float coverH = 136.0f * scale;
    const ImVec2 coverPos(pos.x + pad, pos.y + pad);
    const ImVec2 coverEnd(end.x - pad, coverPos.y + coverH);

    if (CoverFor(*game)) {
        DrawCover(draw, CoverFor(*game), CoverW(*game), CoverH(*game), coverPos,
                  ImVec2(coverEnd.x - coverPos.x, coverH), 13.0f * scale, IM_COL32_WHITE);
    } else {
        const ImU32 accent = AccentFor(*game);
        draw->AddRectFilledMultiColor(coverPos, coverEnd, AlphaF(colors.bg1, 0.95f),
                                      AlphaF(colors.bg0, 0.95f),
                                      Mix(colors.bg0, accent, 0.22f),
                                      Mix(colors.bg1, accent, 0.12f));
        TextCentered(draw, fonts.display, 34.0f * scale, (coverPos.x + coverEnd.x) * 0.5f,
                     coverPos.y + coverH * 0.34f, AlphaF(accent, 0.55f), Initials(*game));
    }
    draw->AddRect(coverPos, coverEnd, AlphaF(colors.panelBorder, 0.6f), 13.0f * scale, 0, 1.0f);

    float cursorY = coverEnd.y + 14.0f * scale;
    ui::TextEllipsized(draw, fonts.title, 16.0f * scale, ImVec2(pos.x + pad, cursorY),
                       colors.text, game->name, width - pad * 2.0f);
    cursorY += 24.0f * scale;
    ui::TextEllipsized(draw, fonts.caption, 10.0f * scale, ImVec2(pos.x + pad, cursorY),
                       colors.textDim, game->subtitle, width - pad * 2.0f);

    cursorY += 24.0f * scale;
    DrawPill(draw, ImVec2(pos.x + pad, cursorY), game->status, StatusColor(game->status));

    cursorY += 38.0f * scale;
    draw->AddLine(ImVec2(pos.x + pad, cursorY), ImVec2(end.x - pad, cursorY),
                  AlphaF(colors.panelBorder, 0.45f), 1.0f);
    cursorY += 14.0f * scale;

    DrawInfoRow(draw, fonts, pos.x + pad, cursorY, width - pad * 2.0f, "PROCESS",
                game->process.empty() ? "Unknown" : game->process);
    cursorY += 42.0f * scale;
    DrawInfoRow(draw, fonts, pos.x + pad, cursorY, width - pad * 2.0f, "APP ID",
                game->appid.empty() ? "Not set" : game->appid);
    cursorY += 42.0f * scale;
    DrawInfoRow(draw, fonts, pos.x + pad, cursorY, width - pad * 2.0f, "INSTALL DIR",
                game->installDir.empty() ? "Not configured" : game->installDir);
    cursorY += 42.0f * scale;
    DrawInfoRow(draw, fonts, pos.x + pad, cursorY, width - pad * 2.0f, "INJECTION",
                "DLL injector");

    // Bottom actions.
    const float buttonW = width - pad * 2.0f;
    const float loadH = 42.0f * scale;
    const float openH = 32.0f * scale;
    const float openY = end.y - pad - loadH - 8.0f * scale - openH;
    const float loadY = end.y - pad - loadH;

    ui::ButtonOptions ghost;
    ghost.kind = ui::ButtonKind::Ghost;
    ghost.font = fonts.medium;
    ghost.fontSize = 9.5f * scale;
    ghost.rounding = 10.0f * scale;

    ui::ButtonOptions primary;
    primary.kind = ui::ButtonKind::Primary;
    primary.font = fonts.medium;
    primary.fontSize = 11.0f * scale;
    primary.rounding = 11.0f * scale;

    if (ui::Button("##detail_open", ImVec2(pos.x + pad, openY), ImVec2(buttonW, openH),
                   "OPEN FOLDER", enabled, ghost)) {
        app::OpenSelectedFolder();
    }
    if (ui::Button("##detail_load", ImVec2(pos.x + pad, loadY), ImVec2(buttonW, loadH),
                   "LOAD GAME", enabled, primary)) {
        app::LoadSelectedGame();
    }

    const std::string verify = game->ready ? "All files verified" : "Setup required";
    TextAt(draw, fonts.caption, 8.5f * scale,
           ImVec2(pos.x + pad, openY - 18.0f * scale), colors.textFaint, verify);
}

// ---------------------------------------------------------------- footer

void DrawFooter(const AppUiState& state, float width, float height, float railW) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float footerH = 40.0f * scale;
    const float top = height - footerH;
    draw->AddRectFilled(ImVec2(railW, top), ImVec2(width, height),
                        AlphaF(colors.bg0, 0.62f));
    draw->AddLine(ImVec2(railW, top), ImVec2(width, top), AlphaF(colors.panelBorder, 0.6f),
                  1.0f);

    const float centerY = top + footerH * 0.5f;
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.2f);
    const ImU32 online = state.online ? colors.success : colors.warning;
    draw->AddCircleFilled(ImVec2(railW + 24.0f * scale, centerY), 5.0f * scale,
                          AlphaF(online, 0.20f + 0.15f * pulse), 16);
    draw->AddCircleFilled(ImVec2(railW + 24.0f * scale, centerY), 2.8f * scale, online, 12);
    TextAt(draw, fonts.body, 10.0f * scale,
           ImVec2(railW + 36.0f * scale, centerY - 7.0f * scale), colors.textDim,
           state.online ? "Online" : "Offline mode");

    const std::string version = "v" + state.localVersion;
    const ImVec2 versionSize = Measure(fonts.caption, 9.5f * scale, version);
    TextAt(draw, fonts.caption, 9.5f * scale,
           ImVec2(railW + (width - railW) * 0.5f - versionSize.x * 0.5f, centerY - 6.0f * scale),
           colors.textFaint, version);

    float statusRight = width - 26.0f * scale;

    if (state.busy) {
        const float barW = 130.0f * scale;
        const ImVec2 barPos(statusRight - barW, centerY - 2.5f * scale);
        ui::ProgressBar(draw, barPos, ImVec2(barW, 5.0f * scale), state.progress,
                        state.indeterminate, AlphaF(colors.panelBorder, 0.45f),
                        colors.primary, static_cast<float>(ImGui::GetTime()));
        const std::string label = state.phase.empty() ? "Working" : state.phase;
        ui::TextEllipsized(draw, fonts.caption, 9.0f * scale,
                           ImVec2(statusRight - barW - 150.0f * scale, centerY - 6.0f * scale),
                           colors.textDim, label, 140.0f * scale);
    } else if (state.backgroundSync) {
        ui::Spinner(draw, ImVec2(statusRight - 92.0f * scale, centerY), 6.0f * scale,
                    1.5f * scale, colors.primary, static_cast<float>(ImGui::GetTime()));
        TextAt(draw, fonts.caption, 9.5f * scale,
               ImVec2(statusRight - 78.0f * scale, centerY - 6.0f * scale), colors.textDim,
               "Checking in background");
    } else {
        int ready = 0;
        for (const auto& game : state.games)
            if (game.ready) ++ready;

        const int total = static_cast<int>(state.games.size());
        const std::string text =
            (total > 0 && ready == total) ? "All files up to date" : "Files need attention";
        const ImVec2 measured = Measure(fonts.caption, 9.5f * scale, text);
        const ImU32 tint = (total > 0 && ready == total) ? colors.success : colors.warning;

        draw->AddCircleFilled(ImVec2(statusRight - measured.x - 14.0f * scale, centerY),
                              2.6f * scale, tint, 12);
        TextAt(draw, fonts.caption, 9.5f * scale,
               ImVec2(statusRight - measured.x, centerY - 6.0f * scale), colors.textDim, text);
    }
}

// ---------------------------------------------------------------- library

std::vector<int> FilteredGames(const AppUiState& state) {
    std::vector<int> result;
    std::string query = g_search;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    for (int i = 0; i < static_cast<int>(state.games.size()); ++i) {
        if (query.empty()) {
            result.push_back(i);
            continue;
        }

        std::string haystack = state.games[static_cast<std::size_t>(i)].name + " " +
                               state.games[static_cast<std::size_t>(i)].subtitle + " " +
                               state.games[static_cast<std::size_t>(i)].id;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (haystack.find(query) != std::string::npos) result.push_back(i);
    }
    return result;
}

void DrawEmptyState(float x, float y, float width, float height, bool enabled, bool filtered) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ui::GlassPanel(draw, ImVec2(x, y), ImVec2(x + width, y + height), 18.0f * scale, 0.9f);
    draw->AddCircleFilled(ImVec2(x + width * 0.5f, y + height * 0.32f), 26.0f * scale,
                          AlphaF(colors.primary, 0.10f), 40);
    ui::IconSearch(draw, ImVec2(x + width * 0.5f, y + height * 0.32f), 26.0f * scale,
                   AlphaF(colors.primary, 0.75f));

    TextCentered(draw, fonts.title, 16.0f * scale, x + width * 0.5f, y + height * 0.48f,
                 colors.text, filtered ? "No games match your search" : "Game list is empty");
    TextCentered(draw, fonts.body, 11.0f * scale, x + width * 0.5f, y + height * 0.58f,
                 colors.textFaint,
                 filtered ? "Try a different title or clear the search field."
                          : "Check your connection and try again in a moment.");

    if (!filtered) {
        ui::ButtonOptions options;
        options.kind = ui::ButtonKind::Soft;
        options.font = fonts.medium;
        options.fontSize = 10.0f * scale;
        options.rounding = 9.0f * scale;
        if (ui::Button("##empty_check", ImVec2(x + width * 0.5f - 70.0f * scale,
                                               y + height * 0.70f),
                       ImVec2(140.0f * scale, 34.0f * scale), "CHECK FILES", enabled,
                       options)) {
            app::CheckFiles();
        }
    }
}

void DrawLibrary(const AppUiState& state, float x, float y, float width, float height,
                 bool enabled, float dt, float bannerAnim) {
    const float scale = Scale();
    const theme::Colors& colors = theme::Current();
    ui::Fonts& fonts = ui::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    TextAt(draw, fonts.title, 27.0f * scale, ImVec2(x, y), colors.text, "Library");
    TextAt(draw, fonts.body, 11.5f * scale, ImVec2(x + 1.0f * scale, y + 36.0f * scale),
           colors.textDim, "Pick a game, then press Load Game. VcDex handles the rest.");

    ui::ButtonOptions checkOptions;
    checkOptions.kind = ui::ButtonKind::Ghost;
    checkOptions.font = fonts.medium;
    checkOptions.fontSize = 9.5f * scale;
    checkOptions.rounding = 9.0f * scale;

    const float checkW = 130.0f * scale;
    if (ui::Button("##header_check", ImVec2(x + width - checkW, y + 6.0f * scale),
                   ImVec2(checkW, 34.0f * scale), "CHECK FILES", enabled, checkOptions)) {
        app::CheckFiles();
    }

    float bodyTop = y + 74.0f * scale;
    if (state.updateAvailable) {
        DrawUpdateBanner(state, x, bodyTop, width, enabled && !state.busy, bannerAnim);
        bodyTop += 68.0f * scale;
    }

    const float footerH = 40.0f * scale;
    const float bodyBottom = height + y - footerH - 4.0f * scale;

    const bool showDetail = width >= 900.0f * scale;
    const float detailW = 300.0f * scale;
    const float leftW = showDetail ? width - detailW - 18.0f * scale : width;

    const GameEntry* selectedGame = state.games.empty()
                                        ? nullptr
                                        : &state.games[static_cast<std::size_t>(
                                              std::clamp(state.selected, 0,
                                                         static_cast<int>(state.games.size()) -
                                                             1))];

    const float heroH = std::clamp(leftW * 0.33f, 190.0f * scale, 250.0f * scale);

    if (selectedGame) {
        DrawHero(state, selectedGame, x, bodyTop, leftW, heroH, enabled, dt);
    } else {
        DrawHero(state, nullptr, x, bodyTop, leftW, heroH, enabled, dt);
    }

    const float cardsLabelY = bodyTop + heroH + 12.0f * scale;
    TextAt(draw, fonts.medium, 8.5f * scale, ImVec2(x + 2.0f * scale, cardsLabelY + 4.0f * scale),
           colors.textFaint, "ALL GAMES");

    if (!state.games.empty()) {
        const std::string count = std::to_string(state.games.size());
        TextAt(draw, fonts.caption, 8.5f * scale,
               ImVec2(x + 70.0f * scale, cardsLabelY + 4.0f * scale), colors.textFaint,
               count + " titles");
    }

    const float gridTop = cardsLabelY + 22.0f * scale;
    const float gridBottom = bodyBottom;
    const float gridH = std::max(0.0f, gridBottom - gridTop);

    const std::vector<int> filtered = FilteredGames(state);

    if (filtered.empty()) {
        DrawEmptyState(x, gridTop, leftW, gridH, enabled, !state.games.empty());
    } else {
        const int columns = leftW > 560.0f * scale ? 2 : 1;
        const int rows = (static_cast<int>(filtered.size()) + columns - 1) / columns;
        const float gap = 12.0f * scale;
        const float cardW = (leftW - gap * static_cast<float>(columns - 1)) /
                            static_cast<float>(columns);
        const float cardH = std::clamp(
            (gridH - gap * static_cast<float>(rows - 1)) / static_cast<float>(std::max(1, rows)),
            112.0f * scale, 168.0f * scale);

        for (int slot = 0; slot < static_cast<int>(filtered.size()); ++slot) {
            const int actual = filtered[static_cast<std::size_t>(slot)];
            const int column = slot % columns;
            const int row = slot / columns;
            const float cardX = x + static_cast<float>(column) * (cardW + gap);
            const float cardY = gridTop + static_cast<float>(row) * (cardH + gap);
            if (cardY + cardH > gridBottom + 1.0f) break;

            const float anim = Clamp01(g_pageAnim * 1.7f - static_cast<float>(slot) * 0.10f);
            DrawGameCard(state, state.games[static_cast<std::size_t>(actual)], actual,
                         ImVec2(cardX, cardY), ImVec2(cardW, cardH),
                         state.selected == actual, enabled, anim);
        }
    }

    if (showDetail) {
        const float detailX = x + leftW + 18.0f * scale;
        DrawDetailPanel(state, selectedGame, detailX, bodyTop, detailW,
                        bodyBottom - bodyTop, enabled, dt);
    }
}

} // namespace

void ShowLibrary() {
    if (g_page != Page::Library) {
        g_page = Page::Library;
        g_pageAnim = 0.0f;
    }
}

void ShowSettings() {
    if (g_page != Page::Settings) {
        g_page = Page::Settings;
        g_pageAnim = 0.0f;
    }
}

bool IsSettings() {
    return g_page == Page::Settings;
}

void Draw(const AppUiState& state, HWND hwnd) {
    ImGuiIO& io = ImGui::GetIO();
    const float scale = Scale();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    const theme::Colors& colors = theme::Current();

    g_boot += dt;
    g_open = Approach(g_open, 1.0f, 9.0f, dt);

    // Keep the boot splash up while the first sync and the automatic update run.
    if (!g_autoUpdate && state.updateAvailable && !state.checking && !state.busy &&
        g_boot > 1.2f) {
        g_autoUpdate = true;
        app::StartSelfUpdate();
    }

    const bool splashWanted =
        state.checking || g_boot < 1.8f ||
        (g_autoUpdate && (state.busy || !state.toastError));
    g_splash = Approach(g_splash, splashWanted ? 1.0f : 0.0f, 6.0f, dt);
    const bool booting = g_splash > 0.45f;

    static float overlay = 0.0f;
    overlay = Approach(overlay, state.busy ? 1.0f : 0.0f, 12.0f, dt);

    // Restore the last game once the list has arrived.
    if (!g_restoredGame && !state.games.empty()) {
        g_restoredGame = true;
        const std::string& last = settings::Get().lastGame;
        if (!last.empty()) {
            for (int i = 0; i < static_cast<int>(state.games.size()); ++i) {
                if (state.games[static_cast<std::size_t>(i)].id == last) {
                    app::SelectGame(i);
                    break;
                }
            }
        }
    }

    if (!state.games.empty()) {
        const int selected = std::clamp(state.selected, 0,
                                        static_cast<int>(state.games.size()) - 1);
        settings::Get().lastGame =
            state.games[static_cast<std::size_t>(selected)].id;
    }

    const bool interactive = !booting && !state.busy && !state.checking;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##vcdex_root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    DrawBackground(draw, width, height, dt);

    const float railW = 84.0f * scale;
    DrawRail(state, height, railW, dt, interactive);
    DrawTitleBar(state, hwnd, width, railW);

    const float contentX = railW + 26.0f * scale;
    const float contentY = 64.0f * scale + 14.0f * scale;
    const float contentW = width - contentX - 26.0f * scale;
    const float contentH = height - contentY;

    g_pageAnim = Approach(g_pageAnim, 1.0f, 13.0f, dt);
    const float pageEase = EaseOutCubic(g_pageAnim);
    const float pageOffset = (1.0f - pageEase) * 10.0f * scale;

    static float bannerAnim = 0.0f;
    bannerAnim = Approach(bannerAnim, state.updateAvailable ? 1.0f : 0.0f, 8.0f, dt);

    if (g_page == Page::Library) {
        DrawLibrary(state, contentX, contentY + pageOffset, contentW, contentH,
                    interactive, dt, bannerAnim);
    } else {
        settings_page::Draw(state, contentX, contentY + pageOffset, contentW, contentH);
    }

    if (pageEase < 0.999f) {
        draw->AddRectFilled(ImVec2(contentX, contentY),
                            ImVec2(contentX + contentW, height - 40.0f * scale),
                            AlphaF(colors.bg0, (1.0f - pageEase) * 0.45f), 0.0f);
    }

    DrawFooter(state, width, height, railW);

    if (!booting) {
        splash::DrawToast(state);
        splash::DrawBusy(state, overlay);
    }

    if (g_splash > 0.003f) splash::Draw(state, g_splash, g_boot);

    if (g_open < 0.999f) {
        draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height),
                            IM_COL32(0, 0, 0, static_cast<int>((1.0f - EaseOutCubic(g_open)) * 220.0f)));
    }

    // Keyboard shortcuts.
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && interactive) {
        if (state.updateAvailable)
            app::StartSelfUpdate();
        else
            app::CheckFiles();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && interactive && !state.games.empty())
        app::LoadSelectedGame();
    if (ImGui::IsKeyPressed(ImGuiKey_K, false) && io.KeyCtrl && g_page == Page::Library)
        g_searchFocus = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (g_search[0] != '\0')
            g_search[0] = '\0';
        else if (g_page == Page::Settings)
            ShowLibrary();
    }

    ImGui::End();
}

} // namespace pages
