#include "UiHttp.h"

#include <core/network/http_client.hpp>
#include <core/base/win_util.hpp>

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
#endif

    bool get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers, int timeoutMs,
             size_t maxBytes, std::vector<unsigned char>& out, int& status) {
        out.clear();
        status = 0;
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
                BOOL sent = WinHttpSendRequest(
                        request, headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                        headerBlock.empty() ? 0 : (DWORD) -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                if (sent && WinHttpReceiveResponse(request, nullptr)) {
                    DWORD code = 0;
                    DWORD codeSize = sizeof(code);
                    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
                    status = (int) code;
                    ok = true;
                    while (true) {
                        DWORD available = 0;
                        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                            break;
                        }
                        size_t at = out.size();
                        out.resize(at + available);
                        DWORD read = 0;
                        if (!WinHttpReadData(request, out.data() + at, available, &read)) {
                            ok = false;
                            break;
                        }
                        out.resize(at + read);
                        if (maxBytes > 0 && out.size() > maxBytes) {
                            ok = false;
                            break;
                        }
                        if (read == 0) {
                            break;
                        }
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connection);
        }
        WinHttpCloseHandle(session);
        return ok;
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
