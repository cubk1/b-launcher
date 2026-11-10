#include "app/instances/ServerBuilds.h"
#include "app/downloads/DownloadSources.h"
#include "app/downloads/Modrinth.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Catalog.h"
#include "app/system/Config.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "app/system/Transfers.h"

#include <core/base/fs.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/server_installer.hpp>
#include <core/java/java_requirement.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>

#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>

namespace ServerBuilds {

    namespace blb = b_launcher_base;

    namespace {
        const char* const DEFAULT_PICKS[] = {"mods", "config", "defaultconfigs", "kubejs", "scripts", "global_packs"};

        std::map<std::string, Options> remembered;
        FileTree::Tree files;
        std::map<std::string, std::set<std::string>> detected;
        std::set<std::string> detectingKeys;

        AsyncTasks tasks;
        AsyncTasks detections;
        std::shared_ptr<blb::cancellation_source> cancelling;
        bool busy = false;
        std::string busyKey;
        std::mutex progressLock;
        std::string progress;
        std::filesystem::path lastFolder;
        std::atomic<uint64_t> transfer{0};

        void show(const std::string& text) {
            std::lock_guard<std::mutex> guard(progressLock);
            progress = text;
        }

        void say(const std::string& text) {
            show(text);
            Transfers::stage(transfer, text);
        }

        void copied(const std::filesystem::path& file) {
            std::error_code ec;
            uintmax_t size = std::filesystem::file_size(file, ec);
            if (!ec) {
                Transfers::io(transfer, size, size);
            }
        }

        nlohmann::json toJson(const Options& o) {
            nlohmann::json picks = nlohmann::json::object();
            for (const auto& [path, on] : o.picks) {
                picks[path] = on;
            }
            return {{"java", o.java},
                    {"javaPath", o.javaPath},
                    {"memory", o.memoryMb},
                    {"jvmArgs", o.jvmArgs},
                    {"port", o.port},
                    {"onlineMode", o.onlineMode},
                    {"maxPlayers", o.maxPlayers},
                    {"motd", o.motd},
                    {"world", o.world},
                    {"picked", o.picked},
                    {"picks", picks},
                    {"seen", o.seen}};
        }

        Options fromJson(const nlohmann::json& doc) {
            Options o;
            o.java = doc.value("java", o.java);
            o.javaPath = doc.value("javaPath", o.javaPath);
            o.memoryMb = doc.value("memory", o.memoryMb);
            o.jvmArgs = doc.value("jvmArgs", o.jvmArgs);
            o.port = doc.value("port", o.port);
            o.onlineMode = doc.value("onlineMode", o.onlineMode);
            o.maxPlayers = doc.value("maxPlayers", o.maxPlayers);
            o.motd = doc.value("motd", o.motd);
            o.world = doc.value("world", o.world);
            o.picked = doc.value("picked", o.picked);
            if (auto picks = doc.find("picks"); picks != doc.end() && picks->is_object()) {
                for (auto it = picks->begin(); it != picks->end(); ++it) {
                    if (it->is_boolean() && blb::fs::safe_relative(it.key())) {
                        o.picks[it.key()] = it->get<bool>();
                    }
                }
            }
            if (auto seen = doc.find("seen"); seen != doc.end() && seen->is_array()) {
                for (const nlohmann::json& item : *seen) {
                    if (item.is_string()) {
                        o.seen.push_back(item.get<std::string>());
                    }
                }
            }
            return o;
        }

        void applyDetected(const std::string& key) {
            auto found = detected.find(key);
            if (found == detected.end()) {
                return;
            }
            Options& o = options(key);
            bool changed = false;
            for (const std::string& path : found->second) {
                if (std::find(o.seen.begin(), o.seen.end(), path) != o.seen.end()) {
                    continue;
                }
                o.seen.push_back(path);
                if (blb::modpack::pick_state(o.picks, path)) {
                    o.picks[path] = false;
                }
                changed = true;
            }
            if (changed) {
                save(key);
            }
        }

