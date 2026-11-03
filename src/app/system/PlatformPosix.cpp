#include "app/system/Platform.h"
#include "app/system/PlatformPosix.h"

#include <core/base/fs.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>

extern char** environ;

namespace Platform {

    namespace {
        namespace blb = b_launcher_base;

        std::vector<std::string> environmentList(const Environment& extra) {
            std::map<std::string, std::string> variables;
            for (char** entry = environ; entry != nullptr && *entry != nullptr; entry++) {
                std::string text(*entry);
                size_t equals = text.find('=');
                if (equals == std::string::npos || equals == 0) {
                    continue;
                }
                variables[text.substr(0, equals)] = text.substr(equals + 1);
            }
            for (const auto& [name, value] : extra) {
                if (!name.empty()) {
                    variables[name] = value;
                }
            }
            std::vector<std::string> list;
            for (const auto& [name, value] : variables) {
                list.push_back(name + "=" + value);
            }
            return list;
        }

        std::vector<char*> pointers(std::vector<std::string>& strings) {
            std::vector<char*> out;
            for (std::string& text : strings) {
                out.push_back(text.data());
            }
            out.push_back(nullptr);
            return out;
        }

        pid_t spawn(std::vector<std::string> arguments, std::vector<std::string> variables,
                    const std::filesystem::path& directory, bool detached, int& failure) {
            std::vector<char*> argv = pointers(arguments);
            std::vector<char*> envp = pointers(variables);
            std::string folder;
            std::error_code missing;
            if (!directory.empty() && std::filesystem::is_directory(directory, missing)) {
                folder = directory.string();
            }
            int report[2];
            if (pipe(report) != 0) {
                failure = errno;
                return -1;
            }
            fcntl(report[0], F_SETFD, FD_CLOEXEC);
            fcntl(report[1], F_SETFD, FD_CLOEXEC);
            pid_t pid = fork();
            if (pid < 0) {
                failure = errno;
                close(report[0]);
                close(report[1]);
                return -1;
            }
            if (pid == 0) {
                close(report[0]);
                if (detached) {
                    setsid();
                    pid_t inner = fork();
                    if (inner != 0) {
                        _exit(inner < 0 ? 127 : 0);
                    }
                } else {
                    setpgid(0, 0);
                }
                int error = 0;
                if (!folder.empty() && chdir(folder.c_str()) != 0) {
                    error = errno;
                } else {
                    environ = envp.data();
                    execvp(argv[0], argv.data());
                    error = errno;
                }
                ssize_t written = write(report[1], &error, sizeof(error));
                (void) written;
                _exit(127);
            }
            close(report[1]);
            int error = 0;
            ssize_t got;
            do {
                got = read(report[0], &error, sizeof(error));
            } while (got < 0 && errno == EINTR);
            close(report[0]);
            if (detached) {
                int status = 0;
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
            }
            if (got == (ssize_t) sizeof(error)) {
                if (!detached) {
                    int status = 0;
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                    }
                }
                failure = error;
                return -1;
            }
            return pid;
        }

        std::vector<std::string> shellCommand(const std::string& command) {
            return {"/bin/sh", "-c", command};
        }

