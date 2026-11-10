#include "app/system/LauncherBuilder.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/DownloadSources.h"
#include "app/instances/Exports.h"
#include "app/instances/Instances.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Config.h"
#include "app/system/Customization.h"
#include "app/system/IconFile.h"
#include "app/system/Notices.h"
#include "app/system/Pixels.h"
#include "app/system/Platform.h"

#include <core/base/fs.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>

#include <cstdio>
#include <mutex>

namespace LauncherBuilder {

    namespace blb = b_launcher_base;

    namespace {
        const char* const SETTINGS[] = {"accent",
                                        "themeLayout",
                                        "themeSettings",
                                        "themeAnimation",
                                        "themeFrameCap",
                                        "themeFontScale",
                                        "themeShaders",
                                        "uiSounds",
                                        "uiStyle",
                                        "zoom",
                                        "compactMode",
                                        "systemFrame",
                                        "backgroundImage",
                                        "isolation",
                                        "jvmArgs",
                                        "gameArgs",
                                        "patches",
                                        "offlineLaunch",
                                        "showLogs",
                                        "logWrapLines",
                                        "logMergeRepeats",
                                        "allowMultipleInstances",
                                        "downloadSources",
                                        "systemProxy",
                                        "modNames",
                                        "homeWidgets",
                                        "homepageVariables",
                                        "rememberWindow"};

        Project current;
        bool loaded = false;

        AsyncTasks tasks;
        std::shared_ptr<blb::cancellation_source> cancelling;
        bool busy = false;
        std::mutex progressLock;
        std::string progress;

        std::string text(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        bool flag(const nlohmann::json& doc, const char* key, bool fallback) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_boolean() ? it->get<bool>() : fallback;
        }

        Project fromJson(const nlohmann::json& doc) {
            Project p;
            if (!doc.is_object()) {
                return p;
            }
            p.by = text(doc, "by");
            p.version = doc.contains("version") ? text(doc, "version") : p.version;
            p.theme = text(doc, "theme");
            p.themeLock = flag(doc, "themeLock", false);
            p.windowTitle = text(doc, "windowTitle");
            p.brandTitle = text(doc, "brandTitle");
            p.logoFile = text(doc, "logoFile");
            p.iconFile = text(doc, "iconFile");
            p.homepage = text(doc, "homepage");
            p.homepageUrl = text(doc, "homepageUrl");
            p.homepagePreset = text(doc, "homepagePreset");
            p.homepageFile = text(doc, "homepageFile");
            p.homepageLock = flag(doc, "homepageLock", false);
            p.announcement = text(doc, "announcement");
            if (auto links = doc.find("links"); links != doc.end() && links->is_array()) {
                for (const nlohmann::json& link : *links) {
                    p.links.push_back({text(link, "label"), text(link, "url")});
                }
            }
            if (auto servers = doc.find("servers"); servers != doc.end() && servers->is_array()) {
                for (const nlohmann::json& server : *servers) {
                    p.servers.push_back({text(server, "name"), text(server, "address")});
                }
            }
            p.autoJoin = text(doc, "autoJoin");
            p.microsoft = flag(doc, "microsoft", true);
            p.yggdrasil = flag(doc, "yggdrasil", true);
            p.offline = flag(doc, "offline", true);
            p.yggdrasilServer = text(doc, "yggdrasilServer");
            if (auto hidden = doc.find("hidden"); hidden != doc.end() && hidden->is_array()) {
                for (const nlohmann::json& item : *hidden) {
                    if (item.is_string()) {
                        p.hidden.push_back(item.get<std::string>());
                    }
                }
            }
            p.featuresLock = flag(doc, "featuresLock", false);
            p.copySettings = flag(doc, "copySettings", false);
            p.settingsLock = flag(doc, "settingsLock", false);
            p.updates = flag(doc, "updates", true);
            return p;
        }

        nlohmann::json toJson(const Project& p) {
            nlohmann::json links = nlohmann::json::array();
            for (const Link& link : p.links) {
                links.push_back({{"label", link.label}, {"url", link.url}});
            }
            nlohmann::json servers = nlohmann::json::array();
            for (const Server& server : p.servers) {
                servers.push_back({{"name", server.name}, {"address", server.address}});
            }
            return {{"by", p.by},
                    {"version", p.version},
                    {"theme", p.theme},
                    {"themeLock", p.themeLock},
                    {"windowTitle", p.windowTitle},
                    {"brandTitle", p.brandTitle},
                    {"logoFile", p.logoFile},
                    {"iconFile", p.iconFile},
                    {"homepage", p.homepage},
                    {"homepageUrl", p.homepageUrl},
                    {"homepagePreset", p.homepagePreset},
                    {"homepageFile", p.homepageFile},
                    {"homepageLock", p.homepageLock},
                    {"announcement", p.announcement},
                    {"links", links},
                    {"servers", servers},
                    {"autoJoin", p.autoJoin},
                    {"microsoft", p.microsoft},
                    {"yggdrasil", p.yggdrasil},
                    {"offline", p.offline},
                    {"yggdrasilServer", p.yggdrasilServer},
                    {"hidden", p.hidden},
                    {"featuresLock", p.featuresLock},
                    {"copySettings", p.copySettings},
                    {"settingsLock", p.settingsLock},
                    {"updates", p.updates}};
        }

