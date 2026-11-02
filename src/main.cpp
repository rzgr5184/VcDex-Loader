#include "app.hpp"
#include "config.hpp"
#include "images.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device*        g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain*      g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;

HWND g_hwnd = nullptr;
bool g_running = true;
float g_scale = 1.0f;
float g_bootElapsed = 0.0f;

struct FontSet {
    ImFont* caption = nullptr;
    ImFont* body = nullptr;
    ImFont* medium = nullptr;
    ImFont* title = nullptr;
    ImFont* icon = nullptr;
};
FontSet g_fonts;

// ---------------------------------------------------------------- palette

const ImU32 kBg          = IM_COL32(9, 11, 16, 255);
const ImU32 kSidebar     = IM_COL32(13, 16, 22, 255);
const ImU32 kCard        = IM_COL32(19, 23, 31, 255);
const ImU32 kCardHover   = IM_COL32(24, 29, 39, 255);
const ImU32 kCardSelected= IM_COL32(25, 31, 42, 255);
const ImU32 kBorder      = IM_COL32(255, 255, 255, 16);
const ImU32 kText        = IM_COL32(237, 241, 247, 255);
const ImU32 kTextDim     = IM_COL32(136, 147, 163, 255);
const ImU32 kTextFaint   = IM_COL32(92, 101, 115, 255);
const ImU32 kAccent      = IM_COL32(158, 232, 78, 255);
const ImU32 kAccentDark  = IM_COL32(96, 154, 32, 255);
const ImU32 kGreen       = IM_COL32(88, 214, 141, 255);
const ImU32 kRed         = IM_COL32(243, 100, 100, 255);
const ImU32 kViolet      = IM_COL32(158, 120, 255, 255);
const ImU32 kCyan        = IM_COL32(74, 210, 255, 255);
const ImU32 kOrange      = IM_COL32(255, 148, 84, 255);

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float Approach(float current, float target, float rate, float dt) {
    return current + (target - current) * (1.0f - std::exp(-rate * dt));
}

std::string Ellipsize(const std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return text;

    std::string out = text.substr(0, maxBytes);
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
        out.pop_back();
    if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0)
        out.pop_back();

    return out + "...";
}

ImU32 WithAlpha(ImU32 color, int alpha) {
    return (color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << 24);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {    t = Clamp01(t);
    auto channel = [&](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFF);
        const float cb = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<ImU32>(ca + (cb - ca) * t);
    };
    return IM_COL32(channel(0), channel(8), channel(16), channel(24));
}

ImU32 AccentFor(const GameEntry& game) {
    return game.accent == 1 ? kOrange : kViolet;
}

std::string InitialsFor(const GameEntry& game) {
    if (game.id == "redmatch2") return "R2";
    if (game.id == "onearmedrobber") return "OAR";

    std::string initials;
    bool newWord = true;
    for (char c : game.name) {
        if (c == ' ') { newWord = true; continue; }
        if (newWord && std::isalpha(static_cast<unsigned char>(c))) {
            initials.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            newWord = false;
        }
    }
    if (initials.empty()) initials = "VC";
    return initials;
}

ImTextureID TexId(ID3D11ShaderResourceView* srv) {
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(srv));
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring result(static_cast<std::size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), needed);
    return result;
}

// Covers are downloaded by the updater into data/covers/. Reload them when the
// file timestamp changes, falling back to the embedded resource on failure.
void SyncCoverTextures(const AppUiState& state) {
    const auto dataDir = app::DataDir();

    for (const auto& game : state.games) {
        if (game.coverRel.empty()) continue;

        images::Id id;
        if (game.id == "redmatch2") {
            id = images::Redmatch2Cover;
        } else if (game.id == "onearmedrobber") {
            id = images::OneArmedCover;
        } else {
            continue;
        }

        images::LoadFile(id, dataDir / Utf8ToWide(game.coverRel));
    }
}

ID3D11ShaderResourceView* CoverFor(const GameEntry& game) {
    if (game.id == "redmatch2") return images::Srv(images::Redmatch2Cover);
    if (game.id == "onearmedrobber") return images::Srv(images::OneArmedCover);
    return nullptr;
}

int CoverWidthFor(const GameEntry& game) {
    if (game.id == "redmatch2") return images::Width(images::Redmatch2Cover);
    if (game.id == "onearmedrobber") return images::Width(images::OneArmedCover);
    return 0;
}

int CoverHeightFor(const GameEntry& game) {
    if (game.id == "redmatch2") return images::Height(images::Redmatch2Cover);
    if (game.id == "onearmedrobber") return images::Height(images::OneArmedCover);
    return 0;
}

// Draws a texture cropped like CSS `object-fit: cover` inside the given rect.
void DrawImageCover(ImDrawList* draw, ID3D11ShaderResourceView* srv, int srcW, int srcH,
                    ImVec2 pos, ImVec2 size, float rounding, ImU32 tint) {
    if (!srv || srcW <= 0 || srcH <= 0 || size.x <= 0.0f || size.y <= 0.0f) {
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            WithAlpha(tint, 26), rounding);
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

    draw->AddImageRounded(TexId(srv), pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          uv0, uv1, tint, rounding);
}

float EaseOutCubic(float value) {
    value = Clamp01(value);
    const float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
}

// ---------------------------------------------------------------- draw helpers

void DrawTextAt(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color, const std::string& text) {
    draw->AddText(font, size, pos, color, text.c_str());
}

ImVec2 Measure(ImFont* font, float size, const std::string& text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
}

float DrawSpaced(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
                 const std::string& text, float spacing) {
    float x = pos.x;
    for (char c : text) {
        const std::string glyph(1, c);
        draw->AddText(font, size, ImVec2(x, pos.y), color, glyph.c_str());
        x += Measure(font, size, glyph).x + spacing;
    }
    return x - pos.x - spacing;
}

void TextCentered(ImDrawList* draw, ImFont* font, float size, ImVec2 centerX,
                  float y, ImU32 color, const std::string& text) {
    const ImVec2 measure = Measure(font, size, text);
    draw->AddText(font, size, ImVec2(centerX.x - measure.x * 0.5f, y), color, text.c_str());
}

