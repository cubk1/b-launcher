#include "app/launch/KeyBinds.h"
#include "app/launch/GameOptions.h"
#include "app/system/AsyncTasks.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/mods.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>

namespace KeyBinds {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        struct KeyDef {
            const char* name;
            int legacy;
            const char* label;
        };

        const KeyDef KEYS[] = {
                {"key.keyboard.escape", 1, "Esc"},
                {"key.keyboard.1", 2, "1"},
                {"key.keyboard.2", 3, "2"},
                {"key.keyboard.3", 4, "3"},
                {"key.keyboard.4", 5, "4"},
                {"key.keyboard.5", 6, "5"},
                {"key.keyboard.6", 7, "6"},
                {"key.keyboard.7", 8, "7"},
                {"key.keyboard.8", 9, "8"},
                {"key.keyboard.9", 10, "9"},
                {"key.keyboard.0", 11, "0"},
                {"key.keyboard.minus", 12, "-"},
                {"key.keyboard.equal", 13, "="},
                {"key.keyboard.backspace", 14, "Backspace"},
                {"key.keyboard.tab", 15, "Tab"},
                {"key.keyboard.q", 16, "Q"},
                {"key.keyboard.w", 17, "W"},
                {"key.keyboard.e", 18, "E"},
                {"key.keyboard.r", 19, "R"},
                {"key.keyboard.t", 20, "T"},
                {"key.keyboard.y", 21, "Y"},
                {"key.keyboard.u", 22, "U"},
                {"key.keyboard.i", 23, "I"},
                {"key.keyboard.o", 24, "O"},
                {"key.keyboard.p", 25, "P"},
                {"key.keyboard.left.bracket", 26, "["},
                {"key.keyboard.right.bracket", 27, "]"},
                {"key.keyboard.enter", 28, "Enter"},
                {"key.keyboard.left.control", 29, "左 Ctrl"},
                {"key.keyboard.a", 30, "A"},
                {"key.keyboard.s", 31, "S"},
                {"key.keyboard.d", 32, "D"},
                {"key.keyboard.f", 33, "F"},
                {"key.keyboard.g", 34, "G"},
                {"key.keyboard.h", 35, "H"},
                {"key.keyboard.j", 36, "J"},
                {"key.keyboard.k", 37, "K"},
                {"key.keyboard.l", 38, "L"},
                {"key.keyboard.semicolon", 39, ";"},
                {"key.keyboard.apostrophe", 40, "'"},
                {"key.keyboard.grave.accent", 41, "`"},
                {"key.keyboard.left.shift", 42, "左 Shift"},
                {"key.keyboard.backslash", 43, "\\"},
                {"key.keyboard.z", 44, "Z"},
                {"key.keyboard.x", 45, "X"},
                {"key.keyboard.c", 46, "C"},
                {"key.keyboard.v", 47, "V"},
                {"key.keyboard.b", 48, "B"},
                {"key.keyboard.n", 49, "N"},
                {"key.keyboard.m", 50, "M"},
                {"key.keyboard.comma", 51, ","},
                {"key.keyboard.period", 52, "."},
                {"key.keyboard.slash", 53, "/"},
                {"key.keyboard.right.shift", 54, "右 Shift"},
                {"key.keyboard.keypad.multiply", 55, "小键盘 *"},
                {"key.keyboard.left.alt", 56, "左 Alt"},
                {"key.keyboard.space", 57, "空格"},
                {"key.keyboard.caps.lock", 58, "Caps Lock"},
                {"key.keyboard.f1", 59, "F1"},
                {"key.keyboard.f2", 60, "F2"},
                {"key.keyboard.f3", 61, "F3"},
                {"key.keyboard.f4", 62, "F4"},
                {"key.keyboard.f5", 63, "F5"},
                {"key.keyboard.f6", 64, "F6"},
                {"key.keyboard.f7", 65, "F7"},
                {"key.keyboard.f8", 66, "F8"},
                {"key.keyboard.f9", 67, "F9"},
                {"key.keyboard.f10", 68, "F10"},
                {"key.keyboard.num.lock", 69, "Num Lock"},
                {"key.keyboard.scroll.lock", 70, "Scroll Lock"},
                {"key.keyboard.keypad.7", 71, "小键盘 7"},
                {"key.keyboard.keypad.8", 72, "小键盘 8"},
                {"key.keyboard.keypad.9", 73, "小键盘 9"},
                {"key.keyboard.keypad.subtract", 74, "小键盘 -"},
                {"key.keyboard.keypad.4", 75, "小键盘 4"},
                {"key.keyboard.keypad.5", 76, "小键盘 5"},
                {"key.keyboard.keypad.6", 77, "小键盘 6"},
                {"key.keyboard.keypad.add", 78, "小键盘 +"},
                {"key.keyboard.keypad.1", 79, "小键盘 1"},
                {"key.keyboard.keypad.2", 80, "小键盘 2"},
                {"key.keyboard.keypad.3", 81, "小键盘 3"},
                {"key.keyboard.keypad.0", 82, "小键盘 0"},
                {"key.keyboard.keypad.decimal", 83, "小键盘 ."},
                {"key.keyboard.f11", 87, "F11"},
                {"key.keyboard.f12", 88, "F12"},
                {"key.keyboard.f13", 100, "F13"},
                {"key.keyboard.f14", 101, "F14"},
                {"key.keyboard.f15", 102, "F15"},
                {"key.keyboard.keypad.equal", 141, "小键盘 ="},
                {"key.keyboard.keypad.enter", 156, "小键盘 Enter"},
                {"key.keyboard.right.control", 157, "右 Ctrl"},
                {"key.keyboard.keypad.divide", 181, "小键盘 /"},
                {"key.keyboard.print.screen", 183, "Print Screen"},
                {"key.keyboard.right.alt", 184, "右 Alt"},
                {"key.keyboard.pause", 197, "Pause"},
                {"key.keyboard.home", 199, "Home"},
                {"key.keyboard.up", 200, "↑"},
                {"key.keyboard.page.up", 201, "Page Up"},
                {"key.keyboard.left", 203, "←"},
                {"key.keyboard.right", 205, "→"},
                {"key.keyboard.end", 207, "End"},
                {"key.keyboard.down", 208, "↓"},
                {"key.keyboard.page.down", 209, "Page Down"},
                {"key.keyboard.insert", 210, "Insert"},
                {"key.keyboard.delete", 211, "Delete"},
                {"key.keyboard.left.win", 219, "左 Win"},
                {"key.keyboard.right.win", 220, "右 Win"},
                {"key.keyboard.menu", 221, "Menu"},
                {"key.mouse.left", -100, "鼠标左键"},
                {"key.mouse.right", -99, "鼠标右键"},
                {"key.mouse.middle", -98, "鼠标中键"},
                {"key.mouse.4", -97, "鼠标侧键 4"},
                {"key.mouse.5", -96, "鼠标侧键 5"},
                {"key.mouse.6", -95, "鼠标侧键 6"},
                {"key.mouse.7", -94, "鼠标侧键 7"},
                {"key.mouse.8", -93, "鼠标侧键 8"},
        };

