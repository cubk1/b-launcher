#include "ui/imgui/screens/downloads/RelationGraph.h"
#include "app/downloads/Dependencies.h"
#include "app/downloads/ModNames.h"
#include "app/downloads/Projects.h"
#include "ui/imgui/screens/instances/ModGraph.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <cstdio>
#include <map>
#include <memory>
#include <set>

namespace RelationGraph {

    using Addons::Kind;
    using Addons::Source;
    using Addons::Status;
    using DownloadsParts::Route;
    using ModGraph::Link;
    using ModGraph::Tone;

    namespace {
        constexpr int DEPTH_LIMIT = 6;

        struct Target {
            bool wiki = false;
            Mcmod::Ref ref;
            Source source = Source::Modrinth;
            Kind kind = Kind::Mod;
            Addons::Hit hit;
        };

        using Targets = std::map<std::string, Target>;

        struct Cached {
            std::string signature;
            ModGraph::Model model;
            std::vector<std::string> absent;
            Source source = Source::Modrinth;
        };

        std::map<std::string, Cached> packs;

        std::function<void(const std::string&)> opener(std::shared_ptr<Targets> targets) {
            return [targets](const std::string& id) {
                auto found = targets->find(id);
                if (found == targets->end()) {
                    return;
                }
                const Target& target = found->second;
                if (target.wiki) {
                    DownloadsParts::openMcmod(target.ref);
                } else {
                    DownloadsParts::openProject(target.source, target.kind, target.hit);
                }
            };
        }

        std::string sizeText(long long size) {
            char text[32];
            if (size >= 1024LL * 1024) {
                std::snprintf(text, sizeof(text), "%.1f MB", size / (1024.0 * 1024.0));
            } else {
                std::snprintf(text, sizeof(text), "%.0f KB", size < 0 ? 0.0 : size / 1024.0);
            }
            return text;
        }

        std::string sideText(const PackContents::Item& item) {
            if (item.server == "unsupported" && item.client != "unsupported" && !item.client.empty()) {
                return "仅客户端";
            }
            if (item.client == "unsupported" && item.server != "unsupported" && !item.server.empty()) {
                return "仅服务端";
            }
            return item.client.empty() && item.server.empty() ? std::string() : std::string("双端");
        }

        std::string title(const std::string& name) {
            return DownloadsParts::displayTitle(name, Kind::Mod);
        }

        const PackContents::Folder* modsFolder(const PackContents::Contents& contents) {
            for (const PackContents::Folder& folder : contents.root.folders) {
                if (folder.name == "mods") {
                    return &folder;
                }
            }
            return nullptr;
        }

