#include "ui/exui/widgets/WebBrowser.h"
#include <ole2.h>
#include <exdisp.h>
#include <commctrl.h>
#include <limits>

namespace exui {
    WebBrowser::~WebBrowser() {
        Destroy();
    }
    bool WebBrowser::Create(HWND parent, int x, int y, int width, int height, std::wstring_view url) {
        // A second Create fails and leaves the existing browser alone.
        if (m_hwndAx) {
            m_lastResult = E_UNEXPECTED;
            return false;
        }
        if (!IsWindow(parent) || width <= 0 || height <= 0) {
            m_lastResult = E_INVALIDARG;
            return false;
        }
        m_lastResult = OleInitialize(nullptr);
        if (FAILED(m_lastResult)) return false;
        m_oleInitialized = true;
        // The system's ATL, so no ATL library needs to be linked.
        m_atl = LoadLibraryExW(L"atl.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!m_atl) {
            m_lastResult = HRESULT_FROM_WIN32(GetLastError());
            Destroy();
            return false;
        }
        using Init = BOOL(WINAPI*)();
        using GetControl = HRESULT(WINAPI*)(HWND, IUnknown**);
        auto initialize = reinterpret_cast<Init>(GetProcAddress(m_atl, "AtlAxWinInit"));
        auto getControl = reinterpret_cast<GetControl>(GetProcAddress(m_atl, "AtlAxGetControl"));
        if (!initialize || !getControl || !initialize()) {
            m_lastResult = E_NOINTERFACE;
            Destroy();
            return false;
        }
        m_hwndAx = CreateWindowExW(WS_EX_CLIENTEDGE, L"AtlAxWin", L"Shell.Explorer.1",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS, x, y, width, height, parent,
                                   nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!m_hwndAx) {
            m_lastResult = HRESULT_FROM_WIN32(GetLastError());
            Destroy();
            return false;
        }
        IUnknown* unknown = nullptr;
        m_lastResult = getControl(m_hwndAx, &unknown);
        if (SUCCEEDED(m_lastResult) && unknown) {
            m_lastResult = unknown->QueryInterface(IID_IWebBrowser2, reinterpret_cast<void**>(&m_browser));
            unknown->Release();
        }
        if (FAILED(m_lastResult) || !m_browser) {
            Destroy();
            return false;
        }
        m_lastResult = m_browser->put_Silent(VARIANT_TRUE);
        if (FAILED(m_lastResult)) {
            Destroy();
            return false;
        }
        if (!url.empty() && !Navigate(url)) {
            Destroy();
            return false;
        }
        return true;
    }
    bool WebBrowser::Navigate(std::wstring_view url) {
        if (!m_browser || !IsWindow(m_hwndAx) || url.size() > std::numeric_limits<UINT>::max()) {
            m_lastResult = E_UNEXPECTED;
            return false;
        }
        BSTR target = SysAllocStringLen(url.data(), static_cast<UINT>(url.size()));
        if (!target) {
            m_lastResult = E_OUTOFMEMORY;
            return false;
        }
        VARIANT empty;
        VariantInit(&empty);
        m_lastResult = m_browser->Navigate(target, &empty, &empty, &empty, &empty);
        SysFreeString(target);
        return SUCCEEDED(m_lastResult);
    }
    void WebBrowser::Destroy() {
        if (m_browser) {
            m_browser->Stop();
            m_browser->Release();
            m_browser = nullptr;
        }
        if (m_hwndAx && IsWindow(m_hwndAx)) DestroyWindow(m_hwndAx);
        m_hwndAx = nullptr;
        if (m_atl) {
            FreeLibrary(m_atl);
            m_atl = nullptr;
        }
        if (m_oleInitialized) {
            OleUninitialize();
            m_oleInitialized = false;
        }
    }
    bool WebBrowser::IsBusy() {
        VARIANT_BOOL value = VARIANT_FALSE;
        m_lastResult = m_browser ? m_browser->get_Busy(&value) : E_UNEXPECTED;
        return SUCCEEDED(m_lastResult) && value != VARIANT_FALSE;
    }
    long WebBrowser::GetReadyState() {
        READYSTATE value = READYSTATE_UNINITIALIZED;
        m_lastResult = m_browser ? m_browser->get_ReadyState(&value) : E_UNEXPECTED;
        return SUCCEEDED(m_lastResult) ? long(value) : 0;
    }
    std::wstring WebBrowser::GetLocationUrl() {
        BSTR value = nullptr;
        m_lastResult = m_browser ? m_browser->get_LocationURL(&value) : E_UNEXPECTED;
        std::wstring text = SUCCEEDED(m_lastResult) && value ? std::wstring(value, SysStringLen(value)) : L"";
        SysFreeString(value);
        return text;
    }
    bool WebBrowser::SetOffline(bool offline) {
        m_lastResult = m_browser ? m_browser->put_Offline(offline ? VARIANT_TRUE : VARIANT_FALSE) : E_UNEXPECTED;
        return SUCCEEDED(m_lastResult);
    }
    bool WebBrowser::IsOffline() {
        VARIANT_BOOL value = VARIANT_FALSE;
        m_lastResult = m_browser ? m_browser->get_Offline(&value) : E_UNEXPECTED;
        return SUCCEEDED(m_lastResult) && value != VARIANT_FALSE;
    }
} // namespace exui
