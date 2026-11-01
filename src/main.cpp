#include "app.hpp"
#include "config.hpp"

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
    draw->AddRectFilled(logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize),
                        WithAlpha(kAccent, 30), 11.0f * g_scale);
    draw->AddRect(logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize),
                  WithAlpha(kAccent, 120), 11.0f * g_scale, 0, 1.0f);

    const std::string mark = "V";
    const ImVec2 markSize = Measure(g_fonts.icon, 22.0f * g_scale, mark);
    DrawTextAt(draw, g_fonts.icon, 22.0f * g_scale,
             ImVec2(logoPos.x + (logoSize - markSize.x) * 0.5f,
                    logoPos.y + (logoSize - markSize.y) * 0.5f - 1.0f * g_scale),
             kAccent, mark);

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

    draw->AddCircleFilled(ImVec2(x + 20.0f * g_scale, y + height * 0.5f),
                          4.0f * g_scale, kViolet, 12);

    DrawTextAt(draw, g_fonts.medium, 12.5f * g_scale,
             ImVec2(x + 32.0f * g_scale, y + 10.0f * g_scale), kText, "Update available");
    DrawTextAt(draw, g_fonts.body, 10.5f * g_scale,
             ImVec2(x + 32.0f * g_scale, y + 28.0f * g_scale), kTextDim,
             "v" + state.localVersion + "  ->  v" + state.remoteVersion);

    const float buttonW = 172.0f * g_scale;
    const float buttonH = 32.0f * g_scale;
    CustomButton("##update_now", ImVec2(x + width - buttonW - 12.0f * g_scale,
                                        y + (height - buttonH) * 0.5f),
                 ImVec2(buttonW, buttonH), "RESTART & UPDATE",
                 kViolet, Mix(kViolet, IM_COL32(255, 255, 255, 255), 0.18f),
                 IM_COL32(18, 14, 28, 255), g_fonts.medium, 10.5f * g_scale,
                 8.0f * g_scale, !busy);
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

    // Icon tile
    const float tile = 72.0f * g_scale;
    const ImVec2 tilePos(pos.x + 22.0f * g_scale, pos.y + 24.0f * g_scale);
    draw->AddRectFilled(tilePos, ImVec2(tilePos.x + tile, tilePos.y + tile),
                        WithAlpha(accent, 34), 15.0f * g_scale);
    draw->AddRect(tilePos, ImVec2(tilePos.x + tile, tilePos.y + tile),
                  WithAlpha(accent, 105), 15.0f * g_scale, 0, 1.2f * g_scale);

    const std::string initials = InitialsFor(game);
    const ImVec2 initialsSize = Measure(g_fonts.icon, 26.0f * g_scale, initials);
    DrawTextAt(draw, g_fonts.icon, 26.0f * g_scale,
             ImVec2(tilePos.x + (tile - initialsSize.x) * 0.5f,
                    tilePos.y + (tile - initialsSize.y) * 0.5f - 2.0f * g_scale),
             accent, initials);

    // Text block
    const float textX = tilePos.x + tile + 18.0f * g_scale;
    DrawTextAt(draw, g_fonts.title, 19.0f * g_scale,
             ImVec2(textX, pos.y + 24.0f * g_scale), kText, game.name);
    DrawTextAt(draw, g_fonts.body, 11.5f * g_scale,
             ImVec2(textX, pos.y + 54.0f * g_scale), kTextDim, game.subtitle);

    DrawStatusPill(draw, ImVec2(textX, pos.y + 82.0f * g_scale),
                   game.status, StatusColor(game.status));

    DrawSpaced(draw, g_fonts.medium, 8.5f * g_scale,
               ImVec2(textX, pos.y + 122.0f * g_scale), kTextFaint, game.tag, 1.6f * g_scale);

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

    draw->AddCircleFilled(center, 30.0f * g_scale, WithAlpha(kAccent, static_cast<int>(16 * alpha)), 40);
    DrawSpinner(draw, center, 20.0f * g_scale, 3.2f * g_scale,
                WithAlpha(kAccent, static_cast<int>(235 * alpha)), time);

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
    if (state.toast.empty() || state.toastTimer <= 0.0f) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    float alpha = 1.0f;
    if (state.toastTimer < 0.6f) alpha = Clamp01(state.toastTimer / 0.6f);
    if (busy) alpha *= 0.35f;

    const ImU32 accent = state.toastError ? kRed : kGreen;
    const std::string message = Ellipsize(state.toast, 82);
    const ImVec2 measure = Measure(g_fonts.body, 12.0f * g_scale, message);

    const float w = std::min(width - 80.0f * g_scale, measure.x + 52.0f * g_scale);
    const float h = 40.0f * g_scale;
    const ImVec2 pos((width - w) * 0.5f, height - 158.0f * g_scale);

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

void DrawUi() {
    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float dt = io.DeltaTime;

    const AppUiState state = app::Snapshot();
    const bool busy = state.busy;
    const float scale = g_scale;

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
    draw->AddCircleFilled(ImVec2(width * 0.78f, -height * 0.15f), width * 0.42f,
                          IM_COL32(126, 96, 255, 9), 96);
    draw->AddCircleFilled(ImVec2(width * 0.16f, height * 1.12f), width * 0.36f,
                          IM_COL32(120, 230, 90, 7), 96);

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
        DrawUpdateBanner(state, contentX, contentW, contentY, busy);
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
                     ImVec2(cardW, cardH), state.selected == i, busy);
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

    const bool canLoad = hasSelection && !busy && selectedGame != nullptr;

    if (CustomButton("##open_folder", ImVec2(loadPos.x - 130.0f * scale - 10.0f * scale,
                                             loadPos.y + 7.0f * scale),
                     ImVec2(130.0f * scale, 36.0f * scale), "OPEN FOLDER",
                     IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 16),
                     kTextDim, g_fonts.medium, 9.5f * scale, 9.0f * scale, hasSelection)) {
        app::OpenSelectedFolder();
    }

    if (CustomButton("##load_game", loadPos, ImVec2(loadW, loadH), "LOAD GAME",
                     kAccent, Mix(kAccent, IM_COL32(255, 255, 255, 255), 0.20f),
                     IM_COL32(16, 22, 10, 255), g_fonts.title, 14.0f * scale,
                     12.0f * scale, canLoad)) {
        app::LoadSelectedGame();
    }

    // Keyboard shortcut
    if (!busy && hasSelection && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        app::LoadSelectedGame();

    if (!busy && state.updateAvailable && ImGui::IsKeyPressed(ImGuiKey_F5, false))
        app::StartSelfUpdate();

    // Hidden Settings placeholder hit test is part of the sidebar.

    DrawToast(state, width, height, busy);
    DrawBusyOverlay(state, width, height, overlay);

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
        WS_POPUP,
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