bool CustomButton(const char* id, ImVec2 pos, ImVec2 size, const std::string& label,
                  ImU32 background, ImU32 backgroundHover, ImU32 textColor,
                  ImFont* font, float fontSize, float rounding, bool enabled,
                  ImU32 borderColor = 0) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);

    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);

    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 color = held ? Mix(background, IM_COL32(0, 0, 0, 255), 0.18f)
                       : (hovered ? backgroundHover : background);

    if (!enabled) {
        color = IM_COL32(32, 36, 44, 255);
        textColor = IM_COL32(120, 128, 140, 255);
    }

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color, rounding);

    if (borderColor)
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, rounding, 0, 1.0f);

    const ImVec2 measure = Measure(font, fontSize, label);
    const ImVec2 textPos(pos.x + (size.x - measure.x) * 0.5f,
                         pos.y + (size.y - measure.y) * 0.5f - fontSize * 0.08f);
    DrawTextAt(draw, font, fontSize, textPos, textColor, label);

    return clicked;
}

void DrawSpinner(ImDrawList* draw, ImVec2 center, float radius, float thickness, ImU32 color, float time) {
    const float start = time * 3.4f;
    for (int i = 0; i < 3; ++i) {
        const float offset = static_cast<float>(i) * 2.094f;
        draw->PathArcTo(center, radius, start + offset, start + offset + 1.45f, 32);
        draw->PathStroke(WithAlpha(color, 190 - i * 45), 0, thickness);
    }
    draw->AddCircle(center, radius, WithAlpha(color, 28), 48, thickness);
}

void DrawStatusPill(ImDrawList* draw, ImVec2 pos, const std::string& status, ImU32 accent) {
    const float fontSize = 11.0f * g_scale;
    const ImVec2 measure = Measure(g_fonts.medium, fontSize, status);
    const ImVec2 size(measure.x + 30.0f * g_scale, 24.0f * g_scale);

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), WithAlpha(accent, 26),
                        size.y * 0.5f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), WithAlpha(accent, 70),
                  size.y * 0.5f, 0, 1.0f);

    draw->AddCircleFilled(ImVec2(pos.x + 12.0f * g_scale, pos.y + size.y * 0.5f),
                          3.4f * g_scale, accent, 12);
    DrawTextAt(draw, g_fonts.medium, fontSize,
             ImVec2(pos.x + 21.0f * g_scale, pos.y + (size.y - measure.y) * 0.5f - 1.0f * g_scale),
             accent, status);
}

ImU32 StatusColor(const std::string& status) {
    if (status == "Undetected" || status == "Safe") return kGreen;
    if (status == "Updating" || status == "Maintenance") return IM_COL32(245, 195, 80, 255);
    return kRed;
}

// ---------------------------------------------------------------- UI

void DrawWindowControls(float width) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float y = 18.0f * g_scale;
    const float size = 34.0f * g_scale;
    const float gap = 6.0f * g_scale;
    const float closeX = width - 22.0f * g_scale - size;
    const float minX = closeX - gap - size;

    auto control = [&](const char* id, float x, bool isClose) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::InvisibleButton(id, ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (hovered) {
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size),
                                isClose ? IM_COL32(200, 52, 60, 255) : IM_COL32(255, 255, 255, 18),
                                size * 0.5f);
        }

        const ImU32 color = hovered ? IM_COL32(255, 255, 255, 255) : kTextDim;
        const ImVec2 center(x + size * 0.5f, y + size * 0.5f);

        if (isClose) {
            const float arm = 5.2f * g_scale;
            draw->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), color, 1.8f * g_scale);
            draw->AddLine(ImVec2(center.x + arm, center.y - arm), ImVec2(center.x - arm, center.y + arm), color, 1.8f * g_scale);
        } else {
            const float arm = 5.2f * g_scale;
            draw->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), color, 1.8f * g_scale);
        }

        if (!clicked) return false;

        if (isClose) {
            g_running = false;
            PostQuitMessage(0);
        } else {
            ShowWindow(g_hwnd, SW_MINIMIZE);
        }
        return true;
    };

    control("##win_min", minX, false);
    control("##win_close", closeX, true);
}

