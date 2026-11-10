#include "ui/imgui/screens/instances/InstancePage.h"
#include "app/system/Features.h"
#include "app/accounts/Accounts.h"
#include "app/system/Catalog.h"
#include "app/launch/Game.h"
#include "app/launch/GameOptions.h"
#include "app/launch/KeyBinds.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/instances/InstanceExport.h"
#include "ui/imgui/screens/instances/InstanceMods.h"
#include "ui/imgui/screens/instances/InstanceScreenshots.h"
#include "app/instances/Exports.h"
#include "app/instances/Screenshots.h"
#include "ui/imgui/screens/instances/InstanceShortcut.h"
#include "app/instances/InstanceSettings.h"
#include "app/instances/Installer.h"
#include "app/launch/LaunchDefaults.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "app/instances/Mods.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "ui/imgui/screens/settings/GameOptionRows.h"
#include "ui/imgui/screens/settings/KeyBindEditor.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "app/launch/Patches.h"
#include "app/system/Platform.h"

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <core/auth/yggdrasil.hpp>
#include <core/base/strings.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace InstancePage {

    namespace blb = b_launcher_base;
    using Settings = blb::instance::instance_settings;

    namespace {
        using InstanceSettings::automaticMemory;
        using InstanceSettings::isJava;
        using InstanceSettings::isolatedByDefault;
        using InstanceSettings::JavaOption;
        using InstanceSettings::javaOptions;
        using InstanceSettings::joinEnvironment;
        using InstanceSettings::parseMegabytes;
        using InstanceSettings::parseSize;
        using InstanceSettings::splitEnvironment;

        const char* const RENAME_OWNER = "instance#rename";
        using Page::FIELD_H;
        constexpr float WIDE_FIELD = 300.0f;
        constexpr float SELECT_W = 180.0f;
        constexpr int TEXT_LIMIT = 1024;

        constexpr int SECTION_MAIN = 0;
        constexpr int SECTION_ADVANCED = 1;
        constexpr int SECTION_MODS = 2;
        constexpr int SECTION_EXPORT = 3;
        constexpr int SECTION_SHORTCUT = 4;
        constexpr int SECTION_SCREENSHOTS = 5;
        constexpr int SECTION_KEYS = 6;
        PageTransition sections(SECTION_MAIN);
        std::string sectionKey;
        bool screenshotsFromHome = false;

        std::string renaming;
        std::string renameText;

        const char* const ISOLATED = "版本独立";
        const char* const SHARED = "共享 .minecraft";

        const char* const AUTO_JAVA = "自动选择";
        const char* const BROWSE_JAVA = "浏览\xE2\x80\xA6";

        constexpr uint32_t MEMORY_STEP = 256;
        constexpr uint32_t MEMORY_MIN = 512;
        float memoryBarX() {
            return Page::left() + 100.0f;
        }

        struct Option {
            const char* label;
            const char* key;
        };

        const std::vector<Option> VISIBILITY = {
                {"保持不变", ""},
                {"最小化", "minimize"},
                {"隐藏，游戏退出后恢复", "hide"},
                {"关闭启动器", "close"},
        };

        const std::vector<Option> PRIORITY = {
                {"低", "low"}, {"低于正常", "below-normal"}, {"正常", ""}, {"高于正常", "above-normal"}, {"高", "high"},
        };

        const std::vector<Option> LOGIN = {
                {"任意账户", ""},
                {"仅微软账户", "microsoft"},
                {"仅离线账户", "offline"},
                {"第三方登录：Authlib Injector", "yggdrasil"},
                {"第三方登录：统一通行证", "nide8"},
        };

        std::string fitEnd(const std::string& text, float available, float size) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, available);
        }

        uint32_t fade(uint32_t argb, float amount) {
            uint32_t alpha = (uint32_t) std::lround(((argb >> 24) & 0xFFu) * UiRender::saturate(amount));
            return (alpha << 24) | (argb & 0x00FFFFFFu);
        }

        uint32_t warning() {
            return UiTheme::warning();
        }

        std::string followLabel(const Instances::Entry& entry) {
            return std::string("跟随默认（") + (isolatedByDefault(entry) ? ISOLATED : SHARED) + "）";
        }

        void commitRename(const std::string& text) {
            std::string from = renaming;
            renaming.clear();
            const Instances::Entry* entry = Instances::find(from);
            if (entry == nullptr || text == entry->info.id) {
                return;
            }
            std::string problem = Instances::checkId(entry->source, text);
            if (!problem.empty()) {
                Instances::notify(problem, true);
                return;
            }
            std::string to = Instances::keyOf(entry->source, text);
            Instances::rename(from, text, [from, to](const std::string& error) {
                if (error.empty()) {
                    MainWindow::renamed(from, to);
                }
            });
        }

        std::shared_ptr<TextBinding> renameValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        RENAME_OWNER, [] { return renameText; }, [](const std::string& text) { commitRename(text); },
                        "实例名称");
                binding->limit = 128;
                return binding;
            }();
            return value;
        }

        void copy(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            if (entry == nullptr) {
                return;
            }
            std::string target = Instances::freeId(entry->source, entry->info.id + "-copy");
            std::string targetKey = Instances::keyOf(entry->source, target);
            Instances::duplicate(key, target, [targetKey](const std::string& error) {
                if (error.empty()) {
                    Instances::select(targetKey);
                }
            });
        }

        void remove(const std::string& key) {
            const std::vector<Instances::Entry>& list = Instances::list();
            std::string neighbour;
            for (size_t i = 0; i < list.size(); i++) {
                if (list[i].key == key) {
                    neighbour = i + 1 < list.size() ? list[i + 1].key : (i > 0 ? list[i - 1].key : std::string());
                    break;
                }
            }
            bool current = Instances::selected() == key;
            Instances::remove(key, [neighbour, current](const std::string& error) {
                if (error.empty() && current) {
                    Instances::select(neighbour);
                }
            });
        }

        void openShortcut(const std::string& key) {
            if (Instances::find(key) == nullptr) {
                return;
            }
            MainWindow::openVersionSettings(key);
            sectionKey = key;
            if (sections.page != SECTION_SHORTCUT) {
                sections.go(SECTION_SHORTCUT);
            }
        }

        bool sameFolder(const std::string& id) {
            const Instances::Entry* entry = Instances::find(id);
            if (entry == nullptr) {
                return false;
            }
            std::error_code error;
            if (std::filesystem::equivalent(entry->gameDirectory, entry->info.directory, error)) {
                return true;
            }
            std::filesystem::path game = entry->gameDirectory.lexically_normal();
            std::filesystem::path version = entry->info.directory.lexically_normal();
            return blb::iequals(blb::path_to_utf8(game / ""), blb::path_to_utf8(version / ""));
        }

        std::string owner(const std::string& id, const char* field) {
            return "instance#" + id + "#" + field;
        }

        void edit(const std::string& id, const std::function<void(Settings&)>& change) {
            Instances::editSettings(id, change);
        }

        const Settings* settingsOf(const std::string& id) {
            const Instances::Entry* entry = Instances::find(id);
            return entry != nullptr ? &entry->info.settings : nullptr;
        }

        std::shared_ptr<TextBinding> textSetting(const std::string& id, const char* field,
                                                 const std::string& placeholder,
                                                 std::function<std::string(const Settings&)> read,
                                                 std::function<void(Settings&, const std::string&)> write) {
            auto binding = std::make_shared<TextBinding>(
                    owner(id, field),
                    [id, read] {
                        const Settings* settings = settingsOf(id);
                        return settings != nullptr ? read(*settings) : std::string();
                    },
                    [id, write](const std::string& value) {
                        edit(id, [&](Settings& settings) { write(settings, value); });
                    },
                    placeholder);
            binding->limit = TEXT_LIMIT;
            return binding;
        }

        void textField(const std::shared_ptr<TextValue>& value, float x, float rowY, float w, bool blocked) {
            float y = rowY + Page::FIELD_Y;
            TextFieldWidget field(value);
            field.box(x, y, w, FIELD_H).bounds(x, y, w, FIELD_H).block(blocked);
            field.paint();
        }

        void select(const std::string& id, const std::shared_ptr<Choice>& choice, float x, float rowY, float w,
                    bool blocked) {
            SelectWidget widget(id, choice);
            widget.bounds(x, rowY + Page::FIELD_Y, w, FIELD_H).block(blocked);
            widget.paint();
        }

        void toggle(const std::string& id, const char* field, std::function<bool(const Settings&)> read,
                    std::function<void(Settings&, bool)> write, float rowY, bool blocked) {
            auto flag = std::make_shared<FlagBinding>(
                    owner(id, field),
                    [id, read] {
                        const Settings* settings = settingsOf(id);
                        return settings != nullptr && read(*settings);
                    },
                    [id, write](bool value) { edit(id, [&](Settings& settings) { write(settings, value); }); });
            ToggleWidget widget(owner(id, field), flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::left(), rowY, Page::width(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        std::shared_ptr<ModeBinding> optionSetting(const std::string& id, const char* field,
                                                   const std::vector<Option>& options, std::string Settings::* member) {
            return std::make_shared<ModeBinding>(
                    owner(id, field),
                    [id, &options, member] {
                        const Settings* settings = settingsOf(id);
                        std::string key = settings != nullptr ? settings->*member : std::string();
                        for (const Option& option : options) {
                            if (key == option.key) {
                                return std::string(option.label);
                            }
                        }
                        for (const Option& option : options) {
                            if (*option.key == 0) {
                                return std::string(option.label);
                            }
                        }
                        return std::string(options.front().label);
                    },
                    [id, &options, member](const std::string& label) {
                        for (const Option& option : options) {
                            if (label == option.label) {
                                edit(id, [&](Settings& settings) { settings.*member = option.key; });
                            }
                        }
                    },
                    [&options] {
                        std::vector<std::string> labels;
                        for (const Option& option : options) {
                            labels.push_back(option.label);
                        }
                        return labels;
                    });
        }

        std::shared_ptr<ModeBinding> optionalSetting(const std::string& id, const char* field,
                                                     std::optional<bool> Settings::* member, const std::string& follow,
                                                     const char* on, const char* off) {
            return std::make_shared<ModeBinding>(
                    owner(id, field),
                    [id, member, follow, on, off] {
                        const Settings* current = settingsOf(id);
                        if (current == nullptr || !(current->*member)) return follow;
                        return std::string(*(current->*member) ? on : off);
                    },
                    [id, member, follow, on](const std::string& option) {
                        edit(id, [&](Settings& settings) {
                            auto& value = settings.*member;
                            if (option == follow)
                                value.reset();
                            else
                                value = option == on;
                        });
                    },
                    [follow, on, off] { return std::vector<std::string>{follow, on, off}; });
        }

        std::string javaLabel(const Settings& settings, const std::vector<JavaOption>& options) {
            if (!settings.java) {
                return AUTO_JAVA;
            }
            const std::vector<blb::java::java_installation>& list = Catalog::java().list;
            for (size_t i = 0; i < list.size() && i < options.size(); i++) {
                if (isJava(*settings.java, list[i])) {
                    return options[i].label;
                }
            }
            return Platform::display(*settings.java);
        }

        void javaRow(const Instances::Entry& entry, float rowY, bool blocked) {
            const std::string& id = entry.key;
            const Settings& settings = entry.info.settings;
            const Catalog::JavaList& javas = Catalog::java();
            static std::set<std::string> checked;
            if (settings.java && javas.status != Catalog::Status::Loading &&
                checked.insert(blb::path_to_utf8(*settings.java)).second &&
                std::none_of(javas.list.begin(), javas.list.end(),
                             [&](const blb::java::java_installation& java) { return isJava(*settings.java, java); })) {
                Catalog::addJava(*settings.java);
            }

            Page::label("Java", rowY);
            auto choice = std::make_shared<ModeBinding>(
                    owner(id, "java"),
                    [id] {
                        const Settings* current = settingsOf(id);
                        return current != nullptr ? javaLabel(*current, javaOptions()) : std::string(AUTO_JAVA);
                    },
                    [id](const std::string& option) {
                        if (option == AUTO_JAVA) {
                            edit(id, [](Settings& s) { s.java.reset(); });
                            return;
                        }
                        if (option == BROWSE_JAVA) {
                            const Settings* current = settingsOf(id);
                            std::filesystem::path initial =
                                    current != nullptr && current->java ? *current->java : std::filesystem::path();
                            if (std::optional<std::filesystem::path> picked = Platform::pickJava(initial)) {
                                std::filesystem::path chosen = *picked;
                                edit(id, [&](Settings& s) { s.java = chosen; });
                                Catalog::addJava(chosen);
                            }
                            return;
                        }
                        for (const JavaOption& java : javaOptions()) {
                            if (java.label == option) {
                                std::filesystem::path chosen = java.path;
                                edit(id, [&](Settings& s) { s.java = chosen; });
                                return;
                            }
                        }
                    },
                    [id] {
                        std::vector<JavaOption> options = javaOptions();
                        std::vector<std::string> labels{AUTO_JAVA};
                        for (const JavaOption& option : options) {
                            labels.push_back(option.label);
                        }
                        if (const Settings* current = settingsOf(id)) {
                            std::string label = javaLabel(*current, options);
                            if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
                                labels.push_back(label);
                            }
                        }
                        labels.push_back(BROWSE_JAVA);
                        return labels;
                    });
            float selectX = Page::controlRight() - WIDE_FIELD;
            SelectWidget widget("instance#java", choice);
            widget.bounds(selectX, rowY + Page::FIELD_Y, WIDE_FIELD, FIELD_H).block(blocked);
            widget.paint();
            if (javas.status == Catalog::Status::Loading) {
                Page::spinner(selectX - 16.0f, rowY + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
            }
            if (!blocked && !widget.showing() && NewUi::hit(selectX, rowY + Page::FIELD_Y, WIDE_FIELD, FIELD_H)) {
                Page::tooltip(settings.java ? Platform::display(*settings.java)
                                            : std::string("按游戏版本选择合适的 Java，没有时自动下载"),
                              selectX, rowY + Page::FIELD_Y, WIDE_FIELD, FIELD_H);
            }
        }

        const Platform::Memory& memoryNow() {
            static Platform::Memory memory = Platform::memory();
            static double readAt = ImGui::GetTime();
            double now = ImGui::GetTime();
            if (now - readAt >= 1.0) {
                memory = Platform::memory();
                readAt = now;
            }
            return memory;
        }

        uint32_t allocationOf(const Settings& settings, uint32_t totalMb) {
            return settings.max_memory_mb != 0 ? settings.max_memory_mb : automaticMemory(totalMb);
        }

        std::string gigabytes(uint32_t mb, bool unit = true) {
            char text[32];
            std::snprintf(text, sizeof(text), unit ? "%.1f GB" : "%.1f", mb / 1024.0);
            return text;
        }

        void memoryCaption(const Instances::Entry& entry, float y) {
            Page::caption("内存", y);
            const Platform::Memory& memory = memoryNow();
            uint32_t used = memory.totalMb - std::min(memory.availableMb, memory.totalMb);
            uint32_t allocation = allocationOf(entry.info.settings, memory.totalMb);
            bool running = Game::state(entry.key) != Game::State::Idle;
            std::string text;
            uint32_t color;
            if (!running && allocation > memory.availableMb) {
                text = "比当前可用内存多 " + gigabytes(allocation - memory.availableMb);
                color = warning();
            } else {
                text = "已用 " + gigabytes(used, false) + " / " + gigabytes(memory.totalMb);
                color = UiTheme::textMuted();
            }
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(text, Page::right() - 2.0f - w, y + 2.0f, 16.0f, color, UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }

        void memoryBar(const std::string& id, float x, float rowY, float w, bool blocked) {
            const Platform::Memory& memory = memoryNow();
            const Settings* settings = settingsOf(id);
            if (settings == nullptr) {
                return;
            }
            uint32_t maximum = settings->max_memory_mb;
            float total = (float) std::max(memory.totalMb, 1024u);
            const float barH = 6.0f;
            const float radius = barH * 0.5f;
            float barY = rowY + (Page::ROW_H - barH) * 0.5f;
            std::string slider = owner(id, "memoryBar");

            bool hovered = !blocked && NewUi::hit(x - 8.0f, rowY + 6.0f, w + 16.0f, Page::ROW_H - 12.0f);
            if (hovered && NewUi::pendingClick && !NewUi::clickConsumed) {
                NewUi::clickConsumed = true;
                NewUi::draggingSlider = slider;
            }
            bool dragging = NewUi::draggingSlider == slider;
            if (dragging) {
                uint32_t top = std::max(MEMORY_MIN, memory.totalMb / MEMORY_STEP * MEMORY_STEP);
                float fraction = UiRender::saturate((NewUi::mouseX - x) / w);
                uint32_t value = (uint32_t) std::lround(fraction * total / MEMORY_STEP) * MEMORY_STEP;
                value = std::clamp(value, MEMORY_MIN, top);
                if (value != maximum) {
                    maximum = value;
                    edit(id, [value](Settings& s) { s.max_memory_mb = value; });
                }
            }
            bool automatic = maximum == 0;
            uint32_t allocation = automatic ? automaticMemory(memory.totalMb) : maximum;
            float target = std::min(1.0f, allocation / total);
            float shown = UiState::motionFrom(slider + "#shown", target, dragging ? 30.0f : 12.0f, target);
            float manual = UiState::motion(slider + "#manual", automatic ? 0.0f : 1.0f, 14.0f);
            float hover = UiState::motion(slider + "#hover", dragging ? 1.0f : hovered ? 0.6f : 0.0f, 18.0f);

            uint32_t used = memory.totalMb - std::min(memory.availableMb, memory.totalMb);
            float usedW = w * std::min(1.0f, used / total);
            float usedX = x + w - usedW;
            float allocationW = w * shown;
            UiRender::roundRect(x, barY, w, barH, radius, UiTheme::outline());
            if (usedW > 0.5f) {
                UiRender::roundRect(usedX, barY, usedW, barH, radius, UiTheme::textFaint());
            }
            if (allocationW > 0.5f) {
                UiRender::roundRect(x, barY, std::max(barH, allocationW), barH, radius,
                                    UiRender::mix(fade(UiTheme::accent(), 0.45f), UiTheme::accent(), manual));
                float end = x + allocationW;
                if (usedW > 0.5f && end > usedX + 0.5f) {
                    float from = std::max(x, usedX);
                    uint32_t amber = UiRender::mix(fade(warning(), 0.55f), warning(), manual);
                    UiRender::roundRect(from, barY, end - from, barH, radius, amber, from <= x ? 0xF : (2 | 8));
                }
            }
            if (manual > 0.01f) {
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * manual;
                float cx = x + allocationW;
                float cy = barY + barH * 0.5f;
                float knob = UiRender::lerp(6.0f, 7.0f, hover) * UiRender::lerp(0.6f, 1.0f, manual);
                UiWidgets::knobShadow(cx, cy, knob);
                UiRender::circle(cx, cy, knob, UiTheme::onAccent());
                UiRender::alphaMul = savedAlpha;
            }
            if (hovered && !dragging) {
                Page::tooltip("其他程序已用 " + gigabytes(used) + "，现在可用 " + gigabytes(memory.availableMb), x,
                              rowY + 6.0f, w, Page::ROW_H - 12.0f);
            }
        }

        void memoryRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;
            uint32_t automatic = automaticMemory(memoryNow().totalMb);

            Page::label("自动分配", rowY);
            toggle(
                    id, "autoMemory", [](const Settings& s) { return s.max_memory_mb == 0; },
                    [automatic](Settings& s, bool value) { s.max_memory_mb = value ? 0 : automatic; }, rowY, blocked);
            rowY += Page::ROW_H;

            Page::label("最大内存", rowY);
            const char* unit = "MB";
            float unitX = Page::controlRight() - UiFont::width(unit, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(unit, unitX, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float fieldW = 64.0f;
            float fieldX = unitX - 8.0f - fieldW;
            textField(textSetting(
                              id, "maxMemory", std::to_string(automatic),
                              [](const Settings& s) {
                                  return s.max_memory_mb != 0 ? std::to_string(s.max_memory_mb) : std::string();
                              },
                              [](Settings& s, const std::string& value) { s.max_memory_mb = parseMegabytes(value); }),
                      fieldX, rowY, fieldW, blocked);
            memoryBar(id, memoryBarX(), rowY, fieldX - 18.0f - memoryBarX(), blocked);
        }

        void header(const Instances::Entry& entry) {
            const std::string& id = entry.key;
            const std::string& name = entry.info.id;
            float x = Page::left();
            float y = Page::top();
            uint32_t image = icon(entry);
            if (image != 0) {
                UiRender::texture(image, x, y + 4.0f, 32.0f, 32.0f);
            } else {
                UiRender::roundRect(x, y + 4.0f, 32.0f, 32.0f, 8.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(0xf071), x + 16.0f, y + 20.0f, UiTheme::danger(), 14.0f);
            }
            float textX = x + 46.0f;

            bool editing = renaming == id;
            if (editing && NewUi::textEditing != RENAME_OWNER) {
                renaming.clear();
                editing = false;
            }
            std::string problem;
            if (editing) {
                TextFieldWidget field(renameValue());
                field.box(textX - 9.0f, y - 1.0f, 280.0f, 26.0f).bounds(textX - 9.0f, y - 1.0f, 280.0f, 26.0f);
                field.paint();
                std::string typed = Ui::trim(UiInput::get(NewUi::INPUT_TEXT));
                problem = typed == name ? std::string() : Instances::checkId(entry.source, typed);
            } else {
                UiRender::textY(fitEnd(name, Page::right() - 80.0f - textX, Page::TITLE_SIZE), textX, y, 24.0f,
                                UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            }

            float lineY = y + 27.0f;
            float lineH = 16.0f;
            if (!problem.empty()) {
                UiRender::textY(problem, textX, lineY, lineH, UiTheme::danger(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            } else if (!entry.info.valid) {
                UiRender::textY(
                        fitEnd("无法读取版本信息：" + entry.info.problem, Page::right() - textX, UiWidgets::TEXT_SMALL),
                        textX, lineY, lineH, UiTheme::danger(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            } else {
                std::string version = "Minecraft " + entry.info.minecraft_version;
                UiRender::textY(version, textX, lineY, lineH, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                float pillX = textX + UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 10.0f;
                for (const blb::install::loader_info& loader : entry.info.loaders) {
                    std::string text = Installer::loaderName(loader.type) +
                            (loader.version.empty() ? std::string() : " " + loader.version);
                    float pillW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 14.0f;
                    if (pillX + pillW > Page::right()) {
                        break;
                    }
                    UiRender::roundRect(pillX, lineY - 1.0f, pillW, 18.0f, 6.0f, UiTheme::surfaceRaised());
                    UiRender::textY(text, pillX + 7.0f, lineY - 1.0f, 18.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    pillX += pillW + 6.0f;
                }
            }

            float buttonY = y + 6.0f;
            if (Page::iconButton("instance#folder", 0xf07c, "打开游戏目录", Page::right() - 62.0f, buttonY)) {
                Platform::open(entry.gameDirectory);
            }
            if (Page::iconButton("instance#menu", 0xf141, "更多操作", Page::right() - 28.0f, buttonY)) {
                openMenu(id, Page::right() - 170.0f, buttonY + 34.0f);
            }
        }

        void optionsRow(const Instances::Entry& entry, float rowY, bool blocked) {
            const std::string& id = entry.key;
            bool usable = Patches::enabled(entry.info.settings.patch);
            bool off = blocked || !usable;
            float shade = UiState::motion(owner(id, "optionsFile#shade"), usable ? 1.0f : 0.4f, 16.0f);
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * shade;
            Page::label(usable ? "游戏设置文件" : "游戏设置文件（需启用补丁）", rowY);
            float fieldX = Page::controlRight() - SELECT_W;
            textField(textSetting(
                              id, "optionsFile", "options.txt", [](const Settings& s) { return s.options_file; },
                              [](Settings& s, const std::string& value) {
                                  s.options_file = InstanceSettings::optionsFile(value);
                              }),
                      fieldX, rowY, SELECT_W, off);
            std::string named = InstanceSettings::versionOptionsFile(entry.info.minecraft_version);
            std::string preset = "按版本命名";
            float presetW = Page::buttonWidth(preset);
            float presetX = fieldX - 8.0f - presetW;
            if (Page::button("instance#optionsPreset", preset, presetX, rowY + Page::FIELD_Y, presetW, false,
                             off || named.empty())) {
                if (NewUi::textEditing == owner(id, "optionsFile")) {
                    NewUi::cancelTextEdit();
                }
                edit(id, [named](Settings& s) { s.options_file = named; });
            }
            UiRender::alphaMul = savedAlpha;
        }

        void gameRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            Page::label("游戏目录", rowY);
            std::string follow = followLabel(entry);
            auto isolation = optionalSetting(id, "isolation", &Settings::isolated, follow, ISOLATED, SHARED);
            select("instance#isolation", isolation, Page::controlRight() - SELECT_W, rowY, SELECT_W, blocked);
            rowY += Page::ROW_H;

            javaRow(entry, rowY, blocked);
            rowY += Page::ROW_H;

            Page::label("启动器可见性", rowY);
            select("instance#visibility", optionSetting(id, "visibility", VISIBILITY, &Settings::launcher_visibility),
                   Page::controlRight() - SELECT_W, rowY, SELECT_W, blocked);
            rowY += Page::ROW_H;

            Page::label("进程优先级", rowY);
            select("instance#priority", optionSetting(id, "priority", PRIORITY, &Settings::process_priority),
                   Page::controlRight() - SELECT_W, rowY, SELECT_W, blocked);
            rowY += Page::ROW_H;

            Page::label("显示游戏日志", rowY);
            std::string followLogs =
                    std::string("跟随全局（") + (LaunchDefaults::showLogs() ? "显示" : "不显示") + "）";
            auto logs = optionalSetting(id, "showLogs", &Settings::show_logs, followLogs, "显示", "不显示");
            select("instance#showLogs", logs, Page::controlRight() - SELECT_W, rowY, SELECT_W, blocked);
            rowY += Page::ROW_H;

            Page::label("启动器补丁", rowY);
            std::string followPatch = std::string("跟随全局（") + (Patches::all() ? "启用" : "不启用") + "）";
            auto patch = optionalSetting(id, "patch", &Settings::patch, followPatch, "启用", "不启用");
            select("instance#patch", patch, Page::controlRight() - SELECT_W, rowY, SELECT_W, blocked);
            rowY += Page::ROW_H;

            optionsRow(entry, rowY, blocked);
        }

        void windowRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            Page::label("窗口大小", rowY);
            float fieldW = 64.0f;
            float heightX = Page::controlRight() - fieldW;
            float widthX = heightX - 26.0f - fieldW;
            textField(textSetting(
                              id, "width", "默认",
                              [](const Settings& s) { return s.width ? std::to_string(*s.width) : std::string(); },
                              [](Settings& s, const std::string& value) { s.width = parseSize(value); }),
                      widthX, rowY, fieldW, blocked);
            UiWidgets::icon(Ui::glyph(0xf00d), heightX - 13.0f, rowY + Page::ROW_H * 0.5f, UiTheme::textFaint(), 9.0f);
            textField(textSetting(
                              id, "height", "默认",
                              [](const Settings& s) { return s.height ? std::to_string(*s.height) : std::string(); },
                              [](Settings& s, const std::string& value) { s.height = parseSize(value); }),
                      heightX, rowY, fieldW, blocked);
            rowY += Page::ROW_H;

            Page::label("全屏启动", rowY);
            toggle(
                    id, "fullscreen", [](const Settings& s) { return s.fullscreen; },
                    [](Settings& s, bool value) { s.fullscreen = value; }, rowY, blocked);
        }

        bool needsAuthServer(const Instances::Entry& entry) {
            const std::string& login = entry.info.settings.login;
            return login == "yggdrasil" || login == "nide8";
        }

        int serverCount(const Instances::Entry& entry) {
            return needsAuthServer(entry) ? 4 : 3;
        }

        const char* const FOLLOW_ACCOUNT = "跟随当前账号";

        std::string accountLabel(const blb::auth::account& account) {
            auto base = [](const blb::auth::account& item) { return item.name + "（" + Accounts::kindOf(item) + "）"; };
            std::string label = base(account);
            const auto& accounts = Accounts::list();
            if (std::count_if(accounts.begin(), accounts.end(), [&](const auto& item) { return base(item) == label; }) >
                1) {
                bool sameServer = std::count_if(accounts.begin(), accounts.end(), [&](const auto& item) {
                                      return base(item) == label && item.server == account.server;
                                  }) > 1;
                if (!account.server.empty()) {
                    label += " · " + account.server;
                }
                if (account.server.empty() || sameServer) {
                    label += " · " + blb::auth::format_uuid(account.uuid);
                }
            }
            return label;
        }

        std::shared_ptr<ModeBinding> accountSetting(const std::string& id) {
            auto binding = std::make_shared<ModeBinding>(
                    owner(id, "account"),
                    [id] {
                        const Settings* settings = settingsOf(id);
                        const blb::auth::account* bound = settings != nullptr && !settings->account.empty()
                                ? Accounts::find(settings->account)
                                : nullptr;
                        if (bound == nullptr || !Accounts::matches(*bound, settings->login, settings->auth_server)) {
                            return std::string(FOLLOW_ACCOUNT);
                        }
                        return accountLabel(*bound);
                    },
                    [id](const std::string& label) {
                        if (label == FOLLOW_ACCOUNT) {
                            Game::setAccount(id, std::string());
                            return;
                        }
                        const Settings* settings = settingsOf(id);
                        for (const blb::auth::account& account : Accounts::list()) {
                            if (accountLabel(account) == label &&
                                (settings == nullptr ||
                                 Accounts::matches(account, settings->login, settings->auth_server))) {
                                Game::setAccount(id, Accounts::keyOf(account));
                                return;
                            }
                        }
                    },
                    [id] {
                        std::vector<std::string> labels = {FOLLOW_ACCOUNT};
                        const Settings* settings = settingsOf(id);
                        for (const blb::auth::account& account : Accounts::list()) {
                            if (settings == nullptr ||
                                Accounts::matches(account, settings->login, settings->auth_server)) {
                                labels.push_back(accountLabel(account));
                            }
                        }
                        return labels;
                    });
            binding->icon(FOLLOW_ACCOUNT, 0xf0c1);
            for (const blb::auth::account& account : Accounts::list()) {
                if (uint32_t face = Textures::of(Accounts::face(account))) {
                    binding->image(accountLabel(account), face, 3.5f);
                }
            }
            return binding;
        }

        std::shared_ptr<ModeBinding> loginSetting(const std::string& id) {
            return std::make_shared<ModeBinding>(
                    owner(id, "login"),
                    [id] {
                        const Settings* settings = settingsOf(id);
                        std::string key = settings != nullptr ? settings->login : std::string();
                        for (const Option& option : LOGIN) {
                            if (key == option.key) {
                                return std::string(option.label);
                            }
                        }
                        return std::string(LOGIN.front().label);
                    },
                    [id](const std::string& label) {
                        for (const Option& option : LOGIN) {
                            if (label != option.label) {
                                continue;
                            }
                            edit(id, [&](Settings& settings) {
                                if (settings.login != option.key) {
                                    settings.login = option.key;
                                    settings.auth_server.clear();
                                }
                            });
                        }
                    },
                    [] {
                        std::vector<std::string> labels;
                        for (const Option& option : LOGIN) {
                            labels.push_back(option.label);
                        }
                        return labels;
                    });
        }

        void serverRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            Page::label("登录方式", rowY);
            select("instance#login", loginSetting(id), Page::controlRight() - WIDE_FIELD, rowY, WIDE_FIELD, blocked);
            rowY += Page::ROW_H;

            Page::label("启动账号", rowY);
            select("instance#account", accountSetting(id), Page::controlRight() - WIDE_FIELD, rowY, WIDE_FIELD,
                   blocked);
            rowY += Page::ROW_H;

            if (needsAuthServer(entry)) {
                bool nide8 = entry.info.settings.login == "nide8";
                Page::label(nide8 ? "服务器 ID" : "认证服务器", rowY);
                float fieldX = Page::controlRight() - WIDE_FIELD;
                textField(textSetting(
                                  id, "authServer",
                                  nide8 ? "服主后台 → 集成说明中的服务器 ID" : "Yggdrasil API 认证服务器地址",
                                  [](const Settings& s) { return s.auth_server; },
                                  [](Settings& s, const std::string& value) { s.auth_server = Ui::trim(value); }),
                          fieldX, rowY, WIDE_FIELD, blocked);
                std::string preset = nide8 ? "服主后台" : "设置为 LittleSkin";
                float presetW = Page::buttonWidth(preset);
                if (Page::button("instance#authPreset", preset, fieldX - 8.0f - presetW, rowY + Page::FIELD_Y, presetW,
                                 false, blocked)) {
                    if (nide8) {
                        Platform::openUrl(std::string(blb::auth::nide8_admin_url));
                    } else {
                        if (NewUi::textEditing == owner(id, "authServer")) {
                            NewUi::cancelTextEdit();
                        }
                        edit(id, [](Settings& s) { s.auth_server = std::string(blb::auth::littleskin_api); });
                    }
                }
                rowY += Page::ROW_H;
            }

            Page::label("自动加入服务器", rowY);
            textField(textSetting(
                              id, "server", "服务器地址", [](const Settings& s) { return s.server; },
                              [](Settings& s, const std::string& value) { s.server = Ui::trim(value); }),
                      Page::controlRight() - WIDE_FIELD, rowY, WIDE_FIELD, blocked);
        }

        void otherRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            Page::label("备注", rowY);
            textField(textSetting(
                              id, "notes", "无", [](const Settings& s) { return s.notes; },
                              [](Settings& s, const std::string& value) { s.notes = value; }),
                      Page::controlRight() - WIDE_FIELD, rowY, WIDE_FIELD, blocked);
            rowY += Page::ROW_H;

            bool hovered = !blocked && NewUi::hit(Page::left(), rowY, Page::width(), Page::ROW_H);
            float hover = UiState::motion("instance#advanced#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(Page::left(), rowY, Page::width(), Page::ROW_H, 12.0f,
                                    UiTheme::overlay(Ui::round(14 * hover)), 4 | 8);
            }
            Page::label("高级", rowY);
            UiWidgets::icon(Ui::glyph(0xf054), Page::controlRight() - 6.0f, rowY + Page::ROW_H * 0.5f,
                            UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), hover), 9.0f);
            if (!blocked && NewUi::click(Page::left(), rowY, Page::width(), Page::ROW_H)) {
                UiSound::request(UiSound::PAGE);
                sections.go(SECTION_ADVANCED);
            }
        }

        int manageCount(const Instances::Entry& entry) {
            return (Instances::hasModLoader(entry) ? 1 : 0) + (GameOptions::ownFile(entry) ? 1 : 0) + 3;
        }

        void manageRows(const Instances::Entry& entry, float y, bool blocked) {
            int count = manageCount(entry);
            int index = 0;
            if (Instances::hasModLoader(entry)) {
                const Mods::List& mods = Mods::of(entry.key);
                std::string value =
                        mods.loading && mods.mods.empty() ? std::string() : std::to_string(mods.mods.size()) + " 个";
                if (Page::navRow("instance#mods", "模组管理", value, y, blocked, Page::rowCorners(index, count),
                                 0xf12e)) {
                    Mods::scan(entry.key);
                    sections.go(SECTION_MODS);
                }
                y += Page::ROW_H;
                index++;
            }
            const Screenshots::List& shots = Screenshots::of(entry.key);
            std::string shotsValue = shots.loading && shots.entries.empty()
                    ? std::string()
                    : std::to_string(shots.entries.size()) + " 张";
            if (Page::navRow("instance#screenshots", "截图管理", shotsValue, y, blocked, Page::rowCorners(index, count),
                             0xf030)) {
                Screenshots::scan(entry.key);
                screenshotsFromHome = false;
                sections.go(SECTION_SCREENSHOTS);
            }
            y += Page::ROW_H;
            index++;
            if (GameOptions::ownFile(entry)) {
                int bound = KeyBindEditor::count(GameOptions::fileOf(entry));
                if (Page::navRow("instance#keys", "按键绑定", bound > 0 ? std::to_string(bound) + " 个" : std::string(),
                                 y, blocked, Page::rowCorners(index, count), 0xf11c)) {
                    sections.go(SECTION_KEYS);
                }
                y += Page::ROW_H;
                index++;
            }
            std::string loaders;
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                loaders += (loaders.empty() ? "" : "、") + Installer::loaderName(loader.type);
            }
            if (Page::navRow("instance#loaders", "修改加载器", loaders.empty() ? std::string("未安装") : loaders, y,
                             blocked || entry.info.minecraft_version.empty() || Installer::running() ||
                                     !Features::enabled(Features::INSTANCE_EDIT),
                             Page::rowCorners(index, count), 0xf0ad)) {
                NewInstanceScreen::openLoaderEdit(entry.key);
                MainWindow::showLoaderEdit();
            }
            y += Page::ROW_H;
            index++;
            if (Page::navRow("instance#export", "导出整合包", "", y, blocked || !entry.info.valid,
                             Page::rowCorners(index, count), 0xf56e)) {
                Exports::tree(entry.key).refresh();
                InstanceExport::reset();
                sections.go(SECTION_EXPORT);
            }
        }

        void mainSection(const Instances::Entry& entry) {
            header(entry);

            const int manage = manageCount(entry);
            const int gameCount = 7;
            const int memoryCount = 2;
            const int windowCount = 2;
            const int serverRowCount = serverCount(entry);
            const int otherCount = 2;
            const int optionCount = GameOptions::ownFile(entry) ? GameOptionRows::COUNT : 0;
            float contentH = 6 * Page::CAPTION_H +
                    (manage + gameCount + memoryCount + windowCount + serverRowCount + otherCount) * Page::ROW_H +
                    5 * Page::GAP;
            if (optionCount > 0) {
                contentH += Page::CAPTION_H + optionCount * Page::ROW_H + Page::GAP;
            }
            Page::Scroll body("instance#body", Page::top() + 54.0f, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || NewUi::overlayCoversMouse();
            float y = body.top - body.offset;

            if (Page::shown(y, Page::CAPTION_H + manage * Page::ROW_H)) {
                Page::caption("管理", y);
                Page::card(y + Page::CAPTION_H, manage);
                manageRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + manage * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + gameCount * Page::ROW_H)) {
                Page::caption("游戏", y);
                Page::card(y + Page::CAPTION_H, gameCount);
                gameRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + gameCount * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + memoryCount * Page::ROW_H)) {
                memoryCaption(entry, y);
                Page::card(y + Page::CAPTION_H, memoryCount);
                memoryRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + memoryCount * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + windowCount * Page::ROW_H)) {
                Page::caption("窗口", y);
                Page::card(y + Page::CAPTION_H, windowCount);
                windowRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + windowCount * Page::ROW_H + Page::GAP;

            if (optionCount > 0) {
                if (Page::shown(y, Page::CAPTION_H + optionCount * Page::ROW_H)) {
                    std::filesystem::path file = GameOptions::fileOf(entry);
                    GameOptionRows::caption("游戏选项", file, blb::path_to_utf8(file.filename()), y);
                    Page::card(y + Page::CAPTION_H, optionCount);
                    GameOptionRows::draw(owner(entry.key, "options"), file, y + Page::CAPTION_H, blocked);
                }
                y += Page::CAPTION_H + optionCount * Page::ROW_H + Page::GAP;
            }

            if (Page::shown(y, Page::CAPTION_H + serverRowCount * Page::ROW_H)) {
                Page::caption("服务器", y);
                Page::card(y + Page::CAPTION_H, serverRowCount);
                serverRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + serverRowCount * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + otherCount * Page::ROW_H)) {
                Page::caption("其他", y);
                Page::card(y + Page::CAPTION_H, otherCount);
                otherRows(entry, y + Page::CAPTION_H, blocked);
            }

            body.end();
        }

        void wideTextRow(const std::string& id, const char* field, const char* label, const char* placeholder,
                         std::function<std::string(const Settings&)> read,
                         std::function<void(Settings&, const std::string&)> write, float rowY, bool blocked) {
            Page::label(label, rowY);
            float fieldX = Page::controlRight() - WIDE_FIELD;
            textField(textSetting(id, field, placeholder, std::move(read), std::move(write)), fieldX, rowY, WIDE_FIELD,
                      blocked);
        }

        void commandRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            wideTextRow(
                    id, "gameArgs", "游戏参数", "无",
                    [](const Settings& s) { return blb::join_arguments(s.game_args); },
                    [](Settings& s, const std::string& value) { s.game_args = blb::split_arguments(value); }, rowY,
                    blocked);
            rowY += Page::ROW_H;

            wideTextRow(
                    id, "preLaunch", "启动前命令", "无", [](const Settings& s) { return s.pre_launch; },
                    [](Settings& s, const std::string& value) { s.pre_launch = Ui::trim(value); }, rowY, blocked);
            rowY += Page::ROW_H;

            wideTextRow(
                    id, "wrapper", "包装命令", "无", [](const Settings& s) { return s.wrapper; },
                    [](Settings& s, const std::string& value) { s.wrapper = Ui::trim(value); }, rowY, blocked);
            rowY += Page::ROW_H;

            wideTextRow(
                    id, "postExit", "退出后命令", "无", [](const Settings& s) { return s.post_exit; },
                    [](Settings& s, const std::string& value) { s.post_exit = Ui::trim(value); }, rowY, blocked);
        }

        void jvmRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            wideTextRow(
                    id, "jvmArgs", "JVM 参数", "无", [](const Settings& s) { return blb::join_arguments(s.jvm_args); },
                    [](Settings& s, const std::string& value) { s.jvm_args = blb::split_arguments(value); }, rowY,
                    blocked);
            rowY += Page::ROW_H;

            Page::label("最小内存", rowY);
            const char* unit = "MB";
            float unitX = Page::controlRight() - UiFont::width(unit, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(unit, unitX, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float fieldX = unitX - 8.0f - 64.0f;
            textField(textSetting(
                              id, "minMemory", "不设置",
                              [](const Settings& s) {
                                  return s.min_memory_mb != 0 ? std::to_string(s.min_memory_mb) : std::string();
                              },
                              [](Settings& s, const std::string& value) { s.min_memory_mb = parseMegabytes(value); }),
                      fieldX, rowY, 64.0f, blocked);
            rowY += Page::ROW_H;

            wideTextRow(
                    id, "environment", "环境变量", "名称=值",
                    [](const Settings& s) { return joinEnvironment(s.environment); },
                    [](Settings& s, const std::string& value) { s.environment = splitEnvironment(value); }, rowY,
                    blocked);
        }

        void compatibilityRows(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& id = entry.key;
            float rowY = y;

            Page::label("不添加默认 JVM 参数", rowY);
            toggle(
                    id, "noDefaultJvmArgs", [](const Settings& s) { return !s.default_jvm_args; },
                    [](Settings& s, bool value) { s.default_jvm_args = !value; }, rowY, blocked);
            rowY += Page::ROW_H;

            Page::label("不检查游戏完整性", rowY);
            toggle(
                    id, "skipIntegrity", [](const Settings& s) { return !s.check_integrity; },
                    [](Settings& s, bool value) { s.check_integrity = !value; }, rowY, blocked);
        }

        void advancedSection(const Instances::Entry& entry) {
            bool covered = NewUi::overlayCoversMouse();
            if (Page::iconButton("instance#back", 0xf053, "返回", Page::left() - 4.0f, Page::top() - 2.0f, covered)) {
                sections.go(SECTION_MAIN);
            }
            const char* title = "高级";
            Page::title(title, Page::left() + 30.0f, Page::top());
            float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            UiRender::textY(fitEnd(entry.info.id, Page::right() - nameX, UiWidgets::TEXT_CONTROL), nameX, Page::top(),
                            24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

            const int commandCount = 4;
            const int jvmCount = 3;
            const int compatibilityCount = 2;
            float contentH =
                    3 * Page::CAPTION_H + (commandCount + jvmCount + compatibilityCount) * Page::ROW_H + 2 * Page::GAP;
            Page::Scroll body("instance#advanced", Page::top() + 40.0f, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;

            if (Page::shown(y, Page::CAPTION_H + commandCount * Page::ROW_H)) {
                Page::caption("自定义命令", y);
                Page::card(y + Page::CAPTION_H, commandCount);
                commandRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + commandCount * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + jvmCount * Page::ROW_H)) {
                Page::caption("Java 虚拟机", y);
                Page::card(y + Page::CAPTION_H, jvmCount);
                jvmRows(entry, y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + jvmCount * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + compatibilityCount * Page::ROW_H)) {
                Page::caption("兼容性", y);
                Page::card(y + Page::CAPTION_H, compatibilityCount);
                compatibilityRows(entry, y + Page::CAPTION_H, blocked);
            }

            body.end();
        }
    }

    uint32_t icon(const Instances::Entry& entry) {
        if (!entry.info.valid) {
            return 0;
        }
        for (const blb::install::loader_info& loader : entry.info.loaders) {
            if (blb::install::is_primary_loader(loader.type)) {
                if (uint32_t image = Icons::loader(loader.type)) {
                    return image;
                }
            }
        }
        for (const blb::install::loader_info& loader : entry.info.loaders) {
            if (uint32_t image = Icons::loader(loader.type)) {
                return image;
            }
        }
        return Icons::vanilla();
    }

    void reset() {
        sections.page = SECTION_MAIN;
        sections.mix = 1.0f;
        screenshotsFromHome = false;
    }

    void draw(const std::string& id) {
        const Instances::Entry* found = Instances::find(id);
        if (found == nullptr) {
            return;
        }
        if (sectionKey != id) {
            sectionKey = id;
            sections.page = SECTION_MAIN;
            sections.mix = 1.0f;
            screenshotsFromHome = false;
        }
        Instances::Entry entry = *found;
        sections.begin();
        if (sections.page == SECTION_ADVANCED) {
            advancedSection(entry);
        } else if (sections.page == SECTION_MODS) {
            if (InstanceMods::draw(entry)) {
                sections.go(SECTION_MAIN);
            }
        } else if (sections.page == SECTION_EXPORT) {
            if (InstanceExport::draw(entry)) {
                sections.go(SECTION_MAIN);
            }
        } else if (sections.page == SECTION_SHORTCUT) {
            if (InstanceShortcut::draw(entry)) {
                sections.go(SECTION_MAIN);
            }
        } else if (sections.page == SECTION_KEYS) {
            std::filesystem::path file = GameOptions::fileOf(entry);
            if (KeyBindEditor::page(owner(entry.key, "keys"), file,
                                    KeyBinds::legacyVersion(entry.info.minecraft_version),
                                    blb::path_to_utf8(file.filename()), NewUi::overlayCoversMouse())) {
                sections.go(SECTION_MAIN);
            }
        } else if (sections.page == SECTION_SCREENSHOTS) {
            if (InstanceScreenshots::draw(entry)) {
                if (screenshotsFromHome) {
                    screenshotsFromHome = false;
                    sections.page = SECTION_MAIN;
                    sections.mix = 1.0f;
                    MainWindow::showHome();
                } else {
                    sections.go(SECTION_MAIN);
                }
            }
        } else {
            mainSection(entry);
        }
        sections.end();
    }

    void openMenu(const std::string& id, float x, float y) {
        NewUi::openMenu(
                "instance#menu#" + id,
                [id] {
                    MenuItems items;
                    items.push_back(ActionMenuItem::make("实例设置", 0xf1de, [id] {
                                        MainWindow::openVersionSettings(id);
                                    })->enabledIf([id] { return MainWindow::shownInstance() != id; }));
                    items.push_back(ActionMenuItem::make("测试游戏", 0xf188, [id] {
                                        Game::launch(id, true);
                                    })->enabledIf([id] { return Game::state(id) != Game::State::Preparing; }));
#if !defined(__APPLE__)
                    items.push_back(ActionMenuItem::make("生成快捷方式", 0xf108, [id] { openShortcut(id); }));
#endif
                    items.push_back(ActionMenuItem::make("打开游戏目录", 0xf07c, [id] {
                        if (const Instances::Entry* entry = Instances::find(id)) {
                            Platform::open(entry->gameDirectory);
                        }
                    }));
                    if (!sameFolder(id)) {
                        items.push_back(ActionMenuItem::make("打开版本文件夹", 0xf07b, [id] {
                            if (const Instances::Entry* entry = Instances::find(id)) {
                                Platform::open(entry->info.directory);
                            }
                        }));
                    }
                    if (Features::enabled(Features::INSTANCE_EDIT)) {
                        items.push_back(ActionMenuItem::make("重命名", 0xf044, [id] { beginRename(id); }));
                        items.push_back(ActionMenuItem::make("复制", 0xf0c5, [id] { copy(id); }));
                    }
                    if (Features::enabled(Features::INSTANCE_DELETE)) {
                        items.push_back(ActionMenuItem::make("删除", 0xf2ed, [id] { remove(id); })
                                                ->dangerous()
                                                ->confirmFirst());
                    }
                    return items;
                },
                x, y, 170.0f);
    }

    void showScreenshots(const std::string& key) {
        MainWindow::openVersionSettings(key);
        sectionKey = key;
        screenshotsFromHome = true;
        Screenshots::scan(key);
        if (sections.page != SECTION_SCREENSHOTS) {
            sections.go(SECTION_SCREENSHOTS);
        }
    }

    void beginRename(const std::string& key) {
        const Instances::Entry* entry = Instances::find(key);
        if (entry == nullptr) {
            return;
        }
        MainWindow::openVersionSettings(key);
        sectionKey = key;
        if (sections.page != SECTION_MAIN) {
            sections.go(SECTION_MAIN);
        }
        renaming = key;
        renameText = entry->info.id;
        NewUi::beginTextEdit(renameValue());
    }
}