        std::filesystem::path storeFile(const std::string& name) {
            if (!blb::fs::safe_filename(name)) {
                return {};
            }
            std::filesystem::path folder = configDirectory();
            return folder.empty() ? std::filesystem::path() : folder / name;
        }
    }

    bool spawnDetached(const std::vector<std::string>& arguments) {
        int failure = 0;
        return spawn(arguments, environmentList({}), {}, true, failure) >= 0;
    }

    std::filesystem::path defaultMinecraft() {
        return blb::default_minecraft_directory();
    }

    std::wstring widen(const std::string& text) {
        std::wstring out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();) {
            unsigned char lead = (unsigned char) text[i];
            int more = lead < 0x80 ? 0 : (lead >> 5) == 6 ? 1 : (lead >> 4) == 14 ? 2 : (lead >> 3) == 30 ? 3 : -1;
            bool valid = more >= 0 && i + (size_t) more < text.size();
            char32_t code = more <= 0 ? lead : lead & (0x3F >> more);
            for (int k = 1; valid && k <= more; k++) {
                unsigned char next = (unsigned char) text[i + k];
                valid = (next & 0xC0) == 0x80;
                code = (code << 6) | (next & 0x3F);
            }
            if (!valid) {
                out.push_back(0xFFFD);
                i++;
                continue;
            }
            out.push_back((wchar_t) code);
            i += (size_t) more + 1;
        }
        return out;
    }

    std::string narrow(const std::wstring& text) {
        std::string out;
        out.reserve(text.size());
        for (wchar_t wide : text) {
            char32_t code = (char32_t) wide;
            if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
                code = 0xFFFD;
            }
            if (code < 0x80) {
                out.push_back((char) code);
            } else if (code < 0x800) {
                out.push_back((char) (0xC0 | (code >> 6)));
                out.push_back((char) (0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back((char) (0xE0 | (code >> 12)));
                out.push_back((char) (0x80 | ((code >> 6) & 0x3F)));
                out.push_back((char) (0x80 | (code & 0x3F)));
            } else {
                out.push_back((char) (0xF0 | (code >> 18)));
                out.push_back((char) (0x80 | ((code >> 12) & 0x3F)));
                out.push_back((char) (0x80 | ((code >> 6) & 0x3F)));
                out.push_back((char) (0x80 | (code & 0x3F)));
            }
        }
        return out;
    }

    bool insideLauncher(const std::filesystem::path& path) {
        std::string root = exeDirectory().lexically_normal().string();
        std::string file = path.lexically_normal().string();
        if (!root.empty() && root.back() != '/') {
            root.push_back('/');
        }
        return !root.empty() && file.size() > root.size() && file.compare(0, root.size(), root) == 0;
    }

    std::vector<unsigned char> iconPixels(const std::filesystem::path&, int) {
        return {};
    }

    std::string createShortcut(const Shortcut&) {
        return "这个系统暂不支持创建快捷方式";
    }

    std::optional<std::filesystem::path> pickShortcut(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> pickIcon(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::string launchArgument() {
        std::vector<std::string> args = commandLine();
        std::string key;
        for (size_t i = 1; i + 1 < args.size(); i++) {
            if (args[i] == "--launch") {
                key = args[i + 1];
            }
        }
        return key;
    }

    bool headlessArgument() {
        std::vector<std::string> args = commandLine();
        return std::find(args.begin() + (args.empty() ? 0 : 1), args.end(), "--headless") != args.end();
    }

    std::string uiArgument() {
        std::string style;
        for (const std::string& arg : commandLine()) {
            if (arg.starts_with("--ui=")) {
                style = arg.substr(5);
            }
        }
        return style;
    }

    bool restart() {
        static bool started = false;
        if (started) {
            return false;
        }
        std::filesystem::path exe = executablePath();
        if (exe.empty() || !spawnDetached({exe.string(), "--wait-for=" + std::to_string(getpid())})) {
            return false;
        }
        started = true;
        closeWindow();
        return true;
    }

    void waitForPrevious() {
        pid_t pid = 0;
        for (const std::string& arg : commandLine()) {
            if (arg.starts_with("--wait-for=")) {
                pid = (pid_t) std::strtol(arg.c_str() + 11, nullptr, 10);
            }
        }
        if (pid <= 0) {
            return;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (kill(pid, 0) == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    bool forwardLaunch(const std::string&, bool) {
        return false;
    }

    void claimTaskbar() {}

    void claimLaunches() {}

    bool setClipboardImage(const Pixels&) {
        return false;
    }

    std::optional<std::filesystem::path> pickFolder(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> pickJava(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> pickFiles(const std::filesystem::path&, const wchar_t*, const wchar_t*,
                                                 const wchar_t*) {
        return {};
    }

    std::optional<std::filesystem::path> pickSaveFile(const std::filesystem::path&, const wchar_t*, void*) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> pickSavePack(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> pickImage(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> pickLogo(const std::filesystem::path&) {
        return std::nullopt;
    }

    std::optional<std::string> loadValue(const std::string& name) {
        std::filesystem::path file = storeFile(name);
        if (file.empty() || !blb::fs::exists(file)) {
            return std::nullopt;
        }
        blb::result<std::string> data = blb::fs::read_file(file);
        if (!data) {
            return std::nullopt;
        }
        return std::move(*data);
    }

    bool storeValue(const std::string& name, const std::string& data, bool) {
        std::filesystem::path file = storeFile(name);
        if (file.empty() || !blb::fs::create_directories(file.parent_path())) {
            return false;
        }
        std::filesystem::path temp = blb::fs::temp_sibling(file);
        int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) {
            return false;
        }
        size_t done = 0;
        while (done < data.size()) {
            ssize_t wrote = write(fd, data.data() + done, data.size() - done);
            if (wrote < 0 && errno == EINTR) {
                continue;
            }
            if (wrote <= 0) {
                break;
            }
            done += (size_t) wrote;
        }
        bool ok = done == data.size() && fsync(fd) == 0;
        ok = close(fd) == 0 && ok;
        if (!ok || rename(temp.c_str(), file.c_str()) != 0) {
            unlink(temp.c_str());
            return false;
        }
        return true;
    }

    std::string display(const std::filesystem::path& path) {
        return blb::path_to_utf8(path);
    }

    std::string runCommand(const std::string& command, const Environment& environment,
                           const std::filesystem::path& directory, const std::function<bool()>& cancelled) {
        if (cancelled && cancelled()) return "已取消";
        int failure = 0;
        pid_t pid = spawn(shellCommand(command), environmentList(environment), directory, false, failure);
        if (pid < 0) {
            return "无法运行命令（错误 " + std::to_string(failure) + "）";
        }
        std::string error;
        int status = 0;
        for (;;) {
            pid_t done = waitpid(pid, &status, WNOHANG);
            if (done == pid) break;
            if (done < 0 && errno != EINTR) {
                error = "无法等待命令退出（错误 " + std::to_string(errno) + "）";
                kill(-pid, SIGKILL);
                break;
            }
            if (cancelled && cancelled()) {
                if (kill(-pid, SIGKILL) == 0 || errno == ESRCH) {
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                    }
                    error = "已取消";
                } else {
                    error = "无法取消命令（错误 " + std::to_string(errno) + "）";
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (error.empty()) {
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            if (code != 0) {
                error = "命令执行失败（退出代码 " + std::to_string(code) + "）";
            }
        }
        return error;
    }

    std::string startCommand(const std::string& command, const Environment& environment,
                             const std::filesystem::path& directory) {
        int failure = 0;
        if (spawn(shellCommand(command), environmentList(environment), directory, true, failure) < 0) {
            return "无法运行命令（错误 " + std::to_string(failure) + "）";
        }
        return {};
    }

    bool setPriority(int pid, int level) {
        static const int NICE[] = {19, 10, 0, -5, -10};
        if (pid <= 0 || level < 0 || level > 4) {
            return false;
        }
        return setpriority(PRIO_PROCESS, (id_t) pid, NICE[level]) == 0;
    }

    void minimizeWindow() {}

    void hideWindow() {}

    void showWindow() {}

    void closeWindow() {}
}
