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

#include <cwctype>
#include <map>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace GameOptions {

    namespace blb = b_launcher_base;

    namespace {
        constexpr ULONGLONG RECHECK_MS = 1000;

        struct Cached {
            ULONGLONG checked = 0;
            std::filesystem::file_time_type stamp{};
            bool exists = false;
            std::map<std::string, std::string> values;
        };

        std::unordered_map<std::wstring, Cached> cache;

        std::wstring keyOf(const std::filesystem::path& file) {
            std::wstring text = file.lexically_normal().wstring();
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
            entry.values.clear();
            if (exists) {
                if (auto content = blb::fs::read_file(file)) {
                    entry.values = parse(*content);
                }
            }
            return entry;
        }
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
        for (const Game::Session& session : Game::sessions()) {
            const Instances::Entry* entry = Instances::find(session.instance);
            if (entry != nullptr && keyOf(fileOf(*entry)) == wanted) {
                return true;
            }
        }
        return false;
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
        auto existing = blb::fs::read_file(file);
        if (!existing && existing.err().code != blb::errc::not_found) {
            return existing.err().message;
        }
        std::string content = existing ? std::move(*existing) : std::string();
        std::string prefix = key + ":";
        std::string line = prefix + value;
        bool replaced = false;
        size_t from = 0;
        while (from < content.size()) {
            size_t end = content.find('\n', from);
            size_t stop = end == std::string::npos ? content.size() : end;
            if (content.compare(from, prefix.size(), prefix) == 0) {
                if (stop > from && content[stop - 1] == '\r') {
                    stop--;
                }
                content.replace(from, stop - from, line);
                replaced = true;
                break;
            }
            if (end == std::string::npos) {
                break;
            }
            from = end + 1;
        }
        if (!replaced) {
            if (!content.empty() && content.back() != '\n') {
                content += '\n';
            }
            content += line + "\n";
        }
        if (auto made = blb::fs::create_directories(file.parent_path()); !made) {
            return made.err().message;
        }
        if (auto written = blb::fs::write_file_atomic(file, content); !written) {
            return written.err().message;
        }
        Cached& entry = cache[keyOf(file)];
        entry.checked = 0;
        entry.exists = false;
        entry.values.clear();
        return {};
    }
}
