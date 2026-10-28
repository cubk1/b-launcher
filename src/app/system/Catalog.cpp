#include "app/system/Catalog.h"
#include "app/system/Config.h"
#include "app/instances/Instances.h"

#include <core/base/strings.hpp>

#include <core/base/cancellation.hpp>
#include <core/meta/game_layout.hpp>
#include <core/network/mirror_provider.hpp>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace Catalog {

    namespace {
        using Continuation = std::function<void()>;

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;

        Versions versions;
        bool versionsStarted = false;
        std::map<std::pair<int, std::string>, LoaderVersions> loaderLists;
        JavaList javaList;
        bool javaStarted = false;

        std::shared_ptr<blb::net::mirror_provider> mirror() {
            static std::shared_ptr<blb::net::mirror_provider> provider = blb::net::make_bmclapi_provider();
            return provider;
        }

        void run(std::function<Continuation(blb::cancellation_token)> work,
                 std::function<Continuation(const std::string&)> fail) {
            blb::cancellation_token token = stopping.token();
            workers.emplace_back([work, fail, token] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    next = fail(error.what());
                }
                std::lock_guard<std::mutex> guard(lock);
                finished.push_back(std::move(next));
            });
        }

        void fetchMinecraft() {
            versionsStarted = true;
            versions.status = Status::Loading;
            versions.error.clear();
            std::shared_ptr<blb::net::mirror_provider> source = mirror();
            run(
                    [source](blb::cancellation_token token) -> Continuation {
                        auto result = blb::meta::fetch_version_manifest(*source, token);
                        if (!result) {
                            std::string error = result.err().message;
                            return [error] {
                                versions.status = Status::Failed;
                                versions.error = error;
                            };
                        }
                        std::vector<blb::meta::manifest_version> list = std::move(result->versions);
                        return [list]() mutable {
                            versions.status = Status::Ready;
                            versions.list = std::move(list);
                        };
                    },
                    [](const std::string& error) -> Continuation {
                        return [error] {
                            versions.status = Status::Failed;
                            versions.error = error;
                        };
                    });
        }

        void fetchLoader(blb::install::loader_type type, const std::string& minecraft) {
            std::pair<int, std::string> key{(int) type, minecraft};
            LoaderVersions& entry = loaderLists[key];
            entry.status = Status::Loading;
            entry.error.clear();
            std::shared_ptr<blb::net::mirror_provider> source = mirror();
            run(
                    [source, type, minecraft, key](blb::cancellation_token token) -> Continuation {
                        auto result = blb::install::list_loader_versions(type, minecraft, *source, token);
                        if (!result) {
                            std::string error = result.err().message;
                            return [key, error] {
                                loaderLists[key].status = Status::Failed;
                                loaderLists[key].error = error;
                            };
                        }
                        std::vector<blb::install::loader_version> list = std::move(*result);
                        return [key, list]() mutable {
                            loaderLists[key].status = Status::Ready;
                            loaderLists[key].list = std::move(list);
                        };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            loaderLists[key].status = Status::Failed;
                            loaderLists[key].error = error;
                        };
                    });
        }

        bool sameJava(const blb::java::java_installation& a, const blb::java::java_installation& b) {
            std::error_code error;
            return a.executable == b.executable || std::filesystem::equivalent(a.executable, b.executable, error);
        }

        std::vector<std::string> pathList(const char* key) {
            std::vector<std::string> out;
            const nlohmann::json& data = Config::data();
            auto found = data.find(key);
            if (found != data.end() && found->is_array()) {
                for (const nlohmann::json& item : *found) {
                    if (item.is_string()) {
                        out.push_back(item.get<std::string>());
                    }
                }
            }
            return out;
        }

        void savePathList(const char* key, const std::vector<std::string>& list) {
            Config::data()[key] = list;
            Config::save();
        }

        bool listed(const std::vector<std::string>& list, const std::filesystem::path& path) {
            std::string text = blb::path_to_utf8(path);
            for (const std::string& item : list) {
                std::error_code error;
                if (item == text || std::filesystem::equivalent(blb::path_from_utf8(item), path, error)) {
                    return true;
                }
            }
            return false;
        }

        void unlist(std::vector<std::string>& list, const std::filesystem::path& path) {
            std::string text = blb::path_to_utf8(path);
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const std::string& item) {
                                          std::error_code error;
                                          return item == text ||
                                                  std::filesystem::equivalent(blb::path_from_utf8(item), path, error);
                                      }),
                       list.end());
        }

        bool hidden(const blb::java::java_installation& java) {
            std::vector<std::string> list = pathList("javaHidden");
            return !list.empty() && listed(list, java.executable);
        }

        void sortJava(std::vector<blb::java::java_installation>& list) {
            std::stable_sort(list.begin(), list.end(),
                             [](const blb::java::java_installation& a, const blb::java::java_installation& b) {
                                 return a.major > b.major;
                             });
        }

        void scanJava() {
            javaStarted = true;
            javaList.status = Status::Loading;
            blb::java::scan_options options;
            for (const Instances::Source& source : Instances::sources()) {
                options.runtime_roots.push_back(blb::meta::game_layout(source.root).runtime());
            }
            for (const std::string& path : pathList("javaPaths")) {
                options.extra_homes.push_back(blb::java::java_home_from_executable(blb::path_from_utf8(path)));
            }
            run(
                    [options](blb::cancellation_token token) -> Continuation {
                        std::vector<blb::java::java_installation> found = blb::java::scan_java(options, token);
                        return [found]() mutable {
                            found.erase(std::remove_if(found.begin(), found.end(),
                                                       [](const blb::java::java_installation& java) {
                                                           return hidden(java) && !addedByHand(java.executable);
                                                       }),
                                        found.end());
                            for (const blb::java::java_installation& added : javaList.list) {
                                if (std::none_of(found.begin(), found.end(),
                                                 [&](const blb::java::java_installation& java) {
                                                     return sameJava(java, added);
                                                 })) {
                                    found.push_back(added);
                                }
                            }
                            sortJava(found);
                            javaList.list = std::move(found);
                            javaList.status = Status::Ready;
                        };
                    },
                    [](const std::string&) -> Continuation { return [] { javaList.status = Status::Failed; }; });
        }
    }

    const JavaList& java() {
        if (!javaStarted) {
            scanJava();
        }
        return javaList;
    }

    void refreshJava() {
        if (javaList.status != Status::Loading || !javaStarted) {
            scanJava();
        }
    }

    void addJava(const std::filesystem::path& path) {
        run(
                [path](blb::cancellation_token) -> Continuation {
                    auto probed = blb::java::probe_java(path);
                    if (!probed) {
                        return nullptr;
                    }
                    blb::java::java_installation java = *probed;
                    return [java] {
                        std::vector<std::string> added = pathList("javaPaths");
                        if (!listed(added, java.executable)) {
                            added.push_back(blb::path_to_utf8(java.executable));
                            savePathList("javaPaths", added);
                        }
                        std::vector<std::string> hiddenList = pathList("javaHidden");
                        if (listed(hiddenList, java.executable)) {
                            unlist(hiddenList, java.executable);
                            savePathList("javaHidden", hiddenList);
                        }
                        if (std::none_of(javaList.list.begin(), javaList.list.end(),
                                         [&](const blb::java::java_installation& listed) {
                                             return sameJava(listed, java);
                                         })) {
                            javaList.list.push_back(java);
                            sortJava(javaList.list);
                        }
                    };
                },
                [](const std::string&) -> Continuation { return nullptr; });
    }

    bool addedByHand(const std::filesystem::path& executable) {
        return listed(pathList("javaPaths"), executable);
    }

    void removeJava(const std::filesystem::path& executable) {
        std::vector<std::string> added = pathList("javaPaths");
        if (listed(added, executable)) {
            unlist(added, executable);
            savePathList("javaPaths", added);
        } else {
            std::vector<std::string> hiddenList = pathList("javaHidden");
            hiddenList.push_back(blb::path_to_utf8(executable));
            savePathList("javaHidden", hiddenList);
        }
        javaList.list.erase(std::remove_if(javaList.list.begin(), javaList.list.end(),
                                           [&](const blb::java::java_installation& java) {
                                               std::error_code error;
                                               return java.executable == executable ||
                                                       std::filesystem::equivalent(java.executable, executable, error);
                                           }),
                            javaList.list.end());
    }

    const Versions& minecraft() {
        if (!versionsStarted) {
            fetchMinecraft();
        }
        return versions;
    }

    void preload() {
        minecraft();
    }

    void refreshMinecraft() {
        if (versions.status != Status::Loading || !versionsStarted) {
            fetchMinecraft();
        }
    }

    const LoaderVersions& loader(blb::install::loader_type type, const std::string& minecraft) {
        auto found = loaderLists.find({(int) type, minecraft});
        if (found == loaderLists.end()) {
            fetchLoader(type, minecraft);
            found = loaderLists.find({(int) type, minecraft});
        }
        return found->second;
    }

    void refreshLoader(blb::install::loader_type type, const std::string& minecraft) {
        auto found = loaderLists.find({(int) type, minecraft});
        if (found == loaderLists.end() || found->second.status != Status::Loading) {
            fetchLoader(type, minecraft);
        }
    }

    void poll() {
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            if (next) {
                next();
            }
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
