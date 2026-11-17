#include "app/multiplayer/TerracottaGuest.h"
#include "app/accounts/Accounts.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/system/Platform.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/json.hpp>
#include <core/base/log.hpp>
#include <core/base/process.hpp>
#include <core/base/strings.hpp>
#if defined(_WIN32)
#include <core/base/win_util.hpp>
#endif
#include <core/network/download_client.hpp>
#include <core/network/http_client.hpp>
#include <core/network/server_ping.hpp>

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#endif

namespace TerracottaGuest {

    namespace blb = b_launcher_base;

    namespace {
        using Clock = std::chrono::steady_clock;
        using Json = nlohmann::json;
        constexpr blb::log::logger LOG("terracotta-guest");
        constexpr auto VERSION = "0.4.2";
        constexpr auto WORLD_CHECK_INTERVAL = std::chrono::seconds(5);
        constexpr auto WORLD_CHECK_TIMEOUT = std::chrono::seconds(3);
        constexpr int WORLD_CHECK_FAILURES = 3;

#if defined(_M_ARM64) || defined(__aarch64__)
        constexpr auto CLASSIFIER = "windows-arm64";
        constexpr auto PACKAGE_HASH = "fc1077247014ac0c712469498bde2ef7f6d881d5fcb7bdd5e11ebe20218fed365be19afdb8d453a7"
                                      "9d77b729f866058522b910741767f4df947faa891434b463";
        constexpr auto EXE_HASH = "30a15c5c53e5817c5a3634532172559327474741d3b2c7ef4e8a30acc6f59cdcf3570bf5f583e3cbe9e2"
                                  "abc8253e977c1abda1e9f36c88c4e99240da257347d0";
        constexpr auto DLL_HASH = "5cb5ce114614101d260f4754c09e8a0dd57e4da885ebb96b91e274326f3e1dd95ed0ade9f542f1922fad"
                                  "0ed025e88a1f368e791e1d01fae69718f0ec3c7b98c8";
#else
        constexpr auto CLASSIFIER = "windows-x86_64";
        constexpr auto PACKAGE_HASH = "6a98f524d4f00373696517306af8aa50d01d55ce4eadb27e9e4bc2f882707a0b5f20d5d4c33371d1"
                                      "459dcf5bf144ffed9beb414202d9ccf32b11dbbfcf19d650";
        constexpr auto EXE_HASH = "6e98d1f2380ed22fb5a2dd4aafce6c773e9cf69100c8bb8e49e7d6983756bdb9a31f80e06bcfbe5a2742"
                                  "144fe806d3d687dec54d8f09d87c659341f99dd9fd80";
        constexpr auto DLL_HASH = "3d4b24061f72c0e957c7b04a0c4098c94c8f1afb4a7e159850b9939c7210d73398be6f27b5ab85073b4e"
                                  "8c999816e7804fef0f6115c39cd061f4aaeb4dcda8cf";
#endif

        std::mutex mutex;
        State shown;
        State pending;
        unsigned long long revision = 0;
        unsigned long long serial = 0;
        unsigned long long seen = 0;
        std::thread worker;
        std::atomic<bool> finished{true};
        blb::cancellation_source cancellation;

        void publish(unsigned long long expected, const State& value) {
            std::lock_guard guard(mutex);
            if (expected == revision) {
                pending = value;
                ++serial;
            }
        }

        void require(bool condition, const std::string& message) {
            if (!condition) throw std::runtime_error(message);
        }

        bool matches(const std::filesystem::path& file, const char* hash) {
            auto digest = blb::digest_file(file);
            return digest && blb::to_hex(digest->sha512) == hash;
        }

        blb::result<blb::net::http_response> localRequest(int port, const std::string& path,
                                                          blb::cancellation_token token = {},
                                                          std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
            blb::net::http_request request;
            request.url = "http://127.0.0.1:" + std::to_string(port) + path;
            request.timeout = timeout;
            request.follow_redirects = false;
            request.bypass_proxy = true;
            request.max_response_size = 1 << 20;
            return blb::net::http_send(request, token);
        }

