#pragma once

#include <string>
#include <vector>

// Central place for the product identity and the GitHub update endpoints.

namespace config {

inline constexpr const char* kProductName    = "VcDex Loader";
inline constexpr const char* kAuthor         = "Laynex";
inline constexpr const char* kVersion        = "1.2.1";

inline constexpr const char* kOwner          = "rzgr5184";
inline constexpr const char* kRepo           = "VcDex-Loader";
inline constexpr const char* kBranch         = "main";

inline constexpr const char* kManifestName   = "manifest.json";

// Automatic background check interval (seconds).
inline constexpr float kAutoCheckSeconds = 600.0f;

inline bool RemoteConfigured() {
    return std::string(kOwner) != "OWNER_TODO";
}

inline std::string BaseUrl() {
    if (!RemoteConfigured()) return {};
    return std::string("https://raw.githubusercontent.com/") + kOwner + "/" + kRepo + "/" + kBranch + "/";
}

// Mirrors used when the primary host is unreachable or rate limited.
// The loader tries every base in order and remembers the manifest source.
inline std::vector<std::string> BaseUrls() {
    std::vector<std::string> bases;
    if (!RemoteConfigured()) return bases;

    bases.push_back(BaseUrl());
    bases.push_back(std::string("https://cdn.jsdelivr.net/gh/") + kOwner + "/" + kRepo + "@" + kBranch + "/");
    bases.push_back(std::string("https://github.com/") + kOwner + "/" + kRepo + "/raw/" + kBranch + "/");
    return bases;
}

} // namespace config