        struct Vanilla {
            const char* id;
            const char* name;
            const char* category;
        };

        const char* const CATEGORIES[] = {"移动", "游戏内容", "物品栏", "创造模式", "多人游戏", "杂项"};

        const Vanilla VANILLA[] = {
                {"key.forward", "向前移动", "移动"},
                {"key.left", "向左移动", "移动"},
                {"key.back", "向后移动", "移动"},
                {"key.right", "向右移动", "移动"},
                {"key.jump", "跳跃", "移动"},
                {"key.sneak", "潜行", "移动"},
                {"key.sprint", "疾跑", "移动"},
                {"key.attack", "攻击/摧毁", "游戏内容"},
                {"key.use", "使用物品/放置方块", "游戏内容"},
                {"key.pickItem", "选取方块", "游戏内容"},
                {"key.drop", "丢弃所选物品", "物品栏"},
                {"key.inventory", "开启/关闭物品栏", "物品栏"},
                {"key.swapOffhand", "与副手交换物品", "物品栏"},
                {"key.hotbar.1", "快捷栏 1", "物品栏"},
                {"key.hotbar.2", "快捷栏 2", "物品栏"},
                {"key.hotbar.3", "快捷栏 3", "物品栏"},
                {"key.hotbar.4", "快捷栏 4", "物品栏"},
                {"key.hotbar.5", "快捷栏 5", "物品栏"},
                {"key.hotbar.6", "快捷栏 6", "物品栏"},
                {"key.hotbar.7", "快捷栏 7", "物品栏"},
                {"key.hotbar.8", "快捷栏 8", "物品栏"},
                {"key.hotbar.9", "快捷栏 9", "物品栏"},
                {"key.saveToolbarActivator", "保存快捷栏", "创造模式"},
                {"key.loadToolbarActivator", "加载快捷栏", "创造模式"},
                {"key.chat", "打开聊天栏", "多人游戏"},
                {"key.playerlist", "显示玩家列表", "多人游戏"},
                {"key.command", "输入命令", "多人游戏"},
                {"key.socialInteractions", "社交屏幕", "多人游戏"},
                {"key.screenshot", "截图", "杂项"},
                {"key.togglePerspective", "切换视角", "杂项"},
                {"key.smoothCamera", "切换电影视角", "杂项"},
                {"key.fullscreen", "切换全屏", "杂项"},
                {"key.spectatorOutlines", "高亮玩家（旁观者）", "杂项"},
                {"key.advancements", "进度", "杂项"},
                {"key.streamStartStop", "开始/停止直播", "杂项"},
                {"key.streamPauseUnpause", "暂停/继续直播", "杂项"},
                {"key.streamCommercials", "显示直播广告", "杂项"},
                {"key.streamToggleMic", "开关麦克风", "杂项"},
                {"key.quickActions", "快捷操作", "杂项"},
        };

