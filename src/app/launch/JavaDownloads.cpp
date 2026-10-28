#include "app/launch/JavaDownloads.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"

#include "ui/panel/Notifications.h"

#include <core/base/cancellation.hpp>
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
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace JavaDownloads {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = std::function<void()>;

        const char* const LIST_URL = "https://cubk.cn/bl/jdks.json";
        constexpr float DOWNLOAD_SHARE = 0.85f;

        struct Job {
            std::atomic<float> fraction{0.0f};
            State state = State::Running;
        };

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;
        List current;
        bool started = false;
        std::map<std::string, std::shared_ptr<Job>> jobs;

        void run(std::function<Continuation(blb::cancellation_token)> work) {
            blb::cancellation_token token = stopping.token();
            workers.emplace_back([work, token] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    std::string text = error.what();
                    next = [text] { Notifications::add(Notifications::FAILED, "Java 下载出错：" + text, 5000); };
                }
                if (next) {
                    std::lock_guard<std::mutex> guard(lock);
                    finished.push_back(std::move(next));
                }
            });
        }

        std::string text(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        uint64_t count(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_number_unsigned() ? it->get<uint64_t>() : 0;
        }

        bool safeFolder(const std::string& folder) {
            return !folder.empty() && folder.find_first_of("/\\:") == std::string::npos && folder != "." &&
                    folder != "..";
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
                    auto major = jdk.find("jdk_version_major");
                    package.major = major != jdk.end() && major->is_number_integer() ? major->get<int>() : 0;
                    auto preview = jdk.find("preview");
                    package.preview = preview != jdk.end() && preview->is_boolean() && preview->get<bool>();
                    package.sha256 = text(item, "sha256");
                    package.prefix = text(item, "unpack_prefix_filter");
                    package.homePrefix = text(item, "package_to_java_home_prefix");
                    package.archiveSize = count(item, "archive_size");
                    package.unpackedSize = count(item, "unpacked_size");
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
                if (name.empty() || name == ".." || name.rfind("../", 0) == 0 ||
                    name.find("/../") != std::string::npos) {
                    return true;
                }
                std::filesystem::path out = target / blb::path_from_utf8(name);
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
        });
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
        if (state(package.folder) == State::Running) {
            return;
        }
        auto job = std::make_shared<Job>();
        jobs[package.folder] = job;
        run([package, job](blb::cancellation_token token) -> Continuation {
            std::filesystem::path base = root();
            std::filesystem::path target = base / blb::path_from_utf8(package.folder);
            std::filesystem::path temp = base / blb::path_from_utf8(package.folder + ".part");
            std::filesystem::path archive = base / blb::path_from_utf8(package.folder + ".zip");
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
                    Notifications::add(Notifications::DONE, label + " 已安装", 3000);
                } else {
                    Notifications::add(Notifications::FAILED, "安装 " + label + " 失败：" + error, 5000);
                }
            };
        });
    }

    void poll() {
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            next();
        }
    }

    void shutdown() {
        stopping.cancel();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }
}
