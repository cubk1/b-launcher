#include "ui/imgui/screens/theme/ThemeCatalog.h"

#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Transfers.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeStore.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/remote_zip.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace ThemeCatalog {

    namespace blb = b_launcher_base;

    namespace {
        const char* const BASE = "https://b-catalog.cubk.cn/";
        const char* const MARKER = ".catalog.json";
        constexpr std::uint64_t MAX_ENTRY = (std::uint64_t) 64 << 20;
        const char* const ALLOWED[] = {".json", ".js",  ".png",  ".jpg", ".jpeg", ".ttf",
                                       ".otf",  ".ttc", ".glsl", ".txt", ".wav"};

        Status state = Status::Idle;
        std::string failure;
        std::vector<Entry> list;
        std::map<std::string, std::string> installed;
        struct Job {
            std::atomic<float> progress{0.0f};
            uint64_t transfer = 0;
            std::string label;
            blb::cancellation_source cancel;
        };

        std::map<std::string, std::shared_ptr<Job>> working;
        AsyncTasks tasks;

        std::string absolute(const std::string& path) {
            if (path.rfind("https://", 0) == 0 || path.rfind("http://", 0) == 0) {
                return path;
            }
            return std::string(BASE) + path;
        }

        std::string text(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        std::vector<Entry> parse(const std::string& body) {
            nlohmann::json doc = nlohmann::json::parse(body, nullptr, false);
            auto themes = doc.is_object() ? doc.find("themes") : doc.end();
            if (!doc.is_object() || themes == doc.end() || !themes->is_array()) {
                throw std::runtime_error("目录格式不对");
            }
            std::vector<Entry> out;
            for (const nlohmann::json& item : *themes) {
                if (!item.is_object()) {
                    continue;
                }
                Entry entry;
                entry.id = text(item, "id");
                auto package = item.find("package");
                if (!ThemeStore::validId(entry.id) || package == item.end() || !package->is_object()) {
                    continue;
                }
                entry.name = text(item, "name");
                entry.author = text(item, "author");
                entry.version = text(item, "version");
                entry.description = text(item, "description");
                entry.updated = text(item, "updated");
                entry.package = absolute(text(*package, "url"));
                entry.sha256 = text(*package, "sha256");
                auto size = package->find("size");
                entry.size = size != package->end() && size->is_number_unsigned() ? size->get<uint64_t>() : 0;
                if (std::string icon = text(item, "icon"); !icon.empty()) {
                    entry.icon = absolute(icon);
                }
                for (const char* key : {"previews", "requires"}) {
                    auto values = item.find(key);
                    if (values == item.end() || !values->is_array()) {
                        continue;
                    }
                    for (const nlohmann::json& value : *values) {
                        if (value.is_string()) {
                            std::string string = value.get<std::string>();
                            (key[0] == 'p' ? entry.previews : entry.dependencies)
                                    .push_back(key[0] == 'p' ? absolute(string) : string);
                        }
                    }
                }
                if (entry.name.empty()) {
                    entry.name = entry.id;
                }
                out.push_back(std::move(entry));
            }
            return out;
        }

        std::string markerOf(const std::string& id) {
            auto data = blb::fs::read_file(ThemeStore::folder() / id / MARKER);
            if (!data) {
                return {};
            }
            nlohmann::json doc = nlohmann::json::parse(*data, nullptr, false);
            return doc.is_object() ? text(doc, "sha256") : std::string();
        }

        void writeMarker(const std::filesystem::path& folder, const Entry& entry) {
            nlohmann::json doc = {{"sha256", entry.sha256}, {"version", entry.version}};
            if (!blb::fs::write_file_atomic(folder / MARKER, doc.dump(1))) {
                throw std::runtime_error("无法写入主题文件夹");
            }
        }

        bool allowed(const std::string& relative) {
            std::string lower = relative;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return std::any_of(std::begin(ALLOWED), std::end(ALLOWED), [&](const char* extension) {
                return lower.size() > std::strlen(extension) &&
                        lower.compare(lower.size() - std::strlen(extension), std::string::npos, extension) == 0;
            });
        }

        uint32_t checksum(const std::string& data) {
            return (uint32_t) crc32(crc32(0L, Z_NULL, 0), (const Bytef*) data.data(), (uInt) data.size());
        }

        std::string digestOf(const std::string& data) {
            blb::sha256_digest digest = blb::sha256::digest(data.data(), data.size());
            return blb::to_hex(digest);
        }

        std::string relativeOf(const std::string& name, const std::string& prefix) {
            if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0 || name.back() == '/') {
                return {};
            }
            std::string relative = name.substr(prefix.size());
            return blb::fs::safe_relative(relative) && allowed(relative) ? relative : std::string();
        }

        void stage(Job& job, const std::string& text) {
            Transfers::stage(job.transfer, job.label.empty() ? text : job.label + "：" + text);
        }

        void cancelled(blb::cancellation_token token) {
            if (token.is_cancelled()) {
                throw std::runtime_error("已取消");
            }
        }

        bool patch(const Entry& entry, const std::filesystem::path& target, Job& job) {
            blb::cancellation_token token = job.cancel.token();
            stage(job, "正在比对文件");
            auto zip = blb::modpack::remote_zip::open({entry.package}, entry.size, token);
            if (!zip) {
                return false;
            }
            std::string prefix = entry.id + "/";
            std::set<std::string> keep;
            std::vector<std::pair<std::string, const blb::modpack::remote_entry*>> changed;
            for (const blb::modpack::remote_entry& item : zip->entries()) {
                std::string relative = relativeOf(item.name, prefix);
                if (relative.empty() || item.size > MAX_ENTRY) {
                    continue;
                }
                keep.insert(relative);
                auto local = blb::fs::read_file(target / blb::path_from_utf8(relative));
                if (local && local->size() == item.size && checksum(*local) == item.crc) {
                    continue;
                }
                changed.emplace_back(relative, &item);
            }
            if (keep.count("theme.json") == 0) {
                return false;
            }
            uint64_t total = 0;
            for (const auto& item : changed) {
                total += item.second->compressed;
            }
            uint64_t done = 0;
            std::string part = "download:" + entry.id;
            stage(job, changed.empty() ? "已是最新" : "正在下载变更");
            for (size_t i = 0; i < changed.size(); i++) {
                cancelled(token);
                auto data = zip->read(changed[i].second->name, token);
                if (!data || checksum(*data) != changed[i].second->crc) {
                    return false;
                }
                std::filesystem::path out = target / blb::path_from_utf8(changed[i].first);
                if (!blb::fs::is_inside(target, out)) {
                    continue;
                }
                std::error_code ec;
                std::filesystem::create_directories(out.parent_path(), ec);
                if (!blb::fs::write_file_atomic(out, *data)) {
                    throw std::runtime_error("无法写入主题文件夹");
                }
                done += changed[i].second->compressed;
                job.progress = (float) (i + 1) / (float) changed.size();
                blb::progress_event event;
                event.task = part;
                event.done_bytes = done;
                event.total_bytes = total;
                event.done_items = i + 1;
                event.total_items = changed.size();
                Transfers::report(job.transfer, event);
            }
            std::error_code ec;
            std::vector<std::filesystem::path> stale;
            for (std::filesystem::recursive_directory_iterator it(target, ec), end; !ec && it != end;
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                std::string relative = blb::path_to_utf8(it->path().lexically_relative(target));
                std::replace(relative.begin(), relative.end(), '\\', '/');
                if (relative != MARKER && keep.count(relative) == 0) {
                    stale.push_back(it->path());
                }
            }
            for (const std::filesystem::path& path : stale) {
                std::filesystem::remove(path, ec);
            }
            return true;
        }

        void full(const Entry& entry, const std::filesystem::path& target, Job& job) {
            blb::cancellation_token token = job.cancel.token();
            stage(job, "正在下载");
            std::string part = "download:" + entry.id;
            std::string body;
            blb::net::http_request request;
            request.url = entry.package;
            request.timeout = std::chrono::minutes(10);
            request.on_data = [&](std::string_view chunk) {
                body.append(chunk);
                if (entry.size > 0) {
                    job.progress = std::min(1.0f, (float) body.size() / (float) entry.size);
                }
                blb::progress_event event;
                event.task = part;
                event.done_bytes = body.size();
                event.total_bytes = std::max<uint64_t>(entry.size, body.size());
                Transfers::report(job.transfer, event);
            };
            auto response = blb::net::http_send(request, token);
            cancelled(token);
            if (!response) {
                throw std::runtime_error(response.err().message);
            }
            if (!response->ok()) {
                throw std::runtime_error("HTTP " + std::to_string(response->status));
            }
            if (body.empty()) {
                body = std::move(response->body);
            }
            stage(job, "正在校验");
            if (!entry.sha256.empty() && digestOf(body) != entry.sha256) {
                throw std::runtime_error("主题包校验失败");
            }
            stage(job, "正在解压");
            auto zip = blb::zip_reader::open_memory(std::move(body));
            if (!zip) {
                throw std::runtime_error("主题包无法解压");
            }
            std::filesystem::path root = ThemeStore::folder();
            std::filesystem::path staging = root / (".catalog-" + blb::random_bytes_hex(6));
            std::error_code ec;
            std::filesystem::create_directories(staging, ec);
            std::string prefix = entry.id + "/";
            bool hasTheme = false;
            bool broken = false;
            (void) zip->for_each([&](const blb::zip_entry& item, blb::zip_reader& reader) {
                std::string relative = relativeOf(item.name, prefix);
                if (relative.empty() || item.directory || item.size > MAX_ENTRY) {
                    return true;
                }
                std::filesystem::path out = staging / blb::path_from_utf8(relative);
                auto data = reader.read_current();
                if (!blb::fs::is_inside(staging, out) || !data) {
                    broken = true;
                    return false;
                }
                std::filesystem::create_directories(out.parent_path(), ec);
                if (!blb::fs::write_file_atomic(out, *data)) {
                    broken = true;
                    return false;
                }
                hasTheme = hasTheme || relative == "theme.json";
                return !token.is_cancelled();
            });
            if (broken || !hasTheme || token.is_cancelled()) {
                std::filesystem::remove_all(staging, ec);
                throw std::runtime_error(token.is_cancelled() ? "已取消" : "主题包内容不完整");
            }
            writeMarker(staging, entry);
            std::filesystem::remove_all(target, ec);
            std::filesystem::rename(staging, target, ec);
            if (ec) {
                std::filesystem::remove_all(staging, ec);
                throw std::runtime_error("无法替换主题文件夹");
            }
        }

        void installOne(const Entry& entry, Job& job) {
            std::filesystem::path target = ThemeStore::folder() / entry.id;
            std::error_code ec;
            std::filesystem::create_directories(ThemeStore::folder(), ec);
            if (std::filesystem::is_directory(target, ec) && patch(entry, target, job)) {
                writeMarker(target, entry);
                return;
            }
            cancelled(job.cancel.token());
            full(entry, target, job);
        }

        void collect(const std::string& id, std::vector<Entry>& order, std::set<std::string>& seen) {
            if (!seen.insert(id).second) {
                return;
            }
            const Entry* entry = find(id);
            if (entry == nullptr) {
                return;
            }
            for (const std::string& parent : entry->dependencies) {
                collect(parent, order, seen);
            }
            order.push_back(*entry);
        }
    }

    void refresh() {
        if (state == Status::Loading) {
            return;
        }
        state = Status::Loading;
        failure.clear();
        tasks.run(
                [](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    auto body = blb::net::http_get(std::string(BASE) + "index.json", token);
                    if (!body) {
                        throw std::runtime_error(body.err().message);
                    }
                    std::vector<Entry> parsed = parse(*body);
                    std::map<std::string, std::string> markers;
                    for (const Entry& entry : parsed) {
                        markers[entry.id] = markerOf(entry.id);
                    }
                    return [parsed = std::move(parsed), markers = std::move(markers)]() mutable {
                        list = std::move(parsed);
                        installed = std::move(markers);
                        state = Status::Ready;
                        ThemeStore::rescan();
                    };
                },
                [](const std::string& message) -> AsyncTasks::Continuation {
                    return [message] {
                        failure = message;
                        state = Status::Failed;
                    };
                });
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }

    Status status() {
        return state;
    }

    const std::string& error() {
        return failure;
    }

    const std::vector<Entry>& entries() {
        return list;
    }

    const Entry* find(const std::string& id) {
        for (const Entry& entry : list) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    }

    Local local(const Entry& entry) {
        const ThemeStore::Entry* found = ThemeStore::find(entry.id);
        if (found == nullptr || found->builtIn) {
            return Local::Missing;
        }
        auto marker = installed.find(entry.id);
        if (!found->zip.empty() || marker == installed.end() || marker->second.empty()) {
            return Local::Unknown;
        }
        return marker->second == entry.sha256 ? Local::Current : Local::Outdated;
    }

    bool busy(const std::string& id) {
        return working.count(id) != 0;
    }

    float progress(const std::string& id) {
        auto it = working.find(id);
        return it != working.end() ? it->second->progress.load() : 0.0f;
    }

    void install(const std::string& id, bool apply) {
        if (busy(id) || find(id) == nullptr) {
            return;
        }
        std::vector<Entry> order;
        std::set<std::string> seen;
        collect(id, order, seen);
        auto job = std::make_shared<Job>();
        const Entry& entry = *find(id);
        Transfers::Spec spec;
        spec.kind = Transfers::Kind::Theme;
        spec.title = entry.name;
        spec.icon = entry.icon;
        spec.folder = ThemeStore::folder() / id;
        spec.cancel = [job] { job->cancel.cancel(); };
        job->transfer = Transfers::begin(std::move(spec));
        Transfers::stage(job->transfer, "正在连接");
        working[id] = job;
        bool update = local(entry) != Local::Missing;
        std::string name = entry.name;
        tasks.run(
                [order, job, id, apply, update, name](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    blb::cancellation_registration link = token.on_cancel([job] { job->cancel.cancel(); });
                    std::vector<std::pair<std::string, std::string>> done;
                    for (const Entry& each : order) {
                        job->progress = 0.0f;
                        job->label = order.size() > 1 ? each.name : std::string();
                        installOne(each, *job);
                        done.emplace_back(each.id, each.sha256);
                    }
                    return [done, job, id, apply, update, name] {
                        Transfers::finish(job->transfer, Transfers::State::Done);
                        for (const auto& [theme, sha] : done) {
                            installed[theme] = sha;
                        }
                        working.erase(id);
                        ThemeStore::rescan();
                        if (apply && ThemeRuntime::current() != id) {
                            Appearance::setTheme(id);
                        } else if (ThemeRuntime::current() == id) {
                            ThemeRuntime::reload();
                        }
                        Notices::add(Notices::Kind::Done,
                                     (update          ? "已更新主题 "
                                              : apply ? "已应用主题 "
                                                      : "已下载主题 ") +
                                             name,
                                     3000);
                    };
                },
                [job, id](const std::string& message) -> AsyncTasks::Continuation {
                    return [job, id, message] {
                        bool stopped = job->cancel.is_cancelled();
                        Transfers::finish(job->transfer,
                                          stopped ? Transfers::State::Cancelled : Transfers::State::Failed,
                                          stopped ? std::string() : message);
                        working.erase(id);
                        ThemeStore::rescan();
                        if (!stopped) {
                            Notices::add(Notices::Kind::Failed, "下载主题失败：" + message, 5000);
                        }
                    };
                });
    }
}