void DrawSidebar(const AppUiState& state, float width, float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), kSidebar);
    draw->AddLine(ImVec2(width - 1.0f, 0), ImVec2(width - 1.0f, height), IM_COL32(255, 255, 255, 10));

    // Logo
    const float logoSize = 38.0f * g_scale;
    const ImVec2 logoPos(22.0f * g_scale, 22.0f * g_scale);
    const float logoPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.0f);
    const ImVec2 logoCenter(logoPos.x + logoSize * 0.5f, logoPos.y + logoSize * 0.5f);

    draw->AddCircleFilled(logoCenter, logoSize * 0.92f,
                          WithAlpha(kAccent, 8 + static_cast<int>(12.0f * logoPulse)), 48);
    draw->AddRectFilled(logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize),
                        WithAlpha(kAccent, 26), 11.0f * g_scale);
    draw->AddRect(logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize),
                  WithAlpha(kAccent, 95 + static_cast<int>(45.0f * logoPulse)),
                  11.0f * g_scale, 0, 1.2f);

    ID3D11ShaderResourceView* logoSrv = images::Srv(images::VcLogo);
    if (logoSrv) {
        const float inset = 3.0f * g_scale;
        DrawImageCover(draw, logoSrv, images::Width(images::VcLogo), images::Height(images::VcLogo),
                       ImVec2(logoPos.x + inset, logoPos.y + inset),
                       ImVec2(logoSize - inset * 2.0f, logoSize - inset * 2.0f),
                       8.0f * g_scale, IM_COL32_WHITE);
    } else {
        const std::string mark = "V";
        const ImVec2 markSize = Measure(g_fonts.icon, 22.0f * g_scale, mark);
        DrawTextAt(draw, g_fonts.icon, 22.0f * g_scale,
                   ImVec2(logoPos.x + (logoSize - markSize.x) * 0.5f,
                          logoPos.y + (logoSize - markSize.y) * 0.5f - 1.0f * g_scale),
                   kAccent, mark);
    }

    DrawTextAt(draw, g_fonts.title, 21.0f * g_scale,
             ImVec2(logoPos.x + logoSize + 12.0f * g_scale, logoPos.y + 2.0f * g_scale),
             kText, "VcDex");
    DrawSpaced(draw, g_fonts.medium, 9.5f * g_scale,
               ImVec2(logoPos.x + logoSize + 13.0f * g_scale, logoPos.y + 27.0f * g_scale),
               kTextFaint, "LOADER", 2.2f * g_scale);

    // Navigation
    float y = 104.0f * g_scale;
    DrawSpaced(draw, g_fonts.medium, 9.5f * g_scale, ImVec2(24.0f * g_scale, y),
               kTextFaint, "MENU", 2.0f * g_scale);

    y += 22.0f * g_scale;
    const float itemH = 44.0f * g_scale;
    const float itemX = 14.0f * g_scale;
    const float itemW = width - itemX * 2.0f;

    auto navItem = [&](const char* id, const std::string& label, bool active, float top, bool enabled) {
        ImGui::SetCursorScreenPos(ImVec2(itemX, top));
        ImGui::InvisibleButton(id, ImVec2(itemW, itemH));
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);

        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            draw->AddRectFilled(ImVec2(itemX, top), ImVec2(itemX + itemW, top + itemH),
                                IM_COL32(255, 255, 255, 12), 9.0f * g_scale);
        }
        if (active) {
            draw->AddRectFilled(ImVec2(itemX, top), ImVec2(itemX + itemW, top + itemH),
                                WithAlpha(kAccent, 18), 9.0f * g_scale);
            draw->AddRectFilled(ImVec2(itemX, top + itemH * 0.25f),
                                ImVec2(itemX + 3.0f * g_scale, top + itemH * 0.75f),
                                kAccent, 2.0f * g_scale);
        }

        const ImU32 textColor = !enabled ? kTextFaint : (active ? kText : kTextDim);

        // Small 2x2 grid icon
        const ImVec2 iconCenter(itemX + 22.0f * g_scale, top + itemH * 0.5f);
        const float dot = 3.2f * g_scale;
        const ImU32 iconColor = active ? kAccent : textColor;
        for (int gy = 0; gy < 2; ++gy)
            for (int gx = 0; gx < 2; ++gx) {
                const ImVec2 c(iconCenter.x + (gx == 0 ? -dot - 1.2f * g_scale : 1.2f * g_scale),
                               iconCenter.y + (gy == 0 ? -dot - 1.2f * g_scale : 1.2f * g_scale));
                draw->AddRectFilled(ImVec2(c.x, c.y), ImVec2(c.x + dot * 2.0f, c.y + dot * 2.0f),
                                    WithAlpha(iconColor, 220), 1.5f * g_scale);
            }

        DrawTextAt(draw, g_fonts.body, 14.0f * g_scale,
                 ImVec2(itemX + 42.0f * g_scale, top + (itemH - Measure(g_fonts.body, 14.0f * g_scale, label).y) * 0.5f),
                 textColor, label);

        if (!enabled) {
            const std::string soon = "SOON";
            const ImVec2 measure = Measure(g_fonts.medium, 8.5f * g_scale, soon);
            const ImVec2 badge(itemX + itemW - measure.x - 22.0f * g_scale, top + itemH * 0.5f - 8.0f * g_scale);
            draw->AddRectFilled(badge, ImVec2(badge.x + measure.x + 12.0f * g_scale, badge.y + 16.0f * g_scale),
                                IM_COL32(255, 255, 255, 12), 8.0f * g_scale);
            DrawTextAt(draw, g_fonts.medium, 8.5f * g_scale,
                     ImVec2(badge.x + 6.0f * g_scale, badge.y + 3.0f * g_scale), kTextFaint, soon);
        }

        return clicked;
    };

    navItem("##nav_games", "Games", true, y, true);
    navItem("##nav_settings", "Settings", false, y + itemH + 6.0f * g_scale, false);

    // Status card
    const float cardH = 118.0f * g_scale;
    const float cardY = height - cardH - 58.0f * g_scale;
    draw->AddRectFilled(ImVec2(itemX, cardY), ImVec2(itemX + itemW, cardY + cardH),
                        IM_COL32(255, 255, 255, 7), 12.0f * g_scale);
    draw->AddRect(ImVec2(itemX, cardY), ImVec2(itemX + itemW, cardY + cardH),
                  IM_COL32(255, 255, 255, 14), 12.0f * g_scale, 0, 1.0f);

    DrawSpaced(draw, g_fonts.medium, 8.5f * g_scale,
               ImVec2(itemX + 16.0f * g_scale, cardY + 14.0f * g_scale),
               kTextFaint, "SERVER", 1.6f * g_scale);

    const ImU32 serverColor = state.online ? kGreen : IM_COL32(240, 170, 80, 255);
    draw->AddCircleFilled(ImVec2(itemX + 20.0f * g_scale, cardY + 36.0f * g_scale),
                          3.4f * g_scale, serverColor, 12);
    DrawTextAt(draw, g_fonts.body, 12.5f * g_scale,
             ImVec2(itemX + 30.0f * g_scale, cardY + 28.0f * g_scale),
             kText, state.online ? "Online" : "Offline");

    DrawSpaced(draw, g_fonts.medium, 8.5f * g_scale,
               ImVec2(itemX + 16.0f * g_scale, cardY + 58.0f * g_scale),
               kTextFaint, "VERSION", 1.6f * g_scale);
    DrawTextAt(draw, g_fonts.body, 12.5f * g_scale,
             ImVec2(itemX + 16.0f * g_scale, cardY + 72.0f * g_scale),
             kTextDim, "v" + state.localVersion);

    int readyCount = 0;
    for (const auto& game : state.games)
        if (game.ready) ++readyCount;

    DrawSpaced(draw, g_fonts.medium, 8.5f * g_scale,
               ImVec2(itemX + 16.0f * g_scale, cardY + 94.0f * g_scale),
               kTextFaint, "FILES", 1.6f * g_scale);
    DrawTextAt(draw, g_fonts.body, 11.0f * g_scale,
             ImVec2(itemX + 54.0f * g_scale, cardY + 92.0f * g_scale),
             kTextDim, std::to_string(readyCount) + "/" + std::to_string(state.games.size()) + " ready");

    // Credit
    TextCentered(draw, g_fonts.body, 11.0f * g_scale, ImVec2(width * 0.5f, 0),
                 height - 38.0f * g_scale, kTextFaint, "made by Laynex");
    TextCentered(draw, g_fonts.medium, 8.5f * g_scale, ImVec2(width * 0.5f, 0),
                 height - 22.0f * g_scale, IM_COL32(72, 80, 94, 255), "VCDEX LOADER");
}

