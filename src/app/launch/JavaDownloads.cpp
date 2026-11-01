#include "app/launch/JavaDownloads.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"
#include "app/system/Server.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/network/download_client.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>

namespace JavaDownloads {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        const std::string LIST_URL = Server::file("jdks.json");
        constexpr float DOWNLOAD_SHARE = 0.85f;

        struct Job {
            std::atomic<float> fraction{0.0f};
            State state = State::Running;
        };

        AsyncTasks tasks;
        List current;
        bool started = false;
        std::map<std::string, std::shared_ptr<Job>> jobs;

        void run(AsyncTasks::Work work, std::function<void()> fail) {
            tasks.run(std::move(work), [fail = std::move(fail)](const std::string& text) -> Continuation {
                return [text, fail] {
                    fail();
                    Notices::add(Notices::Kind::Failed, "Java 下载出错：" + text, 5000);
                };
            });
        }

        std::string text(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        std::optional<uint64_t> count(const nlohmann::json& doc, const char* key,
                                      uint64_t maximum = std::numeric_limits<uint64_t>::max()) {
            auto it = doc.find(key);
            if (it == doc.end()) {
                return 0;
            }
            if (!it->is_number_integer() || (!it->is_number_unsigned() && it->get<int64_t>() < 0)) {
                return std::nullopt;
            }
            uint64_t value = it->get<uint64_t>();
            return value <= maximum ? std::optional<uint64_t>(value) : std::nullopt;
        }

        bool safeFolder(const std::string& folder) {
            return blb::fs::safe_relative(folder) && folder.find_first_of("/\\") == std::string::npos &&
                    folder != "." && folder.back() != '.' && folder.back() != ' ';
        }

        std::vector<Package> parse(const std::string& body, std::string& error) {
            nlohmann::json doc = nlohmann::json::parse(body, nullptr, false);
            auto jdks = doc.is_object() ? doc.find("jdks") : doc.end();
            if (!doc.is_object() || jdks == doc.end() || !jdks->is_array()) {
                error = "Java 列表无法识别";
                return {};
            }
            std::vector<Package> out;
            for (const nlohmann::json& jdk : *jdks) {
                auto packages = jdk.is_object() ? jdk.find("packages") : jdk.end();
                if (!jdk.is_object() || packages == jdk.end() || !packages->is_array()) {
                    continue;
                }
                auto major = count(jdk, "jdk_version_major", std::numeric_limits<int>::max());
                if (!major || *major == 0) {
                    continue;
                }
                for (const nlohmann::json& item : *packages) {
                    if (!item.is_object() || text(item, "os") != "windows" || text(item, "arch") != "x86_64" ||
                        text(item, "package_type") != "zip") {
                        continue;
                    }
                    Package package;
                    package.folder = text(item, "install_folder_name");
                    package.url = text(item, "url");
                    if (!safeFolder(package.folder) || package.url.empty()) {
                        continue;
                    }
                    package.vendor = text(jdk, "vendor");
                    package.product = text(jdk, "product");
                    package.flavour = text(jdk, "flavour");
                    package.version = text(jdk, "jdk_version");
                    package.major = (int) *major;
                    auto preview = jdk.find("preview");
                    package.preview = preview != jdk.end() && preview->is_boolean() && preview->get<bool>();
                    package.sha256 = text(item, "sha256");
                    package.prefix = text(item, "unpack_prefix_filter");
                    package.homePrefix = text(item, "package_to_java_home_prefix");
                    auto archiveSize = count(item, "archive_size", std::numeric_limits<int64_t>::max());
                    auto unpackedSize = count(item, "unpacked_size");
                    if (!archiveSize || !unpackedSize) {
                        continue;
                    }
                    package.archiveSize = *archiveSize;
                    package.unpackedSize = *unpackedSize;
                    out.push_back(std::move(package));
                }
            }
            std::stable_sort(out.begin(), out.end(), [](const Package& a, const Package& b) {
                if (a.major != b.major) {
                    return a.major > b.major;
                }
                if (a.preview != b.preview) {
                    return !a.preview;
                }
                return name(a) < name(b);
            });
            return out;
        }

        std::string hexOf(const blb::sha256_digest& digest) {
            static const char* const digits = "0123456789abcdef";
            std::string out;
            for (std::uint8_t byte : digest) {
                out += digits[byte >> 4];
                out += digits[byte & 0xF];
            }
            return out;
        }

        bool verify(const std::filesystem::path& file, const std::string& expected) {
            if (expected.empty()) {
                return true;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                return false;
            }
            blb::sha256 hasher;
            std::vector<char> buffer(1 << 20);
            while (in) {
                in.read(buffer.data(), (std::streamsize) buffer.size());
                std::streamsize got = in.gcount();
                if (got > 0) {
                    hasher.update(buffer.data(), (size_t) got);
                }
            }
            std::string want = expected;
            std::transform(want.begin(), want.end(), want.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return hexOf(hasher.finish()) == want;
        }

        std::string normalize(std::string path) {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.rfind("./", 0) == 0) {
                path.erase(0, 2);
            }
            return path;
        }

        bool safePath(const std::string& path) {
            if (!blb::fs::safe_relative(path)) return false;
            for (auto part : blb::split(path, '/')) {
                if (!part.empty() && part != "." && !blb::fs::safe_component(part)) return false;
            }
            return true;
        }

        std::string extract(const std::filesystem::path& archive, const Package& package,
                            const std::filesystem::path& target, Job& job, blb::cancellation_token token) {
            auto zip = blb::zip_reader::open(archive);
            if (!zip) {
                return zip.err().message;
            }
            std::string root = normalize(package.prefix);
            if (!root.empty() && root.back() != '/') {
                root += '/';
            }
            uint64_t done = 0;
            std::string failure;
            auto destinationRoot = blb::fs::absolute(target);
            auto visited = zip->for_each([&](const blb::zip_entry& entry, blb::zip_reader& reader) -> bool {
                if (token.is_cancelled()) {
                    failure = "已取消";
                    return false;
                }
                std::string name = normalize(entry.name);
                if (!root.empty()) {
                    if (name.rfind(root, 0) != 0) {
                        return true;
                    }
                    name.erase(0, root.size());
                }
                name = normalize(std::move(name));
                if (name.empty() || (entry.directory && name == ".")) {
                    return true;
                }
                if (!safePath(name)) {
                    failure = "Java 压缩包包含不安全的路径";
                    return false;
                }
                std::filesystem::path out = target / blb::path_from_utf8(name);
                if (!blb::fs::is_inside(target, out)) {
                    failure = "Java 压缩包中的路径超出安装目录";
                    return false;
                }
                // Refuse existing reparse points, including a linked staging root.
                for (auto part = blb::fs::absolute(out); !part.empty(); part = part.parent_path()) {
                    if (blb::fs::stat(part).is_link) {
                        failure = "Java 解压路径包含链接";
                        return false;
                    }
                    if (part == destinationRoot || part == part.root_path()) break;
                }
                std::error_code error;
                if (entry.directory || name.back() == '/') {
                    std::filesystem::create_directories(out, error);
                    return true;
                }
                std::filesystem::create_directories(out.parent_path(), error);
                auto written = reader.extract_current(out);
                if (!written) {
                    failure = written.err().message;
                    return false;
                }
                done += entry.size;
                if (package.unpackedSize > 0) {
                    float share = std::min(1.0f, (float) ((double) done / (double) package.unpackedSize));
                    job.fraction = DOWNLOAD_SHARE + (1.0f - DOWNLOAD_SHARE) * share;
                }
                return true;
            });
            if (!visited && failure.empty()) {
                failure = visited.err().message;
            }
            return failure;
        }

        std::filesystem::path homeOf(const Package& package) {
            std::filesystem::path home = root() / blb::path_from_utf8(package.folder);
            std::string prefix = normalize(package.homePrefix);
            if (!prefix.empty()) {
                home /= blb::path_from_utf8(prefix);
            }
            return home;
        }
    }

