#pragma once

#include "app.hpp"

#include <windows.h>

// Page shell: custom title bar, navigation rail, footer and the two screens.
namespace pages {

void Draw(const AppUiState& state, HWND hwnd);

void ShowLibrary();
void ShowSettings();
bool IsSettings();

} // namespace pages

// Drawn by pages::Draw from ui_settings.cpp.
namespace settings_page {

void Draw(const AppUiState& state, float x, float y, float width, float height);

} // namespace settings_page
