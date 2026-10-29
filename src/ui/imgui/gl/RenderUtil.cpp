#include "RenderUtil.h"
#include "../panel/UiRender.h"
#include "../panel/UiKeys.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace RenderUtil {

    static ImU32 toIm(uint32_t argb) {
        return ((argb >> 24) & 0xFF) << IM_COL32_A_SHIFT | ((argb >> 16) & 0xFF) << IM_COL32_R_SHIFT |
                ((argb >> 8) & 0xFF) << IM_COL32_G_SHIFT | (argb & 0xFF) << IM_COL32_B_SHIFT;
    }

    static ImVec2 screen(float x, float y) {
        float u = UiRender::unit();
        return ImVec2(x * u, y * u);
    }

    static ImVec2 whiteUv() {
        return ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    }

    void gradientH(float x, float y, float w, float h, uint32_t left, uint32_t right) {
        UiRender::flush();
        ImDrawList* dl = UiRender::drawList();
        if (dl == nullptr) {
            return;
        }
        ImVec2 uv = whiteUv();
        dl->PrimReserve(6, 4);
        unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(screen(x, y + h), uv, toIm(left));
        dl->PrimWriteVtx(screen(x + w, y + h), uv, toIm(right));
        dl->PrimWriteVtx(screen(x + w, y), uv, toIm(right));
        dl->PrimWriteVtx(screen(x, y), uv, toIm(left));
        dl->PrimWriteIdx((ImDrawIdx) base);
        dl->PrimWriteIdx((ImDrawIdx) (base + 1));
        dl->PrimWriteIdx((ImDrawIdx) (base + 2));
        dl->PrimWriteIdx((ImDrawIdx) base);
        dl->PrimWriteIdx((ImDrawIdx) (base + 2));
        dl->PrimWriteIdx((ImDrawIdx) (base + 3));
    }

    void gradientV(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
        UiRender::flush();
        ImDrawList* dl = UiRender::drawList();
        if (dl == nullptr) {
            return;
        }
        ImVec2 uv = whiteUv();
        dl->PrimReserve(6, 4);
        unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(screen(x, y + h), uv, toIm(bottom));
        dl->PrimWriteVtx(screen(x + w, y + h), uv, toIm(bottom));
        dl->PrimWriteVtx(screen(x + w, y), uv, toIm(top));
        dl->PrimWriteVtx(screen(x, y), uv, toIm(top));
        dl->PrimWriteIdx((ImDrawIdx) base);
        dl->PrimWriteIdx((ImDrawIdx) (base + 1));
        dl->PrimWriteIdx((ImDrawIdx) (base + 2));
        dl->PrimWriteIdx((ImDrawIdx) base);
        dl->PrimWriteIdx((ImDrawIdx) (base + 2));
        dl->PrimWriteIdx((ImDrawIdx) (base + 3));
    }

    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color) {
        UiRender::flush();
        ImDrawList* dl = UiRender::drawList();
        if (dl == nullptr) {
            return;
        }
        ImVec2 uv = whiteUv();
        ImU32 col = toIm(color);
        dl->PrimReserve(3, 3);
        unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(screen(x1, y1), uv, col);
        dl->PrimWriteVtx(screen(x2, y2), uv, col);
        dl->PrimWriteVtx(screen(x3, y3), uv, col);
        dl->PrimWriteIdx((ImDrawIdx) base);
        dl->PrimWriteIdx((ImDrawIdx) (base + 1));
        dl->PrimWriteIdx((ImDrawIdx) (base + 2));
    }

    void smoothTriangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color) {
        float sideA = (float) std::hypot(x3 - x2, y3 - y2);
        float sideB = (float) std::hypot(x1 - x3, y1 - y3);
        float sideC = (float) std::hypot(x2 - x1, y2 - y1);
        float perimeter = sideA + sideB + sideC;
        float area = std::fabs((x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)) * 0.5f;
        if (perimeter < 1.0E-4f || area < 1.0E-6f) {
            return;
        }
        float centerX = (sideA * x1 + sideB * x2 + sideC * x3) / perimeter;
        float centerY = (sideA * y1 + sideB * y2 + sideC * y3) / perimeter;
        float inradius = 2.0f * area / perimeter;
        float feather = 0.5f / UiRender::pixelScale();
        float inner = std::max(0.0f, inradius - feather) / inradius;
        float outer = (inradius + feather) / inradius;
        float pointX[3] = {x1, x2, x3};
        float pointY[3] = {y1, y2, y3};
        uint32_t clear = color & 0x00FFFFFFu;
        UiRender::flush();
        ImDrawList* dl = UiRender::drawList();
        if (dl == nullptr) {
            return;
        }
        ImVec2 uv = whiteUv();
        ImU32 col = toIm(color);
        ImU32 transparent = toIm(clear);
        dl->PrimReserve(3 + 18, 3 + 18);
        unsigned int base = dl->_VtxCurrentIdx;
        for (int i = 0; i < 3; i++) {
            dl->PrimWriteVtx(screen(centerX + (pointX[i] - centerX) * inner, centerY + (pointY[i] - centerY) * inner),
                             uv, col);
        }
        dl->PrimWriteIdx((ImDrawIdx) base);
        dl->PrimWriteIdx((ImDrawIdx) (base + 1));
        dl->PrimWriteIdx((ImDrawIdx) (base + 2));
        for (int i = 0; i < 3; i++) {
            int next = (i + 1) % 3;
            float innerX0 = centerX + (pointX[i] - centerX) * inner;
            float innerY0 = centerY + (pointY[i] - centerY) * inner;
            float innerX1 = centerX + (pointX[next] - centerX) * inner;
            float innerY1 = centerY + (pointY[next] - centerY) * inner;
            float outerX0 = centerX + (pointX[i] - centerX) * outer;
            float outerY0 = centerY + (pointY[i] - centerY) * outer;
            float outerX1 = centerX + (pointX[next] - centerX) * outer;
            float outerY1 = centerY + (pointY[next] - centerY) * outer;
            unsigned int at = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(screen(innerX0, innerY0), uv, col);
            dl->PrimWriteVtx(screen(innerX1, innerY1), uv, col);
            dl->PrimWriteVtx(screen(outerX1, outerY1), uv, transparent);
            dl->PrimWriteVtx(screen(innerX0, innerY0), uv, col);
            dl->PrimWriteVtx(screen(outerX1, outerY1), uv, transparent);
            dl->PrimWriteVtx(screen(outerX0, outerY0), uv, transparent);
            for (int k = 0; k < 6; k++) {
                dl->PrimWriteIdx((ImDrawIdx) (at + k));
            }
        }
    }

    uint32_t withAlpha(uint32_t color, int alpha) {
        return (color & 0x00FFFFFFu) | ((uint32_t) alpha << 24);
    }

    uint32_t fade(uint32_t a, uint32_t b, double t) {
        int aa = (a >> 24) & 0xFF, ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
        int ba = (b >> 24) & 0xFF, br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        int ra = (int) (aa + (ba - aa) * t);
        int rr = (int) (ar + (br - ar) * t);
        int rg = (int) (ag + (bg - ag) * t);
        int rb = (int) (ab + (bb - ab) * t);
        return ((uint32_t) ra << 24) | ((uint32_t) rr << 16) | ((uint32_t) rg << 8) | (uint32_t) rb;
    }

    static std::string prettyName(ImGuiKey key) {
        if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
            return std::string(1, (char) ('A' + (key - ImGuiKey_A)));
        }
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
            return std::string(1, (char) ('0' + (key - ImGuiKey_0)));
        }
        if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) {
            return "F" + std::to_string(key - ImGuiKey_F1 + 1);
        }
        if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
            return "NUM " + std::to_string(key - ImGuiKey_Keypad0);
        }
        switch (key) {
            case ImGuiKey_Apostrophe: return "'";
            case ImGuiKey_Comma: return ",";
            case ImGuiKey_Minus: return "-";
            case ImGuiKey_Period: return ".";
            case ImGuiKey_Slash: return "/";
            case ImGuiKey_Semicolon: return ";";
            case ImGuiKey_Equal: return "=";
            case ImGuiKey_LeftBracket: return "[";
            case ImGuiKey_Backslash: return "\\";
            case ImGuiKey_RightBracket: return "]";
            case ImGuiKey_GraveAccent: return "`";
            case ImGuiKey_Space: return "SPACE";
            case ImGuiKey_Enter: return "ENTER";
            case ImGuiKey_Escape: return "ESCAPE";
            case ImGuiKey_Tab: return "TAB";
            case ImGuiKey_Backspace: return "BACKSPACE";
            case ImGuiKey_Insert: return "INSERT";
            case ImGuiKey_Delete: return "DELETE";
            case ImGuiKey_RightArrow: return "RIGHT";
            case ImGuiKey_LeftArrow: return "LEFT";
            case ImGuiKey_DownArrow: return "DOWN";
            case ImGuiKey_UpArrow: return "UP";
            case ImGuiKey_PageUp: return "PAGE UP";
            case ImGuiKey_PageDown: return "PAGE DOWN";
            case ImGuiKey_Home: return "HOME";
            case ImGuiKey_End: return "END";
            case ImGuiKey_CapsLock: return "CAPS LOCK";
            case ImGuiKey_ScrollLock: return "SCROLL LOCK";
            case ImGuiKey_NumLock: return "NUM LOCK";
            case ImGuiKey_PrintScreen: return "PRINT SCREEN";
            case ImGuiKey_Pause: return "PAUSE";
            case ImGuiKey_KeypadDecimal: return "NUM DECIMAL";
            case ImGuiKey_KeypadDivide: return "NUM DIVIDE";
            case ImGuiKey_KeypadMultiply: return "NUM MULTIPLY";
            case ImGuiKey_KeypadSubtract: return "NUM SUBTRACT";
            case ImGuiKey_KeypadAdd: return "NUM ADD";
            case ImGuiKey_KeypadEnter: return "NUM ENTER";
            case ImGuiKey_KeypadEqual: return "NUM EQUAL";
            case ImGuiKey_LeftShift: return "LSHIFT";
            case ImGuiKey_RightShift: return "RSHIFT";
            case ImGuiKey_LeftCtrl: return "LCTRL";
            case ImGuiKey_RightCtrl: return "RCTRL";
            case ImGuiKey_LeftAlt: return "LALT";
            case ImGuiKey_RightAlt: return "RALT";
            case ImGuiKey_LeftSuper: return "LSUPER";
            case ImGuiKey_RightSuper: return "RSUPER";
            case ImGuiKey_Menu: return "MENU";
            default: break;
        }
        return std::string();
    }

    std::string keyName(int key) {
        if (UiKeys::isMouseKey(key)) {
            return UiKeys::mouseKeyName(key);
        }
        if (key <= 0) {
            return "";
        }
        std::string name = prettyName((ImGuiKey) key);
        return name.empty() ? "K" + std::to_string(key) : name;
    }
}
