#include "app/launch/JavaDownloads.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"
#include "app/system/Server.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/platform.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/java/java_runtime.hpp>
#include <core/network/download_client.hpp>
#include <core/network/http_client.hpp>

#include <core/base/json.hpp>

#if !defined(_WIN32)
#include <zlib.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#if !defined(_WIN32)

        const char* systemName() {
#if defined(__APPLE__)
            return "macOS";
#else
            return "linux";
#endif
        }

        const char* archName() {
            return blb::current_platform().arch == blb::cpu_arch::arm64 ? "aarch64" : "x86_64";
        }
#endif
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
#if defined(_WIN32)
                    if (!item.is_object() || blb::json_text(item, "os") != "windows" || blb::json_text(item, "arch") != "x86_64" ||
                        blb::json_text(item, "package_type") != "zip") {
                        continue;
                    }
#else
                    if (!item.is_object() || blb::json_text(item, "os") != systemName() ||
                        blb::json_text(item, "arch") != archName() || blb::json_text(item, "package_type") != "targz") {
                        continue;
                    }
#endif
                    Package package;
                    package.folder = blb::json_text(item, "install_folder_name");
                    package.url = blb::json_text(item, "url");
                    if (!blb::fs::safe_filename(package.folder) || package.url.empty()) {
                        continue;
                    }
                    package.vendor = blb::json_text(jdk, "vendor");
                    package.product = blb::json_text(jdk, "product");
                    package.flavour = blb::json_text(jdk, "flavour");
                    package.version = blb::json_text(jdk, "jdk_version");
                    package.major = (int) *major;
                    package.preview = blb::json_bool(jdk, "preview");
                    package.sha256 = blb::json_text(item, "sha256");
                    package.prefix = blb::json_text(item, "unpack_prefix_filter");
                    package.rootPrefix = blb::json_text(item, "package_root_prefix");
                    package.homePrefix = blb::json_text(item, "package_to_java_home_prefix");
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

        bool verify(const std::filesystem::path& file, const std::string& expected) {
            if (expected.empty()) {
                return true;
            }
            auto digest = blb::sha256_file(file);
            return digest && blb::iequals(blb::to_hex(std::span<const std::uint8_t>(*digest)), expected);
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

#if !defined(_WIN32)
        uint64_t octal(const unsigned char* field, size_t length) {
            if (field[0] & 0x80) {
                uint64_t value = field[0] & 0x7F;
                for (size_t i = 1; i < length; i++) {
                    value = (value << 8) | field[i];
                }
                return value;
            }
            uint64_t value = 0;
            for (size_t i = 0; i < length && field[i] >= '0' && field[i] <= '7'; i++) {
                value = value * 8 + (uint64_t) (field[i] - '0');
            }
            return value;
        }

        std::string text(const unsigned char* field, size_t length) {
            size_t end = 0;
            while (end < length && field[end] != 0) {
                end++;
            }
            return std::string((const char*) field, end);
        }

        void paxField(const std::string& records, const char* key, std::string& out) {
            size_t at = 0;
            while (at < records.size()) {
                size_t space = records.find(' ', at);
                if (space == std::string::npos) {
                    return;
                }
                size_t length = (size_t) std::strtoull(records.c_str() + at, nullptr, 10);
                if (length == 0 || at + length > records.size()) {
                    return;
                }
                std::string record = records.substr(space + 1, at + length - space - 2);
                size_t equals = record.find('=');
                if (equals != std::string::npos && record.compare(0, equals, key) == 0) {
                    out = record.substr(equals + 1);
                }
                at += length;
            }
        }

        bool readFully(gzFile file, void* data, size_t size) {
            auto* bytes = (unsigned char*) data;
            while (size > 0) {
                int chunk = gzread(file, bytes, (unsigned) std::min<size_t>(size, 1 << 20));
                if (chunk <= 0) {
                    return false;
                }
                bytes += chunk;
                size -= (size_t) chunk;
            }
            return true;
        }

        std::string extractTar(const std::filesystem::path& archive, const Package& package,
                               const std::filesystem::path& target, Job& job, blb::cancellation_token token) {
            gzFile file = gzopen(archive.c_str(), "rb");
            if (file == nullptr) {
                return "无法打开 Java 压缩包";
            }
            gzbuffer(file, 1 << 17);
            std::string filter = normalize(package.prefix);
            std::string root = normalize(package.rootPrefix.empty() ? package.prefix : package.rootPrefix);
            while (!root.empty() && root.back() == '/') {
                root.pop_back();
            }
            while (!filter.empty() && filter.back() == '/') {
                filter.pop_back();
            }
            auto destinationRoot = blb::fs::absolute(target);
            std::string failure;
            std::string longName;
            std::string longLink;
            uint64_t done = 0;
            std::vector<char> buffer(1 << 16);
            unsigned char header[512];
            while (failure.empty()) {
                if (token.is_cancelled()) {
                    failure = "已取消";
                    break;
                }
                if (!readFully(file, header, sizeof(header))) {
                    failure = "Java 压缩包不完整";
                    break;
                }
                if (std::all_of(header, header + sizeof(header), [](unsigned char c) { return c == 0; })) {
                    break;
                }
                uint64_t size = octal(header + 124, 12);
                char type = (char) header[156];
                uint64_t padded = (size + 511) / 512 * 512;
                if (type == 'L' || type == 'K' || type == 'x' || type == 'g') {
                    if (size > (1 << 20)) {
                        failure = "Java 压缩包格式无效";
                        break;
                    }
                    std::string data((size_t) padded, '\0');
                    if (!readFully(file, data.data(), data.size())) {
                        failure = "Java 压缩包不完整";
                        break;
                    }
                    data.resize((size_t) size);
                    if (type == 'L') {
                        longName = text((const unsigned char*) data.data(), data.size());
                    } else if (type == 'K') {
                        longLink = text((const unsigned char*) data.data(), data.size());
                    } else if (type == 'x') {
                        paxField(data, "path", longName);
                        paxField(data, "linkpath", longLink);
                    }
                    continue;
                }
                std::string name = longName.empty() ? text(header, 100) : longName;
                std::string link = longLink.empty() ? text(header + 157, 100) : longLink;
                if (longName.empty() && std::memcmp(header + 257, "ustar", 5) == 0 && header[345] != 0) {
                    name = text(header + 345, 155) + "/" + name;
                }
                longName.clear();
                longLink.clear();
                name = normalize(name);
                while (!name.empty() && name.back() == '/') {
                    name.pop_back();
                }
                bool wanted = filter.empty() || name == filter || name.rfind(filter + "/", 0) == 0;
                if (wanted && !root.empty()) {
                    wanted = name == root || name.rfind(root + "/", 0) == 0;
                    name = name == root ? std::string() : name.substr(std::min(name.size(), root.size() + 1));
                }
                bool regular = type == '0' || type == '\0' || type == '7';
                std::filesystem::path out = target / blb::path_from_utf8(name);
                if (wanted && !name.empty()) {
                    if (!safePath(name) || !blb::fs::is_inside(target, out)) {
                        failure = "Java 压缩包包含不安全的路径";
                        break;
                    }
                    for (auto part = blb::fs::absolute(out).parent_path(); !part.empty(); part = part.parent_path()) {
                        if (blb::fs::stat(part).is_link) {
                            failure = "Java 解压路径包含链接";
                            break;
                        }
                        if (part == destinationRoot || part == part.root_path()) break;
                    }
                    if (!failure.empty()) {
                        break;
                    }
                }
                std::error_code error;
                if (!wanted || name.empty() || !regular) {
                    if (wanted && !name.empty() && type == '5') {
                        std::filesystem::create_directories(out, error);
                    } else if (wanted && !name.empty() && type == '2') {
                        std::filesystem::path resolved = (out.parent_path() / link).lexically_normal();
                        if (!link.empty() && link[0] != '/' && blb::fs::is_inside(target, resolved)) {
                            std::filesystem::create_directories(out.parent_path(), error);
                            std::filesystem::remove(out, error);
                            std::filesystem::create_symlink(link, out, error);
                        }
                    }
                    for (uint64_t left = padded; left > 0;) {
                        size_t chunk = (size_t) std::min<uint64_t>(left, buffer.size());
                        if (!readFully(file, buffer.data(), chunk)) {
                            failure = "Java 压缩包不完整";
                            break;
                        }
                        left -= chunk;
                    }
                    continue;
                }
                std::filesystem::create_directories(out.parent_path(), error);
                std::FILE* written = std::fopen(out.c_str(), "wb");
                if (written == nullptr) {
                    failure = "无法写入 Java 文件";
                    break;
                }
                for (uint64_t left = padded; left > 0 && failure.empty();) {
                    size_t chunk = (size_t) std::min<uint64_t>(left, buffer.size());
                    if (!readFully(file, buffer.data(), chunk)) {
                        failure = "Java 压缩包不完整";
                        break;
                    }
                    uint64_t used = padded - left;
                    size_t keep = used >= size ? 0 : (size_t) std::min<uint64_t>(chunk, size - used);
                    if (keep > 0 && std::fwrite(buffer.data(), 1, keep, written) != keep) {
                        failure = "无法写入 Java 文件";
                    }
                    left -= chunk;
                }
                if (std::fclose(written) != 0 && failure.empty()) {
                    failure = "无法写入 Java 文件";
                }
                std::filesystem::permissions(out, (std::filesystem::perms)(octal(header + 100, 8) & 0755),
                                             std::filesystem::perm_options::replace, error);
                done += size;
                if (package.unpackedSize > 0) {
                    float share = std::min(1.0f, (float) ((double) done / (double) package.unpackedSize));
                    job.fraction = DOWNLOAD_SHARE + (1.0f - DOWNLOAD_SHARE) * share;
                }
            }
            gzclose(file);
            return failure;
        }
#endif

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

    bool available() {
        return !blb::java::runtime_blocked();
    }

    const List& list() {
        if (!started) {
            refresh();
        }
        return current;
    }

    void refresh() {
        if (!available()) {
            started = true;
            current.status = Status::Failed;
            current.packages.clear();
            current.error = blb::java::runtime_blocked()->message;
            return;
        }
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
        auto home = homeOf(package);
        const auto& known = Catalog::java().list;
        return std::any_of(known.begin(), known.end(), [&](const auto& java) {
            return std::filesystem::equivalent(home, java.home, error);
        });
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
#if defined(_WIN32)
        const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
        std::filesystem::path base =
                local != nullptr && *local != 0 ? std::filesystem::path(local) : Platform::exeDirectory();
#else
        return blb::launcher_data_directory() / "java";
#endif
#if defined(_WIN32)
        return base / L"b-launcher" / L"java";
#endif
    }

    void install(const Package& package) {
        if (!available()) {
            Notices::add(Notices::Kind::Failed, blb::java::runtime_blocked()->message, 5000);
            return;
        }
        if (package.archiveSize > (uint64_t) std::numeric_limits<int64_t>::max()) {
            Notices::add(Notices::Kind::Failed, "Java 下载大小无效", 5000);
            return;
        }
        if (!blb::fs::safe_filename(package.folder) ||
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
#if defined(_WIN32)
                error = extract(archive, package, temp, *job, token);
#else
                error = extractTar(archive, package, temp, *job, token);
#endif
            }
            std::filesystem::remove(archive, ec);
            if (error.empty()) {
                std::filesystem::path home = temp;
                std::string prefix = normalize(package.homePrefix);
                if (!prefix.empty()) {
                    home /= blb::path_from_utf8(prefix);
                }
#if defined(_WIN32)
                if (!std::filesystem::exists(home / "bin" / "java.exe", ec)) {
                    error = "解压后没有找到 java.exe";
                }
#else
                if (!std::filesystem::exists(home / "bin" / "java", ec)) {
                    error = "解压后没有找到 java";
                }
#endif
                if (error.empty()) {
                    auto checked = blb::java::probe_java(home, true, token, {}, true);
                    if (!checked) error = checked.err().message;
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
#if defined(_WIN32)
            std::filesystem::path java = homeOf(package) / "bin" / "java.exe";
#else
            std::filesystem::path java = homeOf(package) / "bin" / "java";
#endif
            std::optional<blb::java::java_installation> installed;
            if (error.empty()) {
                auto checked = blb::java::probe_java(java, true, token, {}, true);
                if (checked) {
                    installed = *checked;
                    installed->source = blb::java::java_source::managed;
                } else {
                    error = checked.err().message;
                }
            }
            std::string label = name(package) + " " + package.version;
            return [job, error, installed, label] {
                job->state = error.empty() ? State::Done : State::Failed;
                if (error.empty()) {
                    job->fraction = 1.0f;
                    Catalog::seenJava(*installed);
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
