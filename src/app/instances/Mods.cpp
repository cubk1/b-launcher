#include "app/instances/Mods.h"
#include "app/system/AsyncTasks.h"
#include "app/instances/Instances.h"
#include "app/system/Platform.h"
#include "app/system/Notices.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <memory>
#include <functional>
#include <map>
#include <set>

namespace Mods {

    namespace {
        using Continuation = AsyncTasks::Continuation;

        AsyncTasks tasks;

        std::map<std::string, List> lists;
        std::set<std::string> scanning;
        std::set<std::string> again;
        std::map<std::string, std::shared_ptr<const Pixels>> icons;

        void run(std::function<Continuation()> work) {
            tasks.run([work = std::move(work)](b_launcher_base::cancellation_token) { return work(); }, {});
        }

        std::string iconKey(const blb::modpack::mod_info& mod) {
            std::string name = blb::path_to_utf8(mod.file);
            const std::string disabled = ".disabled";
            if (name.size() > disabled.size() && blb::to_lower(name.substr(name.size() - disabled.size())) == disabled) {
                name.resize(name.size() - disabled.size());
            }
            return name + "#" + std::to_string(mod.size);
        }

        void checkDependencies(List& list) {
            std::vector<blb::modpack::mod_info> infos;
            for (const Mod& mod : list.mods) {
                infos.push_back(mod.info);
            }
            std::vector<blb::modpack::mod_issue> issues = blb::modpack::check_dependencies(infos, {});
            for (Mod& mod : list.mods) {
                mod.missing.clear();
                for (const blb::modpack::mod_issue& issue : issues) {
                    if (issue.mod_id == mod.info.id && mod.info.enabled &&
                        std::find(mod.missing.begin(), mod.missing.end(), issue.missing) == mod.missing.end()) {
                        mod.missing.push_back(issue.missing);
                    }
                }
            }
        }

        void scan(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            if (entry == nullptr) {
                lists[key].loading = false;
                return;
            }
            if (scanning.count(key) != 0) {
                again.insert(key);
                return;
            }
            scanning.insert(key);
            lists[key].loading = true;
            std::filesystem::path directory = entry->gameDirectory / "mods";
            std::set<std::string> known;
            for (const auto& icon : icons) {
                known.insert(icon.first);
            }
            run([key, directory, known]() -> Continuation {
                std::vector<blb::modpack::mod_info> found;
                std::error_code error;
                if (std::filesystem::is_directory(directory, error)) {
                    found = blb::modpack::scan_mods(directory);
                }
                std::map<std::string, std::shared_ptr<const Pixels>> pictures;
                for (const blb::modpack::mod_info& mod : found) {
                    std::string keyOfIcon = iconKey(mod);
                    if (!mod.icon.empty() && known.count(keyOfIcon) == 0) {
                        auto bytes = blb::modpack::read_mod_icon(mod);
                        if (bytes) {
                            pictures[keyOfIcon] = Pixels::decode(bytes->data(), bytes->size());
                        }
                    }
                }
                return [key, found, pictures]() {
                    scanning.erase(key);
                    for (const auto& picture : pictures) {
                        if (icons.count(picture.first) == 0) {
                            icons[picture.first] = picture.second;
                        }
                    }
                    List& list = lists[key];
                    list.mods.clear();
                    for (const blb::modpack::mod_info& info : found) {
                        Mod mod;
                        mod.info = info;
                        if (mod.info.name.empty()) {
                            mod.info.name = mod.info.id.empty() ? mod.info.file_name : mod.info.id;
                        }
                        auto icon = icons.find(iconKey(info));
                        mod.icon = icon != icons.end() ? icon->second : nullptr;
                        list.mods.push_back(std::move(mod));
                    }
                    std::stable_sort(list.mods.begin(), list.mods.end(), [](const Mod& a, const Mod& b) {
                        return blb::to_lower(a.info.name) < blb::to_lower(b.info.name);
                    });
                    checkDependencies(list);
                    list.loading = false;
                    if (again.erase(key) != 0) {
                        scan(key);
                    }
                };
            });
        }