        std::string propertyValue(const std::string& text) {
            std::string out;
            char escaped[16];
            for (size_t i = 0; i < text.size();) {
                unsigned char c = (unsigned char) text[i];
                uint32_t code = c;
                size_t length = 1;
                if (c >= 0xF0 && i + 3 < text.size()) {
                    code = ((c & 0x07u) << 18) | (((unsigned char) text[i + 1] & 0x3Fu) << 12) |
                            (((unsigned char) text[i + 2] & 0x3Fu) << 6) | ((unsigned char) text[i + 3] & 0x3Fu);
                    length = 4;
                } else if (c >= 0xE0 && i + 2 < text.size()) {
                    code = ((c & 0x0Fu) << 12) | (((unsigned char) text[i + 1] & 0x3Fu) << 6) |
                            ((unsigned char) text[i + 2] & 0x3Fu);
                    length = 3;
                } else if (c >= 0xC0 && i + 1 < text.size()) {
                    code = ((c & 0x1Fu) << 6) | ((unsigned char) text[i + 1] & 0x3Fu);
                    length = 2;
                }
                i += length;
                if (code == '\\') {
                    out += "\\\\";
                } else if (code >= 0x20 && code < 0x7F) {
                    out.push_back((char) code);
                } else if (code >= 0x10000) {
                    code -= 0x10000;
                    std::snprintf(escaped, sizeof(escaped), "\\u%04X\\u%04X", 0xD800 + (code >> 10),
                                  0xDC00 + (code & 0x3FF));
                    out += escaped;
                } else {
                    std::snprintf(escaped, sizeof(escaped), "\\u%04X", code);
                    out += escaped;
                }
            }
            return out;
        }

        std::filesystem::path freeFolder(const std::filesystem::path& parent, const std::string& base) {
            std::error_code ec;
            std::filesystem::path target = parent / blb::path_from_utf8(base);
            for (int n = 2; std::filesystem::exists(target, ec); n++) {
                target = parent / blb::path_from_utf8(base + "-" + std::to_string(n));
            }
            return target;
        }

        struct Job {
            std::string key;
            std::filesystem::path root;
            std::string id;
            std::filesystem::path gameDirectory;
            std::filesystem::path target;
            Options options;
            int memoryMb = 4096;
            std::optional<std::filesystem::path> instanceJava;
            std::optional<std::vector<blb::java::java_installation>> candidates;
            blb::java::scan_options scan;
        };

        std::string script(const Job& job, const blb::install::server_result& result, const std::string& java,
                           bool windows) {
            std::string jvm = result.user_jvm_args ? std::string("@user_jvm_args.txt")
                                                   : "-Xmx" + std::to_string(job.memoryMb) + "M" +
                            (job.options.jvmArgs.empty() ? "" : " " + job.options.jvmArgs);
            std::string launch = blb::join(windows ? result.windows : result.unix, " ");
            if (windows) {
                return "@echo off\r\nchcp 65001 > nul\r\ncd /d \"%~dp0\"\r\n\"" + java + "\" " + jvm + " " + launch +
                        " nogui %*\r\npause\r\n";
            }
            return "#!/usr/bin/env sh\ncd \"$(dirname \"$0\")\"\nexec \"" + java + "\" " + jvm + " " + launch +
                    " nogui \"$@\"\n";
        }

