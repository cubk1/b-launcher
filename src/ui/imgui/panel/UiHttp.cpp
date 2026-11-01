#include "UiHttp.h"

#include <core/network/http_client.hpp>
#include <core/base/win_util.hpp>
#include <core/base/strings.hpp>

#include <optional>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace UiHttp {

#if defined(_WIN32)
    using b_launcher_base::detail::utf8_to_wide;

    static bool responseLength(HINTERNET request, int status, std::optional<uint64_t>& length) {
        // These responses have no body, and transfer coding overrides Content-Length.
        if (status < 200 || status == 204 || status == 304) return true;
        DWORD size = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_TRANSFER_ENCODING, WINHTTP_HEADER_NAME_BY_INDEX,
                                nullptr, &size, WINHTTP_NO_HEADER_INDEX) || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            return true;
        }
        wchar_t text[32]{};
        size = sizeof(text);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                 text, &size, WINHTTP_NO_HEADER_INDEX)) {
            return GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND;
        }
        auto parsed = b_launcher_base::parse_int(b_launcher_base::detail::wide_to_utf8(text));
        if (!parsed || *parsed < 0) return false;
        length = (uint64_t) *parsed;
        return true;
    }
#endif

    bool get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers, int timeoutMs,
             size_t maxBytes, std::vector<unsigned char>& out, int& status, std::stop_token stop) {
        out.clear();
        status = 0;
        if (stop.stop_requested()) return false;
#if defined(_WIN32)
        std::wstring wide = utf8_to_wide(url);
        URL_COMPONENTS parts;
        ZeroMemory(&parts, sizeof(parts));
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256];
        wchar_t path[4096];
        parts.lpszHostName = host;
        parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 4096;
        wchar_t extra[4096];
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = 4096;
        if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
            return false;
        }
        std::wstring target = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);
        bool system = b_launcher_base::net::get_network_settings().use_system_proxy;
        HINTERNET session =
                WinHttpOpen(L"Java/17", system ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session && system) {
            session = WinHttpOpen(L"Java/17", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!session) {
            return false;
        }
        WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
        bool ok = false;
        HINTERNET connection =
                WinHttpConnect(session, std::wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, 0);
        if (connection) {
            HINTERNET request = WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
            if (request) {
                std::wstring headerBlock;
                for (const auto& header : headers) {
                    headerBlock += utf8_to_wide(header.first) + L": " + utf8_to_wide(header.second) + L"\r\n";
                }
                BOOL sent = !stop.stop_requested() && WinHttpSendRequest(
                        request, headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                        headerBlock.empty() ? 0 : (DWORD) -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                if (sent && !stop.stop_requested() && WinHttpReceiveResponse(request, nullptr)) {
                    DWORD code = 0;
                    DWORD codeSize = sizeof(code);
                    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
                    status = (int) code;
                    std::optional<uint64_t> expected;
                    ok = responseLength(request, status, expected) &&
                            (maxBytes == 0 || !expected || *expected <= maxBytes);
                    unsigned char buffer[8192];
                    while (ok && !stop.stop_requested()) {
                        DWORD read = 0;
                        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
                            ok = false;
                            break;
                        }
                        if (read == 0) {
                            break;
                        }
                        if (maxBytes > 0 && read > maxBytes - out.size()) {
                            ok = false;
                            break;
                        }
                        out.insert(out.end(), buffer, buffer + read);
                    }
                    if (ok && expected && out.size() != *expected) {
                        ok = false;
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connection);
        }
        WinHttpCloseHandle(session);
        return ok && !stop.stop_requested();
#else
        (void) url;
        (void) headers;
        (void) timeoutMs;
        (void) maxBytes;
        return false;
#endif
    }

    std::string urlEncode(const std::string& value) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : value) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                c == '*' || c == '_') {
                out += (char) c;
            } else if (c == ' ') {
                out += '+';
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }
}