        void buildPack(Cached& cached, const PackContents::Contents& contents) {
            ModGraph::Model& model = cached.model;
            model = ModGraph::Model();
            cached.absent.clear();
            auto targets = std::make_shared<Targets>();
            const PackContents::Folder* folder = modsFolder(contents);
            if (folder == nullptr) {
                return;
            }
            std::map<std::string, std::string> byProject;
            for (const PackContents::Item& item : folder->items) {
                std::string id = !item.projectId.empty() ? "project:" + item.projectId : "file:" + item.name;
                if (!item.projectId.empty()) {
                    byProject[item.projectId] = id;
                }
            }
            std::set<std::string> absent;
            for (const PackContents::Item& item : folder->items) {
                ModGraph::Item node;
                node.id = !item.projectId.empty() ? "project:" + item.projectId : "file:" + item.name;
                node.name = item.resolved ? title(item.title) : item.title;
                node.version = item.version;
                node.iconUrl = item.iconUrl;
                node.tone = item.manual ? Tone::Muted : Tone::Normal;
                node.rows.push_back(
                        {"文件", item.name + (item.size >= 0 ? "  " + sizeText(item.size) : std::string())});
                std::string side = sideText(item);
                if (!side.empty()) {
                    node.rows.push_back({"环境", side});
                }
                std::string status = item.bundled ? "内置在整合包中"
                        : item.manual             ? "需手动下载"
                        : item.optional           ? "可选"
                                                  : std::string();
                if (!status.empty()) {
                    node.rows.push_back({"状态", status});
                }
                node.openable = item.resolved && !item.projectId.empty();
                if (node.openable) {
                    Target target;
                    target.source = item.source;
                    target.hit.id = item.projectId;
                    target.hit.slug = item.slug;
                    target.hit.title = item.title;
                    target.hit.iconUrl = item.iconUrl;
                    (*targets)[node.id] = target;
                }
                for (const Addons::Dependency& dependency : item.dependencies) {
                    if (dependency.incompatible || dependency.embedded || dependency.projectId.empty()) {
                        continue;
                    }
                    auto found = byProject.find(dependency.projectId);
                    if (found != byProject.end()) {
                        model.relations.push_back(
                                {found->second, node.id, dependency.required ? Link::Required : Link::Optional});
                    } else if (dependency.required) {
                        std::string missing = "missing:" + dependency.projectId;
                        model.relations.push_back({missing, node.id, Link::Required});
                        absent.insert(dependency.projectId);
                    }
                }
                model.items.push_back(std::move(node));
            }
            for (const std::string& projectId : absent) {
                ModGraph::Item node;
                node.id = "missing:" + projectId;
                node.tone = Tone::Missing;
                node.version = "缺少";
                const Projects::Brief* brief = Projects::brief(cached.source, projectId);
                node.name = brief != nullptr && !brief->title.empty() ? title(brief->title) : "#" + projectId;
                node.iconUrl = brief != nullptr ? brief->iconUrl : std::string();
                node.rows.push_back({"状态", "整合包里没有这个必需前置"});
                node.openable = true;
                Target target;
                target.source = cached.source;
                target.hit.id = projectId;
                if (brief != nullptr) {
                    target.hit.slug = brief->slug;
                    target.hit.title = brief->title;
                    target.hit.iconUrl = brief->iconUrl;
                }
                (*targets)[node.id] = target;
                model.items.push_back(std::move(node));
                cached.absent.push_back(projectId);
            }
            model.open = opener(targets);
        }

        std::string packSignature(const Cached& cached, const std::string& versionId) {
            int briefs = 0;
            for (const std::string& projectId : cached.absent) {
                briefs += Projects::brief(cached.source, projectId) != nullptr ? 1 : 0;
            }
            return versionId + "|" + std::to_string(ModNames::revision()) + "|" + std::to_string(briefs);
        }

        ModGraph::Frame frameOf(float x, float y, float w, float h) {
            ModGraph::Frame frame;
            frame.clip = ModGraph::Area{x, y, w, h};
            frame.layout = frame.clip;
            frame.inputTop = y;
            return frame;
        }

        struct Builder {
            Source source = Source::Modrinth;
            std::string game;
            std::string loader;
            ModGraph::Model model;
            std::shared_ptr<Targets> targets = std::make_shared<Targets>();
            std::set<std::string> added;
            std::string digest;

            const Projects::Brief* brief(const std::string& projectId) {
                return projectId.empty() ? nullptr : Projects::brief(source, projectId);
            }

            std::string node(const std::string& projectId, const Dependencies::Node* resolved, const std::string& group,
                             Tone tone) {
                std::string id = "project:" + projectId;
                if (!added.insert(id).second) {
                    return id;
                }
                const Projects::Brief* info = brief(projectId);
                ModGraph::Item item;
                item.id = id;
                item.group = group;
                item.tone = tone;
                item.name = info != nullptr && !info->title.empty() ? title(info->title) : "#" + projectId;
                item.iconUrl = info != nullptr ? info->iconUrl : std::string();
                if (resolved != nullptr && resolved->status == Status::Ready) {
                    item.version =
                            !resolved->version.number.empty() ? resolved->version.number : resolved->version.name;
                } else if (resolved != nullptr && resolved->status == Status::Failed) {
                    item.version = "无兼容版本";
                    if (!resolved->error.empty()) {
                        item.rows.push_back({"原因", resolved->error});
                    }
                } else if (resolved != nullptr) {
                    item.version.clear();
                }
                item.openable = true;
                Target target;
                target.source = source;
                target.hit.id = projectId;
                if (info != nullptr) {
                    target.hit.slug = info->slug;
                    target.hit.title = info->title;
                    target.hit.iconUrl = info->iconUrl;
                    target.kind = info->kind;
                }
                (*targets)[id] = target;
                digest += id + "#" + item.name + "#" + item.version + "#" + item.iconUrl + "|";
                model.items.push_back(std::move(item));
                return id;
            }