        std::string build(const Job& job, blb::cancellation_token token) {
            say("正在读取实例");
            auto resolved = blb::meta::resolve_version(blb::meta::game_layout(job.root), job.id);
            if (!resolved) {
                return resolved.err().message;
            }
            std::optional<std::filesystem::path> instanceJava = job.instanceJava;
            auto candidates = job.candidates;
            auto scan = job.scan;
            blb::install::java_resolver resolver =
                    [instanceJava, candidates,
                     scan](const blb::meta::version& version) -> blb::result<blb::java::java_installation> {
                if (instanceJava) {
                    if (auto probed = blb::java::probe_java(*instanceJava)) {
                        return *probed;
                    }
                }
                auto requirement = blb::java::requirement_for(version);
                if (candidates) {
                    if (auto chosen = blb::java::select_java(*candidates, requirement)) {
                        return *chosen;
                    }
                }
                auto found = blb::java::scan_java(scan);
                if (auto chosen = blb::java::select_java(found, requirement)) {
                    return *chosen;
                }
                return blb::make_error(blb::errc::java_not_found, "没有可用的 Java");
            };
            blb::install::server_options options;
            options.install.mirror = DownloadSources::provider();
            options.install.java_scan = scan;
            options.install.java_candidates = candidates;
            options.client_root = job.root;
            options.java = resolver;
            blb::function_progress_sink sink(
                    [](const blb::progress_event& event) {
                        bool download = event.task.rfind("download:", 0) == 0;
                        if (download || event.task.rfind("verify:", 0) == 0) {
                            Transfers::report(transfer, event);
                        }
                        if (download && event.total_bytes > 0) {
                            char text[64];
                            std::snprintf(text, sizeof(text), "正在下载 %d%%",
                                          (int) (event.done_bytes * 100 / event.total_bytes));
                            show(text);
                        }
                    },
                    [](std::string_view task, std::string_view stage) {
                        if (stage == "start" && task.rfind("processor", 0) == 0) {
                            say("正在处理加载器文件");
                        }
                    });
            say("正在安装服务端");
            auto result = blb::install::install_server(*resolved, job.target, options, &sink, token);
            if (!result) {
                return token.is_cancelled() ? std::string() : result.err().message;
            }
            std::vector<std::string> files =
                    blb::modpack::collect_picked_files(job.gameDirectory, job.options.picks, job.id);
            std::error_code ec;
            for (size_t i = 0; i < files.size(); i++) {
                if (token.is_cancelled()) {
                    return {};
                }
                if (i % 16 == 0) {
                    say("正在复制文件 " + std::to_string(i) + "/" + std::to_string(files.size()));
                }
                std::filesystem::path to = job.target / blb::path_from_utf8(files[i]);
                std::filesystem::create_directories(to.parent_path(), ec);
                std::filesystem::copy_file(job.gameDirectory / blb::path_from_utf8(files[i]), to,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    return "无法复制 " + files[i];
                }
                copied(to);
                Transfers::fraction(transfer, (float) (i + 1) / (float) files.size());
            }
            if (!job.options.world.empty() && blb::fs::safe_component(job.options.world)) {
                say("正在复制世界");
                std::filesystem::path from = job.gameDirectory / "saves" / blb::path_from_utf8(job.options.world);
                for (std::filesystem::recursive_directory_iterator it(from, ec), end; !ec && it != end;
                     it.increment(ec)) {
                    if (!it->is_regular_file(ec) || it->path().filename() == "session.lock") {
                        continue;
                    }
                    std::filesystem::path to = job.target / "world" / std::filesystem::relative(it->path(), from, ec);
                    std::filesystem::create_directories(to.parent_path(), ec);
                    std::filesystem::copy_file(it->path(), to, std::filesystem::copy_options::overwrite_existing, ec);
                    copied(to);
                }
                if (ec) {
                    return "无法复制世界 " + job.options.world;
                }
            }
            std::string properties = "server-port=" + std::to_string(job.options.port) + "\n" +
                    "online-mode=" + (job.options.onlineMode ? "true" : "false") + "\n" +
                    "max-players=" + std::to_string(job.options.maxPlayers) + "\n";
            if (!job.options.motd.empty()) {
                properties += "motd=" + propertyValue(job.options.motd) + "\n";
            }
            if (!job.options.world.empty()) {
                properties += "level-name=world\n";
            }
            if (!blb::fs::write_file(job.target / "server.properties", properties)) {
                return "无法写入 server.properties";
            }
            if (!blb::fs::write_file(job.target / "eula.txt", "eula=true\n")) {
                return "无法写入 eula.txt";
            }
            if (result->user_jvm_args) {
                std::string args = "-Xmx" + std::to_string(job.memoryMb) + "M\n";
                if (!job.options.jvmArgs.empty()) {
                    args += job.options.jvmArgs + "\n";
                }
                if (!blb::fs::write_file(job.target / "user_jvm_args.txt", args)) {
                    return "无法写入 user_jvm_args.txt";
                }
            }
            std::string local = "java";
            if (job.options.java == "custom" && !job.options.javaPath.empty()) {
                local = job.options.javaPath;
            } else if (job.options.java == "instance") {
                if (auto java = resolver(*resolved)) {
                    local = blb::path_to_utf8(java->executable);
                }
            }
#if defined(_WIN32)
            std::string bat = script(job, *result, local, true);
            std::string sh = script(job, *result, "java", false);
#else
            std::string bat = script(job, *result, "java", true);
            std::string sh = script(job, *result, local, false);
#endif
            if (!blb::fs::write_file(job.target / "run.bat", bat) || !blb::fs::write_file(job.target / "run.sh", sh)) {
                return "无法写入启动脚本";
            }
            (void) blb::fs::set_executable(job.target / "run.sh");
            return {};
        }