void DrawUpdateBanner(const AppUiState& state, float x, float width, float y, bool busy) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!state.updateAvailable) return;

    const float height = 52.0f * g_scale;
    const ImVec2 topLeft(x, y);
    const ImVec2 bottomRight(x + width, y + height);

    draw->AddRectFilled(topLeft, bottomRight, WithAlpha(kViolet, 18), 12.0f * g_scale);
    draw->AddRect(topLeft, bottomRight, WithAlpha(kViolet, 70), 12.0f * g_scale, 0, 1.0f);

    const float bannerPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.1f);
    draw->AddCircle(ImVec2(x + 20.0f * g_scale, y + height * 0.5f),
                    4.0f * g_scale + 3.0f * g_scale * bannerPulse,
                    WithAlpha(kViolet, 45), 16, 1.0f);
    draw->AddCircleFilled(ImVec2(x + 20.0f * g_scale, y + height * 0.5f),
                          4.0f * g_scale, kViolet, 12);

    const bool binaryRepair = state.localVersion == state.remoteVersion;
    DrawTextAt(draw, g_fonts.medium, 12.5f * g_scale,
             ImVec2(x + 32.0f * g_scale, y + 10.0f * g_scale), kText,
             binaryRepair ? "Repair available" : "Update available");
    DrawTextAt(draw, g_fonts.body, 10.5f * g_scale,
             ImVec2(x + 32.0f * g_scale, y + 28.0f * g_scale), kTextDim,
             binaryRepair ? "Local executable differs from the official build"
                          : "v" + state.localVersion + "  ->  v" + state.remoteVersion);

    const float buttonW = 172.0f * g_scale;
    const float buttonH = 32.0f * g_scale;
    if (CustomButton("##update_now", ImVec2(x + width - buttonW - 12.0f * g_scale,
                                            y + (height - buttonH) * 0.5f),
                     ImVec2(buttonW, buttonH), busy ? "UPDATING..." : "RESTART & UPDATE",
                     kViolet, Mix(kViolet, IM_COL32(255, 255, 255, 255), 0.18f),
                     IM_COL32(18, 14, 28, 255), g_fonts.medium, 10.5f * g_scale,
                     8.0f * g_scale, true)) {
        app::StartSelfUpdate();
    }
}

void DrawGameCard(const GameEntry& game, int index, ImVec2 pos, ImVec2 size,
                  bool selected, bool busy) {
    ImDrawList* draw = ImGui::GetWindowDrawList();

    char id[64];
    std::snprintf(id, sizeof(id), "##game_%d", index);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);

    const bool hovered = !busy && ImGui::IsItemHovered();
    const bool clicked = !busy && ImGui::IsItemClicked(ImGuiMouseButton_Left);

    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImU32 accent = AccentFor(game);

    if (selected) {
        draw->AddRect(ImVec2(pos.x - 2.0f, pos.y - 2.0f),
                      ImVec2(pos.x + size.x + 2.0f, pos.y + size.y + 2.0f),
                      WithAlpha(accent, 40), 20.0f * g_scale, 0, 5.0f * g_scale);
    }

    const ImU32 background = selected ? kCardSelected : (hovered ? kCardHover : kCard);
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), background, 16.0f * g_scale);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                  selected ? WithAlpha(accent, 190) : kBorder, 16.0f * g_scale, 0,
                  selected ? 1.6f * g_scale : 1.0f * g_scale);

    // Cover tile (animated hover zoom + glow)
    static float coverHover[32] = {};
    const int hoverIndex = std::clamp(index, 0, 31);
    coverHover[hoverIndex] = Approach(coverHover[hoverIndex], hovered ? 1.0f : 0.0f,
                                      14.0f, ImGui::GetIO().DeltaTime);
    const float coverGlow = coverHover[hoverIndex];

    const float tile = 96.0f * g_scale;
    const ImVec2 tilePos(pos.x + 18.0f * g_scale, pos.y + 18.0f * g_scale);
    const ImVec2 tileEnd(tilePos.x + tile, tilePos.y + tile);

    draw->AddRectFilled(ImVec2(tilePos.x - 2.0f, tilePos.y - 2.0f),
                        ImVec2(tileEnd.x + 2.0f, tileEnd.y + 2.0f),
                        WithAlpha(accent, 22 + static_cast<int>(28.0f * coverGlow)),
                        17.0f * g_scale);

    const float grow = 2.0f * g_scale * coverGlow;
    ID3D11ShaderResourceView* cover = CoverFor(game);
    if (cover) {
        DrawImageCover(draw, cover, CoverWidthFor(game), CoverHeightFor(game),
                       ImVec2(tilePos.x - grow * 0.5f, tilePos.y - grow * 0.5f),
                       ImVec2(tile + grow, tile + grow), 15.0f * g_scale, IM_COL32_WHITE);
    } else {
        draw->AddRectFilled(tilePos, tileEnd, WithAlpha(accent, 34), 15.0f * g_scale);
        const std::string initials = InitialsFor(game);
        const ImVec2 initialsSize = Measure(g_fonts.icon, 30.0f * g_scale, initials);
        DrawTextAt(draw, g_fonts.icon, 30.0f * g_scale,
                   ImVec2(tilePos.x + (tile - initialsSize.x) * 0.5f,
                          tilePos.y + (tile - initialsSize.y) * 0.5f - 2.0f * g_scale),
                   accent, initials);
    }
    draw->AddRect(tilePos, tileEnd,
                  WithAlpha(accent, 105 + static_cast<int>(105.0f * coverGlow)),
                  15.0f * g_scale, 0, 1.2f * g_scale);

    // Text block
    const float textX = tilePos.x + tile + 16.0f * g_scale;
    DrawTextAt(draw, g_fonts.title, 19.0f * g_scale,
             ImVec2(textX, pos.y + 20.0f * g_scale), kText, game.name);
    DrawTextAt(draw, g_fonts.body, 11.5f * g_scale,
             ImVec2(textX, pos.y + 48.0f * g_scale), kTextDim, game.subtitle);

    DrawStatusPill(draw, ImVec2(textX, pos.y + 76.0f * g_scale),
                   game.status, StatusColor(game.status));

    DrawSpaced(draw, g_fonts.medium, 8.5f * g_scale,
               ImVec2(textX, pos.y + 114.0f * g_scale), kTextFaint, game.tag, 1.6f * g_scale);

    // Bottom row: readiness
    const float bottomY = pos.y + size.y - 32.0f * g_scale;
    const ImU32 readyColor = game.ready ? kGreen : IM_COL32(240, 170, 80, 255);
    draw->AddCircleFilled(ImVec2(pos.x + 24.0f * g_scale, bottomY + 5.0f * g_scale),
                          3.0f * g_scale, readyColor, 12);
    DrawTextAt(draw, g_fonts.body, 10.5f * g_scale,
             ImVec2(pos.x + 34.0f * g_scale, bottomY - 2.0f * g_scale),
             game.ready ? kTextDim : IM_COL32(200, 165, 110, 255),
             game.ready ? "Files ready" : "Download required");

    // Selection indicator
    const ImVec2 checkCenter(pos.x + size.x - 36.0f * g_scale, pos.y + 36.0f * g_scale);
    if (selected) {
        draw->AddCircleFilled(checkCenter, 11.0f * g_scale, accent, 24);
        draw->AddLine(ImVec2(checkCenter.x - 4.5f * g_scale, checkCenter.y),
                      ImVec2(checkCenter.x - 1.0f * g_scale, checkCenter.y + 3.5f * g_scale),
                      IM_COL32(14, 18, 12, 255), 2.0f * g_scale);
        draw->AddLine(ImVec2(checkCenter.x - 1.0f * g_scale, checkCenter.y + 3.5f * g_scale),
                      ImVec2(checkCenter.x + 5.0f * g_scale, checkCenter.y - 3.5f * g_scale),
                      IM_COL32(14, 18, 12, 255), 2.0f * g_scale);
    } else {
        draw->AddCircle(checkCenter, 10.0f * g_scale, IM_COL32(255, 255, 255, 45), 24, 1.2f * g_scale);
    }

    if (clicked) app::SelectGame(index);
}