        std::string extensionOf(const std::string& file) {
            return blb::to_lower(blb::path_to_utf8(blb::path_from_utf8(file).extension()));
        }

        nlohmann::json describe(const Job& job, const std::string& logo, const std::string& icon,
                                const std::string& iconImage) {
            const Project& p = job.project;
            nlohmann::json doc = nlohmann::json::object();
            if (!p.by.empty()) {
                doc["by"] = p.by;
            }
            if (!p.version.empty()) {
                doc["version"] = p.version;
            }
            if (!p.theme.empty()) {
                doc["theme"] = {{"id", p.theme}, {"lock", p.themeLock}};
            }
            nlohmann::json branding = nlohmann::json::object();
            if (!p.windowTitle.empty()) {
                branding["windowTitle"] = p.windowTitle;
            }
            if (!p.brandTitle.empty()) {
                branding["brandTitle"] = p.brandTitle;
            }
            if (!logo.empty()) {
                branding["logo"] = logo;
            }
            if (!icon.empty()) {
                branding["icon"] = icon;
                branding["iconImage"] = iconImage;
            }
            if (!branding.empty()) {
                doc["branding"] = branding;
            }
            nlohmann::json home = nlohmann::json::object();
            if (p.homepage == "url" && !p.homepageUrl.empty()) {
                home["type"] = "url";
                home["value"] = p.homepageUrl;
            } else if (p.homepage == "preset" && !p.homepagePreset.empty()) {
                home["type"] = "preset";
                home["value"] = p.homepagePreset;
            } else if (p.homepage == "file" && !p.homepageFile.empty()) {
                home["type"] = "file";
                home["value"] = "home.xaml";
            } else if (p.homepage == "announcement" || p.homepage == "none") {
                home["type"] = p.homepage;
            }
            if (home.contains("type") && p.homepageLock) {
                home["lock"] = true;
            }
            if (!p.announcement.empty()) {
                home["announcement"] = p.announcement;
            }
            nlohmann::json links = nlohmann::json::array();
            for (const Link& link : p.links) {
                if (!link.url.empty()) {
                    links.push_back({{"label", link.label.empty() ? link.url : link.label}, {"url", link.url}});
                }
            }
            if (!links.empty()) {
                home["links"] = links;
            }
            if (!home.empty()) {
                doc["homepage"] = home;
            }
            nlohmann::json servers = nlohmann::json::array();
            bool joinListed = p.autoJoin == "choose";
            for (const Server& server : p.servers) {
                if (!server.address.empty()) {
                    servers.push_back({{"name", server.name.empty() ? server.address : server.name},
                                       {"address", server.address}});
                    joinListed = joinListed || server.address == p.autoJoin;
                }
            }
            if (!servers.empty()) {
                doc["servers"] = servers;
                if (joinListed) {
                    doc["autoJoin"] = p.autoJoin;
                }
            }
            bool everyLogin = p.microsoft && p.yggdrasil && p.offline && p.yggdrasilServer.empty();
            if (!everyLogin) {
                nlohmann::json logins = nlohmann::json::array();
                if (p.microsoft) {
                    logins.push_back("microsoft");
                }
                if (p.yggdrasil) {
                    if (p.yggdrasilServer.empty()) {
                        logins.push_back("yggdrasil");
                    } else {
                        logins.push_back({{"type", "yggdrasil"}, {"server", p.yggdrasilServer}});
                    }
                }
                if (p.offline) {
                    logins.push_back("offline");
                }
                if (!logins.empty()) {
                    doc["logins"] = logins;
                }
            }
            if (job.modpack) {
                doc["modpack"] = {{"source", "modpack.mrpack"}, {"instance", job.instanceName}, {"single", job.single}};
            }
            if (!p.hidden.empty()) {
                doc["features"] = {{"hidden", p.hidden}, {"lock", p.featuresLock}};
            }
            if (job.settings.is_object() && !job.settings.empty()) {
                nlohmann::json settings = nlohmann::json::object();
                for (auto it = job.settings.begin(); it != job.settings.end(); ++it) {
                    settings[it.key()] = {{"value", it.value()}, {"lock", p.settingsLock}};
                }
                doc["settings"] = settings;
            }
            if (!p.updates) {
                doc["updates"] = false;
            }
            return doc;
        }