        Json document(const blb::net::http_response& response) {
            require(response.ok(), "联机组件返回 HTTP " + std::to_string(response.status));
            auto doc = Json::parse(response.body, nullptr, false);
            require(doc.is_object(), "联机组件返回了无法识别的数据");
            return doc;
        }

        std::string inflateGzip(const std::string& packed) {
            z_stream stream{};
            require(inflateInit2(&stream, 15 + 16) == Z_OK, "无法解压联机组件");
            stream.next_in = (Bytef*) packed.data();
            stream.avail_in = (uInt) packed.size();
            std::string out;
            std::vector<char> buffer(1 << 16);
            int status = Z_OK;
            while (status == Z_OK) {
                stream.next_out = (Bytef*) buffer.data();
                stream.avail_out = (uInt) buffer.size();
                status = inflate(&stream, Z_NO_FLUSH);
                out.append(buffer.data(), buffer.size() - stream.avail_out);
            }
            inflateEnd(&stream);
            require(status == Z_STREAM_END, "联机组件压缩包已损坏");
            return out;
        }

        std::string_view tarField(std::string_view header, size_t at, size_t length) {
            std::string_view text = header.substr(at, length);
            return text.substr(0, text.find('\0'));
        }

        void unpack(const std::filesystem::path& archive, const std::filesystem::path& directory,
                    const std::vector<std::string>& names) {
            auto packed = blb::fs::read_file(archive);
            require((bool) packed, packed ? "" : packed.err().message);
            std::string tar = inflateGzip(*packed);
            size_t found = 0;
            size_t at = 0;
            while (at + 512 <= tar.size() && tar[at] != '\0') {
                std::string_view header(tar.data() + at, 512);
                std::string_view name = tarField(header, 0, 100);
                std::string_view digits = blb::trim(tarField(header, 124, 12));
                unsigned long long size = 0;
                if (!digits.empty()) {
                    auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), size, 8);
                    require(parsed.ec == std::errc{}, "联机组件压缩包已损坏");
                }
                at += 512;
                require(size <= tar.size() - at, "联机组件压缩包已损坏");
                if (name.starts_with("./")) {
                    name.remove_prefix(2);
                }
                char type = header[156];
                if ((type == '0' || type == '\0') && std::find(names.begin(), names.end(), name) != names.end()) {
                    auto written = blb::fs::write_file_atomic(directory / blb::path_from_utf8(std::string(name)),
                                                              std::string_view(tar.data() + at, (size_t) size));
                    require((bool) written, written ? "" : written.err().message);
                    found++;
                }
                at += (size_t) ((size + 511) / 512 * 512);
            }
            require(found == names.size(), "联机组件压缩包缺少文件");
        }

        std::filesystem::path prepare(const std::filesystem::path& root, blb::cancellation_token token) {
            auto directory = root / VERSION;
            auto base = std::string("terracotta-") + VERSION + "-" + CLASSIFIER;
            auto executable = directory / (base + ".exe");
            auto dll = directory / "VCRUNTIME140.DLL";
            if (matches(executable, EXE_HASH) && matches(dll, DLL_HASH)) return executable;
            auto made = blb::fs::create_directories(directory);
            require((bool) made, made ? "" : made.err().message);
            auto archive = directory / (base + "-pkg.tar.gz");
            if (!matches(archive, PACKAGE_HASH)) {
                blb::net::download_task task;
                task.target = archive;
                auto suffix = std::string("/releases/download/v") + VERSION + "/" + base + "-pkg.tar.gz";
                task.urls = {"https://github.com/burningtnt/Terracotta" + suffix,
                             "https://gitee.com/burningtnt/Terracotta" + suffix,
                             "https://cnb.cool/HMCL-Terracotta/Terracotta/-" + suffix};
                blb::net::download_options options;
                options.concurrency = 1;
                options.retries = 1;
                blb::net::download_client downloader(options);
                auto report = downloader.run(std::span<const blb::net::download_task>(&task, 1), nullptr, token);
                require(!token.is_cancelled(), "已取消");
                require(report.ok(),
                        report.failures.empty() ? "无法下载联机组件" : report.failures.front().reason.message);
                if (!matches(archive, PACKAGE_HASH)) {
                    (void) blb::fs::remove_file(archive);
                    throw std::runtime_error("联机组件压缩包的 SHA-512 校验失败，请重试");
                }
            }
            require(!token.is_cancelled(), "已取消");
            unpack(archive, directory, {base + ".exe", "VCRUNTIME140.DLL"});
            require(matches(executable, EXE_HASH) && matches(dll, DLL_HASH), "联机组件文件的 SHA-512 校验失败");
            return executable;
        }