void DrawBusyOverlay(const AppUiState& state, float width, float height, float overlay) {
    if (overlay <= 0.003f) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = Clamp01(overlay);

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height),
                        IM_COL32(5, 6, 9, static_cast<int>(205 * alpha)));

    const ImVec2 cardSize(470.0f * g_scale, 252.0f * g_scale);
    const ImVec2 cardPos((width - cardSize.x) * 0.5f, (height - cardSize.y) * 0.5f);

    draw->AddRectFilled(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                        WithAlpha(IM_COL32(17, 21, 29, 255), static_cast<int>(252 * alpha)),
                        18.0f * g_scale);
    draw->AddRect(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                  WithAlpha(kAccent, static_cast<int>(45 * alpha)), 18.0f * g_scale, 0, 1.0f);

    const ImVec2 center(cardPos.x + cardSize.x * 0.5f, cardPos.y + 64.0f * g_scale);
    const float time = static_cast<float>(ImGui::GetTime());
    const float orbit = time * 1.9f;
    const float loaderScale = 0.88f + 0.12f * (0.5f + 0.5f * std::sin(time * 2.6f));

    for (int i = 0; i < 4; ++i) {
        const float angle = orbit + static_cast<float>(i) * 1.5707963f;
        const ImVec2 dot(center.x + std::cos(angle) * 38.0f * g_scale,
                         center.y + std::sin(angle) * 38.0f * g_scale);
        draw->AddCircleFilled(dot, (2.6f + static_cast<float>(3 - i) * 0.6f) * g_scale,
                              WithAlpha(kAccent, static_cast<int>((110 + i * 28) * alpha)), 16);
    }

    draw->AddCircleFilled(center, 30.0f * g_scale * loaderScale,
                          WithAlpha(kAccent, static_cast<int>(16 * alpha)), 40);
    ID3D11ShaderResourceView* busyLogo = images::Srv(images::VcLogo);
    if (busyLogo) {
        const float angle = time * 1.25f;
        const float half = 16.0f * g_scale * loaderScale;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        auto corner = [&](float dx, float dy) {
            return ImVec2(center.x + dx * c - dy * s, center.y + dx * s + dy * c);
        };
        draw->AddImageQuad(TexId(busyLogo),
                           corner(-half, -half), corner(half, -half),
                           corner(half, half), corner(-half, half),
                           ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                           WithAlpha(IM_COL32_WHITE, static_cast<int>(245 * alpha)));
    } else {
        DrawSpinner(draw, center, 20.0f * g_scale, 3.2f * g_scale,
                    WithAlpha(kAccent, static_cast<int>(235 * alpha)), time);
    }

    TextCentered(draw, g_fonts.title, 20.0f * g_scale, ImVec2(cardPos.x + cardSize.x * 0.5f, 0),
                 cardPos.y + 108.0f * g_scale, WithAlpha(kText, static_cast<int>(255 * alpha)),
                 state.phase.empty() ? "Working" : state.phase);

    const std::string detail = state.detail.empty() ? "Please wait..." : Ellipsize(state.detail, 64);
    TextCentered(draw, g_fonts.body, 11.5f * g_scale, ImVec2(cardPos.x + cardSize.x * 0.5f, 0),
                 cardPos.y + 140.0f * g_scale, WithAlpha(kTextDim, static_cast<int>(255 * alpha)), detail);

    // Progress bar
    const float barW = 380.0f * g_scale;
    const float barH = 7.0f * g_scale;
    const ImVec2 barPos(cardPos.x + (cardSize.x - barW) * 0.5f, cardPos.y + 176.0f * g_scale);

    draw->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                        WithAlpha(IM_COL32(255, 255, 255, 255), static_cast<int>(18 * alpha)),
                        barH * 0.5f);

    const float fill = state.indeterminate ? 0.35f : Clamp01(state.progress);
    float fillX = barPos.x;
    float fillW = barW * fill;

    if (state.indeterminate) {
        const float cycle = std::fmod(time * 0.55f, 1.0f);
        fillX = barPos.x + (barW - fillW) * cycle;
    }

    draw->AddRectFilled(ImVec2(fillX, barPos.y), ImVec2(fillX + fillW, barPos.y + barH),
                        WithAlpha(kAccent, static_cast<int>(255 * alpha)), barH * 0.5f);

    TextCentered(draw, g_fonts.body, 9.5f * g_scale, ImVec2(cardPos.x + cardSize.x * 0.5f, 0),
                 cardPos.y + 206.0f * g_scale,
                 WithAlpha(kTextFaint, static_cast<int>(255 * alpha)),
                 "Do not close VcDex while this runs");
}

