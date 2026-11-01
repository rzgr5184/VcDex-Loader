#include "sha256.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace crypto {
namespace {

std::string ToHex(const unsigned char* data, std::size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        result.push_back(digits[(data[i] >> 4) & 0x0F]);
        result.push_back(digits[data[i] & 0x0F]);
    }
    return result;
}

bool HashBytes(const unsigned char* data, std::size_t length, std::string& out) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return false;

    DWORD objectSize = 0;
    DWORD resultSize = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                          &resultSize, 0) == 0) {
        std::vector<unsigned char> object(objectSize);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0) {
            std::vector<unsigned char> digest(32);
            if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(length), 0) == 0 &&
                BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
                out = ToHex(digest.data(), digest.size());
                ok = true;
            }
        }
    }

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

} // namespace

std::string Sha256String(const std::string& text) {
    std::string out;
    HashBytes(reinterpret_cast<const unsigned char*>(text.data()), text.size(), out);
    return out;
}

std::string Sha256File(const std::filesystem::path& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || !file) return {};

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objectSize = 0;
        DWORD resultSize = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                              &resultSize, 0) == 0) {
            std::vector<unsigned char> object(objectSize);
            if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0) {
                std::vector<unsigned char> buffer(256 * 1024);
                bool ok = true;
                for (;;) {
                    const std::size_t read = fread(buffer.data(), 1, buffer.size(), file);
                    if (read == 0) break;
                    if (BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0) != 0) {
                        ok = false;
                        break;
                    }
                }
                fclose(file);
                file = nullptr;

                if (ok) {
                    std::vector<unsigned char> digest(32);
                    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0)
                        result = ToHex(digest.data(), digest.size());
                }
            }
        }
    }

    if (file) fclose(file);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

} // namespace crypto