        struct Names {
            bool loading = false;
            unsigned version = 0;
            std::string asked;
            std::unordered_map<std::string, std::string> text;
            std::unordered_map<std::string, std::string> mods;
            std::unordered_map<std::string, std::shared_ptr<const Pixels>> icons;
        };

        struct Cached {
            bool built = false;
            unsigned revision = 0;
            unsigned names = 0;
            Table table;
        };

        struct Decoded {
            std::string key;
            std::string modifier;
            bool suffixed = false;
            bool legacy = false;
        };

        AsyncTasks tasks;
        std::map<std::filesystem::path, Cached> tables;
        std::map<std::filesystem::path, Names> folders;
        unsigned nameVersions = 0;
        const Table EMPTY;

        const KeyDef* byName(const std::string& name) {
            static const std::unordered_map<std::string, const KeyDef*> index = [] {
                std::unordered_map<std::string, const KeyDef*> out;
                for (const KeyDef& def : KEYS) {
                    out.emplace(def.name, &def);
                }
                return out;
            }();
            auto found = index.find(name);
            return found != index.end() ? found->second : nullptr;
        }

        const KeyDef* byLegacy(int code) {
            static const std::unordered_map<int, const KeyDef*> index = [] {
                std::unordered_map<int, const KeyDef*> out;
                for (const KeyDef& def : KEYS) {
                    out.emplace(def.legacy, &def);
                }
                return out;
            }();
            auto found = index.find(code);
            return found != index.end() ? found->second : nullptr;
        }

        const Vanilla* vanillaOf(const std::string& id) {
            static const std::unordered_map<std::string, const Vanilla*> index = [] {
                std::unordered_map<std::string, const Vanilla*> out;
                for (const Vanilla& item : VANILLA) {
                    out.emplace(item.id, &item);
                }
                return out;
            }();
            auto found = index.find(id);
            return found != index.end() ? found->second : nullptr;
        }

        std::string namespaceOf(const std::string& id) {
            size_t start = id.rfind("key.", 0) == 0 ? 4 : 0;
            size_t dot = id.find('.', start);
            if (dot == std::string::npos) {
                return {};
            }
            return id.substr(start, dot - start);
        }

        std::string prettify(const std::string& id) {
            size_t dot = id.rfind('.');
            std::string tail = dot == std::string::npos ? id : id.substr(dot + 1);
            std::string out;
            for (size_t i = 0; i < tail.size(); i++) {
                char c = tail[i];
                if (c == '_' || c == '-') {
                    out += ' ';
                } else if (i > 0 && std::isupper((unsigned char) c) && std::islower((unsigned char) tail[i - 1])) {
                    out += ' ';
                    out += c;
                } else {
                    out += c;
                }
            }
            if (!out.empty()) {
                out[0] = (char) std::toupper((unsigned char) out[0]);
            }
            return out.empty() ? id : out;
        }

