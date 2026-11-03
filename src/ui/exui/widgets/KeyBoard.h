#pragma once

#include "ui/exui/Widget.h"
#include <functional>
#include <map>

namespace exui {
    // A keyboard or mouse diagram. Key names are Minecraft translation keys.
    class KeyBoard : public Widget {
    public:
        KeyBoard();
        void SetMouse(bool mouse);
        void SetKeys(std::map<std::string, int> counts, std::string selected);
        static std::string KeyOf(UINT vk, LPARAM flags);
        std::function<void(const std::string&)> OnPick;
        void OnPaint(Gdiplus::Graphics& g) override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        void OnMouseLeave() override;
        void OnMouseCancel() override;

    private:
        int Hit(int x, int y) const;
        bool mouse = false;
        int hovered = -1, pressed = -1;
        std::map<std::string, int> counts;
        std::string selected;
    };
}
