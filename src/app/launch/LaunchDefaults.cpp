#include "app/launch/LaunchDefaults.h"
#include "app/system/Config.h"

#include <core/base/fs.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <cstdlib>
#include <sstream>

namespace LaunchDefaults {

    namespace blb = b_launcher_base;

    namespace {
        struct GcName {
            blb::launch::gc_mode mode;
            const char* key;
        };

        const GcName GC_NAMES[] = {
                {blb::launch::gc_mode::tuned_g1, "tuned-g1"},
                {blb::launch::gc_mode::g1, "g1"},
                {blb::launch::gc_mode::zgc, "zgc"},
                {blb::launch::gc_mode::generational_zgc, "generational-zgc"},
                {blb::launch::gc_mode::none, "none"},
        };

        std::string text(const char* key, const std::string& fallback) {
            return blb::json_text(Config::data(), key, fallback);
        }

        void put(const char* key, const std::string& value) {
            Config::data()[key] = value;
            Config::save();
        }

        bool legacyCodes(const std::string& minecraft) {
            if (minecraft.empty()) {
                return false;
            }
            if (minecraft[0] == 'a' || minecraft[0] == 'b' || minecraft[0] == 'c' || minecraft.rfind("rd-", 0) == 0 ||
                minecraft.rfind("inf-", 0) == 0) {
                return true;
            }
            if (minecraft.rfind("1.", 0) != 0) {
                return false;
            }
            return std::atoi(minecraft.c_str() + 2) < 11;
        }
    }

    Language language() {
        std::string value = text("gameLanguage", "zh");
        return value == "none" ? Language::None : value == "en" ? Language::English : Language::Chinese;
    }

    void setLanguage(Language language) {
        put("gameLanguage", language == Language::None ? "none" : language == Language::English ? "en" : "zh");
    }

    blb::launch::gc_mode gc() {
        std::string value = text("gc", "tuned-g1");
        for (const GcName& name : GC_NAMES) {
            if (value == name.key) {
                return name.mode;
            }
        }
        return blb::launch::gc_mode::tuned_g1;
    }

    void setGc(blb::launch::gc_mode mode) {
        for (const GcName& name : GC_NAMES) {
            if (name.mode == mode) {
                put("gc", name.key);
            }
        }
    }

    std::string jvmArgs() {
        return text("jvmArgs", "");
    }

    void setJvmArgs(const std::string& value) {
        put("jvmArgs", value);
    }

    std::string gameArgs() {
        return text("gameArgs", "");
    }

    void setGameArgs(const std::string& value) {
        put("gameArgs", value);
    }

    std::string preLaunch() {
        return text("preLaunch", "");
    }

    void setPreLaunch(const std::string& value) {
        put("preLaunch", value);
    }

    bool showLogs() {
        return blb::json_bool(Config::data(), "showLogs");
    }

    void setShowLogs(bool on) {
        Config::data()["showLogs"] = on;
        Config::save();
    }

    bool mergeLogRepeats() {
        return blb::json_bool(Config::data(), "logMergeRepeats", true);
    }

    void setMergeLogRepeats(bool on) {
        Config::data()["logMergeRepeats"] = on;
        Config::save();
    }

    bool wrapLogLines() {
        return blb::json_bool(Config::data(), "logWrapLines", true);
    }

    void setWrapLogLines(bool on) {
        Config::data()["logWrapLines"] = on;
        Config::save();
    }

    void applyLanguage(Language language, const std::filesystem::path& gameDirectory, const std::string& minecraft,
                       const std::string& optionsFile) {
        if (language == Language::None || gameDirectory.empty()) {
            return;
        }
        bool legacy = legacyCodes(minecraft);
        std::string code = language == Language::Chinese ? (legacy ? "zh_CN" : "zh_cn") : (legacy ? "en_US" : "en_us");
        std::filesystem::path file = gameDirectory /
                (optionsFile.empty() ? std::filesystem::path("options.txt") : blb::path_from_utf8(optionsFile));
        auto existing = blb::fs::read_file(file);
        if (!existing && existing.err().code != blb::errc::not_found) {
            return;
        }
        std::string content = existing ? std::move(*existing) : std::string();
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("lang:", 0) == 0) {
                return;
            }
        }
        if (!content.empty() && content.back() != '\n') {
            content += '\n';
        }
        content += "lang:" + code + "\n";
        (void) blb::fs::write_file_atomic(file, content);
    }
}
