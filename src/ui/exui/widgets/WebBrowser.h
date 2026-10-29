#pragma once
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
struct IWebBrowser2;

namespace exui {
    // An embedded Internet Explorer control (ATL's AtlAxWin hosting Shell.Explorer)
    // as a native child window. Use it from the thread that created it.
    class WebBrowser {
    public:
        WebBrowser() = default;
        ~WebBrowser();
        WebBrowser(const WebBrowser&) = delete;
        WebBrowser& operator=(const WebBrowser&) = delete;
        bool Create(HWND parent, int x, int y, int width, int height, std::wstring_view initialUrl = L"");
        bool Navigate(std::wstring_view url);
        bool IsBusy();
        long GetReadyState();
        std::wstring GetLocationUrl();
        bool SetOffline(bool offline);
        bool IsOffline();
        void Destroy();
        HWND GetHwnd() const noexcept {
            return m_hwndAx;
        }
        HRESULT GetLastResult() const noexcept {
            return m_lastResult;
        }

    private:
        HWND m_hwndAx = nullptr;
        HMODULE m_atl = nullptr;
        IWebBrowser2* m_browser = nullptr;
        bool m_oleInitialized = false;
        HRESULT m_lastResult = S_OK;
    };

} // namespace exui