#if defined(_WIN32)
        std::wstring quoteWindowsArg(std::wstring_view arg) {
            if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
                return std::wstring(arg);
            }
            std::wstring out;
            out.push_back(L'"');
            for (auto it = arg.begin();; ++it) {
                size_t backslashes = 0;
                while (it != arg.end() && *it == L'\\') {
                    ++it;
                    ++backslashes;
                }
                if (it == arg.end()) {
                    out.append(backslashes * 2, L'\\');
                    break;
                }
                if (*it == L'"') {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(L'"');
                } else {
                    out.append(backslashes, L'\\');
                    out.push_back(*it);
                }
            }
            out.push_back(L'"');
            return out;
        }

        std::wstring buildIsolatedTempEnvironment(const std::filesystem::path& temporary) {
            auto less = [](const std::wstring& a, const std::wstring& b) {
                return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
            };
            std::map<std::wstring, std::wstring, decltype(less)> merged(less);
            std::unique_ptr<wchar_t, decltype(&FreeEnvironmentStringsW)> parent(GetEnvironmentStringsW(),
                                                                                &FreeEnvironmentStringsW);
            require((bool) parent, "无法读取子进程环境变量");
            for (const wchar_t* entry = parent.get(); *entry; entry += std::wcslen(entry) + 1) {
                std::wstring_view text(entry);
                auto equals = text.find(L'=', text.front() == L'=' ? 1 : 0);
                if (equals != std::wstring_view::npos) {
                    merged[std::wstring(text.substr(0, equals))] = std::wstring(text.substr(equals + 1));
                }
            }
            auto tmp = temporary.native();
            merged.erase(L"TEMP");
            merged.erase(L"TMP");
            merged.emplace(L"TEMP", tmp);
            merged.emplace(L"TMP", tmp);
            std::wstring block;
            for (const auto& [key, val] : merged) {
                block.append(key);
                block.push_back(L'=');
                block.append(val);
                block.push_back(L'\0');
            }
            if (block.empty()) block.push_back(L'\0');
            block.push_back(L'\0');
            return block;
        }
