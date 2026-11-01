#include "app/instances/InstanceSettings.h"
#include "app/system/Catalog.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <cwctype>

namespace InstanceSettings {

    namespace blb = b_launcher_base;

    namespace {
        constexpr uint32_t MEMORY_STEP = 256;

        std::string cleanFilename(const std::string& text) {
            std::string out = blb::fs::filter_filename(text);
            while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
                out.pop_back();
            }
            return std::string(blb::trim(out));
        }

        bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
            if (a.empty() || b.empty()) {
                return false;
            }
            std::wstring left = a.lexically_normal().wstring();
            std::wstring right = b.lexically_normal().wstring();
            return left.size() == right.size() &&
                    std::equal(left.begin(), left.end(), right.begin(),
                               [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
        }
    }

    std::string joinArgs(const std::vector<std::string>& args) {
        return blb::join_arguments(args);
    }

    std::vector<std::string> splitArgs(const std::string& text) {
        return blb::split_arguments(text);
    }

    std::string joinEnvironment(const std::vector<std::pair<std::string, std::string>>& variables) {
        std::vector<std::string> items;
        for (const auto& [name, value] : variables) {
            items.push_back(name + "=" + value);
        }
        return joinArgs(items);
    }

    std::vector<std::pair<std::string, std::string>> splitEnvironment(const std::string& text) {
        std::vector<std::pair<std::string, std::string>> out;
        for (const std::string& item : splitArgs(text)) {
            size_t equals = item.find('=');
            if (equals == std::string::npos || equals == 0) {
                continue;
            }
            out.emplace_back(item.substr(0, equals), item.substr(equals + 1));
        }
        return out;
    }

    std::optional<int> parseSize(const std::string& text) {
        if (text.empty() || text.size() > 5 ||
            !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        int value = std::stoi(text);
        return value > 0 ? std::optional<int>(value) : std::nullopt;
    }

    uint32_t parseMegabytes(const std::string& text) {
        std::optional<int> value = parseSize(std::string(blb::trim(text)));
        return value ? (uint32_t) std::max(*value, (int) MEMORY_STEP) : 0u;
    }

    uint32_t automaticMemory(uint32_t totalMb) {
        return std::clamp(totalMb / 4, 2048u, 6144u);
    }

    bool isolatedByDefault(const Instances::Entry& entry) {
        switch (Instances::isolation()) {
            case blb::instance::isolation_mode::none: return false;
            case blb::instance::isolation_mode::all: return true;
            case blb::instance::isolation_mode::modded: return entry.info.modded();
        }
        return false;
    }

    std::vector<JavaOption> javaOptions() {
        std::vector<JavaOption> out;
        for (const blb::java::java_installation& java : Catalog::java().list) {
            std::string label = "Java " + (java.version.empty() ? std::to_string(java.major) : java.version);
            std::string folder = blb::path_to_utf8(java.home.filename());
            if (!folder.empty()) {
                label += " (" + folder + ")";
            }
            std::string unique = label;
            for (int n = 2;
                 std::any_of(out.begin(), out.end(), [&](const JavaOption& option) { return option.label == unique; });
                 n++) {
                unique = label + " " + std::to_string(n);
            }
            out.push_back(JavaOption{unique, java.executable});
        }
        return out;
    }

    bool isJava(const std::filesystem::path& chosen, const blb::java::java_installation& java) {
        return samePath(chosen, java.executable) || samePath(chosen, java.gui_executable) ||
                samePath(chosen, java.home);
    }

    std::string modpackName(const std::string& title) {
        std::string out = cleanFilename(title);
        return out.empty() ? std::string("modpack") : out;
    }

    std::string optionsFile(const std::string& text) {
        std::string out = cleanFilename(text);
        return blb::iequals(out, "options.txt") ? std::string() : out;
    }

    std::string versionOptionsFile(const std::string& minecraft) {
        return minecraft.empty() ? std::string() : optionsFile("options-" + minecraft + ".txt");
    }
}
