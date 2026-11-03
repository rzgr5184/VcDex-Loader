#include "app.hpp"
#include "backdrop.hpp"
#include "config.hpp"
#include "images.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "ui_pages.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;

HWND g_hwnd = nullptr;
bool g_running = true;
bool g_imguiReady = false;
float g_scale = 1.0f;

ui::Fonts g_fonts;

// Click ripple fed to the liquid shader.
ImVec2 g_ripple(0.5f, 0.5f);
float g_rippleClock = -10.0f;
float g_clock = 0.0f;

// ---------------------------------------------------------------- d3d

bool CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&backBuffer))) ||
        !backBuffer)
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
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc, &g_swapChain,
        &g_device, &featureLevel, &g_context);

    if (FAILED(hr)) return false;
    return CreateRenderTarget();
}

// ---------------------------------------------------------------- fonts

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Turkish glyphs are outside the default Latin range, so ask for them.
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x011E, 0x011F, 0x0130, 0x0131, 0x015E, 0x015F,
        0x0162, 0x0163, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D,
        0x2022, 0x2026, 0x2192, 0x2192, 0
    };

    const std::string dir = "C:\\Windows\\Fonts\\";
    const std::string regular = dir + "segoeui.ttf";
    const std::string semibold = dir + "seguisb.ttf";
    const std::string bold = dir + "segoeuib.ttf";

    const bool hasRegular = GetFileAttributesA(regular.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasSemi = GetFileAttributesA(semibold.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasBold = GetFileAttributesA(bold.c_str()) != INVALID_FILE_ATTRIBUTES;

    auto load = [&](const std::string& path, float size) -> ImFont* {
        if (path.empty()) return nullptr;
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = true;
        fontConfig.GlyphRanges = ranges;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size * g_scale, &fontConfig);
    };

    g_fonts.caption = nullptr;
    g_fonts.body = nullptr;
    g_fonts.medium = nullptr;
    g_fonts.title = nullptr;
    g_fonts.display = nullptr;
    g_fonts.icon = nullptr;

    if (hasRegular) {
        g_fonts.caption = load(regular, 11.5f);
        g_fonts.body = load(regular, 14.0f);
    }
    if (hasSemi) {
        g_fonts.medium = load(semibold, 14.0f);
    } else if (hasBold) {
        g_fonts.medium = load(bold, 13.0f);
    }
    if (hasBold) {
        g_fonts.title = load(bold, 20.0f);
        g_fonts.display = load(bold, 34.0f);
        g_fonts.icon = load(bold, 22.0f);
    }

    if (!g_fonts.body) g_fonts.body = io.Fonts->AddFontDefault();
    if (!g_fonts.caption) g_fonts.caption = g_fonts.body;
    if (!g_fonts.medium) g_fonts.medium = g_fonts.body;
    if (!g_fonts.title) g_fonts.title = g_fonts.body;
    if (!g_fonts.display) g_fonts.display = g_fonts.title;
    if (!g_fonts.icon) g_fonts.icon = g_fonts.title;

    io.FontDefault = g_fonts.body;
    ui::SetFonts(g_fonts);
    ui::SetScale(g_scale);
}

void RebuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplDX11_InvalidateDeviceObjects();
    io.Fonts->Clear();
    LoadFonts();
    io.Fonts->Build();
    ImGui_ImplDX11_CreateDeviceObjects();
}

void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(8, 5);
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

    style.Colors[ImGuiCol_Text] = ImVec4(0.94f, 0.99f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.01f, 0.05f, 0.07f, 1.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.02f, 0.08f, 0.11f, 0.96f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.40f, 0.94f, 1.00f, 0.18f);
    style.Colors[ImGuiCol_Button] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.18f);
    style.Colors[ImGuiCol_Header] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.18f, 0.83f, 0.75f, 0.35f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.35f, 0.93f, 1.00f, 0.70f);
}

