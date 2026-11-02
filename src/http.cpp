#include "http.hpp"
#include "config.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace net {
namespace {

std::wstring Win32ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring text;
    if (length && buffer) {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
            text.pop_back();
    }
    if (buffer) LocalFree(buffer);
    if (text.empty()) text = L"Error " + std::to_wstring(code);
    return text;
}

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = true;
};

bool CrackUrl(const std::wstring& url, UrlParts& parts, std::wstring& error) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"Gecersiz URL: " + Win32ErrorText(GetLastError());
        return false;
    }

    parts.host.assign(components.lpszHostName, components.dwHostNameLength);
    parts.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength)
        parts.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    parts.port = components.nPort;
    parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

struct RequestHandles {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~RequestHandles() {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

bool OpenGetRequest(const std::wstring& url, int timeoutSeconds,
                    RequestHandles& handles, std::wstring& error) {
    UrlParts parts;
    if (!CrackUrl(url, parts, error)) return false;

    handles.session = WinHttpOpen(
        L"VcDexLoader/1.0 (Windows; x64)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!handles.session) {
        error = L"WinHttpOpen: " + Win32ErrorText(GetLastError());
        return false;
    }

    const int timeoutMs = timeoutSeconds * 1000;
    WinHttpSetTimeouts(handles.session, 6000, 6000, timeoutMs, timeoutMs);

    handles.connection = WinHttpConnect(handles.session, parts.host.c_str(), parts.port, 0);
    if (!handles.connection) {
        error = L"WinHttpConnect: " + Win32ErrorText(GetLastError());
        return false;
    }

    const DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
    handles.request = WinHttpOpenRequest(
        handles.connection, L"GET", parts.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!handles.request) {
        error = L"WinHttpOpenRequest: " + Win32ErrorText(GetLastError());
        return false;
    }

    const wchar_t* headers = L"Cache-Control: no-cache\r\nPragma: no-cache\r\n";
    if (!WinHttpSendRequest(handles.request, headers, static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        error = L"WinHttpSendRequest: " + Win32ErrorText(GetLastError());
        return false;
    }

    if (!WinHttpReceiveResponse(handles.request, nullptr)) {
        error = L"WinHttpReceiveResponse: " + Win32ErrorText(GetLastError());
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(handles.request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200) {
        error = L"Sunucu yaniti HTTP " + std::to_wstring(statusCode);
        return false;
    }

    return true;
}

} // namespace

bool Get(const std::wstring& url, std::string& out, std::wstring& error, int timeoutSeconds) {
    out.clear();
    RequestHandles handles;
    if (!OpenGetRequest(url, timeoutSeconds, handles, error)) return false;

    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handles.request, &available)) {
            error = L"WinHttpQueryDataAvailable: " + Win32ErrorText(GetLastError());
            return false;
        }
        if (available == 0) break;

        if (available > buffer.size()) buffer.resize(available);

        DWORD read = 0;
        if (!WinHttpReadData(handles.request, buffer.data(), available, &read)) {
            error = L"WinHttpReadData: " + Win32ErrorText(GetLastError());
            return false;
        }
        if (read == 0) break;
        out.append(buffer.data(), read);
    }

    return true;
}

bool Download(const std::wstring& url, const std::filesystem::path& destination,
              std::wstring& error, int timeoutSeconds, const ProgressFn& onProgress) {
    RequestHandles handles;
    if (!OpenGetRequest(url, timeoutSeconds, handles, error)) return false;

    std::uint64_t contentLength = 0;
    {
        DWORD length = 0;
        DWORD lengthSize = sizeof(length);
        if (WinHttpQueryHeaders(handles.request,
                                WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            contentLength = length;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);

    FILE* file = nullptr;
    if (_wfopen_s(&file, destination.wstring().c_str(), L"wb") != 0 || !file) {
        error = L"Dosya olusturulamadi: " + destination.wstring();
        return false;
    }

    bool ok = true;
    std::uint64_t received = 0;
    std::vector<char> buffer(128 * 1024);

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handles.request, &available)) {
            error = L"WinHttpQueryDataAvailable: " + Win32ErrorText(GetLastError());
            ok = false;
            break;
        }
        if (available == 0) break;

        if (available > buffer.size()) buffer.resize(available);

        DWORD read = 0;
        if (!WinHttpReadData(handles.request, buffer.data(), available, &read)) {
            error = L"WinHttpReadData: " + Win32ErrorText(GetLastError());
            ok = false;
            break;
        }
        if (read == 0) break;

        if (fwrite(buffer.data(), 1, read, file) != read) {
            error = L"Dosyaya yazilamadi: " + destination.wstring();
            ok = false;
            break;
        }

        received += read;
        if (onProgress) onProgress(received, contentLength);
    }

    fclose(file);

    if (!ok) {
        std::error_code removeEc;
        std::filesystem::remove(destination, removeEc);
        return false;
    }
    return true;
}

} // namespace net
