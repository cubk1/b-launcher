#include "app/launch/GameOptions.h"
#include "app/launch/Game.h"
#include "app/launch/Patches.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <cwctype>
#include <map>
#include <string_view>
#include <vector>
#include <system_error>
#include <unordered_map>

namespace GameOptions {

    namespace blb = b_launcher_base;

    const std::array<OptionSpec, 5> OPTIONS{{
            {Option::Brightness, "gamma", "亮度", "游戏里的亮度，0% 是昏暗，100% 是明亮。", 0, 100, 50, "%"},
            {Option::Fov, "fov", "视野", "视野范围（FOV），默认 70，越大看到的范围越广。", 30, 110, 70, ""},
            {Option::Sensitivity, "mouseSensitivity", "鼠标灵敏度", "鼠标转动视角的快慢，默认 100%。", 0, 200, 100, "%"},
            {Option::RenderDistance, "renderDistance", "渲染距离", "能看到多远的区块，默认 12，越大越占用性能。", 2, 32, 12, " 区块"},
            {Option::GuiScale, "guiScale", "界面尺寸", "游戏界面的缩放倍数，自动时随窗口大小变化；超过窗口能容纳的倍数时游戏会自动调小。",
             0, std::numeric_limits<int>::max(), 0, "x"},
    }};

    namespace {
        constexpr ULONGLONG RECHECK_MS = 1000;

        struct Cached {
            ULONGLONG checked = 0;
            std::filesystem::file_time_type stamp{};
            bool exists = false;
            unsigned revision = 0;
            std::map<std::string, std::string> values;
        };

        std::unordered_map<std::wstring, Cached> cache;
        unsigned revisions = 0;

        std::optional<double> number(const std::optional<std::string>& text) {
            if (!text) return std::nullopt;
            std::string_view input(*text);
            auto first = input.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return std::nullopt;
            input = input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1);
            double value = 0;
            auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
            if (error != std::errc() || end != input.data() + input.size() || !std::isfinite(value)) return std::nullopt;
            return value;
        }

        // These options use exact thousandths in [-1, 1]. Keep their storage
        // locale-independent without pulling in a general floating-point formatter.
        std::string decimalThousandths(int value) {
            int magnitude = value < 0 ? -value : value;
            std::string text = std::to_string(magnitude / 1000) + "." +
                    std::to_string(1000 + magnitude % 1000).substr(1);
            while (text.back() == '0' && text[text.size() - 2] != '.') text.pop_back();
            if (value < 0) text.insert(text.begin(), '-');
            return text;
        }

        std::wstring keyOf(const std::filesystem::path& file) {
            std::wstring text = blb::fs::absolute(file).lexically_normal().wstring();
            for (wchar_t& c : text) {
                c = (wchar_t) std::towlower(c);
            }
            return text;
        }

        std::map<std::string, std::string> parse(const std::string& content) {
            std::map<std::string, std::string> out;
            size_t from = 0;
            while (from < content.size()) {
                size_t end = content.find('\n', from);
                std::string_view line(content.data() + from, (end == std::string::npos ? content.size() : end) - from);
                if (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }
                size_t colon = line.find(':');
                if (colon != std::string_view::npos) {
                    out.emplace(std::string(line.substr(0, colon)), std::string(line.substr(colon + 1)));
                }
                if (end == std::string::npos) {
                    break;
                }
                from = end + 1;
            }
            return out;
        }

