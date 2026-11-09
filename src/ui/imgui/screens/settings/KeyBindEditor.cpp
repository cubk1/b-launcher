#include "ui/imgui/screens/settings/KeyBindEditor.h"
#include "app/instances/Instances.h"
#include "app/launch/GameOptions.h"
#include "app/launch/KeyBinds.h"
#include "app/system/Notices.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/overlay/UiOverlays.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KeyBindEditor {

    namespace blb = b_launcher_base;

    namespace {
        constexpr int MAIN = 0;
        constexpr int NAV = 1;
        constexpr int PAD = 2;
        constexpr float BOARD_PAD = 14.0f;
        constexpr float BOARD_ROWS = 6.25f;
        constexpr float KEYBOARD_UNITS = 23.0f;
        constexpr float DESK_GAP = 3.0f;
        constexpr float RAIL_ROW = 36.0f;
        constexpr float RAIL_MIN = 170.0f;
        constexpr float RAIL_MAX = 230.0f;
        constexpr float STICKY_GAP = 10.0f;
        const char* const PAGES[] = {"键盘", "鼠标"};
        constexpr float MOUSE_W = 3.1f;
        constexpr float MOUSE_H = 6.0f;
        constexpr float HEAD_H = 32.0f;
        constexpr float POP_W = 320.0f;
        constexpr float POP_HEAD = 44.0f;
        constexpr float POP_ROW = 34.0f;
        constexpr int POP_ROWS = 6;
        constexpr float POP_FOOT = 44.0f;
        constexpr float ARROW = 7.0f;
        constexpr float SEARCH_W = 260.0f;
        const char* const OPEN_MOTION = "keybinds#popover#open";
        const char* const HEIGHT_MOTION = "keybinds#popover#height";

        struct Cap {
            const char* name;
            const char* label;
            float x;
            float y;
            float w;
            float h;
            ImGuiKey key;
        };

        struct Item {
            const char* name;
            const char* label;
            float w;
            ImGuiKey key;
        };

        struct Search {
            std::string owner;
            std::string text;
            std::shared_ptr<TextBinding> binding;
        };

        struct State {
            std::wstring file;
            int lastFrame = -1;
            int page = 0;
            std::string group;
            std::string selected;
            bool adding = false;
            std::string addGroup;
            std::string capturing;
            int captureFrame = 0;
            bool boardShown = false;
            float boardX = 0.0f;
            float boardY = 0.0f;
            float boardW = 0.0f;
            float boardH = 0.0f;
            std::string listGroup;
            float listShift = 0.0f;
        };

        struct Spot {
            std::string name;
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
        };

        struct CopyRow {
            std::string label;
            std::string detail;
            std::filesystem::path file;
            bool legacy = false;
            bool chosen = false;
        };

        std::map<std::string, Search> searches;
        std::map<std::string, State> states;
        std::vector<CopyRow> copyRows;
        std::filesystem::path copyFrom;

        const Item SPACER{nullptr, nullptr, 1.0f, ImGuiKey_None};
        const Item HALF{nullptr, nullptr, 0.5f, ImGuiKey_None};

        void addRow(std::vector<Cap>& caps, float y, std::initializer_list<Item> items, float h = 1.0f) {
            float x = 0.0f;
            for (const Item& item : items) {
                if (item.name != nullptr) {
                    caps.push_back(Cap{item.name, item.label, x, y, item.w, h, item.key});
                }
                x += item.w;
            }
        }

        std::vector<Cap> build(int section) {
            std::vector<Cap> caps;
            if (section == MAIN) {
                addRow(caps, 0.0f,
                       {{"key.keyboard.escape", "Esc", 1, ImGuiKey_Escape},
                        SPACER,
                        {"key.keyboard.f1", "F1", 1, ImGuiKey_F1},
                        {"key.keyboard.f2", "F2", 1, ImGuiKey_F2},
                        {"key.keyboard.f3", "F3", 1, ImGuiKey_F3},
                        {"key.keyboard.f4", "F4", 1, ImGuiKey_F4},
                        HALF,
                        {"key.keyboard.f5", "F5", 1, ImGuiKey_F5},
                        {"key.keyboard.f6", "F6", 1, ImGuiKey_F6},
                        {"key.keyboard.f7", "F7", 1, ImGuiKey_F7},
                        {"key.keyboard.f8", "F8", 1, ImGuiKey_F8},
                        HALF,
                        {"key.keyboard.f9", "F9", 1, ImGuiKey_F9},
                        {"key.keyboard.f10", "F10", 1, ImGuiKey_F10},
                        {"key.keyboard.f11", "F11", 1, ImGuiKey_F11},
                        {"key.keyboard.f12", "F12", 1, ImGuiKey_F12}});
                addRow(caps, 1.25f,
                       {{"key.keyboard.grave.accent", "`", 1, ImGuiKey_GraveAccent},
                        {"key.keyboard.1", "1", 1, ImGuiKey_1},
                        {"key.keyboard.2", "2", 1, ImGuiKey_2},
                        {"key.keyboard.3", "3", 1, ImGuiKey_3},
                        {"key.keyboard.4", "4", 1, ImGuiKey_4},
                        {"key.keyboard.5", "5", 1, ImGuiKey_5},
                        {"key.keyboard.6", "6", 1, ImGuiKey_6},
                        {"key.keyboard.7", "7", 1, ImGuiKey_7},
                        {"key.keyboard.8", "8", 1, ImGuiKey_8},
                        {"key.keyboard.9", "9", 1, ImGuiKey_9},
                        {"key.keyboard.0", "0", 1, ImGuiKey_0},
                        {"key.keyboard.minus", "-", 1, ImGuiKey_Minus},
                        {"key.keyboard.equal", "=", 1, ImGuiKey_Equal},
                        {"key.keyboard.backspace", "Backspace", 2, ImGuiKey_Backspace}});
                addRow(caps, 2.25f,
                       {{"key.keyboard.tab", "Tab", 1.5f, ImGuiKey_Tab},
                        {"key.keyboard.q", "Q", 1, ImGuiKey_Q},
                        {"key.keyboard.w", "W", 1, ImGuiKey_W},
                        {"key.keyboard.e", "E", 1, ImGuiKey_E},
                        {"key.keyboard.r", "R", 1, ImGuiKey_R},
                        {"key.keyboard.t", "T", 1, ImGuiKey_T},
                        {"key.keyboard.y", "Y", 1, ImGuiKey_Y},
                        {"key.keyboard.u", "U", 1, ImGuiKey_U},
                        {"key.keyboard.i", "I", 1, ImGuiKey_I},
                        {"key.keyboard.o", "O", 1, ImGuiKey_O},
                        {"key.keyboard.p", "P", 1, ImGuiKey_P},
                        {"key.keyboard.left.bracket", "[", 1, ImGuiKey_LeftBracket},
                        {"key.keyboard.right.bracket", "]", 1, ImGuiKey_RightBracket},
                        {"key.keyboard.backslash", "\\", 1.5f, ImGuiKey_Backslash}});
                addRow(caps, 3.25f,
                       {{"key.keyboard.caps.lock", "Caps Lock", 1.75f, ImGuiKey_CapsLock},
                        {"key.keyboard.a", "A", 1, ImGuiKey_A},
                        {"key.keyboard.s", "S", 1, ImGuiKey_S},
                        {"key.keyboard.d", "D", 1, ImGuiKey_D},
                        {"key.keyboard.f", "F", 1, ImGuiKey_F},
                        {"key.keyboard.g", "G", 1, ImGuiKey_G},
                        {"key.keyboard.h", "H", 1, ImGuiKey_H},
                        {"key.keyboard.j", "J", 1, ImGuiKey_J},
                        {"key.keyboard.k", "K", 1, ImGuiKey_K},
                        {"key.keyboard.l", "L", 1, ImGuiKey_L},
                        {"key.keyboard.semicolon", ";", 1, ImGuiKey_Semicolon},
                        {"key.keyboard.apostrophe", "'", 1, ImGuiKey_Apostrophe},
                        {"key.keyboard.enter", "Enter", 2.25f, ImGuiKey_Enter}});
                addRow(caps, 4.25f,
                       {{"key.keyboard.left.shift", "Shift", 2.25f, ImGuiKey_LeftShift},
                        {"key.keyboard.z", "Z", 1, ImGuiKey_Z},
                        {"key.keyboard.x", "X", 1, ImGuiKey_X},
                        {"key.keyboard.c", "C", 1, ImGuiKey_C},
                        {"key.keyboard.v", "V", 1, ImGuiKey_V},
                        {"key.keyboard.b", "B", 1, ImGuiKey_B},
                        {"key.keyboard.n", "N", 1, ImGuiKey_N},
                        {"key.keyboard.m", "M", 1, ImGuiKey_M},
                        {"key.keyboard.comma", ",", 1, ImGuiKey_Comma},
                        {"key.keyboard.period", ".", 1, ImGuiKey_Period},
                        {"key.keyboard.slash", "/", 1, ImGuiKey_Slash},
                        {"key.keyboard.right.shift", "Shift", 2.75f, ImGuiKey_RightShift}});
                addRow(caps, 5.25f,
                       {{"key.keyboard.left.control", "Ctrl", 1.25f, ImGuiKey_LeftCtrl},
                        {"key.keyboard.left.win", "Win", 1.25f, ImGuiKey_LeftSuper},
                        {"key.keyboard.left.alt", "Alt", 1.25f, ImGuiKey_LeftAlt},
                        {"key.keyboard.space", "", 6.25f, ImGuiKey_Space},
                        {"key.keyboard.right.alt", "Alt", 1.25f, ImGuiKey_RightAlt},
                        {"key.keyboard.right.win", "Win", 1.25f, ImGuiKey_RightSuper},
                        {"key.keyboard.menu", "Menu", 1.25f, ImGuiKey_Menu},
                        {"key.keyboard.right.control", "Ctrl", 1.25f, ImGuiKey_RightCtrl}});
            } else if (section == NAV) {
                addRow(caps, 0.0f,
                       {{"key.keyboard.print.screen", "PrtSc", 1, ImGuiKey_PrintScreen},
                        {"key.keyboard.scroll.lock", "ScrLk", 1, ImGuiKey_ScrollLock},
                        {"key.keyboard.pause", "Pause", 1, ImGuiKey_Pause}});
                addRow(caps, 1.25f,
                       {{"key.keyboard.insert", "Ins", 1, ImGuiKey_Insert},
                        {"key.keyboard.home", "Home", 1, ImGuiKey_Home},
                        {"key.keyboard.page.up", "PgUp", 1, ImGuiKey_PageUp}});
                addRow(caps, 2.25f,
                       {{"key.keyboard.delete", "Del", 1, ImGuiKey_Delete},
                        {"key.keyboard.end", "End", 1, ImGuiKey_End},
                        {"key.keyboard.page.down", "PgDn", 1, ImGuiKey_PageDown}});
                addRow(caps, 4.25f, {SPACER, {"key.keyboard.up", "↑", 1, ImGuiKey_UpArrow}, SPACER});
                addRow(caps, 5.25f,
                       {{"key.keyboard.left", "←", 1, ImGuiKey_LeftArrow},
                        {"key.keyboard.down", "↓", 1, ImGuiKey_DownArrow},
                        {"key.keyboard.right", "→", 1, ImGuiKey_RightArrow}});
            } else if (section == PAD) {
                addRow(caps, 1.25f,
                       {{"key.keyboard.num.lock", "Num", 1, ImGuiKey_NumLock},
                        {"key.keyboard.keypad.divide", "/", 1, ImGuiKey_KeypadDivide},
                        {"key.keyboard.keypad.multiply", "*", 1, ImGuiKey_KeypadMultiply},
                        {"key.keyboard.keypad.subtract", "-", 1, ImGuiKey_KeypadSubtract}});
                addRow(caps, 2.25f,
                       {{"key.keyboard.keypad.7", "7", 1, ImGuiKey_Keypad7},
                        {"key.keyboard.keypad.8", "8", 1, ImGuiKey_Keypad8},
                        {"key.keyboard.keypad.9", "9", 1, ImGuiKey_Keypad9}});
                caps.push_back(Cap{"key.keyboard.keypad.add", "+", 3.0f, 2.25f, 1.0f, 2.0f, ImGuiKey_KeypadAdd});
                addRow(caps, 3.25f,
                       {{"key.keyboard.keypad.4", "4", 1, ImGuiKey_Keypad4},
                        {"key.keyboard.keypad.5", "5", 1, ImGuiKey_Keypad5},
                        {"key.keyboard.keypad.6", "6", 1, ImGuiKey_Keypad6}});
                addRow(caps, 4.25f,
                       {{"key.keyboard.keypad.1", "1", 1, ImGuiKey_Keypad1},
                        {"key.keyboard.keypad.2", "2", 1, ImGuiKey_Keypad2},
                        {"key.keyboard.keypad.3", "3", 1, ImGuiKey_Keypad3}});
                caps.push_back(
                        Cap{"key.keyboard.keypad.enter", "Enter", 3.0f, 4.25f, 1.0f, 2.0f, ImGuiKey_KeypadEnter});
                addRow(caps, 5.25f,
                       {{"key.keyboard.keypad.0", "0", 2, ImGuiKey_Keypad0},
                        {"key.keyboard.keypad.decimal", ".", 1, ImGuiKey_KeypadDecimal}});
            }
            return caps;
        }

        const std::vector<Cap>& keyboard() {
            static const std::vector<Cap> all = [] {
                std::vector<Cap> out = build(MAIN);
                for (Cap cap : build(NAV)) {
                    cap.x += 15.5f;
                    out.push_back(cap);
                }
                for (Cap cap : build(PAD)) {
                    cap.x += 19.0f;
                    out.push_back(cap);
                }
                return out;
            }();
            return all;
        }

        bool onBoard(const std::string& key) {
            if (key.rfind("key.mouse.", 0) == 0) {
                return key == "key.mouse.left" || key == "key.mouse.right" || key == "key.mouse.middle" ||
                        key == "key.mouse.4" || key == "key.mouse.5" || key == "key.mouse.6" || key == "key.mouse.7" ||
                        key == "key.mouse.8";
            }
            for (const Cap& cap : keyboard()) {
                if (key == cap.name) {
                    return true;
                }
            }
            return false;
        }

        Search& searchOf(const std::string& owner, const char* placeholder) {
            auto found = searches.find(owner);
            if (found != searches.end()) {
                return found->second;
            }
            Search& entry = searches[owner];
            entry.owner = owner;
            entry.binding = std::make_shared<TextBinding>(
                    owner, [owner] { return searches[owner].text; },
                    [owner](const std::string& text) { searches[owner].text = text; }, placeholder);
            entry.binding->limit = 60;
            return entry;
        }

        std::string liveText(const Search& search) {
            return Ui::trim(NewUi::textEditing == search.owner ? UiInput::get(NewUi::INPUT_TEXT) : search.text);
        }

        uint32_t warningInk() {
            return UiTheme::warningInk();
        }

        uint32_t withAlpha(uint32_t rgba, float alpha) {
            uint32_t base = (rgba >> 24) & 0xFF;
            return ((uint32_t) Ui::round((float) base * std::clamp(alpha, 0.0f, 1.0f)) << 24) | (rgba & 0x00FFFFFFu);
        }

        std::string keyText(const KeyBinds::Binding& binding) {
            std::string modifier = KeyBinds::modifierLabel(binding.modifier);
            std::string key = KeyBinds::keyLabel(binding.key);
            return modifier.empty() ? key : modifier + " + " + key;
        }

        std::string keyTitle(const std::string& key) {
            std::string label = KeyBinds::keyLabel(key);
            return label.rfind("鼠标", 0) == 0 ? label : label + " 键";
        }

        bool matches(const KeyBinds::Binding& binding, const std::string& query) {
            return query.empty() || blb::icontains(binding.name, query) || blb::icontains(binding.category, query) ||
                    blb::icontains(binding.id, query) || blb::icontains(KeyBinds::keyLabel(binding.key), query);
        }

        using Index = std::unordered_map<std::string, std::vector<const KeyBinds::Binding*>>;
        using Conflicts = std::unordered_set<const KeyBinds::Binding*>;

        Index indexOf(const KeyBinds::Table& table) {
            Index index;
            for (const KeyBinds::Binding& binding : table.bindings) {
                if (KeyBinds::bound(binding.key)) {
                    index[binding.key].push_back(&binding);
                }
            }
            return index;
        }

        Conflicts findConflicts(const Index& index) {
            Conflicts out;
            auto compare = [&out](const KeyBinds::Binding* first, const KeyBinds::Binding* second) {
                if (first != second && KeyBinds::conflicts(*first, *second)) {
                    out.insert(first);
                    out.insert(second);
                }
            };
            for (const auto& [key, bindings] : index) {
                for (size_t i = 0; i < bindings.size(); ++i) {
                    for (size_t j = i + 1; j < bindings.size(); ++j) {
                        compare(bindings[i], bindings[j]);
                    }
                    if (bindings[i]->modifier.empty() || bindings[i]->modifier == "NONE") {
                        continue;
                    }
                    for (const char* modifier : {"key.keyboard.left.shift", "key.keyboard.right.shift",
                                                  "key.keyboard.left.control", "key.keyboard.right.control",
                                                  "key.keyboard.left.alt", "key.keyboard.right.alt"}) {
                        auto found = index.find(modifier);
                        if (found != index.end()) {
                            for (const KeyBinds::Binding* other : found->second) {
                                compare(bindings[i], other);
                            }
                        }
                    }
                }
            }
            return out;
        }

        float keyDepth(float unit) {
            return std::clamp(unit * 0.1f, 2.0f, 3.5f);
        }

        float keyRadius(float unit) {
            return std::clamp(unit * 0.17f, 4.0f, 7.0f);
        }

        uint32_t shade(uint32_t face) {
            return UiRender::mix(face, 0xFF000000u, UiTheme::isLight() ? 0.16f : 0.4f);
        }

        uint32_t faceOf(int count) {
            uint32_t face = UiTheme::surfaceRaised();
            if (count == 0) {
                return UiTheme::isLight() ? face : UiRender::mix(face, 0xFFFFFFFFu, 0.08f);
            }
            return UiRender::mix(face, count > 1 ? warningInk() : UiTheme::accent(),
                                 UiTheme::isLight() ? 0.32f : 0.42f);
        }

        bool clashes(const std::vector<const KeyBinds::Binding*>& list, const Conflicts& conflicts) {
            return std::any_of(list.begin(), list.end(),
                               [&](const auto* binding) { return conflicts.contains(binding); });
        }

        int levelOf(const Index& index, const Conflicts& conflicts, const std::string& key) {
            auto found = index.find(key);
            if (found == index.end() || found->second.empty()) {
                return 0;
            }
            return clashes(found->second, conflicts) ? 2 : 1;
        }

        void centredLabel(const std::string& text, float x, float y, float w, float h, uint32_t color, float size) {
            if (text.empty()) {
                return;
            }
            float natural = UiFont::width(text, UiFont::MEDIUM, size);
            if (natural > w - 6.0f && natural > 0.0f) {
                size = std::max(7.0f, size * (w - 6.0f) / natural);
            }
            std::string shown = UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, w - 4.0f));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, size);
            UiRender::textY(shown, x + (w - shownW) * 0.5f, y, h, color, UiFont::MEDIUM, size);
        }

        void keycap(const std::string& motionId, const std::string& label, float x, float y, float w, float h,
                    float radius, float unit, int count, bool selected, bool physical, bool dim, bool labels) {
            float press = UiState::motion(motionId, selected || physical ? 1.0f : 0.0f, 26.0f);
            float saved = UiRender::alphaMul;
            if (dim) {
                UiRender::alphaMul = saved * 0.35f;
            }
            float depth = keyDepth(unit);
            float faceH = h - depth;
            float offset = depth * press;
            uint32_t face = faceOf(count);
            UiRender::roundRect(x, y + depth, w, faceH, radius, shade(face));
            UiRender::roundRect(x, y + offset, w, faceH, radius, face);
            uint32_t edge = selected                    ? UiTheme::accent()
                    : count == 0 && !UiTheme::isLight() ? UiTheme::border(UiTheme::outline())
                                                        : UiTheme::border(UiTheme::outlineSoft());
            UiRender::outline(x, y + offset, w, faceH, radius, edge, 0xF, selected ? 1.5f : 1.0f);
            if (labels) {
                float size = label.size() <= 2 ? std::clamp(unit * 0.42f, 9.0f, UiWidgets::TEXT_CONTROL)
                                               : std::clamp(unit * 0.34f, 8.0f, UiWidgets::TEXT_SMALL);
                uint32_t ink = count > 0 || !UiTheme::isLight() ? UiTheme::textPrimary() : UiTheme::textSecondary();
                centredLabel(label, x, y + offset, w, faceH, ink, size);
            }
            UiRender::alphaMul = saved;
        }

        void mouseShape(const std::string& id, const State& state, const Index& index, const Conflicts& conflicts,
                        const std::unordered_set<std::string>& matched, bool searching, float x, float y, float unit,
                        std::vector<Spot>& spots) {
            float w = MOUSE_W * unit;
            float h = MOUSE_H * unit;
            float depth = keyDepth(unit);
            float radius = w * 0.5f;
            float bodyH = h - depth;
            float buttonsH = h * 0.42f;
            float split = std::max(1.5f, unit * 0.07f);
            float sideW = unit * 0.32f;
            float sideH = unit * 0.8f;
            float sideX = x - sideW * 0.6f;
            float extraH = unit * 0.52f;
            float extraX = x + w - sideW * 0.4f;
            Spot wheel{"key.mouse.middle", x + w * 0.4f, y + buttonsH * 0.18f, w * 0.2f, buttonsH * 0.48f};
            Spot forward{"key.mouse.5", sideX, y + buttonsH + unit * 0.5f, sideW, sideH};
            Spot back{"key.mouse.4", sideX, forward.y + sideH + unit * 0.2f, sideW, sideH};
            Spot six{"key.mouse.6", extraX, y + buttonsH + unit * 0.5f, sideW, extraH};
            Spot seven{"key.mouse.7", extraX, six.y + extraH + unit * 0.16f, sideW, extraH};
            Spot eight{"key.mouse.8", extraX, seven.y + extraH + unit * 0.16f, sideW, extraH};
            Spot left{"key.mouse.left", x, y, w * 0.5f - split * 0.5f, buttonsH};
            Spot right{"key.mouse.right", x + w * 0.5f + split * 0.5f, y, w * 0.5f - split * 0.5f, buttonsH};
            auto dim = [&](const std::string& key) { return searching && matched.count(key) == 0; };

            for (const Spot* side : {&forward, &back, &six, &seven, &eight}) {
                keycap(id + "#key#" + side->name, {}, side->x, side->y, side->w, side->h, side->w * 0.5f, unit,
                       levelOf(index, conflicts, side->name), state.selected == side->name, false, dim(side->name), false);
            }
            uint32_t face = UiTheme::surfaceRaised();
            UiRender::roundRect(x, y + depth, w, bodyH, radius, shade(face));
            UiRender::roundRect(x, y, w, bodyH, radius, face);
            UiRender::outline(x, y, w, bodyH, radius, UiTheme::border(UiTheme::outlineSoft()));
            for (const Spot* zone : {&left, &right}) {
                int count = levelOf(index, conflicts, zone->name);
                bool selected = state.selected == zone->name;
                float press = UiState::motion(id + "#key#" + zone->name, selected ? 1.0f : 0.0f, 26.0f);
                float saved = UiRender::alphaMul;
                if (dim(zone->name)) {
                    UiRender::alphaMul = saved * 0.35f;
                }
                float offset = depth * press * 0.6f;
                UiRender::scissor(zone->x, zone->y, zone->w, zone->h);
                UiRender::roundRect(x, y + offset, w, bodyH, radius,
                                    UiRender::mix(faceOf(count), 0xFF000000u, 0.06f * press));
                if (selected) {
                    UiRender::outline(x, y + offset, w, bodyH, radius, UiTheme::accent(), 0xF, 1.5f);
                }
                UiRender::unscissor();
                float size = std::clamp(unit * 0.34f, 8.0f, UiWidgets::TEXT_SMALL);
                float labelY = zone->y + zone->h * 0.52f + offset;
                centredLabel(zone == &left ? "左键" : "右键", zone->x, labelY, zone->w, zone->h * 0.36f,
                             count > 0 ? UiTheme::textPrimary() : UiTheme::textSecondary(), size);
                UiRender::alphaMul = saved;
            }
            uint32_t seam = shade(face);
            UiRender::rect(x + w * 0.5f - split * 0.5f, y, split, buttonsH, seam);
            UiRender::rect(x, y + buttonsH, w, split, seam);
            keycap(id + "#key#" + wheel.name, {}, wheel.x, wheel.y, wheel.w, wheel.h, wheel.w * 0.5f, unit,
                   levelOf(index, conflicts, wheel.name), state.selected == wheel.name, false, dim(wheel.name), false);
            for (const Spot* spot : {&wheel, &forward, &back, &six, &seven, &eight, &left, &right}) {
                spots.push_back(*spot);
            }
        }

        void groupIcon(const KeyBinds::Group& group, float x, float y, float size) {
            uint32_t texture = group.id == KeyBinds::VANILLA_GROUP ? Icons::vanilla()
                    : group.id == KeyBinds::OPTIFINE_GROUP         ? Icons::loader(blb::install::loader_type::optifine)
                                                                   : Textures::of(group.icon);
            if (texture != 0) {
                ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                     UiRender::transformW(size), UiRender::transformW(size * 0.25f), texture, 0.0f,
                                     0.0f, 1.0f, 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
                return;
            }
            bool debug = group.id == KeyBinds::DEBUG_GROUP;
            bool other = group.id == KeyBinds::OTHER_GROUP;
            int glyph = debug ? 0xf188 : other ? 0xf141 : 0xf12e;
            UiRender::roundRect(x, y, size, size, size * 0.25f, UiTheme::surfaceRaised());
            UiWidgets::icon(Ui::glyph(glyph), x + size * 0.5f, y + size * 0.5f,
                            debug || other ? UiTheme::textSecondary() : UiTheme::textFaint(), size * 0.5f);
        }

        const KeyBinds::Group* groupOf(const KeyBinds::Table& table, const std::string& id) {
            for (const KeyBinds::Group& group : table.groups) {
                if (group.id == id) {
                    return &group;
                }
            }
            return nullptr;
        }

        float segmented(const std::string& id, const char* const* names, int count, int& current, float x, float y,
                        bool blocked) {
            std::vector<float> starts;
            std::vector<float> widths;
            float cursor = x + 2.0f;
            for (int i = 0; i < count; i++) {
                float w = UiFont::width(names[i], UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 32.0f;
                starts.push_back(cursor);
                widths.push_back(w);
                cursor += w;
            }
            float total = cursor + 2.0f - x;
            UiRender::roundRect(x, y, total, Page::FIELD_H, 7.0f, UiTheme::surfaceRaised());
            float slide = std::clamp(UiState::motion(id + "#slide", (float) current, 16.0f), 0.0f, (float) (count - 1));
            int lower = std::min((int) slide, count - 1);
            int upper = std::min(lower + 1, count - 1);
            float blend = slide - (float) lower;
            float pillX = UiRender::lerp(starts[(size_t) lower], starts[(size_t) upper], blend);
            float pillW = UiRender::lerp(widths[(size_t) lower], widths[(size_t) upper], blend);
            UiRender::roundRect(pillX, y + 2.0f, pillW, Page::FIELD_H - 4.0f, 5.0f, UiTheme::surface());
            UiRender::outline(pillX, y + 2.0f, pillW, Page::FIELD_H - 4.0f, 5.0f,
                              UiTheme::border(UiTheme::outlineSoft()));
            for (int i = 0; i < count; i++) {
                float segmentX = starts[(size_t) i];
                float w = widths[(size_t) i];
                bool hovered = !blocked && NewUi::hit(segmentX, y, w, Page::FIELD_H);
                float near = 1.0f - std::clamp(std::fabs(slide - (float) i), 0.0f, 1.0f);
                uint32_t rest = hovered ? UiTheme::textSecondary() : UiTheme::textMuted();
                UiRender::textY(names[i], segmentX + 16.0f, y, Page::FIELD_H,
                                UiRender::mix(rest, UiTheme::textPrimary(), near), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                if (!blocked && NewUi::click(segmentX, y, w, Page::FIELD_H)) {
                    current = i;
                }
            }
            return total;
        }

        void popoverBody();

        struct Row {
            std::string id;
            std::string name;
            std::string group;
            float t = 0.0f;
            bool leaving = false;
        };

        class KeyPopover final : public Overlay {
        public:
            bool active = false;
            std::string editor;
            std::string key;
            std::filesystem::path file;
            bool legacy = false;
            Spot anchor;
            float top = 0.0f;
            float bottom = 0.0f;
            std::string rowsKey;
            std::vector<Row> rows;
            std::string note;
            PageTransition pages{0};

            bool isOpen() override {
                return active;
            }

            void release() override {
                if (!active) {
                    return;
                }
                active = false;
                auto found = states.find(editor);
                if (found != states.end() && found->second.selected == key) {
                    found->second.selected.clear();
                    found->second.adding = false;
                }
            }

            void draw() override {
                popoverBody();
            }

            bool scroll(double) override {
                return contains(NewUi::mouseX, NewUi::mouseY);
            }
        };

        KeyPopover popover;

        void closePopover() {
            UiOverlays::close(&popover);
            popover.release();
        }

        void openPopover(const std::string& editor, const std::string& key, const std::filesystem::path& file,
                         bool legacy) {
            if (popover.active && popover.editor == editor && popover.key == key) {
                return;
            }
            if (!popover.active) {
                UiOverlays::addLayerBefore(&popover, UiOverlays::sub);
                UiOverlays::openOn(nullptr, &popover);
                UiState::remove(OPEN_MOTION);
                UiState::remove(HEIGHT_MOTION);
                popover.pages.page = 0;
                popover.pages.mix = 1.0f;
            }
            popover.active = true;
            popover.editor = editor;
            popover.key = key;
            popover.file = file;
            popover.legacy = legacy;
        }

        float smoothRow(float t) {
            return t * t * (3.0f - 2.0f * t);
        }

        void syncRows(const std::vector<const KeyBinds::Binding*>& here) {
            bool fresh = popover.rowsKey != popover.key;
            if (fresh) {
                popover.rows.clear();
                popover.rowsKey = popover.key;
            }
            std::vector<Row> ordered;
            for (const KeyBinds::Binding* binding : here) {
                std::string modifier = KeyBinds::modifierLabel(binding->modifier);
                Row row;
                row.id = binding->id;
                row.name = modifier.empty() ? binding->name : modifier + " + " + binding->name;
                row.group = binding->group;
                row.t = fresh ? 1.0f : 0.0f;
                for (const Row& old : popover.rows) {
                    if (old.id == row.id) {
                        row.t = old.t;
                        break;
                    }
                }
                ordered.push_back(std::move(row));
            }
            for (size_t i = 0; i < popover.rows.size(); i++) {
                const Row& old = popover.rows[i];
                bool kept = std::any_of(here.begin(), here.end(),
                                        [&old](const KeyBinds::Binding* binding) { return binding->id == old.id; });
                if (!kept) {
                    Row gone = old;
                    gone.leaving = true;
                    ordered.insert(ordered.begin() + (std::ptrdiff_t) std::min(i, ordered.size()), std::move(gone));
                }
            }
            float step = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 14.0f);
            bool moving = false;
            for (Row& row : ordered) {
                float target = row.leaving ? 0.0f : 1.0f;
                row.t += (target - row.t) * step;
                if (std::fabs(target - row.t) < 0.01f) {
                    row.t = target;
                } else {
                    moving = true;
                }
            }
            ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                                         [](const Row& row) { return row.leaving && row.t <= 0.0f; }),
                          ordered.end());
            popover.rows = std::move(ordered);
            if (moving) {
                UiState::keepAnimating();
            }
        }

        void popoverBody() {
            auto found = states.find(popover.editor);
            if (!popover.active || found == states.end() || found->second.selected != popover.key) {
                if (popover.active) {
                    closePopover();
                }
                return;
            }
            State& state = found->second;
            const std::string& editor = popover.editor;
            const KeyBinds::Table& table = KeyBinds::table(popover.file);
            Search& adding = searchOf(editor + "#add", "搜索");
            std::string wanted = liveText(adding);
            bool choosing = state.adding && state.addGroup.empty();
            const KeyBinds::Group* chosenGroup = state.adding ? groupOf(table, state.addGroup) : nullptr;
            if (state.adding && !choosing && chosenGroup == nullptr) {
                state.addGroup.clear();
                choosing = true;
            }
            std::vector<const KeyBinds::Binding*> here;
            std::vector<const KeyBinds::Binding*> candidates;
            std::vector<const KeyBinds::Group*> groups;
            for (const KeyBinds::Binding& binding : table.bindings) {
                if (binding.key == popover.key) {
                    here.push_back(&binding);
                } else if (state.adding && !choosing && binding.group == state.addGroup && matches(binding, wanted)) {
                    candidates.push_back(&binding);
                }
            }
            if (choosing) {
                for (const KeyBinds::Group& group : table.groups) {
                    if (wanted.empty() || blb::icontains(group.label, wanted) || blb::icontains(group.id, wanted)) {
                        groups.push_back(&group);
                    }
                }
            }
            size_t rowCount = choosing ? groups.size() : candidates.size();
            float rowsH = 0.0f;
            if (!state.adding) {
                syncRows(here);
                for (const Row& row : popover.rows) {
                    rowsH += POP_ROW * smoothRow(row.t);
                }
            }
            float areaH = state.adding ? (float) std::clamp((int) rowCount, 1, POP_ROWS) * POP_ROW
                                       : std::clamp(rowsH, POP_ROW, (float) POP_ROWS * POP_ROW);
            float fullH = state.adding ? POP_HEAD + Page::FIELD_H + 14.0f + areaH + 10.0f : POP_HEAD + areaH + POP_FOOT;
            float h = UiState::motionFrom(HEIGHT_MOTION, fullH, 18.0f, fullH);
            int view = choosing ? 1 : state.adding ? 2 : 0;
            if (view != popover.pages.page) {
                popover.pages.page = view;
                popover.pages.mix = 0.0f;
            }
            const Spot& anchor = popover.anchor;
            float arrowX = anchor.x + anchor.w * 0.5f;
            float columnLeft = Page::columnLeft();
            float columnRight = columnLeft + Page::columnWidth();
            float x = std::clamp(arrowX - POP_W * 0.5f, columnLeft, std::max(columnLeft, columnRight - POP_W));
            float y = anchor.y + anchor.h + ARROW + 4.0f;
            bool below = true;
            if (y + h > popover.bottom - 6.0f && anchor.y - ARROW - 4.0f - h >= popover.top) {
                below = false;
                y = anchor.y - ARROW - 4.0f - h;
            }
            float open = UiState::motionFrom(OPEN_MOTION, 1.0f, 18.0f, 0.0f);
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * open;
            NewUi::markOverlayUnion(x, y - ARROW, POP_W, h + 2.0f * ARROW);
            NewUi::markAbove(x, y - ARROW, POP_W, h + 2.0f * ARROW);

            UiRender::shadow(x, y, POP_W, h, 12.0f, 16.0f, 6.0f, UiTheme::shadow());
            UiRender::roundRect(x, y, POP_W, h, 12.0f, UiTheme::surface());
            UiRender::outline(x, y, POP_W, h, 12.0f, UiTheme::border(UiTheme::outlineSoft()));
            float tipX = std::clamp(arrowX, x + 18.0f, x + POP_W - 18.0f);
            if (below) {
                UiRender::triangle(tipX, y - ARROW + 1.0f, ARROW, 0.0f, UiTheme::surface());
            } else {
                UiRender::triangle(tipX, y + h + ARROW - 1.0f, ARROW, 180.0f, UiTheme::surface());
            }
            UiRender::scissor(x, y, POP_W, h);
            popover.pages.begin();

            float innerX = x + 16.0f;
            float innerR = x + POP_W - 16.0f;
            std::string title = choosing     ? std::string("选择模组")
                    : chosenGroup != nullptr ? chosenGroup->label
                                             : keyTitle(popover.key);
            float titleX = innerX;
            if (state.adding) {
                if (Page::iconButton(editor + "#pop#back", 0xf053, "返回", x + 8.0f, y + 8.0f, false)) {
                    if (choosing) {
                        state.adding = false;
                    } else {
                        state.addGroup.clear();
                    }
                    searches[adding.owner].text.clear();
                    if (NewUi::textEditing == adding.owner) {
                        NewUi::cancelTextEdit();
                    }
                }
                titleX = x + 40.0f;
                if (chosenGroup != nullptr) {
                    groupIcon(*chosenGroup, titleX, y + (POP_HEAD - 20.0f) * 0.5f, 20.0f);
                    titleX += 28.0f;
                }
            }
            UiRender::textY(UiFont::fitEnd(title, UiFont::MEDIUM, UiWidgets::TEXT_TITLE, innerR - titleX), titleX, y,
                            POP_HEAD, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_TITLE);
            if (!here.empty()) {
                popover.note = std::to_string(here.size()) + " 个绑定";
            }
            float noteShown =
                    UiState::motion("keybinds#popover#note", !state.adding && !here.empty() ? 1.0f : 0.0f, 14.0f);
            if (noteShown > 0.001f && !popover.note.empty()) {
                float savedNote = UiRender::alphaMul;
                UiRender::alphaMul = savedNote * noteShown;
                float noteW = UiFont::width(popover.note, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(popover.note, innerR - noteW, y, POP_HEAD,
                                clashes(here, findConflicts(indexOf(table))) ? warningInk() : UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::alphaMul = savedNote;
            }
            float listTop = y + POP_HEAD;
            if (state.adding) {
                TextFieldWidget field(adding.binding);
                float fieldY = y + POP_HEAD + 8.0f;
                field.box(innerX, fieldY, innerR - innerX, Page::FIELD_H)
                        .bounds(innerX, fieldY, innerR - innerX, Page::FIELD_H)
                        .block(false);
                field.paint();
                listTop = fieldY + Page::FIELD_H + 6.0f;
            }

            if (!state.adding) {
                float emptiness = 1.0f - std::clamp(rowsH / POP_ROW, 0.0f, 1.0f);
                if (emptiness > 0.001f) {
                    float savedEmpty = UiRender::alphaMul;
                    UiRender::alphaMul = savedEmpty * emptiness;
                    UiRender::textY("没有绑定", innerX, listTop, areaH, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    UiRender::alphaMul = savedEmpty;
                }
                if (!popover.rows.empty()) {
                    float listX = x + 6.0f;
                    float listW = POP_W - 12.0f - 10.0f;
                    Page::Scroll scroll(editor + "#pop#list", listTop, listTop + areaH, rowsH, listX, listW);
                    bool outside = scroll.begin();
                    float cursor = listTop - scroll.offset + std::max(0.0f, (areaH - rowsH) * 0.5f);
                    std::string removed;
                    for (const Row& row : popover.rows) {
                        float rowH = POP_ROW * smoothRow(row.t);
                        float rowY = cursor;
                        cursor += rowH;
                        if (rowH < 0.5f || rowY + rowH < listTop || rowY > listTop + areaH) {
                            continue;
                        }
                        float contentY = rowY + (rowH - POP_ROW) * 0.5f;
                        float savedRow = UiRender::alphaMul;
                        UiRender::alphaMul = savedRow * row.t;
                        bool live = !outside && !row.leaving && row.t > 0.95f;
                        if (live && NewUi::hit(listX, rowY, listW, rowH)) {
                            UiRender::roundRect(listX, rowY + 2.0f, listW, rowH - 4.0f, 7.0f, UiTheme::overlay(12));
                        }
                        float right = listX + listW - 10.0f;
                        float removeX = right - 22.0f;
                        float removeY = contentY + (POP_ROW - 22.0f) * 0.5f;
                        bool removeHovered = live && NewUi::hit(removeX, removeY, 22.0f, 22.0f);
                        if (removeHovered) {
                            UiRender::roundRect(removeX + 1.0f, removeY + 1.0f, 20.0f, 20.0f, 10.0f,
                                                withAlpha(UiTheme::danger(), 0.14f));
                        }
                        UiWidgets::icon(Ui::glyph(0xf00d), removeX + 11.0f, contentY + POP_ROW * 0.5f,
                                        removeHovered ? UiTheme::danger() : UiTheme::textMuted(), 11.0f);
                        if (removeHovered) {
                            Page::tooltip("解除绑定", removeX, removeY, 22.0f, 22.0f);
                        }
                        if (live && NewUi::click(removeX, removeY, 22.0f, 22.0f)) {
                            removed = row.id;
                        }
                        if (const KeyBinds::Group* group = groupOf(table, row.group)) {
                            groupIcon(*group, listX + 10.0f, contentY + (POP_ROW - 18.0f) * 0.5f, 18.0f);
                        }
                        float nameX = listX + 36.0f;
                        UiRender::textY(UiFont::fitEnd(row.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                       removeX - 8.0f - nameX),
                                        nameX, contentY, POP_ROW, UiTheme::textPrimary(), UiFont::MEDIUM,
                                        UiWidgets::TEXT_CONTROL);
                        UiRender::alphaMul = savedRow;
                    }
                    scroll.end();
                    if (!removed.empty()) {
                        std::string error = KeyBinds::unbind(popover.file, removed, popover.legacy);
                        if (!error.empty()) {
                            Notices::add(Notices::Kind::Failed, error, 4000);
                        }
                    }
                }
            } else if (rowCount == 0) {
                std::string empty = choosing ? "没有匹配的模组" : "没有可添加的绑定";
                UiRender::textY(empty, innerX, listTop, POP_ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else {
                float listX = x + 6.0f;
                float listW = POP_W - 12.0f - 10.0f;
                std::string scrollId = editor + (choosing ? "#pop#mods" : "#pop#add");
                Page::Scroll scroll(scrollId, listTop, listTop + areaH, (float) rowCount * POP_ROW, listX, listW);
                bool outside = scroll.begin();
                const KeyBinds::Binding* picked = nullptr;
                const KeyBinds::Group* entered = nullptr;
                for (size_t i = 0; i < rowCount; i++) {
                    float rowY = listTop - scroll.offset + (float) i * POP_ROW;
                    if (rowY + POP_ROW < listTop || rowY > listTop + areaH) {
                        continue;
                    }
                    bool hovered = !outside && NewUi::hit(listX, rowY, listW, POP_ROW);
                    if (hovered) {
                        UiRender::roundRect(listX, rowY + 2.0f, listW, POP_ROW - 4.0f, 7.0f, UiTheme::overlay(12));
                    }
                    float right = listX + listW - 10.0f;
                    if (choosing) {
                        const KeyBinds::Group& group = *groups[i];
                        groupIcon(group, listX + 10.0f, rowY + (POP_ROW - 20.0f) * 0.5f, 20.0f);
                        std::string count = std::to_string(group.count);
                        float countW = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                        UiWidgets::icon(Ui::glyph(0xf054), right - 4.0f, rowY + POP_ROW * 0.5f, UiTheme::textFaint(),
                                        9.0f);
                        UiRender::textY(count, right - 16.0f - countW, rowY, POP_ROW, UiTheme::textMuted(),
                                        UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                        float labelX = listX + 38.0f;
                        UiRender::textY(UiFont::fitEnd(group.label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                       right - 26.0f - countW - labelX),
                                        labelX, rowY, POP_ROW, UiTheme::textPrimary(), UiFont::MEDIUM,
                                        UiWidgets::TEXT_CONTROL);
                        if (!outside && NewUi::click(listX, rowY, listW, POP_ROW)) {
                            entered = &group;
                        }
                        continue;
                    }
                    const KeyBinds::Binding& binding = *candidates[i];
                    std::string current = KeyBinds::keyLabel(binding.key);
                    float currentW = std::min(UiFont::width(current, UiFont::MEDIUM, UiWidgets::TEXT_SMALL), 90.0f);
                    UiRender::textY(UiFont::fitEnd(current, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, currentW),
                                    right - currentW, rowY, POP_ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    right -= currentW + 10.0f;
                    if (!outside && NewUi::click(listX, rowY, listW, POP_ROW)) {
                        picked = &binding;
                    }
                    float nameX = listX + 10.0f;
                    std::string modifier = KeyBinds::modifierLabel(binding.modifier);
                    std::string name = modifier.empty() ? binding.name : modifier + " + " + binding.name;
                    UiRender::textY(UiFont::fitEnd(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, right - nameX), nameX,
                                    rowY, POP_ROW, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                }
                scroll.end();
                if (entered != nullptr) {
                    state.addGroup = entered->id;
                    searches[adding.owner].text.clear();
                    if (NewUi::textEditing == adding.owner) {
                        NewUi::cancelTextEdit();
                    }
                }
                if (picked != nullptr) {
                    std::string error = KeyBinds::assign(popover.file, picked->id, popover.key, popover.legacy);
                    if (!error.empty()) {
                        Notices::add(Notices::Kind::Failed, error, 4000);
                    }
                    state.adding = false;
                    state.addGroup.clear();
                }
            }
            if (!state.adding) {
                std::string add = "添加";
                float addW = Page::buttonWidth(add, 0xf067);
                float addY = y + h - POP_FOOT + (POP_FOOT - Page::FIELD_H) * 0.5f;
                if (Page::button(editor + "#pop#add", add, innerR - addW, addY, addW, true, false, 0xf067)) {
                    state.adding = true;
                    state.addGroup.clear();
                    searches[adding.owner].text.clear();
                }
            }
            popover.pages.end();
            UiRender::unscissor();
            UiRender::alphaMul = saved;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && NewUi::textEditing.empty()) {
                closePopover();
            }
        }

        void copyBody(float x, float y, float w, float h) {
            UiRender::textY("复制按键绑定到", x + 20.0f, y + 16.0f, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            float top = y + 56.0f;
            float bottom = y + h - 64.0f;
            constexpr float ROW = 36.0f;
            int chosen = 0;
            for (const CopyRow& row : copyRows) {
                chosen += row.chosen ? 1 : 0;
            }
            if (copyRows.empty()) {
                UiRender::textY("没有其他使用独立设置的实例", x + 20.0f, top + 20.0f, 24.0f, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            } else {
                float listX = x + 12.0f;
                float listW = w - 24.0f - 10.0f;
                Page::Scroll scroll("keybinds#copy#scroll", top, bottom, (float) copyRows.size() * ROW, listX, listW);
                bool outside = scroll.begin();
                for (size_t i = 0; i < copyRows.size(); i++) {
                    float rowY = top - scroll.offset + (float) i * ROW;
                    if (rowY + ROW < top || rowY > bottom) {
                        continue;
                    }
                    CopyRow& row = copyRows[i];
                    bool hovered = !outside && NewUi::hit(listX, rowY, listW, ROW);
                    if (hovered) {
                        UiRender::roundRect(listX, rowY + 2.0f, listW, ROW - 4.0f, 8.0f, UiTheme::overlay(12));
                    }
                    float boxX = listX + 10.0f;
                    float boxY = rowY + (ROW - 18.0f) * 0.5f;
                    if (row.chosen) {
                        UiRender::roundRect(boxX, boxY, 18.0f, 18.0f, 5.0f, UiTheme::accent());
                        UiWidgets::icon(Ui::glyph(0xf00c), boxX + 9.0f, boxY + 9.0f, UiTheme::onAccent(), 10.0f);
                    } else {
                        UiRender::outline(boxX, boxY, 18.0f, 18.0f, 5.0f, UiTheme::outline(), 0xF, 1.4f);
                    }
                    float detailW = UiFont::width(row.detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    UiRender::textY(row.detail, listX + listW - 10.0f - detailW, rowY, ROW, UiTheme::textMuted(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    float labelX = boxX + 28.0f;
                    UiRender::textY(UiFont::fitEnd(row.label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                   listX + listW - 20.0f - detailW - labelX),
                                    labelX, rowY, ROW, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    if (!outside && NewUi::click(listX, rowY, listW, ROW)) {
                        row.chosen = !row.chosen;
                    }
                }
                scroll.end();
            }
            float buttonY = y + h - 20.0f - Page::FIELD_H;
            std::string label = chosen > 0 ? "复制到 " + std::to_string(chosen) + " 处" : std::string("复制");
            float copyW = std::max(88.0f, Page::buttonWidth(label));
            float copyX = x + w - 20.0f - copyW;
            if (Page::button("keybinds#copy#confirm", label, copyX, buttonY, copyW, true, chosen == 0)) {
                std::vector<KeyBinds::Target> targets;
                for (const CopyRow& row : copyRows) {
                    if (row.chosen) {
                        targets.push_back(KeyBinds::Target{row.file, row.legacy});
                    }
                }
                std::string error = KeyBinds::copy(copyFrom, targets);
                Dialog::close();
                if (error.empty()) {
                    Notices::add(Notices::Kind::Done, "已复制按键绑定到 " + std::to_string(targets.size()) + " 处",
                                 3000);
                } else {
                    Notices::add(Notices::Kind::Failed, error, 5000);
                }
                return;
            }
            float cancelW = Page::buttonWidth("取消");
            if (Page::button("keybinds#copy#cancel", "取消", copyX - 8.0f - cancelW, buttonY, cancelW, false, false)) {
                Dialog::close();
            }
        }

        std::wstring normal(const std::filesystem::path& file) {
            std::wstring text = blb::fs::absolute(file).lexically_normal().wstring();
            for (wchar_t& c : text) {
                c = (wchar_t) std::towlower(c);
            }
            return text;
        }

        void openCopy(const std::filesystem::path& file) {
            copyFrom = file;
            copyRows.clear();
            std::set<std::wstring> seen{normal(file)};
            for (const Instances::Entry& entry : Instances::list()) {
                std::filesystem::path target = GameOptions::fileOf(entry);
                if (!seen.insert(normal(target)).second) {
                    continue;
                }
                CopyRow row;
                row.file = target;
                row.legacy = KeyBinds::legacyVersion(entry.info.minecraft_version);
                if (GameOptions::ownFile(entry)) {
                    row.label = entry.info.id;
                    row.detail = entry.info.minecraft_version;
                } else {
                    const Instances::Source* source = Instances::source(entry.source);
                    row.label = (source != nullptr ? source->label : std::string("游戏目录")) + " 共用设置";
                }
                copyRows.push_back(std::move(row));
            }
            Dialog::open("keybinds#copy", 460.0f, 440.0f, copyBody);
        }
    }

    int count(const std::filesystem::path& file) {
        int bound = 0;
        for (const KeyBinds::Binding& binding : KeyBinds::table(file).bindings) {
            bound += KeyBinds::bound(binding.key) ? 1 : 0;
        }
        return bound;
    }

    void draw(const std::string& id, const std::filesystem::path& file, bool legacy, const std::string& note, float top,
              bool blocked) {
        State& state = states[id];
        const KeyBinds::Table& table = KeyBinds::table(file);
        bool typing = !NewUi::textEditing.empty() || !UiInput::focused.empty();
        int frame = ImGui::GetFrameCount();
        std::wstring target = normal(file);
        if (state.file != target || state.lastFrame + 1 < frame) {
            state.capturing.clear();
            state.selected.clear();
            state.adding = false;
            state.addGroup.clear();
            state.boardShown = false;
            if (popover.active && popover.editor == id) {
                closePopover();
            }
        }
        state.file = std::move(target);
        state.lastFrame = frame;
        bool mousePressed = false;
        for (int button = 0; button < 5; ++button) {
            mousePressed = mousePressed || ImGui::IsMouseClicked(button);
        }
        bool clickedOutside = mousePressed &&
                !NewUi::rawHit(Page::columnLeft(), top, Page::columnWidth(), MainWindow::height() - 14.0f - top);
        if (blocked || typing || !UiOverlays::isEmpty() || NewUi::clickConsumed || clickedOutside) {
            state.capturing.clear();
        }

        if (!state.capturing.empty()) {
            UiState::keepAnimating();
            if (frame > state.captureFrame) {
                std::string pressed;
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    pressed = KeyBinds::UNBOUND;
                }
                for (const Cap& cap : keyboard()) {
                    if (pressed.empty() && cap.key != ImGuiKey_None && ImGui::IsKeyPressed(cap.key, false)) {
                        pressed = cap.name;
                    }
                }
                const char* const buttons[] = {"key.mouse.left", "key.mouse.right", "key.mouse.middle", "key.mouse.4",
                                               "key.mouse.5"};
                for (int button = 0; button < 5 && pressed.empty(); button++) {
                    if (ImGui::IsMouseClicked(button)) {
                        pressed = buttons[button];
                    }
                }
                if (!pressed.empty()) {
                    std::string error = KeyBinds::assign(file, state.capturing, pressed, legacy);
                    if (!error.empty()) {
                        Notices::add(Notices::Kind::Failed, error, 4000);
                    }
                    state.capturing.clear();
                    NewUi::clickConsumed = true;
                    NewUi::rightClick = false;
                }
            }
        } else if (!typing && !blocked && (UiOverlays::isEmpty() || UiOverlays::top() == &popover) &&
                   state.boardShown && NewUi::rawHit(state.boardX, state.boardY, state.boardW, state.boardH)) {
            for (const Cap& cap : keyboard()) {
                if (cap.key != ImGuiKey_None && (cap.key != ImGuiKey_Escape || !popover.active) &&
                    ImGui::IsKeyPressed(cap.key, false)) {
                    state.page = 0;
                    state.selected = cap.name;
                    state.adding = false;
                }
            }
        }

        Index byKey = indexOf(table);
        Conflicts clashing = findConflicts(byKey);
        int conflicts = 0;
        for (const auto& item : byKey) {
            conflicts += clashes(item.second, clashing) ? 1 : 0;
        }
        Search& search = searchOf(id + "#search", "搜索按键绑定");
        std::string query = liveText(search);
        bool searching = !query.empty();
        std::unordered_set<std::string> matched;
        std::unordered_map<std::string, int> hits;
        for (const KeyBinds::Binding& binding : table.bindings) {
            if (matches(binding, query)) {
                hits[binding.group]++;
                if (searching && KeyBinds::bound(binding.key)) {
                    matched.insert(binding.key);
                }
            }
        }
        std::vector<const KeyBinds::Group*> tabs;
        for (const KeyBinds::Group& group : table.groups) {
            if (!searching || hits.count(group.id) != 0) {
                tabs.push_back(&group);
            }
        }
        if (!tabs.empty() && std::none_of(tabs.begin(), tabs.end(), [&](const KeyBinds::Group* group) {
                return group->id == state.group;
            })) {
            state.group = tabs.front()->id;
        }
        std::vector<const KeyBinds::Binding*> shown;
        for (const KeyBinds::Binding& binding : table.bindings) {
            if (binding.group == state.group && matches(binding, query)) {
                shown.push_back(&binding);
            }
        }

        float left = Page::columnLeft();
        float width = Page::columnWidth();
        float bottom = MainWindow::height() - 14.0f;
        float unit = std::clamp((width - 2.0f * BOARD_PAD) / KEYBOARD_UNITS, 16.0f, 46.0f);
        float boardH = 2.0f * BOARD_PAD + BOARD_ROWS * unit;
        float railW = std::clamp(width * 0.3f, RAIL_MIN, RAIL_MAX);
        float listX = left + railW + 14.0f;
        float listW = width - railW - 14.0f;
        float regionH =
                tabs.empty() ? 70.0f : std::max((float) tabs.size() * RAIL_ROW, (float) shown.size() * Page::ROW_H);
        float contentH = HEAD_H + Page::FIELD_H + 10.0f + boardH + 14.0f + Page::FIELD_H + 12.0f + regionH + 8.0f;
        Page::Scroll body(id + "#scroll", top, bottom, contentH);
        bool outside = body.begin();
        bool under = blocked || outside;
        float y = body.top - body.offset;

        UiRender::textY(UiFont::fitEnd(note, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, width * 0.6f), left + 2.0f, y,
                        HEAD_H - 8.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        std::string copyLabel = "复制到其他实例";
        float copyW = Page::buttonWidth(copyLabel, 0xf0c5);
        if (Page::button(id + "#copy", copyLabel, left + width - copyW, y + (HEAD_H - 8.0f - Page::FIELD_H) * 0.5f,
                         copyW, false, under || table.bindings.empty(), 0xf0c5)) {
            openCopy(file);
        }
        y += HEAD_H;
        int page = state.page;
        segmented(id + "#page", PAGES, 2, page, left, y, under);
        if (page != state.page) {
            state.page = page;
            state.selected.clear();
        }
        y += Page::FIELD_H + 10.0f;

        Page::card(left, y, width, boardH);
        state.boardShown = true;
        state.boardX = left;
        state.boardY = y;
        state.boardW = width;
        state.boardH = boardH;
        std::vector<Spot> spots;
        float keysY = y + BOARD_PAD;
        float pan = UiState::motion(id + "#pan", (float) state.page, 9.0f);
        float eased = pan * pan * (3.0f - 2.0f * pan);
        float innerX = left + BOARD_PAD;
        float innerW = width - 2.0f * BOARD_PAD;
        float keyboardW = KEYBOARD_UNITS * unit;
        float mouseUnit = (boardH - 2.0f * BOARD_PAD) / MOUSE_H;
        float mouseW = MOUSE_W * mouseUnit;
        float mouseWorld = keyboardW + DESK_GAP * unit;
        float camera = UiRender::lerp((innerW - keyboardW) * 0.5f, innerW * 0.5f - (mouseWorld + mouseW * 0.5f), eased);
        float originX = innerX + camera;
        auto inView = [&](const Spot& spot) { return spot.x + spot.w > left && spot.x < left + width; };
        UiRender::scissor(left + 1.0f, y + 1.0f, width - 2.0f, boardH - 2.0f);
        float saved = UiRender::alphaMul;
        UiRender::alphaMul = saved * UiRender::lerp(1.0f, 0.3f, eased);
        float gap = std::clamp(unit * 0.12f, 2.5f, 5.0f);
        float radius = keyRadius(unit);
        for (const Cap& cap : keyboard()) {
            Spot spot{cap.name, originX + cap.x * unit + gap * 0.5f, keysY + cap.y * unit + gap * 0.5f,
                      cap.w * unit - gap, cap.h * unit - gap};
            if (!inView(spot)) {
                continue;
            }
            bool physical = !typing && cap.key != ImGuiKey_None && ImGui::IsKeyDown(cap.key);
            keycap(id + "#key#" + cap.name, cap.label, spot.x, spot.y, spot.w, spot.h, radius, unit,
                   levelOf(byKey, clashing, cap.name), state.selected == cap.name, physical,
                   searching && matched.count(cap.name) == 0, true);
            if (state.page == 0) {
                spots.push_back(std::move(spot));
            }
        }
        UiRender::alphaMul = saved * UiRender::lerp(0.3f, 1.0f, eased);
        std::vector<Spot> mouse;
        if (originX + mouseWorld - mouseUnit < left + width) {
            mouseShape(id, state, byKey, clashing, matched, searching, originX + mouseWorld, keysY, mouseUnit, mouse);
        }
        if (state.page == 1) {
            spots.insert(spots.end(), mouse.begin(), mouse.end());
        }
        UiRender::alphaMul = saved;
        UiRender::unscissor();
        bool panning = std::fabs(pan - (float) state.page) > 0.01f;
        const Spot* hovered = nullptr;
        if (!under && !panning) {
            for (const Spot& spot : spots) {
                if (NewUi::hit(spot.x, spot.y, spot.w, spot.h)) {
                    hovered = &spot;
                    break;
                }
            }
        }
        if (hovered != nullptr) {
            UiCursor::hand();
            if (hovered->name.rfind("key.mouse.", 0) == 0) {
                Page::tooltip(keyTitle(hovered->name), hovered->x, hovered->y, hovered->w, hovered->h);
            }
            if (NewUi::click(hovered->x, hovered->y, hovered->w, hovered->h)) {
                state.selected = state.selected == hovered->name ? std::string() : hovered->name;
                state.adding = false;
            }
        }
        y += boardH + 14.0f;

        TextFieldWidget field(search.binding);
        field.box(left, y, SEARCH_W, Page::FIELD_H).bounds(left, y, SEARCH_W, Page::FIELD_H).block(under);
        field.paint();
        std::string total = "共 " + std::to_string(table.bindings.size()) + " 个绑定";
        std::string conflict = conflicts > 0 ? "，" + std::to_string(conflicts) + " 个按键有冲突" : std::string();
        float totalW = UiFont::width(total, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        float conflictW = UiFont::width(conflict, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        float countsX = left + width - totalW - conflictW;
        UiRender::textY(total, countsX, y, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
        if (!conflict.empty()) {
            UiRender::textY(conflict, countsX + totalW, y, Page::FIELD_H, warningInk(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }
        if (table.naming) {
            Page::spinner(countsX - 14.0f, y + Page::FIELD_H * 0.5f, 6.0f, UiTheme::accent());
        }
        y += Page::FIELD_H + 12.0f;

        if (tabs.empty()) {
            std::string text = !table.exists ? "还没有按键设置，启动一次游戏后就会生成"
                    : searching              ? "没有找到“" + query + "”"
                                             : "没有按键绑定";
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(text, left + (width - textW) * 0.5f, y + 20.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        } else {
            for (size_t i = 0; i < tabs.size(); i++) {
                float itemY = y + (float) i * RAIL_ROW;
                if (!Page::shown(itemY, RAIL_ROW)) {
                    continue;
                }
                const KeyBinds::Group& group = *tabs[i];
                bool selected = group.id == state.group;
                bool itemHovered = !under && NewUi::hit(left, itemY, railW, RAIL_ROW - 2.0f);
                float lit = UiState::motion(id + "#rail#" + group.id,
                                            selected              ? 1.0f
                                                    : itemHovered ? 0.5f
                                                                  : 0.0f,
                                            20.0f);
                if (lit > 0.001f) {
                    UiRender::roundRect(left, itemY + 1.0f, railW, RAIL_ROW - 2.0f, 8.0f,
                                        selected ? UiTheme::surfaceRaised() : UiTheme::overlay(Ui::round(20 * lit)));
                }
                groupIcon(group, left + 10.0f, itemY + (RAIL_ROW - 20.0f) * 0.5f, 20.0f);
                int count = searching ? hits[group.id] : group.count;
                std::string countText = std::to_string(count);
                float countW = UiFont::width(countText, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(countText, left + railW - 10.0f - countW, itemY, RAIL_ROW, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float labelX = left + 38.0f;
                UiRender::textY(UiFont::fitEnd(group.label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                               left + railW - 18.0f - countW - labelX),
                                labelX, itemY, RAIL_ROW, selected ? UiTheme::textPrimary() : UiTheme::textSecondary(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (!under && NewUi::click(left, itemY, railW, RAIL_ROW - 2.0f)) {
                    state.group = group.id;
                }
            }

            int count = (int) shown.size();
            float rowW = listW;
            float listH = (float) count * Page::ROW_H;
            float high = body.top + STICKY_GAP - y;
            float low = body.bottom - STICKY_GAP - listH - y;
            if (low >= high || state.listGroup != state.group) {
                state.listShift = high;
                state.listGroup = state.group;
            } else {
                state.listShift = std::clamp(state.listShift, low, high);
            }
            state.listShift = std::clamp(state.listShift, 0.0f, std::max(0.0f, (float) tabs.size() * RAIL_ROW - listH));
            float listTop = y + state.listShift;
            Page::listCard(listX, listTop, rowW, Page::ROW_H, count);
            for (int i = 0; i < count; i++) {
                float rowY = listTop + (float) i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const KeyBinds::Binding& binding = *shown[i];
                bool rowHovered = !under && NewUi::hit(listX, rowY, rowW, Page::ROW_H);
                if (rowHovered) {
                    int corners = Page::rowCorners(i, count);
                    UiRender::roundRect(listX, rowY, rowW, Page::ROW_H, 12.0f, UiTheme::overlay(8), corners);
                }
                bool capturing = state.capturing == binding.id;
                bool isBound = KeyBinds::bound(binding.key);
                bool clash = isBound && clashing.contains(&binding);
                std::string cap = capturing ? std::string("按下按键，Esc 解除绑定") : keyText(binding);
                float capW = std::max(64.0f, UiFont::width(cap, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 20.0f);
                float capX = listX + rowW - 12.0f - capW;
                float capY = rowY + (Page::ROW_H - 24.0f) * 0.5f;
                bool capHovered = !under && NewUi::hit(capX, capY, capW, 24.0f);
                uint32_t capFill = capturing ? withAlpha(UiTheme::accent(), 0.18f)
                        : isBound            ? UiTheme::surfaceRaised()
                                             : UiTheme::overlay(capHovered ? 12 : 0);
                UiRender::roundRect(capX, capY, capW, 24.0f, 7.0f, capFill);
                if (capturing) {
                    float pulse = 0.5f + 0.5f * std::sin((float) ImGui::GetTime() * 6.0f);
                    UiRender::outline(capX, capY, capW, 24.0f, 7.0f, withAlpha(UiTheme::accent(), 0.5f + 0.5f * pulse),
                                      0xF, 1.4f);
                } else {
                    UiRender::outline(capX, capY, capW, 24.0f, 7.0f,
                                      capHovered ? UiTheme::border(UiTheme::outline())
                                                 : UiTheme::border(UiTheme::outlineSoft()));
                }
                uint32_t capInk = capturing ? UiTheme::accentText()
                        : clash             ? warningInk()
                        : isBound           ? UiTheme::textPrimary()
                                            : UiTheme::textMuted();
                float capTextW = UiFont::width(cap, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(cap, capX + (capW - capTextW) * 0.5f, capY, 24.0f, capInk, UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                float nameX = listX + 16.0f;
                float room = capX - 14.0f - nameX;
                bool showCategory = binding.group == KeyBinds::VANILLA_GROUP;
                float categoryW = showCategory
                        ? std::min(UiFont::width(binding.category, UiFont::MEDIUM, UiWidgets::TEXT_SMALL), room * 0.4f)
                        : 0.0f;
                std::string name = UiFont::fitEnd(binding.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                  std::max(40.0f, room - categoryW - 12.0f));
                float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(name, nameX, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (showCategory) {
                    UiRender::textY(UiFont::fitEnd(binding.category, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, categoryW),
                                    nameX + nameW + 12.0f, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                if (rowHovered && name != binding.name) {
                    Page::tooltip(binding.name, nameX, rowY + 8.0f, nameW, Page::ROW_H - 16.0f);
                }
                if (!under && NewUi::click(capX, capY, capW, 24.0f)) {
                    state.capturing = capturing ? std::string() : binding.id;
                    state.captureFrame = frame;
                } else if (!under && NewUi::click(listX, rowY, rowW, Page::ROW_H) && isBound && onBoard(binding.key)) {
                    state.page = binding.key.rfind("key.mouse.", 0) == 0 ? 1 : 0;
                    state.selected = binding.key;
                    state.adding = false;
                    Page::scrollTo(id + "#scroll", 0.0f);
                }
            }
        }
        body.end();

        const Spot* focus = nullptr;
        for (const Spot& spot : spots) {
            if (spot.name == state.selected) {
                focus = &spot;
            }
        }
        if (focus != nullptr) {
            openPopover(id, state.selected, file, legacy);
            popover.anchor = *focus;
            popover.top = body.top;
            popover.bottom = body.bottom;
        } else {
            if (!state.selected.empty() && !onBoard(state.selected)) {
                state.selected.clear();
            }
            if (popover.active && popover.editor == id) {
                closePopover();
            }
        }
    }

    bool page(const std::string& id, const std::filesystem::path& file, bool legacy, const std::string& note,
              bool blocked) {
        bool back = Page::iconButton(id + "#back", 0xf053, "返回", Page::left() - 4.0f, Page::top() - 2.0f, blocked);
        Page::title("按键绑定", Page::left() + 30.0f, Page::top());
        draw(id, file, legacy, note, Page::top() + 44.0f, blocked);
        return back;
    }
}