        Decoded decode(const std::string& value) {
            Decoded out;
            std::string head = value;
            size_t colon = value.find(':');
            if (colon != std::string::npos) {
                head = value.substr(0, colon);
                out.modifier = value.substr(colon + 1);
                out.suffixed = true;
            }
            if (head.rfind("key.", 0) == 0) {
                out.key = head;
                return out;
            }
            char* end = nullptr;
            long code = std::strtol(head.c_str(), &end, 10);
            if (head.empty() || end == nullptr || *end != '\0') {
                out.key = head;
                return out;
            }
            out.legacy = true;
            if (code == 0) {
                out.key = UNBOUND;
                return out;
            }
            const KeyDef* def = byLegacy((int) code);
            out.key = def != nullptr ? std::string(def->name) : "legacy." + std::to_string(code);
            return out;
        }

        bool encode(const std::string& key, const std::string& modifier, bool suffixed, bool legacy, std::string& out) {
            if (bound(key) && !suffixed && !modifier.empty() && modifier != "NONE") {
                return false;
            }
            std::string head;
            bool raw = key.rfind("legacy.", 0) == 0;
            if (legacy) {
                if (key == UNBOUND) {
                    head = "0";
                } else if (raw) {
                    head = key.substr(7);
                } else {
                    const KeyDef* def = byName(key);
                    if (def == nullptr) {
                        return false;
                    }
                    head = std::to_string(def->legacy);
                }
            } else if (raw) {
                const KeyDef* def = byLegacy(std::atoi(key.substr(7).c_str()));
                if (def == nullptr) {
                    return false;
                }
                head = def->name;
            } else {
                head = key;
            }
            if (suffixed) {
                head += ":" + (modifier.empty() ? std::string("NONE") : modifier);
            }
            out = head;
            return true;
        }

        std::string_view modifierKey(std::string_view key) {
            if (key == "key.keyboard.left.shift" || key == "key.keyboard.right.shift") {
                return "SHIFT";
            }
            if (key == "key.keyboard.left.control" || key == "key.keyboard.right.control") {
                return "CONTROL";
            }
            if (key == "key.keyboard.left.alt" || key == "key.keyboard.right.alt") {
                return "ALT";
            }
            return {};
        }

        void absorb(const std::string& content, bool json, const std::set<std::string>& wanted,
                    std::unordered_map<std::string, std::string>& into) {
            if (json) {
                nlohmann::json doc = nlohmann::json::parse(content, nullptr, false);
                if (!doc.is_object()) {
                    return;
                }
                for (auto it = doc.begin(); it != doc.end(); ++it) {
                    if (it.value().is_string() && wanted.count(it.key()) != 0) {
                        into[it.key()] = blb::strip_formatting(it.value().get<std::string>());
                    }
                }
                return;
            }
            size_t from = 0;
            while (from < content.size()) {
                size_t end = content.find('\n', from);
                std::string line = content.substr(from, (end == std::string::npos ? content.size() : end) - from);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                size_t equal = line.find('=');
                if (equal != std::string::npos && wanted.count(line.substr(0, equal)) != 0) {
                    into[line.substr(0, equal)] = blb::strip_formatting(line.substr(equal + 1));
                }
                if (end == std::string::npos) {
                    break;
                }
                from = end + 1;
            }
        }

