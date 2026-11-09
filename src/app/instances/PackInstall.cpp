#include "app/instances/PackInstall.h"

#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/instances/ModpackSetup.h"
#include "app/system/Config.h"
#include "app/system/Customization.h"
#include "app/system/Notices.h"

#include <core/base/json.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/modpack/modpack.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace PackInstall {

    namespace blb = b_launcher_base;

    namespace {
        bool started = false;
        bool chosen = false;

        const Instances::Entry* named(const std::string& name) {
            for (const Instances::Entry& entry : Instances::list()) {
                if (entry.info.id == name) {
                    return &entry;
                }
            }
            return nullptr;
        }

        void install(const std::string& name) {
            std::optional<std::string> data = Customization::read(Customization::modpack());
            if (!data) {
                Notices::add(Notices::Kind::Failed, "定制包里缺少整合包 " + Customization::modpack(), 5000);
                return;
            }
            std::error_code ec;
            std::filesystem::path folder = blb::launcher_data_directory() / "customize";
            std::filesystem::create_directories(folder, ec);
            std::filesystem::path file = folder / std::filesystem::path(Customization::modpack()).filename();
            {
                std::ofstream out(file, std::ios::binary | std::ios::trunc);
                out.write(data->data(), (std::streamsize) data->size());
            }
            auto manifest = blb::modpack::read_modpack(file);
            if (!manifest) {
                Notices::add(Notices::Kind::Failed, "无法识别定制包里的整合包", 5000);
                return;
            }
            ModpackSetup setup = ModpackSetup::fromFile(file, *manifest);
            const Instances::Source* source = Instances::installSource(std::string());
            if (source == nullptr) {
                Notices::add(Notices::Kind::Failed, "没有可以安装整合包的游戏目录", 5000);
                return;
            }
            std::string id =
                    Instances::checkInstallId(source->key, name).empty() ? name : Instances::freeId(source->key, name);
            Installer::startModpack(source->key, setup.pack, id);
            Config::data()["packModpackDone"] = true;
            Config::save();
            Notices::add(Notices::Kind::Info, "正在安装整合包 " + id, 4000);
        }
    }

    void poll() {
        if (!Customization::active() || Instances::loading()) {
            return;
        }
        std::string name = Customization::modpackInstance();
        if (name.empty()) {
            return;
        }
        if (Customization::singleInstance() && !chosen) {
            if (const Instances::Entry* entry = named(name)) {
                if (Instances::selected() != entry->key) {
                    Instances::select(entry->key);
                }
                chosen = true;
            }
        }
        if (started || Customization::modpack().empty()) {
            return;
        }
        if (blb::json_bool(Config::data(), "packModpackDone") || named(name) != nullptr) {
            started = true;
            return;
        }
        if (Installer::running()) {
            return;
        }
        started = true;
        install(name);
    }
}