#endif

        struct Session {
            int port = 0;
            bool verified = false;
            std::filesystem::path transfer;
#if defined(_WIN32)
            HANDLE bootstrapHandle = nullptr;
            DWORD bootstrapPid = 0;
            HANDLE daemon = nullptr;
            HANDLE owner = INVALID_HANDLE_VALUE;

            void claim(const std::filesystem::path& root) {
                auto made = blb::fs::create_directories(root);
                require((bool) made, made ? "" : made.err().message);
                owner = CreateFileW((root / "session.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                require(owner != INVALID_HANDLE_VALUE, "另一个启动器正在使用 HMCL 联机，请先退出那个房间");
            }

            bool isOurImage(HANDLE handle, const std::filesystem::path& expectedReal) {
                wchar_t image[32768];
                DWORD length = (DWORD) std::size(image);
                if (!QueryFullProcessImageNameW(handle, 0, image, &length)) return false;
                auto actual = blb::fs::real_path(std::filesystem::path(std::wstring(image, length)));
                return blb::fs::path_key(actual) == blb::fs::path_key(expectedReal);
            }

            void rememberStartingDaemon(const std::filesystem::path& expectedReal) {
                if (daemon || bootstrapPid == 0) return;
                HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                if (snapshot == INVALID_HANDLE_VALUE) return;
                PROCESSENTRY32W entry{};
                entry.dwSize = sizeof(entry);
                if (Process32FirstW(snapshot, &entry)) {
                    do {
                        if (entry.th32ParentProcessID != bootstrapPid) continue;
                        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                                    FALSE, entry.th32ProcessID);
                        if (!handle) continue;
                        if (isOurImage(handle, expectedReal)) {
                            daemon = handle;
                            break;
                        }
                        CloseHandle(handle);
                    } while (Process32NextW(snapshot, &entry));
                }
                CloseHandle(snapshot);
            }

            template <typename Table>
            bool findListenerOwner(ULONG family, const std::filesystem::path& expectedReal, bool& foreignOwner) {
                DWORD size = 0;
                std::vector<unsigned char> bytes;
                for (int attempt = 0; attempt < 4; ++attempt) {
                    DWORD status = GetExtendedTcpTable(bytes.empty() ? nullptr : bytes.data(), &size, FALSE, family,
                                                       TCP_TABLE_OWNER_PID_LISTENER, 0);
                    if (status == NO_ERROR && !bytes.empty()) {
                        auto table = reinterpret_cast<Table*>(bytes.data());
                        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                            auto& row = table->table[i];
                            if (ntohs(static_cast<u_short>(row.dwLocalPort)) != port) continue;
                            if (row.dwOwningPid == 0) continue;
                            auto handle =
                                    OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                                FALSE, row.dwOwningPid);
                            if (!handle) {
                                foreignOwner = true;
                                continue;
                            }
                            if (isOurImage(handle, expectedReal)) {
                                if (daemon) CloseHandle(daemon);
                                daemon = handle;
                                return true;
                            }
                            CloseHandle(handle);
                            foreignOwner = true;
                        }
                        return false;
                    }
                    if (status != ERROR_INSUFFICIENT_BUFFER || size == 0) return false;
                    bytes.resize(size + 512);
                    size = static_cast<DWORD>(bytes.size());
                }
                return false;
            }

            bool rememberDaemon(const std::filesystem::path& expectedReal, bool& foreignOwner) {
                foreignOwner = false;
                return findListenerOwner<MIB_TCPTABLE_OWNER_PID>(AF_INET, expectedReal, foreignOwner) ||
                        findListenerOwner<MIB_TCP6TABLE_OWNER_PID>(AF_INET6, expectedReal, foreignOwner);
            }
#else
            blb::process bootstrap;
