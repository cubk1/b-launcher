#include "app/system/Catalog.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Config.h"
#include "app/instances/Instances.h"
#include "app/launch/JavaDownloads.h"

#include <core/base/strings.hpp>
#include <core/base/json.hpp>
#include <core/base/hash.hpp>
#include <core/base/zip.hpp>

#include <core/base/cancellation.hpp>
#include <core/meta/game_layout.hpp>
#include <core/network/mirror_provider.hpp>
#include <core/network/http_client.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

namespace Catalog {

    namespace {
        using Continuation = AsyncTasks::Continuation;

        AsyncTasks tasks;

        Versions versions;
        bool versionsStarted = false;
        std::map<std::pair<int, std::string>, LoaderVersions> loaderLists;
        std::map<std::string, FabricApiRequirements> fabricApis;
        JavaList javaList;
        bool javaStarted = false;

        std::shared_ptr<blb::net::mirror_provider> mirror() {
            static std::shared_ptr<blb::net::mirror_provider> provider = blb::net::make_bmclapi_provider();
            return provider;
        }

        void fetchMinecraft() {
            versionsStarted = true;
            versions.status = Status::Loading;
            versions.error.clear();
            std::shared_ptr<blb::net::mirror_provider> source = mirror();
            tasks.run(
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
                        return [list = std::move(list)]() mutable {
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
            tasks.run(
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
                        return [key, list = std::move(list)]() mutable {
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

        blb::result<std::string> fabricRange(const nlohmann::json& value) {
            if (value.is_array()) {
                std::vector<std::string> alternatives;
                for (const auto& item : value) {
                    if (!item.is_string()) {
                        return blb::make_error(blb::errc::parse, "Fabric Loader 版本要求无效");
                    }
                    auto range = fabricRange(item);
                    if (!range) {
                        return range.err();
                    }
                    alternatives.push_back(std::move(*range));
                }
                if (alternatives.empty()) {
                    return blb::make_error(blb::errc::parse, "Fabric Loader 版本要求为空");
                }
                return blb::join(alternatives, " || ");
            }
            if (!value.is_string()) {
                return blb::make_error(blb::errc::parse, "Fabric Loader 版本要求无效");
            }
            std::istringstream input(value.get<std::string>());
            std::vector<std::string> predicates;
            std::string token;
            while (input >> token) {
                if (token == "*" || token == "x" || token == "X") {
                    predicates.push_back("*");
                    continue;
                }
                std::string op;
                for (const char* candidate : {">=", "<=", ">", "<", "=", "~", "^"}) {
                    if (token.starts_with(candidate)) {
                        op = candidate;
                        token.erase(0, op.size());
                        break;
                    }
                }
                if (token.empty() && !(input >> token)) {
                    return blb::make_error(blb::errc::parse, "Fabric Loader 版本要求不完整");
                }
                if (token.empty() || token.front() < '0' || token.front() > '9' ||
                    token.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-+*") !=
                            std::string::npos) {
                    return blb::make_error(blb::errc::parse, "无法识别 Fabric Loader 版本要求");
                }
                auto core = std::string_view(token).substr(0, token.find_first_of("-+"));
                bool wildcard = false;
                for (std::string_view part : blb::split(core, '.')) {
                    bool any = part == "*" || part == "x" || part == "X";
                    if (part.empty() || (wildcard && !any) ||
                        (!any && part.find_first_not_of("0123456789") != std::string_view::npos)) {
                        return blb::make_error(blb::errc::parse, "无法识别 Fabric Loader 版本要求");
                    }
                    wildcard = wildcard || any;
                }
                if (wildcard && !op.empty() && op != "=") {
                    return blb::make_error(blb::errc::parse, "无法识别 Fabric Loader 通配版本要求");
                }
                // Fabric treats a bare version as exact; the shared range parser also accepts Maven ranges.
                predicates.push_back((op.empty() ? "=" : op) + token);
            }
            if (predicates.empty()) {
                return blb::make_error(blb::errc::parse, "Fabric Loader 版本要求为空");
            }
            return blb::join(predicates, " ");
        }

        blb::result<void> readFabricRequirements(blb::zip_reader& jar, FabricApiRequirements& out,
                                                 blb::cancellation_token token, int depth = 0) {
            if (auto checked = token.check(); !checked) {
                return checked.err();
            }
            if (depth > 8) {
                return blb::make_error(blb::errc::parse, "Fabric API 内嵌模组层级过深");
            }
            auto text = jar.read("fabric.mod.json");
            if (!text) {
                return text.err();
            }
            auto doc = nlohmann::json::parse(*text, nullptr, false);
            if (!doc.is_object() || blb::json_text(doc, "id").empty() || blb::json_text(doc, "version").empty()) {
                return blb::make_error(blb::errc::parse, "Fabric API 模组信息无效");
            }
            for (const char* field : {"depends", "breaks"}) {
                auto section = doc.find(field);
                if (section == doc.end()) {
                    continue;
                }
                if (!section->is_object()) {
                    return blb::make_error(blb::errc::parse, "Fabric API 依赖信息无效");
                }
                auto loader = section->find("fabricloader");
                if (loader != section->end()) {
                    auto range = fabricRange(*loader);
                    if (!range) {
                        return range.err();
                    }
                    auto& into = std::string_view(field) == "depends" ? out.required : out.incompatible;
                    if (std::find(into.begin(), into.end(), *range) == into.end()) {
                        into.push_back(std::move(*range));
                    }
                }
            }
            auto nested = doc.find("jars");
            if (nested != doc.end()) {
                if (!nested->is_array()) {
                    return blb::make_error(blb::errc::parse, "Fabric API 内嵌模组信息无效");
                }
                for (const auto& item : *nested) {
                    std::string path = blb::json_text(item, "file");
                    if (path.empty()) {
                        return blb::make_error(blb::errc::parse, "Fabric API 内嵌模组路径为空");
                    }
                    auto bytes = jar.read(path);
                    if (!bytes) {
                        return bytes.err();
                    }
                    auto child = blb::zip_reader::open_memory(std::move(*bytes));
                    if (!child) {
                        return child.err();
                    }
                    if (auto read = readFabricRequirements(*child, out, token, depth + 1); !read) {
                        return read.err();
                    }
                }
            }
            return blb::ok();
        }

        blb::result<FabricApiRequirements> fetchFabricRequirements(const blb::install::loader_version& version,
                                                                   blb::cancellation_token token) {
            auto bytes = blb::net::http_get(version.url, token);
            if (!bytes) {
                return bytes.err();
            }
            auto file = nlohmann::json::parse(version.extra, nullptr, false);
            auto hashes = file.is_object() ? file.find("hashes") : file.end();
            if (hashes != file.end()) {
                std::string expected = blb::json_text(*hashes, "sha1");
                if (!expected.empty() && !blb::iequals(blb::to_hex(blb::sha1::digest(*bytes)), expected)) {
                    return blb::make_error(blb::errc::checksum_mismatch, "Fabric API 校验失败");
                }
            }
            auto jar = blb::zip_reader::open_memory(std::move(*bytes));
            if (!jar) {
                return jar.err();
            }
            FabricApiRequirements out;
            if (auto read = readFabricRequirements(*jar, out, token); !read) {
                return read.err();
            }
            out.status = Status::Ready;
            return out;
        }

        bool sameJava(const blb::java::java_installation& a, const blb::java::java_installation& b) {
            std::error_code error;
            return a.executable == b.executable || std::filesystem::equivalent(a.executable, b.executable, error);
        }

        std::vector<std::string> pathList(const char* key) {
            return blb::json_strings(Config::data(), key);
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

        void sortJava(std::vector<blb::java::java_installation>& list) {
            std::stable_sort(list.begin(), list.end(),
                             [](const blb::java::java_installation& a, const blb::java::java_installation& b) {
                                 return a.major > b.major;
                             });
        }

        void scanJava() {
            javaStarted = true;
            javaList.status = Status::Loading;
            auto options = javaScanOptions();
            tasks.run(
                    [options](blb::cancellation_token token) -> Continuation {
                        std::vector<blb::java::java_installation> found = blb::java::scan_java(options, token);
                        return [found = std::move(found)]() mutable {
                            for (const blb::java::java_installation& added : javaList.list) {
                                if (std::none_of(found.begin(), found.end(),
                                                 [&](const blb::java::java_installation& java) {
                                                     return sameJava(java, added);
                                                 })) {
                                    found.push_back(added);
                                }
                            }
                            auto hidden = pathList("javaHidden");
                            auto manual = pathList("javaPaths");
                            std::erase_if(found, [&](const blb::java::java_installation& java) {
                                return listed(hidden, java.executable) && !listed(manual, java.executable);
                            });
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

    blb::java::scan_options javaScanOptions() {
        blb::java::scan_options options;
        for (const Instances::Source& source : Instances::sources()) {
            options.runtime_roots.push_back(blb::meta::game_layout(source.root).runtime());
        }
        options.extra_roots.push_back(JavaDownloads::root());
        std::vector<std::string> kept = pathList("javaPaths");
        std::vector<std::string> found = pathList("javaFound");
        kept.insert(kept.end(), found.begin(), found.end());
        for (const std::string& path : kept) {
            options.extra_homes.push_back(blb::path_from_utf8(path));
        }
        for (const std::string& path : pathList("javaHidden")) {
            if (!listed(kept, blb::path_from_utf8(path))) {
                options.excluded.push_back(blb::path_from_utf8(path));
            }
        }
        return options;
    }

    std::optional<std::vector<blb::java::java_installation>> javaCandidates() {
        if (!javaStarted || javaList.status != Status::Ready) {
            return std::nullopt;
        }
        return javaList.list;
    }

    int addFound(const std::vector<blb::java::java_installation>& found) {
        std::vector<std::string> hiddenList = pathList("javaHidden");
        std::vector<std::string> saved = pathList("javaFound");
        int added = 0;
        for (const blb::java::java_installation& java : found) {
            if (listed(hiddenList, java.executable) ||
                std::any_of(javaList.list.begin(), javaList.list.end(),
                            [&](const blb::java::java_installation& known) { return sameJava(known, java); })) {
                continue;
            }
            javaList.list.push_back(java);
            if (!listed(saved, java.executable)) {
                saved.push_back(blb::path_to_utf8(java.executable));
            }
            added++;
        }
        if (added > 0) {
            savePathList("javaFound", saved);
            sortJava(javaList.list);
        }
        return added;
    }

    void seenJava(const std::filesystem::path& executable) {
        if (javaList.status != Status::Ready || listed(pathList("javaHidden"), executable) ||
            std::any_of(javaList.list.begin(), javaList.list.end(), [&](const blb::java::java_installation& known) {
                std::error_code error;
                return known.executable == executable ||
                        std::filesystem::equivalent(known.executable, executable, error);
            })) {
            return;
        }
        scanJava();
    }

    void refreshJava() {
        if (javaList.status != Status::Loading || !javaStarted) {
            scanJava();
        }
    }

    void addJava(const std::filesystem::path& path) {
        tasks.run(
                [path](blb::cancellation_token token) -> Continuation {
                    auto probed = blb::java::probe_java(path, true, token);
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
        }
        std::vector<std::string> found = pathList("javaFound");
        if (listed(found, executable)) {
            unlist(found, executable);
            savePathList("javaFound", found);
        }
        std::vector<std::string> hiddenList = pathList("javaHidden");
        if (!listed(hiddenList, executable)) {
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

    Kind kindOf(const blb::meta::manifest_version& version) {
        if (version.type == "release") {
            return Kind::Release;
        }
        if (version.type.rfind("old_", 0) == 0) {
            return Kind::Old;
        }
        bool aprilFirst = version.release_time.size() >= 10 && version.release_time.compare(4, 6, "-04-01") == 0;
        return aprilFirst || version.id == "1.RV-Pre1" ? Kind::AprilFools : Kind::Snapshot;
    }

    void preload() {
        minecraft();
        java();
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

    const FabricApiRequirements& fabricApiRequirements(const blb::install::loader_version& version,
                                                       bool retryFailed) {
        std::string key = version.url + "\n" + version.extra;
        auto [found, inserted] = fabricApis.try_emplace(key);
        if (inserted || (retryFailed && found->second.status == Status::Failed)) {
            found->second = FabricApiRequirements{};
            tasks.run(
                    [key, version](blb::cancellation_token token) -> Continuation {
                        auto result = fetchFabricRequirements(version, token);
                        FabricApiRequirements value;
                        if (result) {
                            value = std::move(*result);
                        } else {
                            value.status = Status::Failed;
                            value.error = result.err().message;
                        }
                        return [key, value = std::move(value)]() mutable { fabricApis[key] = std::move(value); };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            fabricApis[key].status = Status::Failed;
                            fabricApis[key].error = error;
                        };
                    });
        }
        return found->second;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