        struct Finished {
            std::filesystem::path target;
            std::string error;
            bool cancelled = false;
        };

        void complete(const Finished& finished) {
            busy = false;
            busyKey.clear();
            Transfers::finish(transfer,
                              finished.cancelled               ? Transfers::State::Cancelled
                                      : finished.error.empty() ? Transfers::State::Done
                                                               : Transfers::State::Failed,
                              finished.error);
            if (finished.cancelled) {
                return;
            }
            if (!finished.error.empty()) {
                Notices::add(Notices::Kind::Failed, "生成服务端失败：" + finished.error, 6000);
                return;
            }
            Notices::add(Notices::Kind::Done, "已生成服务端 " + blb::path_to_utf8(finished.target.filename()), 4000);
            Platform::reveal(finished.target);
        }
    }

    Options& options(const std::string& instanceKey) {
        auto found = remembered.find(instanceKey);
        if (found != remembered.end()) {
            return found->second;
        }
        Options loaded;
        const nlohmann::json& data = Config::data();
        auto saved = data.find("serverOptions");
        if (saved != data.end() && saved->is_object()) {
            auto mine = saved->find(instanceKey);
            if (mine != saved->end() && mine->is_object()) {
                loaded = fromJson(*mine);
            }
        }
        return remembered[instanceKey] = loaded;
    }

    void save(const std::string& instanceKey) {
        auto found = remembered.find(instanceKey);
        if (found == remembered.end()) {
            return;
        }
        Config::data()["serverOptions"][instanceKey] = toJson(found->second);
        Config::save();
    }