    const List& list() {
        if (!started) {
            refresh();
        }
        return current;
    }

    void refresh() {
        if (started && current.status == Status::Loading) {
            return;
        }
        started = true;
        current.status = Status::Loading;
        current.error.clear();
        run([](blb::cancellation_token token) -> Continuation {
            blb::net::http_request request;
            request.url = LIST_URL;
            request.headers.push_back({"User-Agent", Platform::USER_AGENT});
            request.timeout = std::chrono::seconds(30);
            auto response = blb::net::http_send(request, token);
            std::string error;
            std::vector<Package> packages;
            if (!response) {
                error = response.err().message;
            } else if (!response->ok()) {
                error = "HTTP " + std::to_string(response->status);
            } else {
                packages = parse(response->body, error);
            }
            return [packages, error] {
                current.packages = packages;
                current.error = error;
                current.status = error.empty() ? Status::Ready : Status::Failed;
            };
        }, [] { current.status = Status::Failed; current.error = "Java 列表读取失败，请重试"; });
    }

    std::string name(const Package& package) {
        std::string out = package.vendor;
        if (!package.product.empty()) {
            out += (out.empty() ? "" : " ") + package.product;
        }
        if (!package.flavour.empty() && package.flavour.rfind("AdoptOpenJDK", 0) != 0) {
            out += " " + package.flavour;
        }
        return out;
    }