            void link(const std::string& from, const std::string& to, Link kind) {
                model.relations.push_back({from, to, kind});
                digest += from + ">" + to + "#" + std::to_string((int) kind) + "|";
            }

            void walk(const Addons::Version& version, const std::string& owner, int depth,
                      std::set<std::string>& seen) {
                for (const Addons::Dependency& dependency : version.dependencies) {
                    if (!dependency.required || dependency.incompatible || dependency.embedded) {
                        continue;
                    }
                    const Dependencies::Node& resolved = Dependencies::resolve(source, dependency, game, loader);
                    std::string projectId = dependency.projectId;
                    if (projectId.empty() && resolved.status == Status::Ready) {
                        projectId = resolved.version.projectId;
                    }
                    if (projectId.empty()) {
                        continue;
                    }
                    std::string id = node(projectId, &resolved, "前置", Tone::Normal);
                    link(id, owner, Link::Required);
                    if (resolved.status == Status::Ready && depth < DEPTH_LIMIT && seen.insert(projectId).second) {
                        walk(resolved.version, id, depth + 1, seen);
                    }
                }
            }
        };
    }

    bool viewSwitch(const std::string& id, bool graph, float x, float y, bool blocked) {
        constexpr float SEGMENT = 30.0f;
        UiRender::roundRect(x, y, SEGMENT * 2.0f, Page::FIELD_H, 7.0f, UiTheme::surfaceRaised());
        const std::pair<int, const char*> options[] = {{0xf03a, "列表"}, {0xf542, "节点"}};
        bool next = graph;
        for (int i = 0; i < 2; i++) {
            float segmentX = x + (float) i * SEGMENT;
            bool active = (i == 1) == graph;
            bool hovered = !blocked && NewUi::hit(segmentX, y, SEGMENT, Page::FIELD_H);
            if (active) {
                UiRender::roundRect(segmentX + 2.0f, y + 2.0f, SEGMENT - 4.0f, Page::FIELD_H - 4.0f, 5.0f,
                                    UiTheme::surface());
                UiRender::outline(segmentX + 2.0f, y + 2.0f, SEGMENT - 4.0f, Page::FIELD_H - 4.0f, 5.0f,
                                  UiTheme::border(UiTheme::outlineSoft()));
            }
            uint32_t ink = active ? UiTheme::textPrimary() : hovered ? UiTheme::textSecondary() : UiTheme::textMuted();
            UiWidgets::icon(Ui::glyph(options[i].first), segmentX + SEGMENT * 0.5f, y + Page::FIELD_H * 0.5f, ink,
                            11.0f);
            if (hovered) {
                Page::tooltip(options[i].second, segmentX, y, SEGMENT, Page::FIELD_H);
            }
            if (!blocked && NewUi::click(segmentX, y, SEGMENT, Page::FIELD_H)) {
                next = i == 1;
            }
        }
        return next;
    }

    void pack(const Route& route, const PackContents::Contents& contents, const std::string& versionId,
              const std::string& query, float x, float y, float w, float h, bool covered) {
        std::string key = "downloads#graph#pack#" + std::to_string((int) route.source) + ":" + route.hit.id;
        Cached& cached = packs[key];
        cached.source = route.source;
        std::string signature = packSignature(cached, versionId);
        if (signature != cached.signature) {
            buildPack(cached, contents);
            cached.signature = packSignature(cached, versionId);
            cached.model.signature = cached.signature;
        }
        if (cached.model.items.empty()) {
            DownloadsParts::message("整合包里没有模组", UiTheme::textMuted(), y, y + h, false);
            return;
        }
        ModGraph::drawModel(key, cached.model, query, frameOf(x, y, w, h), covered);
    }