    FileTree::Tree& tree(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr) {
            return files;
        }
        files.reset(entry->gameDirectory, entry->info.id);
        Options& o = options(instanceKey);
        if (!o.picked) {
            std::error_code ec;
            for (const char* name : DEFAULT_PICKS) {
                if (std::filesystem::exists(entry->gameDirectory / name, ec)) {
                    o.picks[name] = true;
                }
            }
            o.picked = true;
        }
        return files;
    }

    void open(const std::string& instanceKey) {
        tree(instanceKey).refresh();
        applyDetected(instanceKey);
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr || detectingKeys.count(instanceKey) != 0) {
            return;
        }
        detectingKeys.insert(instanceKey);
        std::filesystem::path gameDirectory = entry->gameDirectory;
        detections.run(
                [instanceKey, gameDirectory](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    auto found = blb::modpack::detect_client_only(gameDirectory, *DownloadSources::provider(), token);
                    std::set<std::string> client;
                    if (found) {
                        client.insert(found->begin(), found->end());
                    }
                    return [instanceKey, client] {
                        detectingKeys.erase(instanceKey);
                        detected[instanceKey] = client;
                        applyDetected(instanceKey);
                    };
                },
                [instanceKey](const std::string&) -> AsyncTasks::Continuation {
                    return [instanceKey] { detectingKeys.erase(instanceKey); };
                });
    }

    bool detecting(const std::string& instanceKey) {
        return detectingKeys.count(instanceKey) != 0;
    }

    bool clientOnly(const std::string& instanceKey, const std::string& path) {
        auto found = detected.find(instanceKey);
        return found != detected.end() && found->second.count(path) != 0;
    }

    void picked(const std::string& instanceKey, const std::string& path) {
        auto found = detected.find(instanceKey);
        if (found != detected.end()) {
            Options& o = options(instanceKey);
            std::string prefix = path + "/";
            for (const std::string& file : found->second) {
                if (file.rfind(prefix, 0) == 0 && blb::modpack::pick_state(o.picks, file)) {
                    o.picks[file] = false;
                }
            }
        }
        save(instanceKey);
    }

    std::vector<std::string> worlds(const Instances::Entry& entry) {
        std::vector<std::string> out;
        for (const auto& [name, st] : blb::fs::list_directory(entry.gameDirectory / "saves")) {
            if (st.is_directory && !st.is_link) {
                out.push_back(name);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string loaderText(const Instances::Entry& entry) {
        for (const blb::install::loader_info& loader : entry.info.loaders) {
            switch (loader.type) {
                case blb::install::loader_type::forge:
                case blb::install::loader_type::neoforge:
                case blb::install::loader_type::cleanroom:
                case blb::install::loader_type::fabric:
                case blb::install::loader_type::legacy_fabric:
                case blb::install::loader_type::quilt:
                    return Modrinth::label(std::string(blb::install::loader_name(loader.type))) + " " + loader.version;
                default: break;
            }
        }
        return "原版";
    }

    bool supported(const Instances::Entry& entry) {
        for (const blb::install::loader_info& loader : entry.info.loaders) {
            if (loader.type == blb::install::loader_type::cleanroom) {
                return false;
            }
        }
        return entry.info.valid;
    }

    int defaultMemory(const Instances::Entry& entry) {
        return entry.info.settings.max_memory_mb > 0 ? (int) entry.info.settings.max_memory_mb : 4096;
    }

    void start(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (busy || source == nullptr || !supported(*entry)) {
            return;
        }
        tree(instanceKey);
        auto job = std::make_shared<Job>();
        job->key = instanceKey;
        job->root = source->root;
        job->id = entry->info.id;
        job->gameDirectory = entry->gameDirectory;
        job->options = options(instanceKey);
        job->memoryMb = job->options.memoryMb > 0 ? job->options.memoryMb : defaultMemory(*entry);
        job->instanceJava = entry->info.settings.java;
        job->candidates = Catalog::javaCandidates();
        job->scan = Catalog::javaScanOptions();
        std::filesystem::path start = lastFolder.empty() ? Platform::desktopDirectory() : lastFolder;
        std::optional<std::filesystem::path> parent = Platform::pickFolder(start, L"选择服务端的存放位置");
        if (!parent || busy) {
            return;
        }
        lastFolder = *parent;
        std::string base = blb::fs::filter_filename(entry->info.id + "-server", '_');
        job->target = freeFolder(*parent, base.empty() ? std::string("server") : base);
        busy = true;
        busyKey = instanceKey;
        Transfers::Spec spec;
        spec.kind = Transfers::Kind::Server;
        spec.title = entry->info.id + " 服务端";
        spec.minecraft = entry->info.minecraft_version;
        spec.folder = job->target;
        spec.cancel = [] { ServerBuilds::cancel(); };
        transfer = Transfers::begin(std::move(spec));
        say("正在准备");
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::filesystem::path target = job->target;
        tasks.run(
                [job, token](blb::cancellation_token) -> AsyncTasks::Continuation {
                    std::string error = build(*job, token);
                    if (!error.empty() || token.is_cancelled()) {
                        std::error_code ec;
                        std::filesystem::remove_all(job->target, ec);
                    }
                    Finished finished{job->target, error, token.is_cancelled()};
                    return [finished] { complete(finished); };
                },
                [target, token](const std::string& error) -> AsyncTasks::Continuation {
                    Finished finished{target, error, token.is_cancelled()};
                    return [finished] { complete(finished); };
                });
    }

    void cancel() {
        if (busy && cancelling) {
            cancelling->cancel();
        }
    }

    bool running() {
        return busy;
    }

    const std::string& runningKey() {
        return busyKey;
    }

    std::string status() {
        std::lock_guard<std::mutex> guard(progressLock);
        return progress;
    }

    void poll() {
        tasks.poll();
        detections.poll();
    }

    void shutdown() {
        cancel();
        tasks.stop();
        detections.stop();
    }
}