void DrawToast(const AppUiState& state, float width, float height, bool busy) {
    static float toastSlide = 0.0f;
    const bool visible = !state.toast.empty() && state.toastTimer > 0.0f;
    toastSlide = Approach(toastSlide, visible ? 1.0f : 0.0f, 18.0f,
                          ImGui::GetIO().DeltaTime);
    if (!visible) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    float alpha = 1.0f;
    if (state.toastTimer < 0.6f) alpha = Clamp01(state.toastTimer / 0.6f);
    if (busy) alpha *= 0.35f;

    const ImU32 accent = state.toastError ? kRed : kGreen;
    const std::string message = Ellipsize(state.toast, 82);
    const ImVec2 measure = Measure(g_fonts.body, 12.0f * g_scale, message);

    const float w = std::min(width - 80.0f * g_scale, measure.x + 52.0f * g_scale);
    const float h = 40.0f * g_scale;
    const float slide = (1.0f - EaseOutCubic(toastSlide)) * 20.0f * g_scale;
    const ImVec2 pos((width - w) * 0.5f, height - 158.0f * g_scale + slide);

    draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                        WithAlpha(IM_COL32(20, 25, 33, 255), static_cast<int>(245 * alpha)),
                        10.0f * g_scale);
    draw->AddRect(pos, ImVec2(pos.x + w, pos.y + h),
                  WithAlpha(accent, static_cast<int>(110 * alpha)), 10.0f * g_scale, 0, 1.0f);

    draw->AddCircleFilled(ImVec2(pos.x + 20.0f * g_scale, pos.y + h * 0.5f),
                          4.0f * g_scale, WithAlpha(accent, static_cast<int>(255 * alpha)), 12);
    DrawTextAt(draw, g_fonts.body, 12.0f * g_scale,
             ImVec2(pos.x + 32.0f * g_scale,
                    pos.y + (h - Measure(g_fonts.body, 12.0f * g_scale, message).y) * 0.5f - 1.0f),
             WithAlpha(kText, static_cast<int>(255 * alpha)), message);
}

void DrawSplash(const AppUiState& state, float width, float height, float alpha) {
    if (alpha <= 0.003f) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float a = Clamp01(alpha);
    auto A = [a](int value) { return static_cast<int>(value * a); };

    const float t = g_bootElapsed;

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), WithAlpha(kBg, A(255)));

    // Soft animated orbs behind the logo.
    const float driftX = std::sin(t * 0.55f) * width * 0.06f;
    const float driftY = std::cos(t * 0.42f) * height * 0.05f;
    draw->AddCircleFilled(ImVec2(width * 0.52f + driftX, height * 0.40f + driftY),
                          width * 0.30f, WithAlpha(kAccent, A(11)), 96);
    draw->AddCircleFilled(ImVec2(width * 0.46f - driftX, height * 0.58f - driftY),
                          width * 0.26f, WithAlpha(kViolet, A(13)), 96);

    const ImVec2 center(width * 0.5f, height * 0.40f);
    const float base = 66.0f * g_scale;
    const float pulse = 0.5f + 0.5f * std::sin(t * 2.4f);

    draw->AddCircle(center, base + 24.0f * g_scale,
                    WithAlpha(kAccent, A(22 + static_cast<int>(22.0f * pulse))),
                    96, 1.4f * g_scale);
    draw->AddCircle(center, base + 36.0f * g_scale, WithAlpha(kViolet, A(16)), 96,
                    1.0f * g_scale);

    for (int i = 0; i < 3; ++i) {
        const float start = t * 1.55f + static_cast<float>(i) * 2.094f;
        draw->PathArcTo(center, base + 24.0f * g_scale, start, start + 1.05f, 48);
        draw->PathStroke(WithAlpha(i == 0 ? kAccent : kViolet, A(150 - i * 38)), 0,
                         2.2f * g_scale);
    }

    // Rotating VcDex logo.
    ID3D11ShaderResourceView* logoSrv = images::Srv(images::VcLogo);
    if (logoSrv) {
        const float angle = t * 1.35f;
        const float half = base * 0.88f;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        auto corner = [&](float dx, float dy) {
            return ImVec2(center.x + dx * c - dy * s, center.y + dx * s + dy * c);
        };
        draw->AddImageQuad(TexId(logoSrv),
                           corner(-half, -half), corner(half, -half),
                           corner(half, half), corner(-half, half),
                           ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                           WithAlpha(IM_COL32_WHITE, A(255)));
    } else {
        TextCentered(draw, g_fonts.title, 52.0f * g_scale, ImVec2(width * 0.5f, 0),
                     center.y - 30.0f * g_scale, WithAlpha(kAccent, A(255)), "V");
    }

    TextCentered(draw, g_fonts.title, 34.0f * g_scale, ImVec2(width * 0.5f, 0),
                 center.y + base + 38.0f * g_scale, WithAlpha(kText, A(255)), "VcDex");

    const std::string sub = "LOADER";
    float subWidth = 0.0f;
    for (char ch : sub)
        subWidth += Measure(g_fonts.medium, 10.0f * g_scale, std::string(1, ch)).x +
                    4.0f * g_scale;
    subWidth -= 4.0f * g_scale;
    DrawSpaced(draw, g_fonts.medium, 10.0f * g_scale,
               ImVec2(width * 0.5f - subWidth * 0.5f, center.y + base + 82.0f * g_scale),
               WithAlpha(kTextFaint, A(255)), sub, 4.0f * g_scale);

    const std::string detail = Ellipsize(
        state.detail.empty() ? "Preparing VcDex..." : state.detail, 72);
    TextCentered(draw, g_fonts.body, 11.5f * g_scale, ImVec2(width * 0.5f, 0),
                 center.y + base + 108.0f * g_scale, WithAlpha(kTextDim, A(255)), detail);

    const float barW = 300.0f * g_scale;
    const float barH = 5.0f * g_scale;
    const ImVec2 barPos((width - barW) * 0.5f, center.y + base + 136.0f * g_scale);

    draw->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                        WithAlpha(IM_COL32(255, 255, 255, 255), A(18)), barH * 0.5f);

    const float fill = state.indeterminate ? 0.30f : Clamp01(state.progress);
    float fillX = barPos.x;
    const float fillW = barW * fill;
    if (state.indeterminate) {
        const float cycle = std::fmod(t * 0.55f, 1.0f);
        fillX = barPos.x + (barW - fillW) * cycle;
    }
    draw->AddRectFilled(ImVec2(fillX, barPos.y), ImVec2(fillX + fillW, barPos.y + barH),
                        WithAlpha(kAccent, A(255)), barH * 0.5f);

    const std::string version = "VCDEX LOADER   v" + state.localVersion;
    TextCentered(draw, g_fonts.medium, 9.0f * g_scale, ImVec2(width * 0.5f, 0),
                 height - 54.0f * g_scale, WithAlpha(kTextFaint, A(255)), version);
    TextCentered(draw, g_fonts.body, 9.5f * g_scale, ImVec2(width * 0.5f, 0),
                 height - 36.0f * g_scale, WithAlpha(IM_COL32(72, 80, 94, 255), A(255)),
                 "made by Laynex");
}

