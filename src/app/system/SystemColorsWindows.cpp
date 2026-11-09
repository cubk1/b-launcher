#include "app/system/SystemColors.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace SystemColors {

    namespace {
        bool dword(const wchar_t* key, const wchar_t* name, DWORD& value) {
            DWORD size = sizeof(value);
            return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
                    ERROR_SUCCESS;
        }
    }

    State read() {
        State state;
        DWORD light = 0;
        if (dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", light)) {
            state.darkKnown = true;
            state.dark = light == 0;
        }
        DWORD abgr = 0;
        if (dword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", abgr)) {
            state.accentKnown = true;
            state.accent = 0xFF000000u | ((abgr & 0xFFu) << 16) | (abgr & 0xFF00u) | ((abgr >> 16) & 0xFFu);
        }
        return state;
    }
}