        struct Finished {
            std::filesystem::path file;
            std::string error;
            bool cancelled = false;
        };

        void complete(const Finished& finished) {
            busy = false;
            if (finished.cancelled) {
                return;
            }
            if (!finished.error.empty()) {
                Notices::add(Notices::Kind::Failed, "生成定制启动器失败：" + finished.error, 5000);
                return;
            }
            Notices::add(Notices::Kind::Done, "已生成 " + blb::path_to_utf8(finished.file.filename()), 4000);
            Platform::reveal(finished.file);
        }
    }

    Project& project() {
        if (!loaded) {
            loaded = true;
            auto saved = Config::data().find("launcherBuilder");
            if (saved != Config::data().end()) {
                current = fromJson(*saved);
            }
        }
        return current;
    }

    void save() {
        Config::data()["launcherBuilder"] = toJson(project());
        Config::save();
    }

    std::string suggestedName() {
        const Project& p = project();
        std::string base = !p.windowTitle.empty() ? p.windowTitle : !p.brandTitle.empty() ? p.brandTitle : "";
        std::string name = blb::fs::filter_filename(base, '_');
        return name.empty() ? std::string("b-launcher-custom") : name;
    }

    std::string problem() {
        const Project& p = project();
        if (!p.microsoft && !p.yggdrasil && !p.offline) {
            return "没有开启任何登录方式";
        }
        if (p.homepage == "url" && p.homepageUrl.empty()) {
            return "主页网址为空";
        }
        if (p.homepage == "file" && p.homepageFile.empty()) {
            return "没有选择主页文件";
        }
        return {};
    }

    std::shared_ptr<Job> prepare(ThemeFiles themes) {
        auto job = std::make_shared<Job>();
        job->project = project();
        job->themes = std::move(themes);
        job->settings = nlohmann::json::object();
        if (job->project.copySettings) {
            const nlohmann::json& data = Config::data();
            for (const char* key : SETTINGS) {
                auto it = data.find(key);
                if (it != data.end()) {
                    job->settings[key] = *it;
                }
            }
        }
        return job;
    }