    bool installed(const Package& package) {
        std::error_code error;
        return std::filesystem::exists(homeOf(package) / "bin" / "java.exe", error);
    }

    bool busy() {
        for (const auto& [folder, job] : jobs) {
            if (job->state == State::Running) {
                return true;
            }
        }
        return false;
    }

    State state(const std::string& folder) {
        auto found = jobs.find(folder);
        return found != jobs.end() ? found->second->state : State::None;
    }

    float progress(const std::string& folder) {
        auto found = jobs.find(folder);
        return found != jobs.end() ? found->second->fraction.load() : 0.0f;
    }

    std::filesystem::path root() {
        const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
        std::filesystem::path base =
                local != nullptr && *local != 0 ? std::filesystem::path(local) : Platform::exeDirectory();
        return base / L"b-launcher" / L"java";
    }

    void install(const Package& package) {
        if (package.archiveSize > (uint64_t) std::numeric_limits<int64_t>::max()) {
            Notices::add(Notices::Kind::Failed, "Java 下载大小无效", 5000);
            return;
        }
        if (!safeFolder(package.folder) ||
            (!package.homePrefix.empty() && !safePath(normalize(package.homePrefix)))) {
            Notices::add(Notices::Kind::Failed, "Java 安装路径无效", 5000);
            return;
        }
        if (state(package.folder) == State::Running) {
            return;
        }
        auto job = std::make_shared<Job>();
        jobs[package.folder] = job;
        run([package, job](blb::cancellation_token token) -> Continuation {
            std::filesystem::path base = root();
            std::filesystem::path target = base / blb::path_from_utf8(package.folder);
            std::filesystem::path temp = blb::fs::temp_sibling(target);
            std::filesystem::path archive = temp;
            archive += ".zip";
            std::error_code ec;
            std::filesystem::create_directories(base, ec);
            std::filesystem::remove_all(temp, ec);
            std::string error;

            blb::net::download_task task;
            task.urls = {package.url};
            task.target = archive;
            task.size = package.archiveSize > 0 ? (std::int64_t) package.archiveSize : -1;
            uint64_t expected = package.archiveSize;
            blb::function_progress_sink sink([job, expected](const blb::progress_event& event) {
                uint64_t total = event.total_bytes > 0 ? event.total_bytes : expected;
                if (total > 0) {
                    job->fraction = DOWNLOAD_SHARE * std::min(1.0f, (float) ((double) event.done_bytes / total));
                }
            });
            {
                blb::progress_tracker tracker(&sink, "java");
                blb::net::download_client client;
                blb::net::download_report report = client.run(std::span(&task, 1), &tracker, token);
                if (!report.ok()) {
                    error = report.failures.empty() ? std::string("下载失败") : report.failures.front().reason.message;
                }
            }
            if (error.empty() && !verify(archive, package.sha256)) {
                error = "下载的文件校验不通过";
            }
            if (error.empty()) {
                job->fraction = DOWNLOAD_SHARE;
                error = extract(archive, package, temp, *job, token);
            }
            std::filesystem::remove(archive, ec);
            if (error.empty()) {
                std::filesystem::path home = temp;
                std::string prefix = normalize(package.homePrefix);
                if (!prefix.empty()) {
                    home /= blb::path_from_utf8(prefix);
                }
                if (!std::filesystem::exists(home / "bin" / "java.exe", ec)) {
                    error = "解压后没有找到 java.exe";
                }
            }
            if (error.empty()) {
                std::filesystem::remove_all(target, ec);
                std::filesystem::rename(temp, target, ec);
                if (ec) {
                    error = "无法移动到安装目录";
                }
            }
            if (!error.empty()) {
                std::filesystem::remove_all(temp, ec);
            }
            std::filesystem::path java = homeOf(package) / "bin" / "java.exe";
            std::string label = name(package) + " " + package.version;
            return [job, error, java, label] {
                job->state = error.empty() ? State::Done : State::Failed;
                if (error.empty()) {
                    job->fraction = 1.0f;
                    Catalog::addJava(java);
                    Notices::add(Notices::Kind::Done, label + " 已安装", 3000);
                } else {
                    Notices::add(Notices::Kind::Failed, "安装 " + label + " 失败：" + error, 5000);
                }
            };
        }, [job] { job->state = State::Failed; });
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
