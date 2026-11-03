#include "settings.hpp"

#include "app.hpp"
#include "json.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace settings {
namespace {

Data g_data;
bool g_loaded = false;

std::filesystem::path SettingsPath() {
    return app::DataDir() / "settings.json";
}

std::string Escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

bool AsBool(const json::Value& root, const char* key, bool fallback) {
    const json::Value* value = root.Find(key);
    return value ? value->AsBool(fallback) : fallback;
}

int AsInt(const json::Value& root, const char* key, int fallback, int low, int high) {
    const json::Value* value = root.Find(key);
    if (!value || value->type != json::Value::Type::Number) return fallback;
    return std::clamp(static_cast<int>(value->AsNumber(fallback)), low, high);
}

} // namespace

Data& Get() {
    return g_data;
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;

    g_data = Data{};

    std::error_code ec;
    const auto path = SettingsPath();
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    if (text.empty()) return;

    json::Value root;
    if (!json::Parse(text, root) || !root.IsObject()) return;

    if (const json::Value* v = root.Find("theme"); v && v->type == json::Value::Type::String)
        g_data.theme = theme::IdFromKey(v->AsString("lagoon"));

    if (const json::Value* effects = root.Find("effects"); effects && effects->IsObject()) {
        g_data.animations = AsBool(*effects, "animations", g_data.animations);
        g_data.particles = AsBool(*effects, "particles", g_data.particles);
        g_data.parallax = AsBool(*effects, "parallax", g_data.parallax);
        g_data.reduceMotion = AsBool(*effects, "reduceMotion", g_data.reduceMotion);
        g_data.intensity = AsInt(*effects, "intensity", g_data.intensity, 0, 100);
        if (const json::Value* blur = effects->Find("blur"); blur &&
            blur->type == json::Value::Type::String) {
            g_data.blurHigh = blur->AsString("high") != "low";
        }
    }

    if (const json::Value* window = root.Find("window"); window && window->IsObject()) {
        g_data.windowWidth = AsInt(*window, "width", g_data.windowWidth, 1024, 3840);
        g_data.windowHeight = AsInt(*window, "height", g_data.windowHeight, 740, 2160);
    }

    if (const json::Value* last = root.Find("lastGame"); last &&
        last->type == json::Value::Type::String) {
        g_data.lastGame = last->AsString();
    }
}

bool Save() {
    std::error_code ec;
    const auto path = SettingsPath();
    std::filesystem::create_directories(path.parent_path(), ec);

    const auto temporary = path;
    const auto tmpPath = std::filesystem::path(path.string() + ".tmp");

    std::string blur = g_data.blurHigh ? "high" : "low";
    std::ostringstream out;
    out << "{\n"
        << "  \"schema\": 1,\n"
        << "  \"theme\": \"" << theme::Key(g_data.theme) << "\",\n"
        << "  \"effects\": {\n"
        << "    \"animations\": " << (g_data.animations ? "true" : "false") << ",\n"
        << "    \"particles\": " << (g_data.particles ? "true" : "false") << ",\n"
        << "    \"parallax\": " << (g_data.parallax ? "true" : "false") << ",\n"
        << "    \"reduceMotion\": " << (g_data.reduceMotion ? "true" : "false") << ",\n"
        << "    \"intensity\": " << std::clamp(g_data.intensity, 0, 100) << ",\n"
        << "    \"blur\": \"" << blur << "\"\n"
        << "  },\n"
        << "  \"window\": { \"width\": " << g_data.windowWidth
        << ", \"height\": " << g_data.windowHeight << " },\n"
        << "  \"lastGame\": \"" << Escape(g_data.lastGame) << "\"\n"
        << "}\n";

    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        const std::string text = out.str();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file.good()) return false;
    }

    if (!MoveFileExW(tmpPath.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
}

void ApplyToTheme() {
    theme::SetTheme(g_data.theme, true);
}

} // namespace settings