void DrawUi() {
    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float dt = io.DeltaTime;

    const AppUiState state = app::Snapshot();
    const bool busy = state.busy;
    const bool working = busy || state.backgroundSync;
    const float scale = g_scale;

    static float coverRefresh = 0.0f;
    coverRefresh += dt;
    if (coverRefresh > 0.4f) {
        coverRefresh = 0.0f;
        SyncCoverTextures(state);
    }

    g_bootElapsed += dt;

    // Auto-apply updates during the boot splash, as requested.
    static bool autoUpdateStarted = false;
    if (!autoUpdateStarted && state.updateAvailable && !state.checking && !state.busy &&
        g_bootElapsed > 1.2f) {
        autoUpdateStarted = true;
        app::StartSelfUpdate();
    }

    const bool splashWanted = state.checking || g_bootElapsed < 1.8f ||
        (autoUpdateStarted && (state.busy || !state.toastError));

    static float splashAlpha = 1.0f;
    splashAlpha = Approach(splashAlpha, splashWanted ? 1.0f : 0.0f, 6.0f, dt);
    const bool booting = splashAlpha > 0.45f;

    static float overlay = 0.0f;
    overlay = Approach(overlay, busy ? 1.0f : 0.0f, 14.0f, dt);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##vcdx_root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), kBg);

    // Slow drifting aurora blobs.
    const float bgTime = static_cast<float>(ImGui::GetTime());
    const float bgX = std::sin(bgTime * 0.35f) * width * 0.05f;
    const float bgY = std::cos(bgTime * 0.27f) * height * 0.05f;
    draw->AddCircleFilled(ImVec2(width * 0.78f + bgX, -height * 0.15f + bgY),
                          width * 0.42f, IM_COL32(126, 96, 255, 9), 96);
    draw->AddCircleFilled(ImVec2(width * 0.16f - bgX, height * 1.12f - bgY),
                          width * 0.36f, IM_COL32(120, 230, 90, 7), 96);

    const float sidebarW = 222.0f * scale;

    DrawSidebar(state, sidebarW, height);

    // Window drag area + controls
    ImGui::SetCursorScreenPos(ImVec2(sidebarW, 0));
    ImGui::InvisibleButton("##title_drag", ImVec2(width - sidebarW - 140.0f * scale, 64.0f * scale));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ReleaseCapture();
        SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    DrawWindowControls(width);

    const float contentX = sidebarW + 30.0f * scale;
    const float contentW = width - contentX - 30.0f * scale;

    DrawTextAt(draw, g_fonts.title, 25.0f * scale,
             ImVec2(contentX, 30.0f * scale), kText, "Game Library");
    DrawTextAt(draw, g_fonts.body, 11.5f * scale,
             ImVec2(contentX, 64.0f * scale), kTextDim,
             "Pick a game, then press Load Game. VcDex handles the rest.");

    float contentY = 96.0f * scale;
    if (state.updateAvailable) {
        DrawUpdateBanner(state, contentX, contentW, contentY, working || booting);
        contentY += 66.0f * scale;
    }

    // Cards grid
    const float gap = 16.0f * scale;
    const int columns = 2;
    const float cardW = (contentW - gap * (columns - 1)) / static_cast<float>(columns);
    const float cardH = 190.0f * scale;

    if (state.games.empty()) {
        const ImVec2 emptyPos(contentX, contentY + 20.0f * scale);
        draw->AddRectFilled(emptyPos, ImVec2(contentX + contentW, contentY + 160.0f * scale),
                            IM_COL32(255, 255, 255, 6), 14.0f * scale);
        DrawTextAt(draw, g_fonts.medium, 13.0f * scale,
                 ImVec2(emptyPos.x + 22.0f * scale, emptyPos.y + 22.0f * scale),
                 kTextDim, "Game list is empty");
        DrawTextAt(draw, g_fonts.body, 11.0f * scale,
                 ImVec2(emptyPos.x + 22.0f * scale, emptyPos.y + 46.0f * scale),
                 kTextFaint, "Check your connection or try again in a moment.");
    }

    for (int i = 0; i < static_cast<int>(state.games.size()); ++i) {
        const int column = i % columns;
        const int row = i / columns;
        const ImVec2 pos(contentX + column * (cardW + gap),
                         contentY + row * (cardH + gap));
        if (pos.y + cardH > height - 112.0f * scale) break;

        DrawGameCard(state.games[static_cast<std::size_t>(i)], i, pos,
                     ImVec2(cardW, cardH), state.selected == i, working || booting);
    }

    // Footer
    draw->AddLine(ImVec2(contentX, height - 100.0f * scale),
                  ImVec2(width - 30.0f * scale, height - 100.0f * scale),
                  IM_COL32(255, 255, 255, 12), 1.0f);

    const bool hasSelection = !state.games.empty();
    const GameEntry* selectedGame = hasSelection
        ? &state.games[static_cast<std::size_t>(
              std::clamp(state.selected, 0, static_cast<int>(state.games.size()) - 1))]
        : nullptr;

    if (selectedGame) {
        DrawTextAt(draw, g_fonts.medium, 14.0f * scale,
                 ImVec2(contentX, height - 79.0f * scale), kText, selectedGame->name);
        DrawTextAt(draw, g_fonts.body, 10.5f * scale,
                 ImVec2(contentX, height - 59.0f * scale), kTextDim,
                 selectedGame->ready ? "All files ready - one click to inject"
                                     : "Files will be downloaded before injection");
    }

    const float loadW = 208.0f * scale;
    const float loadH = 50.0f * scale;
    const ImVec2 loadPos(width - 30.0f * scale - loadW, height - 75.0f * scale);

    const bool canLoad = hasSelection && !working && !booting && selectedGame != nullptr;

    if (CustomButton("##check_files",
                     ImVec2(loadPos.x - 130.0f * scale - 10.0f * scale -
                                124.0f * scale - 10.0f * scale,
                            loadPos.y + 7.0f * scale),
                     ImVec2(124.0f * scale, 36.0f * scale), "CHECK FILES",
                     IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 16),
                     kTextDim, g_fonts.medium, 9.5f * scale, 9.0f * scale,
                     !working && !booting)) {
        app::CheckFiles();
    }

    if (CustomButton("##open_folder", ImVec2(loadPos.x - 130.0f * scale - 10.0f * scale,
                                             loadPos.y + 7.0f * scale),
                     ImVec2(130.0f * scale, 36.0f * scale), "OPEN FOLDER",
                     IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 16),
                     kTextDim, g_fonts.medium, 9.5f * scale, 9.0f * scale,
                     hasSelection && !booting)) {
        app::OpenSelectedFolder();
    }

    if (CustomButton("##load_game", loadPos, ImVec2(loadW, loadH), "LOAD GAME",
                     kAccent, Mix(kAccent, IM_COL32(255, 255, 255, 255), 0.20f),
                     IM_COL32(16, 22, 10, 255), g_fonts.title, 14.0f * scale,
                     12.0f * scale, canLoad)) {
        app::LoadSelectedGame();
    }

    // Keyboard shortcut
    if (!working && !booting && hasSelection && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        app::LoadSelectedGame();

    if (!working && !booting && ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (state.updateAvailable)
            app::StartSelfUpdate();
        else
            app::CheckFiles();
    }

    // Hidden Settings placeholder hit test is part of the sidebar.

    if (!booting) {
        DrawToast(state, width, height, busy);
        DrawBusyOverlay(state, width, height, overlay);
    }

    if (splashAlpha > 0.003f)
        DrawSplash(state, width, height, splashAlpha);

    ImGui::End();
}

