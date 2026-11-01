#pragma once

#include <filesystem>
#include <string>

namespace net {

// Performs an HTTPS GET request and stores the response body in `out`.
bool Get(const std::wstring& url, std::string& out, std::wstring& error, int timeoutSeconds = 20);

// Downloads a remote file into `destination` (overwriting it).
bool Download(const std::wstring& url, const std::filesystem::path& destination,
              std::wstring& error, int timeoutSeconds = 90);

} // namespace net
