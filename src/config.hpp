#pragma once

#include <string>

// Central place for the product identity and the GitHub update endpoints.
// NOTE: kOwner is filled in when the public update repository is created.

namespace config {

inline constexpr const char* kProductName    = "VcDex Loader";
inline constexpr const char* kAuthor         = "Laynex";
inline constexpr const char* kVersion        = "1.1.0";

inline constexpr const char* kOwner          = "rzgr5184";
inline constexpr const char* kRepo           = "VcDex-Loader";
inline constexpr const char* kBranch         = "main";

inline constexpr const char* kManifestName   = "manifest.json";

inline bool RemoteConfigured() {
    return std::string(kOwner) != "OWNER_TODO";
}

inline std::string BaseUrl() {
    if (!RemoteConfigured()) return {};
    return std::string("https://raw.githubusercontent.com/") + kOwner + "/" + kRepo + "/" + kBranch + "/";
}

} // namespace config
