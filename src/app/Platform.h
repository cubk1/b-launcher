#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Platform {

    extern void* window;

    inline constexpr const char* USER_AGENT =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 "
            "Safari/537.36";

    std::filesystem::path exeDirectory();
    std::filesystem::path defaultMinecraft();
    std::filesystem::path cacheDirectory();
    bool open(const std::filesystem::path& path);
    bool openUrl(const std::string& url);
    void reveal(const std::filesystem::path& path);
    bool recycle(const std::filesystem::path& path);
    std::optional<std::filesystem::path> pickFolder(const std::filesystem::path& initial);
    std::optional<std::filesystem::path> pickJava(const std::filesystem::path& initial);
    std::optional<std::filesystem::path> pickImage(const std::filesystem::path& initial);
    std::vector<std::filesystem::path> pickFiles(const std::filesystem::path& initial, const wchar_t* title,
                                                 const wchar_t* typeName, const wchar_t* pattern);
    std::optional<std::filesystem::path> pickSavePack(const std::filesystem::path& suggested);
    std::optional<std::filesystem::path> pickSaveFile(const std::filesystem::path& suggested, const wchar_t* title,
                                                      void* owner = nullptr);
    uint32_t totalMemoryMb();

    struct Memory {
        uint32_t totalMb = 0;
        uint32_t availableMb = 0;
    };
    Memory memory();

    using Environment = std::vector<std::pair<std::string, std::string>>;
    std::string runCommand(const std::string& command, const Environment& environment,
                           const std::filesystem::path& directory, const std::function<bool()>& cancelled);
    std::string startCommand(const std::string& command, const Environment& environment,
                             const std::filesystem::path& directory);
    bool setPriority(int pid, int level);

    void minimizeWindow();
    void hideWindow();
    void showWindow();
    void closeWindow();
    std::string display(const std::filesystem::path& path);

    std::optional<std::string> loadValue(const std::string& name);
    bool storeValue(const std::string& name, const std::string& data, bool text);
}
