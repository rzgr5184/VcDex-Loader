#pragma once

#include <filesystem>
#include <string>

namespace crypto {

std::string Sha256File(const std::filesystem::path& path);
std::string Sha256String(const std::string& text);

} // namespace crypto
