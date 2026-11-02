#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace net {

// Optional progress reporter: (bytes received, total bytes or 0 when unknown).
using ProgressFn = std::function<void(std::uint64_t received, std::uint64_t total)>;

// Performs an HTTPS GET request and stores the response body in `out`.
bool Get(const std::wstring& url, std::string& out, std::wstring& error, int timeoutSeconds = 20);

// Downloads a remote file into `destination` (overwriting it).
bool Download(const std::wstring& url, const std::filesystem::path& destination,
              std::wstring& error, int timeoutSeconds = 90,
              const ProgressFn& onProgress = {});

} // namespace net