        Mod* findMod(const std::string& key, const std::filesystem::path& file) {
            auto found = lists.find(key);
            if (found == lists.end()) {
                return nullptr;
            }
            for (Mod& mod : found->second.mods) {
                if (mod.info.file == file) {
                    return &mod;
                }
            }
            return nullptr;
        }
    }

    const List& of(const std::string& instanceKey) {
        auto found = lists.find(instanceKey);
        if (found != lists.end()) {
            return found->second;
        }
        lists[instanceKey];
        scan(instanceKey);
        return lists[instanceKey];
    }

    void rescan(const std::string& instanceKey) {
        scan(instanceKey);
    }

    void setEnabled(const std::string& instanceKey, const std::filesystem::path& file, bool enabled) {
        Mod* mod = findMod(instanceKey, file);
        if (mod == nullptr || mod->info.enabled == enabled) {
            return;
        }
        auto renamed = blb::modpack::set_mod_enabled(mod->info, enabled);
        if (!renamed) {
            Notices::add(Notices::Kind::Failed,
                         (enabled ? "无法启用 " : "无法禁用 ") + mod->info.name + "：" + renamed.err().message, 5000);
            return;
        }
        mod->info.file = *renamed;
        mod->info.file_name = blb::path_to_utf8(renamed->filename());
        mod->info.enabled = enabled;
        checkDependencies(lists[instanceKey]);
    }

    void remove(const std::string& instanceKey, const std::filesystem::path& file) {
        Mod* mod = findMod(instanceKey, file);
        if (mod == nullptr) {
            return;
        }
        std::string name = mod->info.name;
        if (!Platform::recycle(file)) {
            Notices::add(Notices::Kind::Failed, "无法删除 " + name, 5000);
            return;
        }
        List& list = lists[instanceKey];
        list.mods.erase(
                std::remove_if(list.mods.begin(), list.mods.end(), [&](const Mod& m) { return m.info.file == file; }),
                list.mods.end());
        checkDependencies(list);
        Notices::add(Notices::Kind::Done, "已将 " + name + " 移到回收站", 2500);
    }

    std::filesystem::path folder(const std::string& instanceKey, const char* kind) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        return entry != nullptr ? entry->gameDirectory / kind : std::filesystem::path();
    }

    void add(const std::string& instanceKey, const std::vector<std::filesystem::path>& files, const char* kind,
             const std::string& what) {
        std::filesystem::path target = folder(instanceKey, kind);
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (target.empty() || entry == nullptr || files.empty()) {
            return;
        }
        std::string instanceName = entry->info.id;
        bool mods = std::string(kind) == "mods";
        run([instanceKey, files, target, what, instanceName, mods]() -> Continuation {
            int copied = 0;
            std::string problem;
            std::error_code error;
            std::filesystem::create_directories(target, error);
            for (const std::filesystem::path& file : files) {
                std::error_code copyError;
                std::filesystem::path destination = target / file.filename();
                if (std::filesystem::equivalent(file, destination, copyError)) {
                    copied++;
                    continue;
                }
                auto copiedFile = blb::fs::copy_file(file, destination);
                if (!copiedFile) {
                    problem = blb::path_to_utf8(file.filename()) + "：" + copiedFile.err().message;
                } else {
                    copied++;
                }
            }
            return [instanceKey, copied, problem, what, instanceName, mods, total = files.size()]() {
                if (copied > 0) {
                    Notices::add(Notices::Kind::Done,
                                 "已添加 " + std::to_string(copied) + " 个" + what + "到 " + instanceName, 3000);
                }
                if (!problem.empty()) {
                    Notices::add(Notices::Kind::Failed,
                                 std::to_string(total - copied) + " 个" + what + "添加失败：" + problem, 5000);
                }
                if (mods && lists.count(instanceKey) != 0) {
                    scan(instanceKey);
                }
            };
        });
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