    bool attachModpack(Job& job, const std::string& instanceKey, const std::string& instanceName) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (source == nullptr || !entry->info.valid) {
            return false;
        }
        job.modpack = true;
        job.root = source->root;
        job.instanceId = entry->info.id;
        job.gameDirectory = entry->gameDirectory;
        job.pack = Exports::options(instanceKey);
        job.pack.curseforge.api_key = CurseForge::apiKey();
        job.instanceName = !instanceName.empty() ? instanceName
                : !job.pack.name.empty()         ? job.pack.name
                                                 : entry->info.id;
        return true;
    }

    std::string run(const Job& job, const std::function<void(const std::string&)>& say, blb::cancellation_token token) {
        std::error_code ec;
        std::filesystem::path temp =
                std::filesystem::temp_directory_path(ec) / ("blbbuild-" + blb::random_bytes_hex(6));
        std::filesystem::create_directories(temp, ec);
        if (ec) {
            return "无法创建临时文件夹";
        }
        struct Cleanup {
            std::filesystem::path folder;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(folder, ignored);
            }
        } cleanup{temp};
        const Project& p = job.project;
        std::filesystem::path modpack = temp / "modpack.mrpack";
        if (job.modpack) {
            say("正在导出整合包");
            blb::function_progress_sink sink([&say](const blb::progress_event& event) {
                char line[64];
                if (event.task == "export:hash") {
                    std::snprintf(line, sizeof(line), "正在查找文件来源 %llu/%llu",
                                  (unsigned long long) event.done_items, (unsigned long long) event.total_items);
                } else {
                    std::snprintf(line, sizeof(line), "正在导出整合包");
                }
                say(line);
            });
            blb::install::install_options options;
            options.mirror = DownloadSources::provider();
            blb::install::game_installer installer(blb::meta::game_layout(job.root), options);
            auto report = blb::modpack::export_modpack(installer, job.instanceId, job.gameDirectory, modpack, job.pack,
                                                       &sink, token);
            if (!report) {
                return token.is_cancelled() ? std::string() : report.err().message;
            }
        }
        if (token.is_cancelled()) {
            return {};
        }
        say("正在打包");
        std::filesystem::path pack = temp / "customize.blbpack";
        std::string ico;
        {
            auto writer = blb::zip_writer::create(pack);
            if (!writer) {
                return "无法创建定制包";
            }
            std::string logo;
            if (!p.logoFile.empty()) {
                logo = "assets/logo" + extensionOf(p.logoFile);
                if (!writer->add_file(logo, blb::path_from_utf8(p.logoFile))) {
                    return "无法读取 Logo 图片";
                }
            }
            std::string icon;
            std::string iconImage;
            if (!p.iconFile.empty()) {
                auto bytes = blb::fs::read_file(blb::path_from_utf8(p.iconFile));
                if (!bytes || bytes->empty()) {
                    return "无法读取程序图标";
                }
                std::string ext = extensionOf(p.iconFile);
                if (ext == ".ico") {
                    ico = *bytes;
                } else if (auto image = Pixels::decode(bytes->data(), bytes->size())) {
                    ico = IconFile::ico(image->rgba, image->width, image->height);
                }
                if (ico.empty()) {
                    return "程序图标不是有效的图片";
                }
                icon = "assets/icon.ico";
                iconImage = ext == ".ico" ? icon : "assets/icon-image" + ext;
                if (!writer->add_buffer(icon, ico) || (iconImage != icon && !writer->add_buffer(iconImage, *bytes))) {
                    return "无法写入程序图标";
                }
            }
            if (p.homepage == "file" && !p.homepageFile.empty() &&
                !writer->add_file("home.xaml", blb::path_from_utf8(p.homepageFile))) {
                return "无法读取主页文件";
            }
            for (const auto& [path, data] : job.themes) {
                if (!blb::fs::safe_relative(path) || !writer->add_buffer(path, data)) {
                    return "无法写入主题文件";
                }
            }
            if (job.modpack && !writer->add_file("modpack.mrpack", modpack)) {
                return "无法写入整合包";
            }
            if (!writer->add_buffer("customize.json", describe(job, logo, icon, iconImage).dump(2)) ||
                !writer->close()) {
                return "无法写入定制包";
            }
        }
        if (token.is_cancelled()) {
            return {};
        }
        say("正在写入启动器");
        std::string error;
        if (!job.zip) {
            if (!Customization::writeProgram(pack, job.output, ico, error)) {
                return error;
            }
            return {};
        }
        std::filesystem::path partial = blb::fs::temp_sibling(job.output);
        {
            auto writer = blb::zip_writer::create(partial);
            if (!writer) {
                return "无法写入 " + blb::path_to_utf8(job.output);
            }
            std::string prefix = job.folder.empty() ? std::string() : job.folder + "/";
            std::string name = job.folder.empty() ? std::string("b-launcher") : job.folder;
            if (!Customization::addProgram(*writer, prefix, name, pack, ico, error) || !writer->close()) {
                std::filesystem::remove(partial, ec);
                return error.empty() ? "无法写入 " + blb::path_to_utf8(job.output) : error;
            }
        }
        if (!blb::fs::rename_file(partial, job.output)) {
            std::filesystem::remove(partial, ec);
            return "无法写入 " + blb::path_to_utf8(job.output);
        }
        return {};
    }

    void start(ThemeFiles themes) {
        if (busy) {
            return;
        }
        if (std::string issue = problem(); !issue.empty()) {
            Notices::add(Notices::Kind::Failed, "无法生成自定义启动器：" + issue, 5000);
            return;
        }
        std::shared_ptr<Job> job = prepare(std::move(themes));
        std::filesystem::path suggested =
                Platform::desktopDirectory() / blb::path_from_utf8(suggestedName() + Customization::programExtension());
        std::optional<std::filesystem::path> picked = Platform::pickSaveAs(suggested);
        if (!picked || busy) {
            return;
        }
        job->output = *picked;
        busy = true;
        {
            std::lock_guard<std::mutex> guard(progressLock);
            progress = "正在准备";
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::filesystem::path output = job->output;
        tasks.run(
                [job, token](blb::cancellation_token) -> AsyncTasks::Continuation {
                    std::string error = run(
                            *job,
                            [](const std::string& line) {
                                std::lock_guard<std::mutex> guard(progressLock);
                                progress = line;
                            },
                            token);
                    Finished finished{job->output, error, token.is_cancelled()};
                    return [finished] { complete(finished); };
                },
                [output, token](const std::string& error) -> AsyncTasks::Continuation {
                    Finished finished{output, error, token.is_cancelled()};
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

    std::string status() {
        std::lock_guard<std::mutex> guard(progressLock);
        return progress;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        cancel();
        tasks.stop();
    }
}