#endif

            ~Session() {
                bool running = verified;
#if defined(_WIN32)
                running = running && daemon && WaitForSingleObject(daemon, 0) == WAIT_TIMEOUT;
#endif
                if (running) {
                    (void) localRequest(port, "/state/ide", {}, std::chrono::seconds(3));
                    (void) localRequest(port, "/panic?peaceful=true", {}, std::chrono::milliseconds(1500));
                }
#if defined(_WIN32)
                if (daemon) {
                    if (WaitForSingleObject(daemon, 1500) == WAIT_TIMEOUT) {
                        LOG.warn("Terracotta did not stop after peaceful exit; ending our owned daemon");
                        TerminateProcess(daemon, 1);
                        WaitForSingleObject(daemon, 1000);
                    }
                    CloseHandle(daemon);
                }
                if (bootstrapHandle) {
                    if (WaitForSingleObject(bootstrapHandle, 0) == WAIT_TIMEOUT) {
                        TerminateProcess(bootstrapHandle, 1);
                        WaitForSingleObject(bootstrapHandle, 1000);
                    }
                    CloseHandle(bootstrapHandle);
                }
#else
                if (bootstrap.valid() && bootstrap.running()) {
                    (void) bootstrap.kill();
                    (void) bootstrap.wait(std::chrono::seconds(1));
                }
#endif
                if (!transfer.empty()) {
                    (void) blb::fs::remove_file(transfer);
                    (void) blb::fs::remove_file(blb::path_from_utf8(blb::path_to_utf8(transfer) + ".tmp"));
                }
#if defined(_WIN32)
                if (owner != INVALID_HANDLE_VALUE) CloseHandle(owner);
#endif
            }
        };

        void bootstrap(Session& session, const std::filesystem::path& root, const std::filesystem::path& executable,
                       blb::cancellation_token token) {
            auto temporary = std::filesystem::absolute(root / "runtime");
            auto made = blb::fs::create_directories(temporary);
            require((bool) made, made ? "" : made.err().message);
            session.transfer = temporary / ("http-" + blb::random_bytes_hex(8) + ".json");
            require(!token.is_cancelled(), "已取消");
#if defined(_WIN32)
            auto expectedReal = blb::fs::real_path(executable);
            auto envBlock = buildIsolatedTempEnvironment(temporary);
            auto commandLine =
                    quoteWindowsArg(executable.native()) + L" --hmcl " + quoteWindowsArg(session.transfer.native());
            auto workingDir = executable.parent_path().native();
            SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
            HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            SIZE_T attrSize = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
            std::vector<unsigned char> attrStorage(attrSize);
            STARTUPINFOEXW si{};
            si.StartupInfo.cb = sizeof(si);
            DWORD creationFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
            BOOL inheritHandles = FALSE;
            LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
            if (nul != INVALID_HANDLE_VALUE && attrSize > 0) {
                auto candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrStorage.data());
                if (InitializeProcThreadAttributeList(candidate, 1, 0, &attrSize)) {
                    attrs = candidate;
                    if (UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &nul, sizeof(nul),
                                                  nullptr, nullptr)) {
                        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                        si.StartupInfo.hStdInput = nul;
                        si.StartupInfo.hStdOutput = nul;
                        si.StartupInfo.hStdError = nul;
                        si.lpAttributeList = attrs;
                        creationFlags |= EXTENDED_STARTUPINFO_PRESENT;
                        inheritHandles = TRUE;
                    }
                }
            }
            PROCESS_INFORMATION pi{};
            BOOL started = CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, inheritHandles,
                                          creationFlags, envBlock.data(), workingDir.c_str(), &si.StartupInfo, &pi);
            DWORD startError = started ? ERROR_SUCCESS : GetLastError();
            if (attrs) DeleteProcThreadAttributeList(attrs);
            if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
            require(started != FALSE, "无法启动独立联机组件：" + blb::detail::last_error_message(startError));
            CloseHandle(pi.hThread);
            session.bootstrapHandle = pi.hProcess;
            session.bootstrapPid = pi.dwProcessId;
#else
            blb::process_options options;
            options.args = {blb::path_to_utf8(executable), "--hmcl", blb::path_to_utf8(session.transfer)};
            options.working_directory = executable.parent_path();
            options.environment = {{"TMP", blb::path_to_utf8(temporary)}, {"TEMP", blb::path_to_utf8(temporary)}};
            options.capture_output = false;
            auto child = blb::process::start(options);
            require((bool) child, child ? "" : child.err().message);
            session.bootstrap = std::move(*child);