    void mod(const Route& route, const Addons::Version& version, const Mcmod::Ref* mcmod,
             const std::string& gameVersion, const std::string& loader, float x, float y, float w, float h,
             bool covered) {
        Builder builder;
        builder.source = route.source;
        builder.game = gameVersion;
        builder.loader = loader;
        ModGraph::Item root;
        root.id = "root";
        root.group = "此模组";
        root.head = true;
        root.tone = Tone::Root;
        root.name = title(route.hit.title);
        root.version = !version.number.empty() ? version.number : version.name;
        root.iconUrl = route.hit.iconUrl;
        builder.digest = version.id + "|" + gameVersion + "|" + loader + "|" + root.name + "|" + root.iconUrl + "|";
        builder.model.items.push_back(root);
        std::set<std::string> seen = {route.hit.id};
        builder.walk(version, "root", 1, seen);
        for (const Addons::Dependency& dependency : version.dependencies) {
            if (dependency.projectId.empty() || dependency.required) {
                continue;
            }
            if (dependency.incompatible) {
                std::string id = builder.node(dependency.projectId, nullptr, "不兼容", Tone::Muted);
                builder.link(id, "root", Link::Incompatible);
            } else if (dependency.embedded) {
                std::string id = builder.node(dependency.projectId, nullptr, "内置", Tone::Normal);
                builder.link(id, "root", Link::Optional);
            } else {
                const Dependencies::Node& resolved =
                        Dependencies::resolve(route.source, dependency, gameVersion, loader);
                std::string id = builder.node(dependency.projectId, &resolved, "可选", Tone::Normal);
                builder.link(id, "root", Link::Optional);
            }
        }
        if (mcmod != nullptr) {
            const Mcmod::Relations& relations = Mcmod::relations(*mcmod);
            builder.digest += "mcmod" + std::to_string((int) relations.status) + "|";
            for (const char* type : {"integrates", "required_by"}) {
                bool integrates = std::string(type) == "integrates";
                for (const Mcmod::Relation& relation : relations.items) {
                    if (relation.type != type || relation.mod.id <= 0) {
                        continue;
                    }
                    std::string id = "mcmod:" + std::to_string(relation.mod.id);
                    if (!builder.added.insert(id).second) {
                        continue;
                    }
                    ModGraph::Item item;
                    item.id = id;
                    item.group = integrates ? "联动" : "被依赖";
                    item.name = Mcmod::displayName(relation.mod);
                    item.openable = true;
                    std::string slug = Mcmod::slugOn(relation.mod, "modrinth");
                    const Projects::Brief* info = slug.empty() ? nullptr : Projects::brief(Source::Modrinth, slug);
                    if (info != nullptr) {
                        item.iconUrl = info->iconUrl;
                    }
                    if (!relation.scope.empty()) {
                        item.rows.push_back({"范围", relation.scope});
                    }
                    Target target;
                    target.wiki = info == nullptr;
                    target.ref = relation.mod;
                    if (info != nullptr) {
                        target.source = Source::Modrinth;
                        target.kind = info->kind;
                        target.hit.id = info->id;
                        target.hit.slug = info->slug;
                        target.hit.title = info->title;
                        target.hit.iconUrl = info->iconUrl;
                    }
                    (*builder.targets)[id] = target;
                    builder.digest += id + "#" + item.name + "#" + item.iconUrl + "|";
                    builder.model.items.push_back(std::move(item));
                    if (integrates) {
                        builder.link(id, "root", Link::Integration);
                    } else {
                        builder.link("root", id, Link::Required);
                    }
                }
            }
        }
        if (builder.model.items.size() <= 1) {
            bool waiting = mcmod != nullptr && Mcmod::relations(*mcmod).status == Status::Loading;
            DownloadsParts::message(waiting ? "正在获取关系" : "这个版本没有依赖或联动", UiTheme::textMuted(), y, y + h,
                                    waiting);
            return;
        }
        builder.model.grouped = true;
        builder.model.signature = builder.digest;
        builder.model.open = opener(builder.targets);
        std::string key = "downloads#graph#mod#" + std::to_string((int) route.source) + ":" + route.hit.id;
        ModGraph::drawModel(key, builder.model, std::string(), frameOf(x, y, w, h), covered);
    }
}