        void loadNames(const std::filesystem::path& folderKey, const std::filesystem::path& folder,
                       const std::set<std::string>& wanted, const std::string& signature) {
            Names& names = folders[folderKey];
            names.loading = true;
            names.asked = signature;
            tasks.run(
                    [folderKey, folder, wanted](blb::cancellation_token token) -> Continuation {
                        std::unordered_map<std::string, std::string> chinese;
                        std::unordered_map<std::string, std::string> english;
                        std::unordered_map<std::string, std::string> mods;
                        std::unordered_map<std::string, std::shared_ptr<const Pixels>> icons;
                        std::error_code error;
                        for (const auto& item : std::filesystem::directory_iterator(folder / "mods", error)) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
                            std::string name = blb::to_lower(blb::path_to_utf8(item.path().filename()));
                            if (!name.ends_with(".jar") && !name.ends_with(".jar.disabled")) {
                                continue;
                            }
                            auto zip = blb::zip_reader::open(item.path());
                            if (!zip) {
                                continue;
                            }
                            (void) zip->for_each([&](const blb::zip_entry& entry, blb::zip_reader& reader) {
                                if (entry.directory) {
                                    return true;
                                }
                                std::string path = blb::to_lower(entry.name);
                                if (path.rfind("assets/", 0) != 0 || path.find("/lang/") == std::string::npos) {
                                    return true;
                                }
                                std::string file = path.substr(path.rfind('/') + 1);
                                bool zh = file == "zh_cn.json" || file == "zh_cn.lang";
                                bool en = file == "en_us.json" || file == "en_us.lang";
                                if (!zh && !en) {
                                    return true;
                                }
                                if (auto content = reader.read_current()) {
                                    absorb(*content, file.ends_with(".json"), wanted, zh ? chinese : english);
                                }
                                return true;
                            });
                            if (auto info = blb::modpack::read_mod(item.path()); info && !info->id.empty()) {
                                mods[info->id] = info->name.empty() ? info->id : info->name;
                                if (!info->icon.empty() && icons.count(info->id) == 0) {
                                    if (auto bytes = blb::modpack::read_mod_icon(*info)) {
                                        if (auto pixels = Pixels::decode(bytes->data(), bytes->size())) {
                                            icons[info->id] = pixels;
                                        }
                                    }
                                }
                            }
                        }
                        for (auto& item : chinese) {
                            english[item.first] = std::move(item.second);
                        }
                        return [folderKey, text = std::move(english), mods = std::move(mods),
                                icons = std::move(icons)]() mutable {
                            Names& names = folders[folderKey];
                            names.loading = false;
                            names.text = std::move(text);
                            names.mods = std::move(mods);
                            names.icons = std::move(icons);
                            names.version = ++nameVersions;
                        };
                    },
                    [folderKey](const std::string&) -> Continuation {
                        return [folderKey] {
                            Names& names = folders[folderKey];
                            names.loading = false;
                            names.version = ++nameVersions;
                        };
                    });
        }

        int orderOf(const std::string& id) {
            for (int i = 0; i < (int) std::size(VANILLA); i++) {
                if (id == VANILLA[i].id) {
                    return i;
                }
            }
            return (int) std::size(VANILLA);
        }

        int rankOf(const std::string& category) {
            for (int i = 0; i < (int) std::size(CATEGORIES); i++) {
                if (category == CATEGORIES[i]) {
                    return i;
                }
            }
            return (int) std::size(CATEGORIES);
        }

        void describe(Binding& binding, const Names& names) {
            if (const Vanilla* vanilla = vanillaOf(binding.id)) {
                binding.name = vanilla->name;
                binding.category = vanilla->category;
                binding.group = VANILLA_GROUP;
                return;
            }
            auto text = names.text.find(binding.id);
            binding.name = text != names.text.end() && !text->second.empty() ? text->second : prettify(binding.id);
            std::string space = namespaceOf(binding.id);
            binding.group = space.empty() ? std::string(OTHER_GROUP) : space;
            for (const std::string& key : {"key.categories." + space, "key.category." + space}) {
                auto found = names.text.find(key);
                if (found != names.text.end() && !found->second.empty()) {
                    binding.category = found->second;
                    return;
                }
            }
            auto mod = names.mods.find(space);
            if (mod != names.mods.end()) {
                binding.category = mod->second;
            } else {
                binding.category = space.empty() ? std::string("其他") : space;
            }
        }
    }

    const Table& table(const std::filesystem::path& file) {
        if (file.empty()) {
            return EMPTY;
        }
        Cached& cached = tables[blb::fs::path_key(file)];
        unsigned revision = GameOptions::revision(file);
        const std::map<std::string, std::string>& values = GameOptions::all(file);
        std::filesystem::path folder = file.parent_path();
        std::filesystem::path folderKey = blb::fs::path_key(folder);
        Names& names = folders[folderKey];
        if (cached.built && cached.revision == revision && cached.names == names.version) {
            cached.table.naming = names.loading;
            return cached.table;
        }
        cached.built = true;
        cached.revision = revision;
        cached.names = names.version;
        Table fresh;
        std::set<std::string> wanted;
        std::string signature;
        for (const auto& [key, value] : values) {
            if (key.rfind("key_", 0) != 0) {
                continue;
            }
            Binding binding;
            binding.id = key.substr(4);
            Decoded decoded = decode(value);
            binding.key = decoded.key;
            binding.modifier = decoded.modifier;
            binding.suffixed = decoded.suffixed;
            fresh.legacy = fresh.legacy || decoded.legacy;
            describe(binding, names);
            if (vanillaOf(binding.id) == nullptr) {
                std::string space = namespaceOf(binding.id);
                wanted.insert(binding.id);
                wanted.insert("key.categories." + space);
                wanted.insert("key.category." + space);
                signature += binding.id + "\n";
            }
            fresh.bindings.push_back(std::move(binding));
        }
        fresh.exists = !values.empty();
        std::stable_sort(fresh.bindings.begin(), fresh.bindings.end(), [](const Binding& a, const Binding& b) {
            int rankA = rankOf(a.category);
            int rankB = rankOf(b.category);
            if (rankA != rankB) {
                return rankA < rankB;
            }
            if (a.category != b.category) {
                return blb::to_lower(a.category) < blb::to_lower(b.category);
            }
            int orderA = orderOf(a.id);
            int orderB = orderOf(b.id);
            if (orderA != orderB) {
                return orderA < orderB;
            }
            return blb::to_lower(a.name) < blb::to_lower(b.name);
        });
        std::map<std::string, Group> grouped;
        for (const Binding& binding : fresh.bindings) {
            Group& group = grouped[binding.group];
            if (group.id.empty()) {
                group.id = binding.group;
                if (binding.group == VANILLA_GROUP) {
                    group.label = "Minecraft";
                } else if (binding.group == DEBUG_GROUP) {
                    group.label = "调试";
                } else if (binding.group == OPTIFINE_GROUP) {
                    group.label = "OptiFine";
                } else {
                    auto mod = names.mods.find(binding.group);
                    group.label = mod != names.mods.end()  ? mod->second
                            : binding.group == OTHER_GROUP ? std::string("其他")
                                                           : binding.category;
                    auto icon = names.icons.find(binding.group);
                    if (icon != names.icons.end()) {
                        group.icon = icon->second;
                    }
                }
            }
            group.count++;
        }
        for (auto& item : grouped) {
            fresh.groups.push_back(std::move(item.second));
        }
        auto place = [](const Group& group) {
            return group.id == VANILLA_GROUP ? 0 : group.id == DEBUG_GROUP ? 1 : group.id == OTHER_GROUP ? 3 : 2;
        };
        std::stable_sort(fresh.groups.begin(), fresh.groups.end(), [&place](const Group& a, const Group& b) {
            if (place(a) != place(b)) {
                return place(a) < place(b);
            }
            return blb::to_lower(a.label) < blb::to_lower(b.label);
        });
        if (!names.loading && !signature.empty() && signature != names.asked) {
            loadNames(folderKey, folder, wanted, signature);
        }
        fresh.naming = names.loading;
        cached.table = std::move(fresh);
        return cached.table;
    }

    std::string keyLabel(const std::string& key) {
        if (key.empty() || key == UNBOUND) {
            return "未绑定";
        }
        if (const KeyDef* def = byName(key)) {
            return def->label;
        }
        if (key.rfind("legacy.", 0) == 0) {
            return "键码 " + key.substr(7);
        }
        if (key.rfind("key.mouse.", 0) == 0) {
            return "鼠标侧键 " + key.substr(10);
        }
        std::string tail = key.rfind("key.keyboard.", 0) == 0 ? key.substr(13) : key;
        for (char& c : tail) {
            c = c == '.' ? ' ' : (char) std::toupper((unsigned char) c);
        }
        return tail;
    }

    std::string modifierLabel(const std::string& modifier) {
        if (modifier == "CONTROL") {
            return "Ctrl";
        }
        if (modifier == "SHIFT") {
            return "Shift";
        }
        if (modifier == "ALT") {
            return "Alt";
        }
        return {};
    }

    bool bound(const std::string& key) {
        return !key.empty() && key != UNBOUND;
    }

    bool conflicts(const Binding& first, const Binding& second) {
        if (!bound(first.key) || !bound(second.key) ||
            (first.group == DEBUG_GROUP) != (second.group == DEBUG_GROUP)) {
            return false;
        }
        std::string_view firstModifier = first.modifier == "NONE" ? std::string_view() : first.modifier;
        std::string_view secondModifier = second.modifier == "NONE" ? std::string_view() : second.modifier;
        if ((!firstModifier.empty() && firstModifier == modifierKey(second.key)) ||
            (!secondModifier.empty() && secondModifier == modifierKey(first.key))) {
            return true;
        }
        return first.key == second.key &&
                (firstModifier.empty() || secondModifier.empty() || firstModifier == secondModifier);
    }

    bool legacyVersion(const std::string& minecraft) {
        if (minecraft.rfind("1.", 0) == 0) {
            return blb::compare_versions(minecraft, "1.13") < 0;
        }
        return minecraft.rfind("b1.", 0) == 0 || minecraft.rfind("a1.", 0) == 0;
    }

    std::string assign(const std::filesystem::path& file, const std::string& id, const std::string& key,
                       bool legacyHint) {
        const Table& current = table(file);
        bool legacy = current.bindings.empty() ? legacyHint : current.legacy;
        bool suffixed = false;
        for (const Binding& binding : current.bindings) {
            if (binding.id == id) {
                suffixed = binding.suffixed;
                break;
            }
        }
        std::string value;
        if (!encode(key, "NONE", suffixed, legacy, value)) {
            return "这个版本不支持 " + keyLabel(key);
        }
        return GameOptions::set(file, "key_" + id, value);
    }

    std::string unbind(const std::filesystem::path& file, const std::string& id, bool legacyHint) {
        return assign(file, id, UNBOUND, legacyHint);
    }

    std::string copy(const std::filesystem::path& from, const std::vector<Target>& targets) {
        std::vector<Binding> source = table(from).bindings;
        if (source.empty()) {
            return "没有可以复制的按键";
        }
        struct Pending {
            std::filesystem::path file;
            std::vector<std::pair<std::string, std::string>> values;
        };
        std::vector<Pending> pending;
        for (const Target& target : targets) {
            if (GameOptions::inUse(target.file)) {
                return blb::path_to_utf8(target.file) + "：游戏正在使用这些设置，退出游戏后才能修改";
            }
            const Table& destination = table(target.file);
            bool legacy = destination.bindings.empty() ? target.legacy : destination.legacy;
            bool modifiers = std::any_of(destination.bindings.begin(), destination.bindings.end(),
                                         [](const Binding& binding) { return binding.suffixed; });
            std::unordered_map<std::string, bool> suffixes;
            for (const Binding& binding : destination.bindings) {
                suffixes.emplace(binding.id, binding.suffixed);
            }
            Pending write{target.file, {}};
            for (const Binding& binding : source) {
                auto suffix = suffixes.find(binding.id);
                bool suffixed = suffix != suffixes.end() ? suffix->second : modifiers;
                if (modifiers && !binding.modifier.empty() && binding.modifier != "NONE") {
                    suffixed = true;
                }
                std::string value;
                if (!encode(binding.key, binding.modifier, suffixed, legacy, value)) {
                    std::string combination = keyLabel(binding.key);
                    if (bound(binding.key) && !binding.modifier.empty() && binding.modifier != "NONE") {
                        std::string label = modifierLabel(binding.modifier);
                        combination = (label.empty() ? binding.modifier : label) + " + " + combination;
                    }
                    return blb::path_to_utf8(target.file) + "：无法完整复制 " + binding.name + "（" + combination + "）";
                }
                write.values.push_back({"key_" + binding.id, value});
            }
            pending.push_back(std::move(write));
        }
        size_t copied = 0;
        for (const Pending& write : pending) {
            std::string error = GameOptions::setAll(write.file, write.values);
            if (!error.empty()) {
                std::string prefix = copied > 0 ? "已复制到 " + std::to_string(copied) + " 处；" : std::string();
                return prefix + blb::path_to_utf8(write.file) + "：" + error;
            }
            ++copied;
        }
        return {};
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
