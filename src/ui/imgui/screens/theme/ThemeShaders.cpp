#include "ui/imgui/screens/theme/ThemeShaders.h"

#include "app/system/Config.h"
#include "app/system/Notices.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/theme/ThemeAssets.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeStore.h"

#include <core/base/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>

namespace ThemeShaders {

    namespace {
        constexpr float BUDGET_MS = 3.0f;
        constexpr int OVER_FRAMES = 30;
        constexpr int SAFE_FRAMES = 600;

        const UiTheme::Token TOKENS[8] = {UiTheme::SHELL,       UiTheme::SIDEBAR,      UiTheme::PANEL,
                                          UiTheme::SURFACE,     UiTheme::TEXT_PRIMARY, UiTheme::ACCENT,
                                          UiTheme::ACCENT_SOFT, UiTheme::OUTLINE};

        struct Entry {
            std::string source;
            std::string hash;
            int slot = -1;
            bool missing = false;
            bool halfRes = false;
            bool slow = false;
            bool guarded = false;
            int over = 0;
            int seen = 0;
        };

        std::map<std::string, Entry> entries;
        bool suppressed = false;
        bool drewThisFrame = false;
        bool armed = false;
        int safeFrames = 0;

        std::filesystem::path sentinel() {
            return ThemeStore::folder() / ".gpu-sentinel";
        }

        void clearSentinel() {
            std::error_code ec;
            std::filesystem::remove(sentinel(), ec);
            armed = false;
        }

        void writeSentinel(const Entry& entry) {
            std::error_code ec;
            std::filesystem::create_directories(ThemeStore::folder(), ec);
            std::ofstream out(sentinel(), std::ios::binary | std::ios::trunc);
            out << ThemeRuntime::current() << "\n" << entry.source << "\n" << entry.hash << "\n";
            armed = true;
            safeFrames = 0;
        }

        std::string hashOf(const std::string& text) {
            unsigned long long hash = 1469598103934665603ULL;
            for (unsigned char c : text) {
                hash = (hash ^ c) * 1099511628211ULL;
            }
            char out[17];
            std::snprintf(out, sizeof(out), "%016llx", hash);
            return out;
        }

        std::string pathOf(const std::string& source) {
            std::string owner;
            std::string path;
            return ThemePaintSpec::splitSource(source, owner, path) ? owner + "/" + path : source;
        }
    }

    void startup() {
        std::error_code ec;
        if (std::filesystem::exists(sentinel(), ec)) {
            Config::data()["themeShaderBlocked"] = true;
            Config::save();
            clearSentinel();
            Notices::add(Notices::Kind::Failed, "上次运行时自定义着色器没有正常结束，已暂停自定义着色器", 6000);
        }
    }

    void shutdown() {
        if (armed) {
            clearSentinel();
        }
    }

    void frame() {
        for (auto& [key, entry] : entries) {
            if (entry.slot < 0 || entry.slow) {
                continue;
            }
            const ShaderUtil::CustomInfo& info = ShaderUtil::customInfo(entry.slot);
            if (info.samples == entry.seen) {
                continue;
            }
            entry.seen = info.samples;
            entry.over = info.gpuMs > BUDGET_MS ? entry.over + 1 : 0;
            if (entry.over >= OVER_FRAMES) {
                entry.over = 0;
                if (!entry.halfRes) {
                    entry.halfRes = true;
                } else {
                    entry.slow = true;
                    if (armed) {
                        clearSentinel();
                    }
                }
            }
        }
        if (armed && drewThisFrame && ++safeFrames >= SAFE_FRAMES) {
            clearSentinel();
        }
        drewThisFrame = false;
    }

    bool enabled() {
        return b_launcher_base::json_bool(Config::data(), "themeShaders", true);
    }

    void setEnabled(bool on) {
        Config::data()["themeShaders"] = on;
        Config::save();
    }

    bool blocked() {
        return b_launcher_base::json_bool(Config::data(), "themeShaderBlocked", false);
    }

    void unblock() {
        Config::data()["themeShaderBlocked"] = false;
        Config::save();
        for (auto& [key, entry] : entries) {
            entry.slow = false;
            entry.over = 0;
            entry.guarded = false;
        }
    }

    void suppress(bool on) {
        suppressed = on;
    }

    void reset() {
        entries.clear();
    }

    bool draw(const ThemePaintSpec::Layer& layer, const ShaderUtil::Shape& shape, const ShaderUtil::Shape* clip,
              float opacity) {
        if (suppressed || !enabled() || blocked()) {
            return false;
        }
        Entry& entry = entries[layer.source];
        if (entry.source.empty()) {
            entry.source = layer.source;
            std::string owner;
            std::string path;
            std::optional<std::string> text;
            if (ThemePaintSpec::splitSource(layer.source, owner, path)) {
                text = ThemeStore::read(owner, path);
            }
            if (!text) {
                entry.missing = true;
            } else {
                entry.hash = hashOf(*text);
                entry.slot = ShaderUtil::customSlot(layer.source + "#" + entry.hash, *text);
            }
        }
        if (entry.missing || entry.slow || entry.slot < 0 || ShaderUtil::customInfo(entry.slot).failed) {
            return false;
        }
        if (!entry.guarded) {
            entry.guarded = true;
            writeSentinel(entry);
        }
        ShaderUtil::Custom custom;
        custom.slot = entry.slot;
        custom.time = (float) ThemeRuntime::time();
        custom.mouse[0] = UiRender::transformX(NewUi::mouseX) - shape.rect[0];
        custom.mouse[1] = UiRender::transformY(NewUi::mouseY) - shape.rect[1];
        for (int k = 0; k < 8; k++) {
            custom.tokens[k] = UiTheme::token(TOKENS[k]);
        }
        for (size_t k = 0; k < layer.textures.size() && k < 4; k++) {
            const ThemeAssets::Texture& texture = ThemeAssets::get(layer.textures[k]);
            custom.textures[k] = texture.ready ? texture.id : 0;
        }
        custom.backdrop = layer.backdrop;
        custom.halfRes = layer.halfRes || entry.halfRes;
        custom.opacity = opacity;
        ShaderUtil::custom(shape, clip, custom);
        drewThisFrame = true;
        return true;
    }

    std::vector<LayerState> states() {
        std::vector<LayerState> out;
        for (const auto& [key, entry] : entries) {
            const ShaderUtil::CustomInfo& info = ShaderUtil::customInfo(entry.slot);
            LayerState state;
            state.source = pathOf(entry.source);
            state.log = info.log;
            state.gpuMs = info.gpuMs;
            state.halfRes = entry.halfRes;
            if (entry.missing) {
                state.status = "找不到文件";
            } else if (info.failed) {
                state.status = "编译失败";
            } else if (entry.slow) {
                state.status = "太慢，已停用";
            } else if (blocked()) {
                state.status = "已暂停";
            } else if (entry.halfRes) {
                state.status = "半分辨率";
            } else {
                state.status = "正常";
            }
            out.push_back(std::move(state));
        }
        return out;
    }

    std::vector<std::string> problems() {
        std::vector<std::string> out;
        for (const LayerState& state : states()) {
            if (state.status == "找不到文件" || state.status == "编译失败" || state.status == "太慢，已停用") {
                std::string line = state.source + "：" + state.status;
                if (!state.log.empty()) {
                    std::string first = state.log.substr(0, state.log.find('\n'));
                    line += "，" + first;
                }
                out.push_back(line);
            }
        }
        return out;
    }
}
