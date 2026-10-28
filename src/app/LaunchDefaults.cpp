#include "LaunchDefaults.h"
#include "Config.h"

#include <cstdlib>
#include <fstream>
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
            const nlohmann::json& doc = Config::data();
            auto value = doc.find(key);
            return value != doc.end() && value->is_string() ? value->get<std::string>() : fallback;
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
        const nlohmann::json& doc = Config::data();
        auto value = doc.find("showLogs");
        return value != doc.end() && value->is_boolean() && value->get<bool>();
    }

    void setShowLogs(bool on) {
        Config::data()["showLogs"] = on;
        Config::save();
    }

    std::vector<std::string> split(const std::string& input) {
        std::vector<std::string> out;
        std::string current;
        bool quoted = false;
        bool any = false;
        for (char c : input) {
            if (c == '"') {
                quoted = !quoted;
                any = true;
            } else if ((c == ' ' || c == '\t') && !quoted) {
                if (any) {
                    out.push_back(current);
                }
                current.clear();
                any = false;
            } else {
                current += c;
                any = true;
            }
        }
        if (any) {
            out.push_back(current);
        }
        return out;
    }

    void applyLanguage(Language language, const std::filesystem::path& gameDirectory, const std::string& minecraft) {
        if (language == Language::None || gameDirectory.empty()) {
            return;
        }
        bool legacy = legacyCodes(minecraft);
        std::string code = language == Language::Chinese ? (legacy ? "zh_CN" : "zh_cn") : (legacy ? "en_US" : "en_us");
        std::filesystem::path file = gameDirectory / "options.txt";
        std::string content;
        {
            std::ifstream in(file, std::ios::binary);
            if (in) {
                std::ostringstream read;
                read << in.rdbuf();
                content = read.str();
            }
        }
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
        std::error_code error;
        std::filesystem::create_directories(gameDirectory, error);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << content;
    }
}