#endif
            auto deadline = Clock::now() + std::chrono::seconds(20);
            bool portConfirmed = false;
            bool verifiedOwner = false;
            bool cancelled = false;
            std::optional<Clock::time_point> mismatchSince;
            while (Clock::now() < deadline) {
                if (!cancelled && token.is_cancelled()) {
                    cancelled = true;
                    auto grace = Clock::now() + std::chrono::seconds(2);
                    if (grace < deadline) {
                        deadline = grace;
                    }
                }
#if defined(_WIN32)
                session.rememberStartingDaemon(expectedReal);
                if (cancelled && session.daemon) {
                    break;
                }
#endif
                if (!portConfirmed) {
                    auto contents = blb::fs::read_file(session.transfer);
                    if (contents) {
                        auto doc = Json::parse(*contents, nullptr, false);
                        if (doc.is_object() && doc.contains("port") && doc["port"].is_number_integer()) {
                            int port = doc["port"].get<int>();
                            if (port > 0 && port <= 65535) {
                                session.port = port;
                                portConfirmed = true;
                            }
                        }
                    }
                }
                if (portConfirmed) {
#if defined(_WIN32)
                    if (!verifiedOwner) {
                        bool foreignOwner = false;
                        if (session.rememberDaemon(expectedReal, foreignOwner)) {
                            verifiedOwner = true;
                        } else if (foreignOwner) {
                            if (!mismatchSince)
                                mismatchSince = Clock::now();
                            else if (Clock::now() - *mismatchSince >= std::chrono::milliseconds(500)) {
                                throw std::runtime_error("无法确认独立联机进程的身份");
                            }
                        } else {
                            mismatchSince.reset();
                        }
                    }
                    if (verifiedOwner) {
#else
                    {
#endif
                        auto response = localRequest(session.port, "/meta");
                        if (response) {
                            auto meta = document(*response);
                            require(blb::json_text(meta, "version") == VERSION &&
                                            blb::json_text(meta, "target_os") == "windows",
                                    "联机组件版本或平台与预期不符");
                            session.verified = true;
                            LOG.info("Terracotta {} ready at localhost:{}; EasyTier {}", VERSION, session.port,
                                     blb::json_text(meta, "easytier_version"));
                            require(!token.is_cancelled(), "已取消");
                            return;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            require(!cancelled, "已取消");
            throw std::runtime_error(portConfirmed && !verifiedOwner ? "无法确认独立联机进程的身份"
                                                                     : "联机组件启动超时，未能取得本地控制端口");
        }

        std::string nodeQuery(blb::cancellation_token token) {
            blb::net::http_request request;
            request.url = "https://terracotta.glavo.site/nodes";
            request.timeout = std::chrono::seconds(5);
            request.max_response_size = 1 << 20;
            auto response = blb::net::http_send(request, token);
            if (!response || !response->ok()) return {};
            auto nodes = Json::parse(response->body, nullptr, false);
            std::string query;
            if (nodes.is_array()) {
                for (const auto& node : nodes) {
                    if (!node.is_object()) continue;
                    auto url = blb::json_text(node, "url");
                    if (!url.empty()) query += "&public_nodes=" + blb::url_encode(url);
                }
            }
            return query;
        }

        std::string exceptionText(int type) {
            switch (type) {
                case 0: return "无法连接房主，请确认房间码正确且房主仍在联机";
                case 1: return "房主已关闭房间或拒绝了连接";
                case 2: return "本机 EasyTier 联机组件意外退出";
                case 3: return "房主的 EasyTier 联机组件意外退出";
                case 4: return "房主的 Minecraft 世界已关闭";
                case 5: return "房主的联机握手响应无效";
                default: return "联机组件报告了未知错误（" + std::to_string(type) + "）";
            }
        }

        bool validAddress(const std::string& address) {
            if (address == "127.0.0.1") return true;
            constexpr std::string_view prefix = "127.0.0.1:";
            if (!address.starts_with(prefix)) return false;
            int port = 0;
            const char* end = address.data() + address.size();
            auto parsed = std::from_chars(address.data() + prefix.size(), end, port);
            return parsed.ec == std::errc{} && parsed.ptr == end && port > 0 && port <= 65535;
        }

        void run(unsigned long long expected, State value, const std::filesystem::path& root, const std::string& player,
                 blb::cancellation_token token) {
            try {
                {
#if !defined(_WIN32)
                    throw std::runtime_error("此版本的 HMCL 联机兼容目前支持 Windows");
#endif
                    Session session;
#if defined(_WIN32)
                    session.claim(root);
#endif
                    auto executable = prepare(root, token);
                    bootstrap(session, root, executable, token);
                    auto waiting = localRequest(session.port, "/state", token);
                    require((bool) waiting, waiting ? "" : waiting.err().message);
                    auto initial = document(*waiting);
                    require(blb::json_text(initial, "state") == "waiting",
                            "独立联机组件尚未退出上一个房间，请退出后重试");
                    auto nodes = nodeQuery(token);
                    auto entering = localRequest(session.port,
                                                 "/state/guesting?room=" + blb::url_encode(value.code) +
                                                         "&player=" + blb::url_encode(player) + nodes,
                                                 token);
                    require((bool) entering, entering ? "" : entering.err().message);
                    require(entering->status != 400, "房间码无效，或联机组件当前无法加入房间");
                    require(entering->ok(), "加入房间请求失败（HTTP " + std::to_string(entering->status) + "）");
                    value.stage = Stage::Connecting;
                    value.detail = "正在连接房主";
                    publish(expected, value);
                    long long lastIndex = -1;
                    int failures = 0;
                    int worldFailures = 0;
                    bool everReady = false;
                    bool worldReachable = false;
                    std::string forwardAddress;
                    auto nextWorldCheck = Clock::now();
                    auto deadline = Clock::now() + std::chrono::seconds(90);
                    while (!token.is_cancelled()) {
                        auto response = localRequest(session.port, "/state", token);
                        if (!response || !response->ok()) {
                            if (token.is_cancelled()) break;
                            require(++failures < 3, "与本地联机组件的连接已中断");
                        } else {
                            failures = 0;
                            auto doc = document(*response);
                            require(doc.contains("index") && doc["index"].is_number_integer(),
                                    "联机组件状态缺少版本编号");
                            auto index = doc["index"].get<long long>();
                            if (index > lastIndex) {
                                lastIndex = index;
                                auto name = blb::json_text(doc, "state");
                                if (name == "guest-ok") {
                                    auto address = blb::json_text(doc, "url");
                                    require(validAddress(address), "联机组件没有提供有效的本地游戏地址");
                                    if (forwardAddress != address) {
                                        forwardAddress = std::move(address);
                                        worldReachable = false;
                                        worldFailures = 0;
                                        nextWorldCheck = Clock::now();
                                    }
                                    value.stage = worldReachable ? Stage::Ready : Stage::Connecting;
                                    value.address = worldReachable ? forwardAddress : std::string();
                                    value.detail = worldReachable ? "联机通道已就绪，请在 Minecraft 中加入此地址"
                                            : everReady           ? "正在重新检查房主游戏连接"
                                                                  : "正在验证房主游戏连接";
                                    value.members.clear();
                                    if (auto profiles = doc.find("profiles");
                                        profiles != doc.end() && profiles->is_array()) {
                                        for (const auto& profile : *profiles) {
                                            if (profile.is_object())
                                                value.members.push_back({blb::json_text(profile, "name"),
                                                                         blb::json_text(profile, "kind")});
                                        }
                                    }
                                } else if (name == "guest-connecting" || name == "guest-starting") {
                                    value.stage = Stage::Connecting;
                                    value.address.clear();
                                    forwardAddress.clear();
                                    worldReachable = false;
                                    worldFailures = 0;
                                    value.detail = name == "guest-starting" ? "正在建立联机通道" : "正在连接房主";
                                } else if (name == "exception") {
                                    int type = doc.contains("type") && doc["type"].is_number_integer()
                                            ? doc["type"].get<int>()
                                            : -1;
                                    throw std::runtime_error(exceptionText(type));
                                } else {
                                    throw std::runtime_error(name == "waiting" ? "已离开联机房间"
                                                                               : "联机组件返回了非预期状态：" + name);
                                }
                                publish(expected, value);
                            }
                        }
                        if (!forwardAddress.empty() && Clock::now() >= nextWorldCheck) {
                            auto status = blb::net::ping_server(forwardAddress, token, WORLD_CHECK_TIMEOUT);
                            if (token.is_cancelled()) break;
                            nextWorldCheck = Clock::now() + WORLD_CHECK_INTERVAL;
                            worldReachable = (bool) status;
                            if (worldReachable) {
                                worldFailures = 0;
                                everReady = true;
                                value.stage = Stage::Ready;
                                value.address = forwardAddress;
                                value.detail = "联机通道已就绪，请在 Minecraft 中加入此地址";
                            } else {
                                value.stage = Stage::Connecting;
                                value.address.clear();
                                require(++worldFailures < WORLD_CHECK_FAILURES,
                                        "无法通过联机通道连接房主游戏，请确认房间和网络正常后重试");
                                value.detail = "房主游戏暂未响应，正在重新检查（" + std::to_string(worldFailures) +
                                        "/" + std::to_string(WORLD_CHECK_FAILURES) + "）";
                            }
                            publish(expected, value);
                        }
                        if (!everReady && value.stage != Stage::Ready)
                            require(Clock::now() < deadline, "连接房间超时，请确认房主在线后重试");
                        if (token.wait_for(std::chrono::milliseconds(500))) break;
                    }
                }
                value = State{};
            } catch (const std::exception& error) {
                value.stage = Stage::Failed;
                value.address.clear();
                value.members.clear();
                value.error = error.what();
                value.detail.clear();
                if (!token.is_cancelled()) LOG.warn("Guest session failed: {}", value.error);
            }
            publish(expected, value);
            finished.store(true, std::memory_order_release);
        }
    }

    const State& state() {
        return shown;
    }

    bool active() {
        return shown.stage == Stage::Preparing || shown.stage == Stage::Connecting || shown.stage == Stage::Ready ||
                shown.stage == Stage::Closing;
    }

    std::string stageText() {
        switch (shown.stage) {
            case Stage::Idle: return "未加入 HMCL 房间";
            case Stage::Preparing: return "正在准备联机组件";
            case Stage::Connecting: return "正在连接房间";
            case Stage::Ready: return "联机通道已就绪";
            case Stage::Closing: return "正在退出房间";
            case Stage::Failed: return "加入房间失败";
        }
        return {};
    }

    void poll() {
        {
            std::lock_guard guard(mutex);
            if (serial != seen) {
                shown = pending;
                seen = serial;
            }
        }
        if (worker.joinable() && finished.load(std::memory_order_acquire)) {
            worker.join();
            if (shown.stage == Stage::Closing) shown = State{};
        }
    }

    void join(const std::string& input) {
        poll();
        if (active() || worker.joinable() || Multiplayer::active()) return;
        std::string_view text = blb::trim(input);
        auto code = std::string(blb::trim(text.substr(0, text.find_first_of("\r\n"))));
        if (code.empty() || code.size() > 4096) {
            shown = State{};
            shown.stage = Stage::Failed;
            shown.error = "请输入有效的 HMCL 房间码";
            return;
        }
        auto root = Platform::cacheDirectory();
        if (root.empty()) root = Platform::userDirectory() / ".b-cache";
        root = std::filesystem::absolute(root / "terracotta-guest");
        auto account = Accounts::current();
        std::string name = account && !account->name.empty() ? account->name : "Player";
        shown = State{};
        shown.stage = Stage::Preparing;
        shown.code = std::move(code);
        shown.detail = "首次使用需要下载官方 Terracotta 联机组件";
        unsigned long long expected;
        {
            std::lock_guard guard(mutex);
            expected = ++revision;
            pending = shown;
            seen = ++serial;
        }
        cancellation = blb::cancellation_source{};
        finished.store(false, std::memory_order_release);
        worker = std::thread(run, expected, shown, root, name, cancellation.token());
    }

    void leave() {
        poll();
        if (!active()) return;
        {
            std::lock_guard guard(mutex);
            ++revision;
            seen = serial;
        }
        shown.stage = Stage::Closing;
        shown.address.clear();
        shown.members.clear();
        shown.error.clear();
        shown.detail.clear();
        cancellation.cancel();
    }

    void dismiss() {
        poll();
        if (shown.stage == Stage::Failed && !worker.joinable()) shown = State{};
    }

    void shutdown() {
        leave();
        cancellation.cancel();
        if (worker.joinable()) worker.join();
        shown = State{};
        std::lock_guard guard(mutex);
        pending = shown;
        seen = ++serial;
        ++revision;
    }
}