// ---------------------------------------------------------------- window

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_imguiReady && ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
        return true;

    switch (message) {
    case WM_SIZE:
        if (g_device && g_swapChain && wParam != SIZE_MINIMIZED) {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            if (width > 0 && height > 0) {
                DestroyRenderTarget();
                g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
                backdrop::Resize(static_cast<int>(width), static_cast<int>(height));
            }
        }
        if (wParam != SIZE_MINIMIZED && g_scale > 0.0f) {
            settings::Get().windowWidth =
                static_cast<int>(static_cast<float>(LOWORD(lParam)) / g_scale);
            settings::Get().windowHeight =
                static_cast<int>(static_cast<float>(HIWORD(lParam)) / g_scale);
        }
        return 0;

    case WM_DPICHANGED:
        if (g_imguiReady) {
            g_scale = static_cast<float>(HIWORD(wParam)) / 96.0f;
            if (g_scale < 1.0f) g_scale = 1.0f;
            ui::SetScale(g_scale);
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            RebuildFonts();
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        if (info) {
            info->ptMinTrackSize.x = static_cast<LONG>(1024.0f * g_scale);
            info->ptMinTrackSize.y = static_cast<LONG>(740.0f * g_scale);
        }
        return 0;
    }

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

    app::InitPaths();
    settings::Load();
    settings::ApplyToTheme();
    theme::Update(1.0f);

    const int windowW = static_cast<int>(
        std::clamp(settings::Get().windowWidth, 1024, 3840) * g_scale);
    const int windowH = static_cast<int>(
        std::clamp(settings::Get().windowHeight, 740, 2160) * g_scale);

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
        WS_EX_APPWINDOW, windowClass.lpszClassName, L"VcDex Loader",
        WS_POPUP | WS_MINIMIZEBOX, (screenW - windowW) / 2, (screenH - windowH) / 2, windowW,
        windowH, nullptr, nullptr, instance, nullptr);

    if (!g_hwnd) return 1;

    // Rounded corners on Windows 11; silently ignored on older versions.
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
    g_imguiReady = true;

    images::Init(g_device);
    backdrop::Init(g_device, g_context);

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

        const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
        g_clock += dt;

        theme::Update(dt);
        app::Tick(dt);

        // The click ripple is a shader uniform, so capture clicks before the
        // ImGui frame consumes them.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.DisplaySize.x > 0.0f &&
            io.DisplaySize.y > 0.0f) {
            g_ripple = ImVec2(io.MousePos.x / io.DisplaySize.x,
                              io.MousePos.y / io.DisplaySize.y);
            g_rippleClock = g_clock;
        }

        backdrop::Resize(static_cast<int>(io.DisplaySize.x),
                         static_cast<int>(io.DisplaySize.y));

        backdrop::Params params;
        params.time = g_clock;
        params.mouse = io.DisplaySize.x > 0.0f
                           ? ImVec2(io.MousePos.x / io.DisplaySize.x,
                                    io.MousePos.y / io.DisplaySize.y)
                           : ImVec2(0.5f, 0.5f);
        params.intensity = settings::Get().animations
                               ? static_cast<float>(settings::Get().intensity) / 100.0f
                               : 0.35f;
        params.caustic = settings::Get().animations ? 0.7f : 0.15f;
        params.grain = settings::Get().animations ? 0.030f : 0.0f;
        params.highQuality = settings::Get().blurHigh;
        params.reduceMotion = settings::Get().reduceMotion || !settings::Get().animations;
        params.ripple = g_ripple;
        params.rippleAge = g_clock - g_rippleClock;
        if (!settings::Get().animations) params.time = 0.0f;

        backdrop::Render(params, theme::Current());

        ImGui::NewFrame();
        pages::Draw(app::Snapshot(), g_hwnd);
        ImGui::Render();

        const float clearColor[4] = {0.012f, 0.055f, 0.075f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapChain->Present(1, 0);
    }

    settings::Save();

    images::Shutdown();
    backdrop::Shutdown();

    g_imguiReady = false;
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
