#include "theme.hpp"

#include <algorithm>
#include <cstddef>

namespace theme {
namespace {

constexpr ImU32 Rgb(int r, int g, int b) {
    return IM_COL32(r, g, b, 255);
}

constexpr ImU32 Rgba(int r, int g, int b, int a) {
    return IM_COL32(r, g, b, a);
}

Colors Make(std::initializer_list<int> bg0, std::initializer_list<int> bg1,
            std::initializer_list<int> bg2, std::initializer_list<int> primary,
            std::initializer_list<int> primarySoft, std::initializer_list<int> secondary,
            std::initializer_list<int> accent, std::initializer_list<int> glow,
            std::initializer_list<int> blob0, std::initializer_list<int> blob1,
            std::initializer_list<int> blob2, std::initializer_list<int> text,
            std::initializer_list<int> textDim, std::initializer_list<int> textFaint,
            std::initializer_list<int> panelTint, std::initializer_list<int> panelBorder,
            std::initializer_list<int> success, std::initializer_list<int> warning,
            std::initializer_list<int> error) {
    auto at = [](std::initializer_list<int> list, std::size_t index) {
        return index < list.size() ? *(list.begin() + static_cast<std::ptrdiff_t>(index)) : 0;
    };

    Colors c;
    c.bg0 = Rgb(at(bg0, 0), at(bg0, 1), at(bg0, 2));
    c.bg1 = Rgb(at(bg1, 0), at(bg1, 1), at(bg1, 2));
    c.bg2 = Rgb(at(bg2, 0), at(bg2, 1), at(bg2, 2));

    c.primary = Rgb(at(primary, 0), at(primary, 1), at(primary, 2));
    c.primarySoft = Rgb(at(primarySoft, 0), at(primarySoft, 1), at(primarySoft, 2));
    c.secondary = Rgb(at(secondary, 0), at(secondary, 1), at(secondary, 2));
    c.accent = Rgb(at(accent, 0), at(accent, 1), at(accent, 2));
    c.glow = Rgb(at(glow, 0), at(glow, 1), at(glow, 2));

    c.blob0 = Rgb(at(blob0, 0), at(blob0, 1), at(blob0, 2));
    c.blob1 = Rgb(at(blob1, 0), at(blob1, 1), at(blob1, 2));
    c.blob2 = Rgb(at(blob2, 0), at(blob2, 1), at(blob2, 2));

    c.text = Rgb(at(text, 0), at(text, 1), at(text, 2));
    c.textDim = Rgb(at(textDim, 0), at(textDim, 1), at(textDim, 2));
    c.textFaint = Rgb(at(textFaint, 0), at(textFaint, 1), at(textFaint, 2));

    c.panelTint = Rgba(at(panelTint, 0), at(panelTint, 1), at(panelTint, 2),
                       at(panelTint, 3));
    c.panelBorder = Rgba(at(panelBorder, 0), at(panelBorder, 1), at(panelBorder, 2),
                         at(panelBorder, 3));

    c.success = Rgb(at(success, 0), at(success, 1), at(success, 2));
    c.warning = Rgb(at(warning, 0), at(warning, 1), at(warning, 2));
    c.error = Rgb(at(error, 0), at(error, 1), at(error, 2));
    return c;
}

const Definition kThemes[] = {
    {
        Id::Lagoon, "lagoon", "Lagoon", "Turquoise depths",
        Make(
            {3, 20, 27}, {6, 36, 48}, {10, 51, 64},
            {45, 212, 191}, {94, 234, 212}, {56, 189, 248},
            {234, 253, 255}, {34, 211, 238},
            {20, 184, 166}, {14, 165, 233}, {103, 232, 249},
            {241, 253, 255}, {166, 196, 206}, {98, 130, 141},
            {210, 255, 252, 16}, {160, 240, 255, 46},
            {52, 211, 153}, {251, 191, 36}, {251, 113, 133})
    },
    {
        Id::Aurora, "aurora", "Aurora", "Violet night",
        Make(
            {8, 5, 18}, {18, 10, 36}, {27, 16, 53},
            {167, 139, 250}, {196, 181, 253}, {34, 211, 238},
            {245, 208, 254}, {139, 92, 246},
            {124, 58, 237}, {34, 211, 238}, {236, 72, 153},
            {247, 243, 255}, {181, 168, 207}, {110, 98, 136},
            {230, 220, 255, 16}, {196, 181, 253, 51},
            {52, 211, 153}, {251, 191, 36}, {251, 113, 133})
    },
    {
        Id::Ember, "ember", "Ember", "Molten sunset",
        Make(
            {20, 8, 5}, {35, 16, 10}, {51, 22, 13},
            {251, 146, 60}, {253, 186, 116}, {244, 63, 94},
            {253, 230, 138}, {249, 115, 22},
            {249, 115, 22}, {244, 63, 94}, {250, 204, 21},
            {255, 246, 239}, {204, 171, 155}, {138, 106, 92},
            {255, 230, 210, 14}, {253, 186, 116, 51},
            {52, 211, 153}, {250, 204, 21}, {251, 113, 133})
    },
    {
        Id::Matcha, "matcha", "Matcha", "Forest calm",
        Make(
            {4, 18, 12}, {8, 32, 24}, {12, 43, 31},
            {52, 211, 153}, {110, 231, 183}, {163, 230, 53},
            {217, 249, 157}, {16, 185, 129},
            {16, 185, 129}, {132, 204, 22}, {94, 234, 212},
            {240, 255, 247}, {159, 198, 179}, {95, 133, 116},
            {220, 255, 235, 14}, {110, 231, 183, 46},
            {110, 231, 183}, {251, 191, 36}, {251, 113, 133})
    },
    {
        Id::Ice, "ice", "Ice", "Frozen glass",
        Make(
            {7, 11, 18}, {12, 20, 32}, {18, 29, 46},
            {147, 197, 253}, {191, 219, 254}, {226, 232, 240},
            {255, 255, 255}, {96, 165, 250},
            {96, 165, 250}, {148, 163, 184}, {226, 232, 240},
            {248, 250, 252}, {169, 184, 202}, {102, 120, 141},
            {235, 245, 255, 14}, {191, 219, 254, 46},
            {110, 231, 183}, {251, 191, 36}, {251, 113, 133})
    },
};

constexpr int kThemeCount = static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0]));

