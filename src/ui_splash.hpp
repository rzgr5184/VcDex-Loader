#pragma once

#include "app.hpp"

// Full screen layers: splash, blocking busy overlay and toast notifications.
namespace splash {

void Draw(const AppUiState& state, float alpha, float bootElapsed);
void DrawBusy(const AppUiState& state, float overlay);
void DrawToast(const AppUiState& state);

} // namespace splash