        const Cached& load(const std::filesystem::path& file) {
            Cached& entry = cache[keyOf(file)];
            ULONGLONG now = GetTickCount64();
            if (entry.checked != 0 && now - entry.checked < RECHECK_MS) {
                return entry;
            }
            entry.checked = now;
            std::error_code error;
            std::filesystem::file_time_type stamp = std::filesystem::last_write_time(file, error);
            bool exists = !error;
            if (exists == entry.exists && (!exists || stamp == entry.stamp)) {
                return entry;
            }
            entry.exists = exists;
            entry.stamp = stamp;
            entry.revision = ++revisions;
            entry.values.clear();
            if (exists) {
                if (auto content = blb::fs::read_file(file)) {
                    entry.values = parse(*content);
                }
            }
            return entry;
        }
    }

    int value(const std::filesystem::path& file, Option option) {
        const auto& spec = OPTIONS.at((size_t) option);
        if (file.empty()) return spec.fallback;
        auto stored = number(get(file, spec.key));
        if (!stored) return spec.fallback;
        double value = *stored;
        switch (option) {
            case Option::Brightness: value = std::clamp(value, 0.0, 1.0) * 100; break;
            case Option::Sensitivity: value = std::clamp(value, 0.0, 1.0) * 200; break;
            case Option::Fov:
                if (std::abs(value) <= 1.0001) value = 70 + value * 40;
                break;
            default: break;
        }
        return (int) std::lround(std::clamp(value, (double) spec.low, (double) spec.high));
    }

    std::string setValue(const std::filesystem::path& file, Option option, double value) {
        if (file.empty()) return "没有可修改的游戏设置文件";
        if (!std::isfinite(value)) return "游戏选项数值无效";
        const auto& spec = OPTIONS.at((size_t) option);
        int picked = (int) std::lround(std::clamp(value, (double) spec.low, (double) spec.high));
        std::string encoded;
        switch (option) {
            case Option::Brightness: encoded = decimalThousandths(picked * 10); break;
            case Option::Fov: encoded = decimalThousandths((picked - 70) * 25); break;
            case Option::Sensitivity: encoded = decimalThousandths(picked * 5); break;
            default: encoded = std::to_string(picked); break;
        }
        if (encoded.empty()) return "游戏选项数值无效";
        return set(file, spec.key, encoded);
    }

    std::filesystem::path fileOf(const Instances::Entry& entry) {
        const blb::instance::instance_settings& settings = entry.info.settings;
        bool own = Patches::enabled(settings.patch) && !settings.options_file.empty();
        return entry.gameDirectory /
                (own ? blb::path_from_utf8(settings.options_file) : std::filesystem::path("options.txt"));
    }

    bool ownFile(const Instances::Entry& entry) {
        const blb::instance::instance_settings& settings = entry.info.settings;
        return entry.isolated || (Patches::enabled(settings.patch) && !settings.options_file.empty());
    }

    std::filesystem::path sharedFile(const std::string& source) {
        const Instances::Source* found = Instances::source(source);
        return found != nullptr ? found->root / "options.txt" : std::filesystem::path();
    }

    bool inUse(const std::filesystem::path& file) {
        std::wstring wanted = keyOf(file);
        return std::ranges::any_of(Game::sessions(), [&](const Game::Session& session) {
            return !session.optionsFile.empty() &&
                    (keyOf(session.optionsFile) == wanted || blb::fs::same_file(file, session.optionsFile));
        });
    }

    std::optional<std::string> get(const std::filesystem::path& file, const std::string& key) {
        const Cached& entry = load(file);
        auto found = entry.values.find(key);
        if (found == entry.values.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::string set(const std::filesystem::path& file, const std::string& key, const std::string& value) {
        return setAll(file, {{key, value}});
    }

    const std::map<std::string, std::string>& all(const std::filesystem::path& file) {
        return load(file).values;
    }

    unsigned revision(const std::filesystem::path& file) {
        return load(file).revision;
    }

    std::string setAll(const std::filesystem::path& file,
                       const std::vector<std::pair<std::string, std::string>>& values) {
        if (values.empty()) return {};
        if (inUse(file)) return "游戏正在使用这些设置，退出游戏后才能修改";
        auto existing = blb::fs::read_file(file);
        if (!existing && existing.err().code != blb::errc::not_found) {
            return existing.err().message;
        }
        std::string content = existing ? std::move(*existing) : std::string();
        std::string newline = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::vector<std::string> lines;
        size_t from = 0;
        while (from < content.size()) {
            size_t end = content.find('\n', from);
            size_t stop = end == std::string::npos ? content.size() : end;
            std::string line = content.substr(from, stop - from);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            if (end == std::string::npos) {
                break;
            }
            from = end + 1;
        }
        std::unordered_map<std::string, size_t> at;
        for (size_t i = 0; i < lines.size(); i++) {
            size_t colon = lines[i].find(':');
            if (colon != std::string::npos) {
                at.emplace(lines[i].substr(0, colon), i);
            }
        }
        for (const auto& [key, value] : values) {
            auto found = at.find(key);
            if (found != at.end()) {
                lines[found->second] = key + ":" + value;
            } else {
                at.emplace(key, lines.size());
                lines.push_back(key + ":" + value);
            }
        }
        std::string out;
        for (const std::string& line : lines) {
            out += line + newline;
        }
        if (auto made = blb::fs::create_directories(file.parent_path()); !made) {
            return made.err().message;
        }
        if (auto written = blb::fs::write_file_atomic(file, out); !written) {
            return written.err().message;
        }
        cache.erase(keyOf(file));
        return {};
    }
}
