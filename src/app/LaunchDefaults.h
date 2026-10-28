#pragma once

#include <core/launcher/arguments.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace LaunchDefaults {

    enum class Language { None, Chinese, English };

    Language language();
    void setLanguage(Language language);

    b_launcher_base::launch::gc_mode gc();
    void setGc(b_launcher_base::launch::gc_mode mode);

    std::string jvmArgs();
    void setJvmArgs(const std::string& text);
    std::string gameArgs();
    void setGameArgs(const std::string& text);
    std::string preLaunch();
    void setPreLaunch(const std::string& text);

    bool showLogs();
    void setShowLogs(bool on);

    std::vector<std::string> split(const std::string& text);
    void applyLanguage(Language language, const std::filesystem::path& gameDirectory, const std::string& minecraft);
}