// ---------------------------------------------------------------- d3d

bool CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&backBuffer))) || !backBuffer)
        return false;

    const HRESULT hr = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();
    return SUCCEEDED(hr);
}

void DestroyRenderTarget() {
    if (g_renderTarget) {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

bool CreateDevice(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = static_cast<UINT>(width);
    desc.BufferDesc.Height = static_cast<UINT>(height);
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 0;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &desc, &g_swapChain, &g_device, &featureLevel, &g_context);

    if (FAILED(hr)) return false;
    return CreateRenderTarget();
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    const std::string dir = "C:\\Windows\\Fonts\\";
    const std::string regular = dir + "segoeui.ttf";
    const std::string bold = dir + "segoeuib.ttf";
    const std::string semibold = dir + "seguisb.ttf";

    const bool hasRegular = GetFileAttributesA(regular.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasBold = GetFileAttributesA(bold.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasSemi = GetFileAttributesA(semibold.c_str()) != INVALID_FILE_ATTRIBUTES;

    auto load = [&](const std::string& path, float size) -> ImFont* {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = true;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size * g_scale, &fontConfig);
    };

    if (hasRegular) {
        g_fonts.caption = load(regular, 13.0f);
        g_fonts.body = load(regular, 15.0f);
    }
    if (hasSemi) {
        g_fonts.medium = load(semibold, 14.0f);
    } else if (hasBold) {
        g_fonts.medium = load(bold, 13.0f);
    }
    if (hasBold) {
        g_fonts.title = load(bold, 18.0f);
        g_fonts.icon = load(bold, 24.0f);
    }

    if (!g_fonts.body) g_fonts.body = io.Fonts->AddFontDefault();
    if (!g_fonts.caption) g_fonts.caption = g_fonts.body;
    if (!g_fonts.medium) g_fonts.medium = g_fonts.body;
    if (!g_fonts.title) g_fonts.title = g_fonts.body;
    if (!g_fonts.icon) g_fonts.icon = g_fonts.title;

    io.FontDefault = g_fonts.body;
}

void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(8, 8);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.ScaleAllSizes(g_scale);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.97f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.05f, 0.07f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_Button] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.18f);
    colors[ImGuiCol_Header] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
}

// ---------------------------------------------------------------- window

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
        return true;

    switch (message) {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            if (width > 0 && height > 0) {
                DestroyRenderTarget();
                g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        g_running = false;
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const float systemDpi = static_cast<float>(GetDpiForSystem());
    g_scale = systemDpi / 96.0f;
    if (g_scale < 1.0f) g_scale = 1.0f;

    const int windowW = static_cast<int>(1060.0f * g_scale);
    const int windowH = static_cast<int>(700.0f * g_scale);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = L"VcDexLoaderWindow";
    windowClass.hbrBackground = nullptr;

    RegisterClassExW(&windowClass);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        windowClass.lpszClassName,
        L"VcDex Loader",
        WS_POPUP | WS_MINIMIZEBOX,
        (screenW - windowW) / 2,
        (screenH - windowH) / 2,
        windowW, windowH,
        nullptr, nullptr, instance, nullptr);

    if (!g_hwnd) return 1;

    // Rounded corners on Windows 11, dark border.
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    if (!CreateDevice(g_hwnd, windowW, windowH)) {
        MessageBoxW(g_hwnd, L"DirectX 11 baslatilamadi.", L"VcDex Loader", MB_OK | MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    LoadFonts();
    SetupStyle();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    images::Init(g_device);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    app::Init(g_hwnd);

    MSG message;
    while (g_running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) g_running = false;
        }

        if (!g_running) break;
        if (app::ExitRequested()) {
            g_running = false;
            break;
        }

        if (IsIconic(g_hwnd)) {
            Sleep(16);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        app::Tick(io.DeltaTime);

        ImGui::NewFrame();
        DrawUi();
        ImGui::Render();

        const float clearColor[4] = { 0.035f, 0.043f, 0.063f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapChain->Present(1, 0);
    }

    images::Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyRenderTarget();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();

    DestroyWindow(g_hwnd);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return 0;
}
