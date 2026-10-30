#include "app/instances/Exports.h"
#include "app/accounts/Accounts.h"
#include "app/instances/Instances.h"
#include "app/system/Config.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/cancellation.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>
#include <core/network/mirror_provider.hpp>

#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace Exports {

    namespace blb = b_launcher_base;

    namespace {
        std::map<std::string, Options> remembered;

        std::thread worker;
        std::shared_ptr<blb::cancellation_source> cancelling;
        bool busy = false;
        std::string busyKey;
        std::mutex lock;
        bool done = false;
        std::string failure;
        bool cancelled = false;
        std::filesystem::path written;
        std::string progress;

        std::string fileName(const Options& options) {
            std::string base = options.name.empty() ? std::string("modpack") : options.name;
            if (!options.version.empty()) {
                base += "-" + options.version;
            }
            std::string out;
            for (char c : base) {
                if (std::string("\\/:*?\"<>|").find(c) == std::string::npos && (unsigned char) c >= 32) {
                    out += c;
                }
            }
            return out + ".mrpack";
        }
    }

    Options& options(const std::string& instanceKey) {
        auto found = remembered.find(instanceKey);
        if (found != remembered.end()) {
            return found->second;
        }
        Options options;
        if (const Instances::Entry* entry = Instances::find(instanceKey)) {
            options.name = entry->info.id;
        }
        if (const blb::auth::account* account = Accounts::current()) {
            options.author = account->name;
        }
        const nlohmann::json& data = Config::data();
        auto saved = data.find("exportOptions");
        if (saved != data.end() && saved->is_object()) {
            auto mine = saved->find(instanceKey);
            if (mine != saved->end() && mine->is_object()) {
                auto parsed = blb::modpack::parse_export_options(mine->dump());
                if (parsed) {
                    options = *parsed;
                }
            }
        }
        return remembered[instanceKey] = options;
    }

    void save(const std::string& instanceKey) {
        auto found = remembered.find(instanceKey);
        if (found == remembered.end()) {
            return;
        }
        nlohmann::json parsed =
                nlohmann::json::parse(blb::modpack::serialize_export_options(found->second), nullptr, false);
        if (parsed.is_object()) {
            Config::data()["exportOptions"][instanceKey] = parsed;
            Config::save();
        }
    }

    void start(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (busy || source == nullptr) {
            return;
        }
        // The instances may be read again while the file dialog is open, so what is needed is copied before it.
        std::filesystem::path root = source->root;
        std::string id = entry->info.id;
        std::filesystem::path gameDirectory = entry->gameDirectory;
        Options chosen = options(instanceKey);
        std::optional<std::filesystem::path> picked = Platform::pickSavePack(blb::path_from_utf8(fileName(chosen)));
        if (!picked || busy) {
            return;
        }
        std::filesystem::path output = *picked;
        if (worker.joinable()) {
            worker.join();
        }
        busy = true;
        busyKey = instanceKey;
        {
            std::lock_guard<std::mutex> guard(lock);
            done = false;
            failure.clear();
            cancelled = false;
            progress = "正在准备";
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        worker = std::thread([root, id, gameDirectory, output, chosen, token] {
            std::string error;
            bool wasCancelled = false;
            try {
                blb::function_progress_sink sink([](const blb::progress_event& event) {
                    char text[64];
                    if (event.task == "export:hash") {
                        std::snprintf(text, sizeof(text), "正在查找文件来源 %llu/%llu",
                                      (unsigned long long) event.done_items, (unsigned long long) event.total_items);
                    } else if (event.total_bytes > 0) {
                        std::snprintf(text, sizeof(text), "正在打包 %d%%",
                                      (int) (event.done_bytes * 100 / event.total_bytes));
                    } else {
                        std::snprintf(text, sizeof(text), "正在打包");
                    }
                    std::lock_guard<std::mutex> guard(lock);
                    progress = text;
                });
                blb::install::install_options installOptions;
                installOptions.mirror = blb::net::make_bmclapi_provider();
                blb::install::game_installer installer(blb::meta::game_layout(root), installOptions);
                auto report = blb::modpack::export_modpack(installer, id, gameDirectory, output, chosen, &sink, token);
                if (!report) {
                    error = report.err().message;
                    wasCancelled = report.err().code == blb::errc::cancelled;
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (!error.empty()) {
                std::error_code removeError;
                std::filesystem::remove(output, removeError);
            }
            std::lock_guard<std::mutex> guard(lock);
            failure = error;
            cancelled = wasCancelled || token.is_cancelled();
            written = output;
            done = true;
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
        std::lock_guard<std::mutex> guard(lock);
        return progress;
    }

    void poll() {
        if (!busy) {
            return;
        }
        std::string error;
        bool wasCancelled;
        std::filesystem::path file;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!done) {
                return;
            }
            error = failure;
            wasCancelled = cancelled;
            file = written;
        }
        if (worker.joinable()) {
            worker.join();
        }
        busy = false;
        busyKey.clear();
        if (wasCancelled) {
            return;
        }
        if (!error.empty()) {
            Notices::add(Notices::Kind::Failed, "导出失败：" + error, 5000);
            return;
        }
        Notices::add(Notices::Kind::Done, "已导出 " + blb::path_to_utf8(file.filename()), 4000);
        Platform::reveal(file);
    }

    void shutdown() {
        cancel();
        if (worker.joinable()) {
            worker.join();
        }
    }
}
