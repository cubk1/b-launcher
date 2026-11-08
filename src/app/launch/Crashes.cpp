#include "app/launch/Crashes.h"
#include "app/launch/AskAi.h"
#include "app/system/About.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/fs.hpp>
#include <core/base/json.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace Crashes {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr size_t npos = std::string_view::npos;
        constexpr size_t REPORT_LIMIT = 512u << 10;
        constexpr size_t NATIVE_LIMIT = 256u << 10;
        constexpr size_t OUTPUT_LIMIT = 2u << 20;
        constexpr size_t FILE_LIMIT = 32u << 20;
        constexpr size_t SUMMARY_LIMIT = 400;
        constexpr size_t MAX_CAUSES = 3;
        constexpr size_t MAX_LINES = 4;
        constexpr int SUMMARY_SCAN = 3000;
        constexpr int64_t SLACK_SECONDS = 5;
        constexpr std::string_view MARKER = "#@!@# Game crashed! Crash report saved to: #@!@# ";
        const char* const SHARE_URL = "https://api.mclo.gs/1/log";
        constexpr size_t SHARE_BYTES = 8u << 20;
        constexpr size_t SHARE_LINES = 25000;

        AsyncTasks tasks;
        bool busy = false;

        bool has(std::string_view text, std::string_view needle) {
            return text.find(needle) != npos;
        }

        bool any(std::string_view text, std::initializer_list<std::string_view> needles) {
            return std::any_of(needles.begin(), needles.end(),
                               [text](std::string_view needle) { return has(text, needle); });
        }

        std::string upTo(std::string_view text, size_t at, std::string_view stops) {
            size_t end = text.find_first_of(stops, at);
            return std::string(blb::trim(text.substr(at, end == npos ? npos : end - at)));
        }

        std::string lineAfter(std::string_view text, std::string_view marker) {
            size_t at = text.find(marker);
            return at == npos ? std::string() : upTo(text, at + marker.size(), "\r\n");
        }

        std::string between(std::string_view text, std::string_view marker, char close) {
            size_t at = text.find(marker);
            const char stops[] = {close, '\r', '\n'};
            return at == npos ? std::string() : upTo(text, at + marker.size(), std::string_view(stops, 3));
        }

        int number(const std::string& text) {
            std::optional<std::int64_t> value = blb::parse_int(text);
            return value && *value > 0 && *value < 1000 ? (int) *value : 0;
        }

        std::string joined(const std::vector<std::string>& lines) {
            std::string out;
            for (const std::string& line : lines) {
                out += (out.empty() ? "" : "\n") + line;
            }
            return out;
        }

        std::string clip(std::string_view text, size_t limit) {
            if (text.size() <= limit) {
                return std::string(text);
            }
            size_t cut = limit;
            while (cut > 0 && ((unsigned char) text[cut] & 0xC0) == 0x80) {
                cut--;
            }
            return std::string(text.substr(0, cut)) + "…";
        }

        std::string readHead(const std::filesystem::path& file, size_t limit) {
            if (file.empty()) {
                return {};
            }
            std::ifstream in(file, std::ios::binary);
            std::string out(limit, '\0');
            in.read(out.data(), (std::streamsize) limit);
            out.resize((size_t) std::max<std::streamsize>(0, in.gcount()));
            return out;
        }

        std::string readTail(const std::filesystem::path& file, size_t limit) {
            std::ifstream in(file, std::ios::binary | std::ios::ate);
            if (!in) {
                return {};
            }
            std::streamoff size = in.tellg();
            std::streamoff from = size > (std::streamoff) limit ? size - (std::streamoff) limit : 0;
            in.seekg(from);
            std::string out((size_t) (size - from), '\0');
            in.read(out.data(), (std::streamsize) out.size());
            out.resize((size_t) std::max<std::streamsize>(0, in.gcount()));
            return out;
        }

        void redact(std::string& text, const std::string& token) {
            if (token.size() < 16) {
                return;
            }
            for (size_t at = text.find(token); at != npos; at = text.find(token, at)) {
                text.replace(at, token.size(), "<access token>");
            }
        }

        int64_t floorOf(int64_t since) {
            return since - SLACK_SECONDS * blb::fs::mtime_ticks_per_second();
        }

        std::filesystem::path newest(const std::filesystem::path& folder, std::string_view prefix,
                                     std::string_view suffix, int64_t since) {
            std::filesystem::path best;
            int64_t bestTime = floorOf(since);
            for (const auto& [name, stat] : blb::fs::list_directory(folder)) {
                if (stat.is_directory || !name.starts_with(prefix) || !name.ends_with(suffix) ||
                    stat.mtime < bestTime) {
                    continue;
                }
                best = folder / blb::path_from_utf8(name);
                bestTime = stat.mtime;
            }
            return best;
        }

        bool fresh(const std::filesystem::path& file, int64_t since) {
            blb::fs::file_stat stat = blb::fs::stat(file);
            return stat.exists && !stat.is_directory && stat.mtime >= floorOf(since);
        }

        std::filesystem::path crashReportOf(const Launch& launch, std::string_view output) {
            size_t at = output.rfind(MARKER);
            if (at != npos) {
                std::filesystem::path file = blb::path_from_utf8(upTo(output, at + MARKER.size(), "\r\n"));
                if (!file.empty() && blb::fs::exists(file)) {
                    return file;
                }
            }
            return newest(launch.gameDirectory / "crash-reports", "crash-", ".txt", launch.since);
        }

        std::filesystem::path errorLogOf(const Launch& launch) {
            if (launch.pid > 0) {
                std::filesystem::path file =
                        launch.gameDirectory / ("hs_err_pid" + std::to_string(launch.pid) + ".log");
                if (blb::fs::exists(file)) {
                    return file;
                }
            }
            return newest(launch.gameDirectory, "hs_err_pid", ".log", launch.since);
        }

        std::string frameOf(std::string_view native) {
            size_t at = native.find("# Problematic frame:");
            if (at == npos) {
                return {};
            }
            at = native.find('\n', at);
            if (at == npos) {
                return {};
            }
            std::string line = upTo(native, at + 1, "\r\n");
            size_t open = line.find('[');
            size_t close = line.find(']', open == std::string::npos ? 0 : open);
            if (open == std::string::npos || close == std::string::npos) {
                return {};
            }
            return line.substr(open + 1, close - open - 1);
        }

        bool driver(const std::string& module) {
            std::string name = blb::to_lower(module);
            for (std::string_view prefix :
                 {"atio6axx", "atioglxx", "atig6pxx", "atiumd",   "amdvlk",    "amdxc",     "ig4icd",
                  "ig7icd",   "ig75icd",  "ig8icd",   "ig9icd",   "ig11icd",   "igxelpicd", "igdumd",
                  "igd10",    "nvoglv",   "nvwgf",    "nvd3dum",  "libnvidia", "radeonsi",  "iris_dri",
                  "i965_dri", "libgl",    "opengl32", "vulkan-1", "libvulkan"}) {
                if (name.starts_with(prefix)) {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> bullets(std::string_view text, std::string_view marker) {
            std::vector<std::string> out;
            size_t at = text.find(marker);
            size_t from = at == npos ? npos : text.find('\n', at);
            for (int scanned = 0; from != npos && out.size() < MAX_LINES && scanned < 40; scanned++) {
                size_t end = text.find('\n', from + 1);
                std::string_view line = blb::trim(text.substr(from + 1, end == npos ? npos : end - from - 1));
                from = end;
                if (line.starts_with("- ")) {
                    out.emplace_back(blb::trim(line.substr(2)));
                } else if (!line.empty() && !out.empty()) {
                    break;
                }
            }
            return out;
        }

        std::vector<std::string> forgeDependencies(std::string_view text) {
            std::vector<std::string> out;
            for (size_t at = text.find("Mod ID: '"); at != npos && out.size() < MAX_LINES;
                 at = text.find("Mod ID: '", at + 1)) {
                size_t end = text.find('\n', at);
                std::string_view line = text.substr(at, end == npos ? npos : end - at);
                std::string id = between(line, "Mod ID: '", '\'');
                std::string by = between(line, "Requested by: '", '\'');
                std::string range = between(line, "Expected range: '", '\'');
                std::string actual = between(line, "Actual version: '", '\'');
                if (id.empty() || by.empty()) {
                    continue;
                }
                std::string entry = by + " 需要 " + id + (range.empty() ? "" : " " + range);
                entry += actual.empty() || actual == "[MISSING]" ? "，但没有安装" : "，当前是 " + actual;
                if (std::find(out.begin(), out.end(), entry) == out.end()) {
                    out.push_back(entry);
                }
            }
            return out;
        }

        std::vector<std::string> requirements(std::string_view text) {
            std::vector<std::string> out;
            size_t from = 0;
            while (from < text.size() && out.size() < MAX_LINES) {
                size_t end = text.find('\n', from);
                std::string_view line = blb::trim(text.substr(from, end == npos ? npos : end - from));
                if (line.starts_with("Mod ") && has(line, " requires ")) {
                    std::string entry(line);
                    if (std::find(out.begin(), out.end(), entry) == out.end()) {
                        out.push_back(entry);
                    }
                }
                if (end == npos) {
                    break;
                }
                from = end + 1;
            }
            return out;
        }

        std::vector<Cause> diagnose(const Launch& launch, std::string_view crash, std::string_view native,
                                    std::string_view tail) {
            std::string text;
            text.reserve(crash.size() + native.size() + tail.size() + 2);
            text.append(crash).append("\n").append(native).append("\n").append(tail);
            std::string_view all(text);
            std::vector<Cause> out;
            auto add = [&out](std::string title, std::string detail) {
                if (out.size() >= MAX_CAUSES ||
                    std::any_of(out.begin(), out.end(), [&](const Cause& cause) { return cause.title == title; })) {
                    return;
                }
                out.push_back(Cause{std::move(title), std::move(detail)});
            };
            std::string major = launch.javaMajor > 0 ? std::to_string(launch.javaMajor) : std::string();

            if (has(all, "Manually triggered debug crash")) {
                add("手动触发的调试崩溃", "在游戏里同时按住 F3 和 C 几秒会让游戏故意崩溃，这不是错误。");
                return out;
            }
            std::string option = between(all, "Unrecognized VM option '", '\'');
            if (option.empty()) {
                option = lineAfter(all, "Unrecognized option: ");
            }
            if (!option.empty()) {
                add("Java 不支持某个启动参数", "Java 不认识参数 " + option + "，请检查 JVM 参数，或换用其他 Java。");
            }
            if (any(all,
                    {"Could not reserve enough space for", "Invalid maximum heap size", "Invalid initial heap size",
                     "exceeds the maximum representable size"})) {
                if (launch.javaBits == 32) {
                    add("32 位 Java 无法分配这么多内存",
                        "32 位 Java 最多只能使用约 1 GB 内存，请换用 64 位 Java，或调低最大内存。");
                } else {
                    add("最大内存设置过大", "Java 无法分配设置的内存，请在实例设置里调低最大内存。");
                }
            }
            if (any(all,
                    {"There is insufficient memory for the Java Runtime Environment",
                     "Native memory allocation (malloc) failed", "Native memory allocation (mmap) failed"})) {
                add("电脑可用内存不足", "系统剩余内存不够 Java 使用。请关闭其他程序、调低最大内存，或增大虚拟内存。");
            }
            if (has(all, "java.lang.OutOfMemoryError")) {
                add("游戏内存不足",
                    launch.memoryMb > 0 ? "分配给游戏的 " + std::to_string(launch.memoryMb) +
                                    " MB 内存已经用完，请在实例设置里调高最大内存。"
                                        : "分配给游戏的内存已经用完，请在实例设置里调高最大内存。");
            }
            int required = 0;
            if (has(all, "UnsupportedClassVersionError")) {
                required = number(between(all, "class file version ", '.')) - 44;
                required = std::max(required, number(between(all, "Unsupported major.minor version ", '.')) - 44);
            }
            required = std::max(required, number(between(all, "compatibility level JAVA_", ' ')));
            if (required > 0) {
                add("Java 版本过低",
                    "需要 Java " + std::to_string(required) + " 或更高版本" +
                            (major.empty() ? "" : "，当前是 Java " + major) + "。请在实例设置里换用更新的 Java。");
            }
            if (any(all, {"cannot be cast to class java.net.URLClassLoader", "Unsupported class file major version"})) {
                add("Java 版本过高",
                    "这个版本的游戏或模组加载器不支持当前的 Java" + (major.empty() ? "" : " " + major) +
                            "，请在实例设置里换用较旧的 Java，比如 Java 8。");
            }
            if (any(all,
                    {"Pixel format not accelerated", "GLFW error 65542", "GLFW error 65543",
                     "The driver does not appear to support OpenGL", "Couldn't set pixel format",
                     "No OpenGL context found in the current thread"})) {
                add("显卡驱动不支持 OpenGL", "请到显卡厂商官网更新显卡驱动；笔记本请让 Java 使用独立显卡。");
            }
            std::string frame = frameOf(native);
            std::string module = frame.substr(0, frame.find('+'));
            if (!module.empty() && driver(module)) {
                add("显卡驱动崩溃", "崩溃发生在显卡驱动 " + module + " 中。请更新显卡驱动，或关闭光影后再试。");
            }
            if (any(all,
                    {"Mod resolution encountered an incompatible mod set", "Incompatible mods found!",
                     "Mod resolution failed"})) {
                std::vector<std::string> lines = bullets(all, "A potential solution has been determined");
                if (lines.empty()) {
                    lines = bullets(all, "Unmet dependency listing");
                }
                add("模组依赖不满足或互相冲突",
                    lines.empty() ? "请检查模组的前置模组和适用的游戏版本。" : joined(lines));
            }
            if (has(all, "Missing or unsupported mandatory dependencies")) {
                std::vector<std::string> lines = forgeDependencies(all);
                add("缺少前置模组或版本不对",
                    lines.empty() ? "请安装缺少的前置模组，或换用版本匹配的模组。" : joined(lines));
            } else if (std::string missing = lineAfter(all, "MissingModsException: "); !missing.empty()) {
                add("缺少前置模组或版本不对", missing);
            } else if (has(all, "Currently, ") && has(all, " is not installed")) {
                std::vector<std::string> lines = requirements(all);
                if (!lines.empty()) {
                    add("缺少前置模组或版本不对", joined(lines));
                }
            }
            if (any(all,
                    {"DuplicateModsFoundException", "Found duplicate mods", "Duplicate mods found",
                     "Found a duplicate mod"})) {
                add("安装了重复的模组", "同一个模组装了不止一个文件，请在模组管理里删掉多余的。");
            }
            std::string mixin = between(all, "Mixin apply for mod ", ' ');
            if (mixin.empty() &&
                any(all,
                    {"MixinApplyError", "InvalidInjectionException", "Critical injection failure",
                     "MixinTransformerError"})) {
                std::string config = between(all, "in config [", ']');
                mixin = config.empty() ? std::string("?") : config.substr(0, config.find('.'));
            }
            if (!mixin.empty()) {
                add("模组注入失败",
                    mixin != "?" ? "模组 " + mixin + " 与当前版本的游戏或其他模组不兼容，请更新或删除它。"
                                 : "有模组与当前版本的游戏或其他模组不兼容，请检查最近添加或更新的模组。");
            }
            std::string failed = between(all, "Failed to create mod instance. ModID: ", ',');
            if (failed.empty()) {
                failed = lineAfter(all, "Caught exception from ");
            }
            if (!failed.empty()) {
                add("模组加载出错", "模组 " + failed + " 在加载时出错，请更新或删除它。");
            }
            std::string suspected = lineAfter(crash, "Suspected Mods: ");
            if (suspected.empty()) {
                suspected = lineAfter(crash, "Suspected Mod: ");
            }
            if (!suspected.empty() && !blb::iequals(suspected, "NONE") && !blb::icontains(suspected, "unknown")) {
                add("可能有问题的模组", suspected);
            }
            if (any(all,
                    {"Invalid or corrupt jarfile", "java.util.zip.ZipException", "zip END header not found",
                     "error in opening zip file", "java.util.zip.ZipError"})) {
                add("有文件已经损坏", "有 jar 文件损坏或不完整，请重新下载对应的模组。");
            }
            if (any(all,
                    {"Could not find or load main class", "ClassNotFoundException: net.minecraft.client.main.Main",
                     "ClassNotFoundException: net.fabricmc.loader.impl.launch.knot.KnotClient",
                     "ClassNotFoundException: cpw.mods.modlauncher.Launcher",
                     "ClassNotFoundException: cpw.mods.bootstraplauncher.BootstrapLauncher"})) {
                add("游戏文件不完整", "找不到游戏的启动入口，请重新安装这个实例的游戏或模组加载器。");
            }
            if (any(all,
                    {"java.lang.UnsatisfiedLinkError", "Failed to locate library:", "no lwjgl in java.library.path"})) {
                add("本地库无法加载", "可能是本地库文件损坏，或 Java 的位数和架构与系统不符。");
            }
            if (std::string denied = lineAfter(all, "java.nio.file.AccessDeniedException: "); !denied.empty()) {
                add("文件无法访问", denied + " 被其他程序占用，或没有访问权限。");
            }
            if (any(all, {"Open J9 is not supported", "OpenJ9 is incompatible"})) {
                add("不支持 OpenJ9", "请换用 HotSpot 版本的 Java，比如 Microsoft、Adoptium 或 Zulu 的 Java。");
            }
            if (out.empty() && !native.empty()) {
                add("Java 虚拟机崩溃",
                    module.empty() ? "Java 本身崩溃了，可以尝试更新显卡驱动或换用其他 Java。"
                                   : "崩溃发生在 " + module + " 中，可以尝试更新显卡驱动或换用其他 Java。");
            }
            if (out.empty() && launch.code == -1073741819) {
                add("游戏进程发生访问冲突", "常见原因是显卡驱动、光影或带有原生代码的模组，可以先更新显卡驱动。");
            }
            if (out.empty() && has(all, "Could not create the Java Virtual Machine")) {
                add("Java 虚拟机无法启动", "请检查 Java 和 JVM 参数设置。");
            }
            return out;
        }

        bool exceptional(std::string_view line) {
            for (std::string_view word : {std::string_view("Exception"), std::string_view("Error")}) {
                for (size_t at = line.find(word); at != npos; at = line.find(word, at + word.size())) {
                    size_t end = at + word.size();
                    if (end < line.size() && line[end] != ':') {
                        continue;
                    }
                    size_t start = at;
                    while (start > 0) {
                        char c = line[start - 1];
                        if (!std::isalnum((unsigned char) c) && c != '.' && c != '_' && c != '$') {
                            break;
                        }
                        start--;
                    }
                    if (line.substr(start, at - start).find('.') != npos) {
                        return true;
                    }
                }
            }
            return false;
        }

        std::string summaryOf(std::string_view crash, std::string_view native, std::string_view tail) {
            if (size_t at = crash.find("Description: "); at != npos) {
                for (size_t from = crash.find('\n', at); from != npos;) {
                    size_t end = crash.find('\n', from + 1);
                    std::string_view line = blb::trim(crash.substr(from + 1, end == npos ? npos : end - from - 1));
                    if (!line.empty()) {
                        return clip(line, SUMMARY_LIMIT);
                    }
                    from = end;
                }
            }
            if (!native.empty()) {
                std::string error = lineAfter(native, "#  EXCEPTION_");
                if (!error.empty()) {
                    error = "EXCEPTION_" + error;
                } else if (std::string signal = lineAfter(native, "#  SIG"); !signal.empty()) {
                    error = "SIG" + signal;
                }
                error = error.substr(0, error.find(" at pc="));
                std::string frame = frameOf(native);
                if (!error.empty()) {
                    return clip(frame.empty() ? error : error + " " + frame, SUMMARY_LIMIT);
                }
            }
            size_t end = tail.size();
            for (int seen = 0; end > 0 && seen < SUMMARY_SCAN; seen++) {
                size_t start = tail.rfind('\n', end - 1);
                size_t from = start == npos ? 0 : start + 1;
                std::string_view line = blb::trim(tail.substr(from, end - from));
                if (!line.starts_with("at ") && !line.starts_with("[b-launcher]") && exceptional(line)) {
                    return clip(line, SUMMARY_LIMIT);
                }
                if (start == npos) {
                    break;
                }
                end = start;
            }
            return {};
        }

        std::tm localOf(std::chrono::system_clock::time_point at) {
            std::time_t time = std::chrono::system_clock::to_time_t(at);
            std::tm out{};
#if defined(_WIN32)
            localtime_s(&out, &time);
#else
            localtime_r(&time, &out);
#endif
            return out;
        }

        std::string stamp(std::chrono::system_clock::time_point at, const char* format) {
            std::tm local = localOf(at);
            char text[64];
            size_t size = std::strftime(text, sizeof(text), format, &local);
            return std::string(text, size);
        }

        std::string megabytes(uint64_t bytes) {
            return std::to_string(bytes >> 20) + " MB";
        }

        struct Excerpt {
            std::string title;
            std::string whole;
            std::vector<std::string> lines;
            bool fromEnd = false;
        };

        size_t encodedSize(std::string_view text) {
            size_t size = 0;
            for (unsigned char c : text) {
                bool plain = (c < 0x80 && std::isalnum(c)) || c == '-' || c == '_' || c == '.' || c == '~';
                size += plain ? 1 : 3;
            }
            return size;
        }

        Excerpt excerptOf(const Report& report) {
            Excerpt out;
            auto keep = [&out, &report](std::string_view line) {
                std::string text = clip(blb::trim(line), SUMMARY_LIMIT);
                redact(text, report.launch.token);
                if (!text.empty()) {
                    out.lines.push_back(std::move(text));
                }
            };
            std::string crash = readHead(report.crashReport, REPORT_LIMIT);
            if (!blb::trim(crash).empty()) {
                redact(crash, report.launch.token);
                out.title = "崩溃报告";
                out.whole = std::string(blb::trim(blb::replace_all(crash, "\r\n", "\n")));
                return out;
            }
            std::string native = readHead(report.errorLog, NATIVE_LIMIT);
            for (std::string_view line : blb::split(native, '\n')) {
                if (line.starts_with("---------------")) {
                    break;
                }
                std::string_view body = blb::trim(line);
                while (body.starts_with("#")) {
                    body = blb::trim(body.substr(1));
                }
                keep(body);
            }
            if (!out.lines.empty()) {
                out.title = "JVM 崩溃日志（节选）";
                return out;
            }
            std::string_view tail(report.output);
            size_t end = tail.size();
            for (int seen = 0; end > 0 && seen < SUMMARY_SCAN; seen++) {
                size_t start = tail.rfind('\n', end - 1);
                size_t from = start == npos ? 0 : start + 1;
                std::string_view line = tail.substr(from, end - from);
                if (!blb::trim(line).starts_with("[b-launcher]")) {
                    keep(line);
                }
                if (start == npos) {
                    break;
                }
                end = start;
            }
            out.title = "游戏日志（末尾）";
            out.fromEnd = true;
            return out;
        }

        std::string duration(double seconds) {
            long long total = (long long) seconds;
            if (total < 60) {
                return std::to_string(total) + " 秒";
            }
            if (total < 3600) {
                return std::to_string(total / 60) + " 分 " + std::to_string(total % 60) + " 秒";
            }
            return std::to_string(total / 3600) + " 小时 " + std::to_string(total / 60 % 60) + " 分";
        }

        std::string details(const Report& report) {
            const Launch& launch = report.launch;
            const blb::platform_info& host = blb::current_platform();
            std::string out = std::string(About::NAME) + " " + About::VERSION + "\n";
            auto line = [&out](const std::string& label, const std::string& value) {
                if (!value.empty()) {
                    out += label + "：" + value + "\n";
                }
            };
            line("时间", stamp(report.at, "%Y-%m-%d %H:%M:%S"));
            line("系统",
                 std::string(blb::os_name(host.os)) + " " + host.os_version + " " +
                         std::string(blb::arch_name(host.arch)));
            line("处理器", std::to_string(host.cpu_count) + " 线程");
            line("内存", "共 " + megabytes(host.total_memory) + "，可用 " + megabytes(blb::available_memory()));
            out += "\n";
            line("实例", launch.name);
            line("Minecraft", launch.minecraft);
            std::string loaders;
            for (const std::string& loader : launch.loaders) {
                loaders += (loaders.empty() ? "" : "，") + loader;
            }
            line("加载器", loaders);
            line("游戏目录", blb::path_to_utf8(launch.gameDirectory));
            line("Java", launch.java);
            line("Java 版本",
                 launch.javaVersion.empty() || launch.javaBits == 0
                         ? launch.javaVersion
                         : launch.javaVersion + "（" + std::to_string(launch.javaBits) + " 位）");
            line("最大内存", launch.memoryMb > 0 ? std::to_string(launch.memoryMb) + " MB" : std::string());
            line("退出代码", exitText(launch.code));
            line("运行时长", duration(launch.uptime));
            line("崩溃报告", blb::path_to_utf8(report.crashReport.filename()));
            if (!report.causes.empty()) {
                out += "\n可能的原因：\n";
                for (const Cause& cause : report.causes) {
                    out += "- " + cause.title + "\n";
                    for (std::string_view part : blb::split(cause.detail, '\n')) {
                        out += "  " + std::string(part) + "\n";
                    }
                }
            }
            if (!report.summary.empty()) {
                out += "\n错误信息：\n" + report.summary + "\n";
            }
            std::vector<std::string> mods;
            for (const auto& [name, stat] : blb::fs::list_directory(launch.gameDirectory / "mods")) {
                if (!stat.is_directory) {
                    mods.push_back(name);
                }
            }
            if (!mods.empty()) {
                std::sort(mods.begin(), mods.end());
                out += "\n模组（" + std::to_string(mods.size()) + "）：\n";
                for (const std::string& name : mods) {
                    out += name + "\n";
                }
            }
            return out;
        }

        std::string bundle(const Report& report, const std::filesystem::path& target) {
            std::filesystem::path partial = blb::fs::temp_sibling(target);
            blb::result<blb::zip_writer> made = blb::zip_writer::create(partial);
            if (!made) {
                return made.err().message;
            }
            blb::zip_writer zip = std::move(*made);
            std::string problem;
            auto add = [&](const std::string& name, const std::string& data) {
                if (problem.empty()) {
                    if (blb::result<void> added = zip.add_buffer(name, data); !added) {
                        problem = added.err().message;
                    }
                }
            };
            auto attach = [&](const std::filesystem::path& file) {
                if (file.empty()) {
                    return;
                }
                std::string data = readTail(file, FILE_LIMIT);
                redact(data, report.launch.token);
                if (!data.empty()) {
                    add(blb::path_to_utf8(file.filename()), data);
                }
            };
            add("launcher-info.txt", details(report));
            add("game-output.log", report.output);
            attach(report.crashReport);
            attach(report.errorLog);
            for (const char* name : {"latest.log", "debug.log"}) {
                std::filesystem::path file = report.launch.gameDirectory / "logs" / name;
                if (fresh(file, report.launch.since)) {
                    attach(file);
                }
            }
            if (blb::result<void> closed = zip.close(); !closed && problem.empty()) {
                problem = closed.err().message;
            }
            if (problem.empty()) {
                if (blb::result<void> moved = blb::fs::rename_file(partial, target); !moved) {
                    problem = moved.err().message;
                }
            }
            if (!problem.empty()) {
                (void) blb::fs::remove_file(partial);
            }
            return problem;
        }

        struct Share {
            std::string title;
            std::string content;
        };

        struct Shared {
            std::string title;
            std::string link;
        };

        bool sharing = false;
        std::weak_ptr<const Report> sharedReport;
        Shared shared;

        std::string opening(const Report& report) {
            const Launch& launch = report.launch;
            const blb::platform_info& host = blb::current_platform();
            std::string text = "我的 Minecraft 游戏崩溃了，请根据下面的信息分析原因，并告诉我具体怎么解决。\n\n";
            std::string game = "Minecraft " + launch.minecraft;
            for (const std::string& loader : launch.loaders) {
                game += "，" + loader;
            }
            text += "游戏版本：" + game + "\n";
            if (!launch.javaVersion.empty()) {
                text += "Java：" + launch.javaVersion +
                        (launch.javaBits > 0 ? "（" + std::to_string(launch.javaBits) + " 位）" : std::string()) +
                        (launch.memoryMb > 0 ? "，最大内存 " + std::to_string(launch.memoryMb) + " MB"
                                             : std::string()) +
                        "\n";
            }
            text += "系统：" + std::string(blb::os_name(host.os)) + " " + host.os_version + " " +
                    std::string(blb::arch_name(host.arch)) + "\n";
            text += "退出代码：" + exitText(launch.code) + "\n";
            if (!report.causes.empty()) {
                text += "启动器的初步判断：\n";
                for (const Cause& cause : report.causes) {
                    text += "- " + cause.title + "：" + blb::replace_all(cause.detail, "\n", "；") + "\n";
                }
            }
            return text;
        }

        std::string question(const Report& report, const Shared& link, size_t limit) {
            std::string text = opening(report);
            if (!link.link.empty()) {
                if (!report.summary.empty()) {
                    text += "错误信息：" + report.summary + "\n";
                }
                return text + "\n完整的" + link.title + "在这里，请先打开阅读：" + link.link + "\n";
            }
            Excerpt excerpt = excerptOf(report);
            if (!excerpt.whole.empty()) {
                return text + "\n" + excerpt.title + "：\n```\n" + excerpt.whole + "\n```\n";
            }
            if (!report.summary.empty()) {
                text += "错误信息：" + report.summary + "\n";
            }
            std::string head = "\n" + excerpt.title + "：\n```\n";
            std::string foot = "\n```\n";
            size_t used = encodedSize(text) + encodedSize(head) + encodedSize(foot);
            std::vector<std::string> kept;
            for (const std::string& line : excerpt.lines) {
                size_t cost = encodedSize(line) + 3;
                if (used + cost > limit) {
                    break;
                }
                kept.push_back(line);
                used += cost;
            }
            if (excerpt.fromEnd) {
                std::reverse(kept.begin(), kept.end());
            }
            if (!kept.empty()) {
                text += head + blb::join(kept, "\n") + foot;
            }
            return text;
        }

        std::string fitted(std::string_view text, bool fromEnd) {
            size_t lines = 0;
            if (!fromEnd) {
                size_t end = 0;
                while (end < text.size() && lines < SHARE_LINES) {
                    size_t at = text.find('\n', end);
                    size_t next = at == npos ? text.size() : at + 1;
                    if (next > SHARE_BYTES) {
                        break;
                    }
                    end = next;
                    lines++;
                }
                return std::string(text.substr(0, end));
            }
            size_t start = text.size();
            if (start > 0 && text[start - 1] == '\n') {
                start--;
            }
            size_t keep = text.size();
            while (start > 0 && lines < SHARE_LINES) {
                size_t at = text.rfind('\n', start - 1);
                size_t from = at == npos ? 0 : at + 1;
                if (text.size() - from > SHARE_BYTES) {
                    break;
                }
                keep = from;
                lines++;
                if (at == npos) {
                    break;
                }
                start = at;
            }
            return std::string(text.substr(keep));
        }

        Share shareable(const Report& report) {
            std::string crash = readHead(report.crashReport, SHARE_BYTES);
            if (!blb::trim(crash).empty()) {
                redact(crash, report.launch.token);
                return {"崩溃报告", fitted(crash, false)};
            }
            return {"游戏日志", fitted(report.output, true)};
        }

        std::string upload(const std::string& content, blb::cancellation_token token) {
            nlohmann::json body = {{"content", content}, {"source", About::NAME}};
            blb::net::http_request request;
            request.method = "POST";
            request.url = SHARE_URL;
            request.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
            request.body = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            request.timeout = std::chrono::milliseconds(30000);
            blb::result<blb::net::http_response> response = blb::net::http_send(request, token);
            if (!response || !response->ok()) {
                return {};
            }
            nlohmann::json reply = nlohmann::json::parse(response->body, nullptr, false);
            if (reply.is_discarded() || !reply.is_object() || !blb::json_bool(reply, "success")) {
                return {};
            }
            std::string link = blb::json_text(reply, "url");
            return link.rfind("https://", 0) == 0 ? link : std::string();
        }
    }

    std::shared_ptr<const Report> examine(Launch launch, std::string output) {
        if (launch.code == 0 && !has(output, MARKER)) {
            return nullptr;
        }
        auto report = std::make_shared<Report>();
        report->at = std::chrono::system_clock::now();
        redact(output, launch.token);
        report->crashReport = crashReportOf(launch, output);
        report->errorLog = errorLogOf(launch);
        std::string crash = readHead(report->crashReport, REPORT_LIMIT);
        std::string native = readHead(report->errorLog, NATIVE_LIMIT);
        std::string_view tail(output);
        if (tail.size() > OUTPUT_LIMIT) {
            tail.remove_prefix(tail.size() - OUTPUT_LIMIT);
        }
        report->causes = diagnose(launch, crash, native, tail);
        report->summary = summaryOf(crash, native, tail);
        report->output = std::move(output);
        report->launch = std::move(launch);
        return report;
    }

    std::string exitText(int code) {
        if (code > -256 && code < 65536) {
            return std::to_string(code);
        }
        char text[48];
        std::snprintf(text, sizeof(text), "%d（0x%08X）", code, (unsigned) code);
        return text;
    }

    std::string fileName(const Report& report) {
        std::string name = blb::fs::filter_filename(report.launch.name, '_');
        return (name.empty() ? std::string("minecraft") : name) + "-crash-" + stamp(report.at, "%Y%m%d-%H%M%S") +
                ".zip";
    }

    void ask(std::shared_ptr<const Report> report, const std::string& key) {
        if (report == nullptr || sharing) {
            return;
        }
        if (sharedReport.lock() == report) {
            AskAi::ask(key, question(*report, shared, AskAi::budget(key)));
            return;
        }
        sharing = true;
        tasks.run(
                [report, key](blb::cancellation_token token) -> Continuation {
                    Share share = shareable(*report);
                    Shared result{share.title, share.content.empty() ? std::string() : upload(share.content, token)};
                    return [report, key, result] {
                        sharing = false;
                        if (!result.link.empty()) {
                            sharedReport = report;
                            shared = result;
                        } else {
                            Notices::add(Notices::Kind::Info, "无法上传到 mclo.gs，改为直接发送", 4000);
                        }
                        AskAi::ask(key, question(*report, result, AskAi::budget(key)));
                    };
                },
                [report, key](const std::string&) -> Continuation {
                    return [report, key] {
                        sharing = false;
                        AskAi::ask(key, question(*report, Shared{}, AskAi::budget(key)));
                    };
                });
    }

    bool asking() {
        return sharing;
    }

    void save(std::shared_ptr<const Report> report, const std::filesystem::path& target) {
        if (busy || report == nullptr) {
            return;
        }
        busy = true;
        tasks.run(
                [report, target](blb::cancellation_token) -> Continuation {
                    std::string problem = bundle(*report, target);
                    return [problem, target] {
                        busy = false;
                        if (!problem.empty()) {
                            Notices::add(Notices::Kind::Failed, "无法导出崩溃信息：" + problem, 5000);
                            return;
                        }
                        Notices::add(Notices::Kind::Done, "已导出崩溃信息", 3000);
                        Platform::reveal(target);
                    };
                },
                [](const std::string& error) -> Continuation {
                    return [error] {
                        busy = false;
                        Notices::add(Notices::Kind::Failed, "无法导出崩溃信息：" + error, 5000);
                    };
                });
    }

    bool saving() {
        return busy;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