ImU32 MixColor(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto channel = [a, b, t](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFFu);
        const float cb = static_cast<float>((b >> shift) & 0xFFu);
        return static_cast<ImU32>(ca + (cb - ca) * t + 0.5f);
    };
    return IM_COL32(channel(0), channel(8), channel(16), channel(24));
}

Colors Lerp(const Colors& a, const Colors& b, float t) {
    Colors out;
    out.bg0 = MixColor(a.bg0, b.bg0, t);
    out.bg1 = MixColor(a.bg1, b.bg1, t);
    out.bg2 = MixColor(a.bg2, b.bg2, t);
    out.primary = MixColor(a.primary, b.primary, t);
    out.primarySoft = MixColor(a.primarySoft, b.primarySoft, t);
    out.secondary = MixColor(a.secondary, b.secondary, t);
    out.accent = MixColor(a.accent, b.accent, t);
    out.glow = MixColor(a.glow, b.glow, t);
    out.blob0 = MixColor(a.blob0, b.blob0, t);
    out.blob1 = MixColor(a.blob1, b.blob1, t);
    out.blob2 = MixColor(a.blob2, b.blob2, t);
    out.text = MixColor(a.text, b.text, t);
    out.textDim = MixColor(a.textDim, b.textDim, t);
    out.textFaint = MixColor(a.textFaint, b.textFaint, t);
    out.panelTint = MixColor(a.panelTint, b.panelTint, t);
    out.panelBorder = MixColor(a.panelBorder, b.panelBorder, t);
    out.success = MixColor(a.success, b.success, t);
    out.warning = MixColor(a.warning, b.warning, t);
    out.error = MixColor(a.error, b.error, t);
    return out;
}

Colors g_current = kThemes[0].colors;
Colors g_from = kThemes[0].colors;
Colors g_target = kThemes[0].colors;
Id g_id = Id::Lagoon;
float g_blend = 1.0f;
bool g_transitioning = false;

} // namespace

int Count() {
    return kThemeCount;
}

const Definition& At(int index) {
    index = std::clamp(index, 0, kThemeCount - 1);
    return kThemes[index];
}

int IndexOf(Id id) {
    return std::clamp(static_cast<int>(id), 0, kThemeCount - 1);
}

Id IdFromKey(const std::string& key) {
    for (int i = 0; i < kThemeCount; ++i) {
        if (key == kThemes[i].key) return kThemes[i].id;
    }
    return Id::Lagoon;
}

const char* Key(Id id) {
    return kThemes[IndexOf(id)].key;
}

const char* Name(Id id) {
    return kThemes[IndexOf(id)].name;
}

void SetTheme(Id id, bool instant) {
    if (instant) {
        g_id = id;
        g_target = kThemes[IndexOf(id)].colors;
        g_from = g_target;
        g_current = g_target;
        g_blend = 1.0f;
        g_transitioning = false;
        return;
    }

    if (id == g_id && !g_transitioning) return;

    g_from = g_current;
    g_target = kThemes[IndexOf(id)].colors;
    g_id = id;
    g_blend = 0.0f;
    g_transitioning = true;
}

Id CurrentId() {
    return g_id;
}

void Update(float dt) {
    if (!g_transitioning) return;

    g_blend += dt / 0.55f;
    if (g_blend >= 1.0f) {
        g_blend = 1.0f;
        g_current = g_target;
        g_transitioning = false;
        return;
    }

    // Smoothstep makes the crossfade feel like it accelerates then settles.
    const float t = g_blend * g_blend * (3.0f - 2.0f * g_blend);
    g_current = Lerp(g_from, g_target, t);
}

const Colors& Current() {
    return g_current;
}

bool Transitioning() {
    return g_transitioning;
}

float TransitionAmount() {
    return g_blend;
}

} // namespace theme
