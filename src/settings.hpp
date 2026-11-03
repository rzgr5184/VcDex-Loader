#pragma once

#include "theme.hpp"

#include <string>

// Persistent user preferences stored in <data>/settings.json.
//
// The file is written atomically (tmp + MoveFileExW) and is intentionally not
// part of the updater manifest, so automatic updates never overwrite it.
namespace settings {

struct Data {
    theme::Id theme = theme::Id::Lagoon;

    bool animations = true;
    bool particles = true;
    bool parallax = true;
    bool reduceMotion = false;

    int intensity = 70;   // 0..100 backdrop strength
    bool blurHigh = true; // glass blur quality

    int windowWidth = 1180;
    int windowHeight = 760;

    std::string lastGame;
};

Data& Get();

void Load();
bool Save();

// Pushes the loaded theme into the theme manager instantly.
void ApplyToTheme();

} // namespace settings
