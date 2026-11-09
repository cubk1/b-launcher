#include "ui/imgui/screens/instances/ModGraph.h"
#include "app/downloads/Addons.h"
#include "app/downloads/ModNames.h"
#include "app/instances/ModManager.h"
#include "app/instances/Mods.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/instances/ModVersionPicker.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace ModGraph {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float NODE_W = 210.0f;
        constexpr float NODE_H = 38.0f;
        constexpr float PAD = 10.0f;
        constexpr float CLUSTER_GAP = 22.0f;
        constexpr float LANE_STEP = NODE_W + 2.0f * PAD + CLUSTER_GAP;
        constexpr float ROW_STEP = NODE_H + 10.0f;
        constexpr float HEADER_GAP = 12.0f;
        constexpr float TITLE_H = 24.0f;
        constexpr float CLUSTER_RADIUS = 14.0f;
        constexpr int ROWS_PREFERRED = 8;
        constexpr float ZOOM_MIN = 0.25f;
        constexpr float ZOOM_MAX = 2.0f;
        constexpr float MARGIN = 40.0f;
        constexpr float CARD_W = 340.0f;
        constexpr float LABEL_W = 52.0f;
        constexpr float ABOUT_LINE = 17.0f;
        constexpr int ABOUT_LINES = 3;
        constexpr int PICK_ROWS = 8;
        constexpr float PICK_H = 10.0f + Page::FIELD_H + 8.0f + PICK_ROWS * 34.0f + 12.0f;
        constexpr float CARD_HEAD = 46.0f;
        constexpr float CARD_ROW = 26.0f;
        constexpr float TOOL_STEP = 32.0f;
        constexpr double DOUBLE_CLICK = 0.3;
        const char* const BUILTIN[] = {"minecraft", "java",     "fabricloader", "fabric-loader", "quilt_loader",
                                       "forge",     "neoforge", "javafml",      "lowcodefml",    "mixinextras"};

        enum class Kind { Local, Added, Missing, Remote };

        struct Node {
            std::string id;
            Kind kind = Kind::Local;
            int mod = -1;
            int item = -1;
            std::string name;
            std::string projectId;
            float x = 0.0f;
            float y = 0.0f;
            int cluster = -1;
            int dependents = 0;
        };

        struct Edge {
            int from = 0;
            int to = 0;
            bool required = true;
            Link link = Link::Required;
        };

        struct Cluster {
            std::string id;
            std::string title;
            int hub = -1;
            std::vector<int> members;
            int lanes = 1;
            int rows = 0;
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
        };

        struct Pending {
            std::string id;
            std::string name;
            float x = 0.0f;
            float y = 0.0f;
            double since = 0.0;
        };

        struct View {
            std::string signature;
            std::map<std::string, std::pair<float, float>> pinned;
            std::vector<Pending> pending;
            float originX = 0.0f;
            float originY = 0.0f;
            int dragNode = -1;
            bool loaded = false;
            bool dirty = false;
            double changedAt = 0.0;
            float lastPanX = 0.0f;
            float lastPanY = 0.0f;
            float lastZoom = 0.0f;
            std::vector<Node> nodes;
            std::vector<Edge> edges;
            std::vector<Cluster> clusters;
            std::map<std::string, std::pair<float, float>> moved;
            bool fitted = false;
            float panX = 0.0f;
            float panY = 0.0f;
            float zoom = 1.0f;
            float panTargetX = 0.0f;
            float panTargetY = 0.0f;
            float zoomTarget = 1.0f;
            std::string selected;
            std::string expanded;
            bool picking = false;
            bool pickNext = false;
            std::string lastQuery;
            int dragCluster = -1;
            bool panning = false;
            bool dragged = false;
            float pressX = 0.0f;
            float pressY = 0.0f;
            float lastX = 0.0f;
            float lastY = 0.0f;
            double lastPress = -1.0;
            std::string lastPressed;
            bool touched = false;
        };

        std::map<std::string, View> views;
        const Model* model = nullptr;
        bool movable = false;

        const Item* itemOf(const Node& node) {
            if (node.kind != Kind::Remote || model == nullptr || node.item < 0 ||
                node.item >= (int) model->items.size()) {
                return nullptr;
            }
            return &model->items[(size_t) node.item];
        }

        bool missing(const Node& node) {
            if (node.kind == Kind::Missing) {
                return true;
            }
            const Item* item = itemOf(node);
            return item != nullptr && item->tone == Tone::Missing;
        }

        bool openable(const Node& node) {
            const Item* item = itemOf(node);
            return item != nullptr && item->openable && model->open;
        }

        std::filesystem::path layoutFile(const Instances::Entry& entry) {
            return entry.info.directory / "blb-modgraph.json";
        }

        void touch(View& view) {
            view.dirty = true;
            view.changedAt = ImGui::GetTime();
        }

        void load(View& view, const Instances::Entry& entry) {
            view.loaded = true;
            auto text = blb::fs::read_file(layoutFile(entry));
            if (!text) {
                return;
            }
            nlohmann::json doc = nlohmann::json::parse(*text, nullptr, false);
            if (!doc.is_object()) {
                return;
            }
            auto read = [&doc](const char* name, std::map<std::string, std::pair<float, float>>& into) {
                auto found = doc.find(name);
                if (found == doc.end() || !found->is_object()) {
                    return;
                }
                for (auto it = found->begin(); it != found->end(); ++it) {
                    const nlohmann::json& value = it.value();
                    if (value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number()) {
                        into[it.key()] = {value[0].get<float>(), value[1].get<float>()};
                    }
                }
            };
            read("clusters", view.moved);
            read("pinned", view.pinned);
            auto camera = doc.find("camera");
            if (camera != doc.end() && camera->is_array() && camera->size() == 3 && (*camera)[0].is_number() &&
                (*camera)[1].is_number() && (*camera)[2].is_number()) {
                view.panX = view.panTargetX = (*camera)[0].get<float>();
                view.panY = view.panTargetY = (*camera)[1].get<float>();
                view.zoom = view.zoomTarget = std::clamp((*camera)[2].get<float>(), ZOOM_MIN, ZOOM_MAX);
                view.fitted = true;
            }
            view.lastPanX = view.panTargetX;
            view.lastPanY = view.panTargetY;
            view.lastZoom = view.zoomTarget;
        }

        void save(View& view, const Instances::Entry& entry) {
            auto write = [](const std::map<std::string, std::pair<float, float>>& from) {
                nlohmann::json out = nlohmann::json::object();
                for (const auto& item : from) {
                    out[item.first] = nlohmann::json::array({item.second.first, item.second.second});
                }
                return out;
            };
            nlohmann::json doc = nlohmann::json::object();
            doc["clusters"] = write(view.moved);
            doc["pinned"] = write(view.pinned);
            doc["camera"] = nlohmann::json::array({view.panTargetX, view.panTargetY, view.zoomTarget});
            (void) blb::fs::write_file_atomic(layoutFile(entry), doc.dump());
            view.dirty = false;
        }

        uint32_t warningInk() {
            return UiTheme::warningInk();
        }

        uint32_t withAlpha(uint32_t rgba, float alpha) {
            uint32_t base = (rgba >> 24) & 0xFF;
            return ((uint32_t) Ui::round((float) base * std::clamp(alpha, 0.0f, 1.0f)) << 24) | (rgba & 0x00FFFFFFu);
        }

        ImU32 imColor(uint32_t rgba) {
            uint32_t c = UiRender::applyAlpha(rgba);
            return (c & 0xFF00FF00u) | ((c & 0x00FF0000u) >> 16) | ((c & 0x000000FFu) << 16);
        }

        ImVec2 point(float x, float y) {
            float unit = UiRender::unit();
            return ImVec2(UiRender::transformX(x) * unit, UiRender::transformY(y) * unit);
        }

        std::string stemOf(const std::filesystem::path& file) {
            std::string name = blb::to_lower(blb::path_to_utf8(file.filename()));
            const std::string disabled = ".disabled";
            if (name.size() > disabled.size() && name.ends_with(disabled)) {
                name.resize(name.size() - disabled.size());
            }
            return name;
        }

        std::string nameOf(const Node& node) {
            if (node.kind == Kind::Missing && !node.projectId.empty()) {
                return ModManager::title(node.projectId);
            }
            return node.name;
        }

        std::string signatureOf(const std::string& key, const Mods::List& list) {
            std::string out = std::to_string(list.serial);
            for (const ModManager::Change& change : ModManager::changes(key)) {
                out += "|" + stemOf(change.file) + "#" + change.projectId + "#" + change.target.id;
            }
            int identified = 0;
            for (const Mods::Mod& mod : list.mods) {
                identified += ModManager::remote(mod) != nullptr ? 1 : 0;
            }
            return out + "|" + std::to_string(identified) + "|" + std::to_string(ModManager::metadataRevision()) + "|" +
                    std::to_string(ModNames::revision());
        }

        float headerOf(const Cluster& cluster) {
            return cluster.hub >= 0 ? NODE_H : TITLE_H;
        }

        void place(View& view) {
            for (Cluster& cluster : view.clusters) {
                float header = headerOf(cluster);
                if (cluster.hub >= 0) {
                    view.nodes[(size_t) cluster.hub].x = cluster.x + PAD;
                    view.nodes[(size_t) cluster.hub].y = cluster.y + PAD;
                }
                for (size_t k = 0; k < cluster.members.size(); k++) {
                    Node& node = view.nodes[(size_t) cluster.members[k]];
                    size_t column = k / (size_t) cluster.rows;
                    size_t row = k % (size_t) cluster.rows;
                    node.x = cluster.x + PAD + (float) column * LANE_STEP;
                    node.y = cluster.y + PAD + header + HEADER_GAP + (float) row * ROW_STEP;
                }
            }
            for (Node& node : view.nodes) {
                auto pin = view.pinned.find(node.id);
                if (pin != view.pinned.end() && node.cluster < 0) {
                    node.x = pin->second.first;
                    node.y = pin->second.second;
                }
            }
        }

        int laneCount(size_t count) {
            return std::max(2, (int) std::lround(std::sqrt((double) count * LANE_STEP * ROW_STEP * 1.7) / LANE_STEP));
        }

        void measure(Cluster& cluster, int lanes) {
            int members = (int) cluster.members.size();
            cluster.lanes = std::clamp((members + ROWS_PREFERRED - 1) / ROWS_PREFERRED, 1, lanes);
            cluster.rows = members == 0 ? 0 : (members + cluster.lanes - 1) / cluster.lanes;
            cluster.w = (float) cluster.lanes * LANE_STEP - CLUSTER_GAP;
            cluster.h = PAD + headerOf(cluster) +
                    (cluster.rows > 0 ? HEADER_GAP + (float) cluster.rows * ROW_STEP - (ROW_STEP - NODE_H) : 0.0f) +
                    PAD;
        }

        void pack(View& view, std::vector<Cluster>& clusters, const std::vector<size_t>& order, int lanes);

        void group(View& view) {
            std::vector<Node>& nodes = view.nodes;
            size_t count = nodes.size();
            std::vector<std::vector<int>> deps(count);
            std::vector<std::vector<int>> users(count);
            for (const Edge& edge : view.edges) {
                if (edge.required) {
                    deps[(size_t) edge.to].push_back(edge.from);
                    users[(size_t) edge.from].push_back(edge.to);
                }
            }
            std::vector<bool> hub(count, false);
            for (size_t i = 0; i < count; i++) {
                nodes[i].dependents = (int) users[i].size();
                hub[i] = missing(nodes[i]) ? !users[i].empty() : users[i].size() >= 2;
            }
            auto lower = [&nodes](int index) { return blb::to_lower(nameOf(nodes[(size_t) index])); };
            std::vector<int> owner(count, -2);
            for (size_t i = 0; i < count; i++) {
                if (hub[i]) {
                    owner[i] = (int) i;
                    continue;
                }
                int best = -1;
                for (int dependency : deps[i]) {
                    if (!hub[(size_t) dependency]) {
                        continue;
                    }
                    if (best < 0 || users[(size_t) dependency].size() < users[(size_t) best].size() ||
                        (users[(size_t) dependency].size() == users[(size_t) best].size() &&
                         lower(dependency) < lower(best))) {
                        best = dependency;
                    }
                }
                if (best >= 0) {
                    owner[i] = best;
                }
            }
            std::function<int(int)> resolve = [&](int index) -> int {
                if (owner[(size_t) index] != -2) {
                    return owner[(size_t) index];
                }
                owner[(size_t) index] = -1;
                if (users[(size_t) index].size() == 1) {
                    owner[(size_t) index] = resolve(users[(size_t) index].front());
                }
                return owner[(size_t) index];
            };
            for (size_t i = 0; i < count; i++) {
                resolve((int) i);
            }

            std::vector<Cluster> clusters;
            std::map<int, size_t> byHub;
            for (size_t i = 0; i < count; i++) {
                if (hub[i]) {
                    Cluster cluster;
                    cluster.id = "hub:" + nodes[i].id;
                    cluster.hub = (int) i;
                    byHub[(int) i] = clusters.size();
                    nodes[i].cluster = (int) clusters.size();
                    clusters.push_back(std::move(cluster));
                }
            }
            for (size_t i = 0; i < count; i++) {
                auto pin = view.pinned.find(nodes[i].id);
                if (hub[i] && pin != view.pinned.end()) {
                    view.moved["hub:" + nodes[i].id] = {pin->second.first - PAD, pin->second.second - PAD};
                    view.pinned.erase(pin);
                }
            }
            int loose = -1;
            for (size_t i = 0; i < count; i++) {
                if (hub[i]) {
                    continue;
                }
                if (view.pinned.count(nodes[i].id) != 0) {
                    nodes[i].cluster = -1;
                    continue;
                }
                size_t target;
                if (owner[i] >= 0) {
                    target = byHub[owner[i]];
                } else {
                    if (loose < 0) {
                        Cluster cluster;
                        cluster.id = "loose";
                        loose = (int) clusters.size();
                        clusters.push_back(std::move(cluster));
                    }
                    target = (size_t) loose;
                }
                nodes[i].cluster = (int) target;
                clusters[target].members.push_back((int) i);
            }
            for (Cluster& cluster : clusters) {
                auto keyOf = [&](int index) {
                    const std::vector<int>& using_ = users[(size_t) index];
                    if (using_.size() == 1 && using_.front() != cluster.hub &&
                        nodes[(size_t) using_.front()].cluster == nodes[(size_t) index].cluster) {
                        return lower(using_.front()) + "\x01" + lower(index);
                    }
                    return lower(index);
                };
                std::vector<std::pair<std::string, int>> keyed;
                for (int member : cluster.members) {
                    keyed.push_back({keyOf(member), member});
                }
                std::sort(keyed.begin(), keyed.end());
                for (size_t k = 0; k < keyed.size(); k++) {
                    cluster.members[k] = keyed[k].second;
                }
            }

            int lanes = laneCount(count);
            for (Cluster& cluster : clusters) {
                measure(cluster, lanes);
            }
            std::vector<size_t> order(clusters.size());
            for (size_t i = 0; i < order.size(); i++) {
                order[i] = i;
            }
            auto rank = [&](size_t index) {
                const Cluster& cluster = clusters[index];
                if (cluster.hub < 0) {
                    return 2;
                }
                return missing(nodes[(size_t) cluster.hub]) ? 0 : 1;
            };
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                if (rank(a) != rank(b)) {
                    return rank(a) < rank(b);
                }
                if (clusters[a].members.size() != clusters[b].members.size()) {
                    return clusters[a].members.size() > clusters[b].members.size();
                }
                return clusters[a].hub >= 0 && clusters[b].hub >= 0 && lower(clusters[a].hub) < lower(clusters[b].hub);
            });
            pack(view, clusters, order, lanes);
        }

        void pack(View& view, std::vector<Cluster>& clusters, const std::vector<size_t>& order, int lanes) {
            std::vector<float> sky((size_t) lanes, 0.0f);
            for (size_t index : order) {
                Cluster& cluster = clusters[index];
                int start = 0;
                float top = -1.0f;
                for (int s = 0; s + cluster.lanes <= lanes; s++) {
                    float highest = 0.0f;
                    for (int j = 0; j < cluster.lanes; j++) {
                        highest = std::max(highest, sky[(size_t) (s + j)]);
                    }
                    if (top < 0.0f || highest < top - 0.5f) {
                        top = highest;
                        start = s;
                    }
                }
                cluster.x = (float) start * LANE_STEP;
                cluster.y = top;
                for (int j = 0; j < cluster.lanes; j++) {
                    sky[(size_t) (start + j)] = top + cluster.h + CLUSTER_GAP;
                }
                auto moved = view.moved.find(cluster.id);
                if (moved != view.moved.end()) {
                    cluster.x = moved->second.first;
                    cluster.y = moved->second.second;
                }
            }
            view.clusters = std::move(clusters);
            place(view);
        }

        void arrange(View& view) {
            std::vector<Cluster> clusters;
            std::map<std::string, size_t> byGroup;
            for (size_t i = 0; i < view.nodes.size(); i++) {
                const Item* item = itemOf(view.nodes[i]);
                std::string name = item != nullptr ? item->group : std::string();
                auto found = byGroup.find(name);
                if (found == byGroup.end()) {
                    Cluster cluster;
                    cluster.id = "group:" + name;
                    cluster.title = name;
                    found = byGroup.emplace(name, clusters.size()).first;
                    clusters.push_back(std::move(cluster));
                }
                Cluster& cluster = clusters[found->second];
                view.nodes[i].cluster = (int) found->second;
                if (item != nullptr && item->head && cluster.hub < 0) {
                    cluster.hub = (int) i;
                    cluster.title.clear();
                } else {
                    cluster.members.push_back((int) i);
                }
            }
            for (Node& node : view.nodes) {
                node.dependents = 0;
            }
            for (const Edge& edge : view.edges) {
                if (edge.link == Link::Required || edge.link == Link::Optional) {
                    view.nodes[(size_t) edge.from].dependents++;
                }
            }
            int lanes = laneCount(view.nodes.size());
            std::vector<size_t> order(clusters.size());
            for (size_t i = 0; i < clusters.size(); i++) {
                measure(clusters[i], lanes);
                order[i] = i;
            }
            pack(view, clusters, order, lanes);
        }

        void buildModel(const Model& source, View& view) {
            std::vector<Node> nodes;
            std::vector<Edge> edges;
            std::map<std::string, int> byId;
            for (size_t i = 0; i < source.items.size(); i++) {
                const Item& item = source.items[i];
                if (item.id.empty() || byId.count(item.id) != 0) {
                    continue;
                }
                Node node;
                node.id = item.id;
                node.kind = Kind::Remote;
                node.item = (int) i;
                node.name = item.name;
                byId[item.id] = (int) nodes.size();
                nodes.push_back(std::move(node));
            }
            std::set<std::pair<int, int>> seen;
            for (const Relation& relation : source.relations) {
                auto from = byId.find(relation.from);
                auto to = byId.find(relation.to);
                if (from == byId.end() || to == byId.end() || from->second == to->second ||
                    !seen.insert({from->second, to->second}).second) {
                    continue;
                }
                edges.push_back(Edge{from->second, to->second, relation.link == Link::Required, relation.link});
            }
            view.nodes = std::move(nodes);
            view.edges = std::move(edges);
            if (source.grouped) {
                arrange(view);
            } else {
                group(view);
            }
            view.dragCluster = -1;
        }

        void build(const std::string& key, const Mods::List& list, View& view) {
            std::vector<Node> nodes;
            std::vector<Edge> edges;
            std::map<std::string, int> byModId;
            std::map<std::string, int> byProject;
            std::map<std::string, int> missing;
            std::set<std::pair<int, int>> seen;
            std::set<std::string> builtin(std::begin(BUILTIN), std::end(BUILTIN));

            auto claim = [&](const std::string& id, int index) {
                if (!id.empty()) {
                    byModId.emplace(id, index);
                }
            };
            for (size_t i = 0; i < list.mods.size(); i++) {
                const Mods::Mod& mod = list.mods[i];
                Node node;
                node.id = "file:" + stemOf(mod.info.file);
                node.mod = (int) i;
                node.name = ModManager::displayName(mod);
                int index = (int) nodes.size();
                if (const ModManager::Remote* remote = ModManager::remote(mod)) {
                    node.projectId = remote->projectId;
                }
                for (Addons::Source source : {Addons::Source::Modrinth, Addons::Source::CurseForge}) {
                    if (const ModManager::Remote* remote = ModManager::remote(mod, source)) {
                        byProject.emplace(remote->projectId, index);
                    }
                }
                nodes.push_back(std::move(node));
                claim(mod.info.id, index);
                for (const std::string& provided : mod.info.provides) {
                    claim(provided, index);
                }
            }
            size_t locals = nodes.size();
            for (const ModManager::Change& change : ModManager::changes(key)) {
                if (!change.file.empty()) {
                    continue;
                }
                Node node;
                node.id = "add:" + change.projectId;
                node.kind = Kind::Added;
                node.name = change.name;
                node.projectId = change.projectId;
                int index = (int) nodes.size();
                nodes.push_back(std::move(node));
                byProject.emplace(change.projectId, index);
                if (const ModManager::Project* project = ModManager::project(change.projectId)) {
                    std::string underscored = project->slug;
                    std::replace(underscored.begin(), underscored.end(), '-', '_');
                    claim(project->slug, index);
                    claim(underscored, index);
                }
            }
            auto absent = [&](const std::string& id, const std::string& name, const std::string& projectId) {
                auto found = missing.find(id);
                if (found != missing.end()) {
                    return found->second;
                }
                Node node;
                node.id = "missing:" + id;
                node.kind = Kind::Missing;
                node.name = name;
                node.projectId = projectId;
                int index = (int) nodes.size();
                nodes.push_back(std::move(node));
                missing[id] = index;
                return index;
            };
            auto link = [&](int from, int to, bool required) {
                if (from == to || !seen.insert({from, to}).second) {
                    return;
                }
                edges.push_back(Edge{from, to, required});
            };
            for (size_t n = 0; n < locals; n++) {
                const Mods::Mod& mod = list.mods[(size_t) nodes[n].mod];
                for (const blb::modpack::mod_dependency& dependency : mod.info.dependencies) {
                    if (dependency.id.empty() || builtin.count(dependency.id) != 0) {
                        continue;
                    }
                    auto found = byModId.find(dependency.id);
                    if (found != byModId.end()) {
                        link(found->second, (int) n, dependency.required);
                    } else if (dependency.required) {
                        link(absent("id:" + dependency.id, dependency.id, {}), (int) n, true);
                    }
                }
            }
            for (const ModManager::Change& change : ModManager::changes(key)) {
                std::string wanted = change.file.empty() ? "add:" + change.projectId : "file:" + stemOf(change.file);
                int target = -1;
                for (size_t i = 0; i < nodes.size(); i++) {
                    if (nodes[i].id == wanted) {
                        target = (int) i;
                        break;
                    }
                }
                if (target < 0) {
                    continue;
                }
                for (const Addons::Dependency& dependency : change.target.dependencies) {
                    if (!dependency.required || dependency.incompatible) {
                        continue;
                    }
                    std::string projectId = ModManager::dependencyProject(dependency);
                    if (projectId.empty()) {
                        continue;
                    }
                    auto found = byProject.find(projectId);
                    if (found != byProject.end()) {
                        link(found->second, target, true);
                        continue;
                    }
                    const ModManager::Project* project = ModManager::project(projectId);
                    if (project == nullptr) {
                        continue;
                    }
                    std::string underscored = project->slug;
                    std::replace(underscored.begin(), underscored.end(), '-', '_');
                    auto bySlug = byModId.find(project->slug);
                    if (bySlug == byModId.end()) {
                        bySlug = byModId.find(underscored);
                    }
                    if (!project->slug.empty() && bySlug != byModId.end()) {
                        link(bySlug->second, target, true);
                    } else {
                        link(absent("project:" + projectId, {}, projectId), target, true);
                    }
                }
            }
            view.nodes = std::move(nodes);
            view.edges = std::move(edges);
            for (auto it = view.pending.begin(); it != view.pending.end();) {
                bool arrived = std::any_of(view.nodes.begin(), view.nodes.end(),
                                           [&it](const Node& node) { return node.id == it->id; });
                if (arrived) {
                    view.pinned[it->id] = {it->x, it->y};
                    touch(view);
                    it = view.pending.erase(it);
                } else {
                    ++it;
                }
            }
            group(view);
            view.dragCluster = -1;
        }

        void fit(View& view, float offsetX, float offsetY, float w, float h, bool instant) {
            if (view.clusters.empty()) {
                return;
            }
            float minX = view.clusters.front().x;
            float minY = view.clusters.front().y;
            float maxX = minX;
            float maxY = minY;
            for (const Cluster& cluster : view.clusters) {
                minX = std::min(minX, cluster.x);
                minY = std::min(minY, cluster.y);
                maxX = std::max(maxX, cluster.x + cluster.w);
                maxY = std::max(maxY, cluster.y + cluster.h);
            }
            float spanW = maxX - minX;
            float spanH = maxY - minY;
            float zoom = std::min((w - 2.0f * MARGIN) / spanW, (h - 2.0f * MARGIN) / spanH);
            zoom = std::clamp(zoom, ZOOM_MIN, 1.0f);
            view.zoomTarget = zoom;
            view.panTargetX = spanW * zoom > w - 2.0f * MARGIN ? offsetX + MARGIN - minX * zoom
                                                               : offsetX + (w - spanW * zoom) * 0.5f - minX * zoom;
            view.panTargetY = spanH * zoom > h - 2.0f * MARGIN ? offsetY + MARGIN - minY * zoom
                                                               : offsetY + (h - spanH * zoom) * 0.5f - minY * zoom;
            if (instant) {
                view.zoom = view.zoomTarget;
                view.panX = view.panTargetX;
                view.panY = view.panTargetY;
            }
        }

        void zoomAt(View& view, float level, float x, float y) {
            level = std::clamp(level, ZOOM_MIN, ZOOM_MAX);
            float worldX = (x - view.panTargetX) / view.zoomTarget;
            float worldY = (y - view.panTargetY) / view.zoomTarget;
            view.zoomTarget = level;
            view.panTargetX = x - worldX * level;
            view.panTargetY = y - worldY * level;
        }

        void ease(float& value, float target, float t, float snap) {
            value += (target - value) * t;
            if (std::fabs(target - value) < snap) {
                value = target;
            }
        }

        void grid(const View& view, float originX, float originY, const Area& clip, float cx, float cy, float reveal) {
            if (reveal <= 0.001f) {
                return;
            }
            float step = 24.0f * view.zoom;
            while (step < 14.0f) {
                step *= 2.0f;
            }
            float reach = 0.0f;
            for (float px : {clip.x, clip.x + clip.w}) {
                for (float py : {clip.y, clip.y + clip.h}) {
                    reach = std::max(reach, std::hypot(px - cx, py - cy));
                }
            }
            float eased = 1.0f - (1.0f - reveal) * (1.0f - reveal) * (1.0f - reveal);
            float radius = eased * reach;
            uint32_t accent = UiTheme::accent();
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * eased;
            UiRender::roundRect(clip.x, clip.y, clip.w, clip.h, 0.0f, withAlpha(accent, 0.025f));
            UiRender::flush();
            ImDrawList* list = UiRender::drawList();
            float unit = UiRender::unit();
            ImU32 minor = imColor(withAlpha(accent, 0.22f));
            ImU32 major = imColor(withAlpha(accent, 0.4f));
            float dot = UiRender::transformW(1.0f) * unit;
            float baseX = originX + view.panX;
            float baseY = originY + view.panY;
            float startX = clip.x + std::fmod(baseX - clip.x, step);
            float startY = clip.y + std::fmod(baseY - clip.y, step);
            if (startX < clip.x) {
                startX += step;
            }
            if (startY < clip.y) {
                startY += step;
            }
            for (float gy = startY; gy <= clip.y + clip.h; gy += step) {
                float offsetY = gy - cy;
                if (std::fabs(offsetY) >= radius) {
                    continue;
                }
                float half = std::sqrt(radius * radius - offsetY * offsetY);
                long row = std::lround((gy - baseY) / step);
                for (float gx = startX; gx <= clip.x + clip.w; gx += step) {
                    if (std::fabs(gx - cx) >= half) {
                        continue;
                    }
                    long column = std::lround((gx - baseX) / step);
                    bool strong = row % 5 == 0 && column % 5 == 0;
                    list->AddCircleFilled(point(gx, gy), strong ? dot * 1.5f : dot, strong ? major : minor, 8);
                }
            }
            if (reveal < 1.0f) {
                list->AddCircle(point(cx, cy), UiRender::transformW(radius) * unit,
                                imColor(withAlpha(accent, 0.55f * (1.0f - eased))), 0,
                                UiRender::transformW(1.6f) * unit);
            }
            UiRender::alphaMul = saved;
        }

        void roundIcon(uint32_t texture, float x, float y, float size, float radius) {
            ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                 UiRender::transformW(size), UiRender::transformW(radius), texture, 0.0f, 0.0f, 1.0f,
                                 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        float rasterCorrection(int family, float size) {
            float raster = UiFont::textScale * UiRender::stableScale() * UiRender::rasterScale;
            float probe = UiFont::drawSlot(family, 100.0f).size / (100.0f * raster);
            float actual = UiFont::drawSlot(family, size).size;
            return actual > 0.0f ? size * raster * probe / actual : 1.0f;
        }

        void zoomText(const std::string& text, float x, float y, float h, uint32_t color, float base, float scale,
                      float raster) {
            if (text.empty()) {
                return;
            }
            float size = base * std::max(0.05f, raster);
            float baseW = UiFont::width(text, UiFont::MEDIUM, base);
            float drawnW = UiFont::width(text, UiFont::MEDIUM, size);
            float correction = drawnW > 0.0f ? baseW * scale / drawnW : scale / std::max(0.05f, raster);
            float savedScale = UiRender::scaleFactor;
            float savedX = UiRender::pivotX;
            float savedY = UiRender::pivotY;
            float pivotX = UiRender::transformX(x);
            float pivotY = UiRender::transformY(y + h * 0.5f);
            UiRender::scaleFactor = savedScale * correction;
            UiRender::pivotX = pivotX;
            UiRender::pivotY = pivotY;
            UiRender::textY(text, x, y, h, color, UiFont::MEDIUM, size);
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedX;
            UiRender::pivotY = savedY;
        }

        void scaledGlyph(int glyph, float cx, float cy, uint32_t color, float size, float zoom) {
            float shown = size * zoom;
            float correction = rasterCorrection(UiFont::ICON, shown);
            if (std::fabs(correction - 1.0f) < 0.002f) {
                correction = 1.002f;
            }
            float savedScale = UiRender::scaleFactor;
            float savedX = UiRender::pivotX;
            float savedY = UiRender::pivotY;
            float pivotX = UiRender::transformX(cx);
            float pivotY = UiRender::transformY(cy);
            UiRender::scaleFactor = savedScale * correction;
            UiRender::pivotX = pivotX;
            UiRender::pivotY = pivotY;
            UiWidgets::icon(Ui::glyph(glyph), cx, cy, color, shown);
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedX;
            UiRender::pivotY = savedY;
        }

        float easeInOut(float value) {
            return value < 0.5f
                    ? 4.0f * value * value * value
                    : 1.0f - (-2.0f * value + 2.0f) * (-2.0f * value + 2.0f) * (-2.0f * value + 2.0f) * 0.5f;
        }

        bool matches(const Mods::List& list, const Node& node, const std::string& query) {
            if (query.empty()) {
                return true;
            }
            if (node.kind == Kind::Local) {
                return ModManager::matches(list.mods[(size_t) node.mod], query);
            }
            return blb::icontains(nameOf(node), query);
        }

        void nodeMenu(const std::string& key, const Mods::List& list, const Node& node, float x, float y) {
            if (node.kind == Kind::Remote) {
                if (!openable(node)) {
                    return;
                }
                std::string id = node.id;
                std::function<void(const std::string&)> open = model->open;
                NewUi::openMenu(
                        "mods#graph#menu",
                        [id, open] {
                            MenuItems items;
                            items.push_back(ActionMenuItem::make("查看详情", 0xf05a, [id, open] { open(id); }));
                            return items;
                        },
                        x, y, 150.0f);
                return;
            }
            if (node.kind == Kind::Missing) {
                if (node.projectId.empty()) {
                    return;
                }
                std::string projectId = node.projectId;
                NewUi::openMenu(
                        "mods#graph#menu",
                        [key, projectId] {
                            MenuItems items;
                            items.push_back(ActionMenuItem::make(
                                    "添加前置", 0xf067, [key, projectId] { ModManager::addProject(key, projectId); }));
                            return items;
                        },
                        x, y, 150.0f);
                return;
            }
            if (node.kind == Kind::Added) {
                ModManager::Change change;
                change.projectId = node.projectId;
                NewUi::openMenu(
                        "mods#graph#menu",
                        [key, change] {
                            MenuItems items;
                            items.push_back(ActionMenuItem::make("撤销", 0xf0e2,
                                                                 [key, change] { ModManager::unstage(key, change); }));
                            return items;
                        },
                        x, y, 150.0f);
                return;
            }
            const Mods::Mod& mod = list.mods[(size_t) node.mod];
            std::filesystem::path file = mod.info.file;
            std::string homepage = mod.info.homepage;
            const ModManager::Change* staged = ModManager::changeFor(key, file);
            bool hasChange = staged != nullptr;
            ModManager::Change change = hasChange ? *staged : ModManager::Change();
            std::string id = node.id;
            NewUi::openMenu(
                    "mods#graph#menu",
                    [key, id, file, homepage, hasChange, change] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("切换版本", 0xf021, [key, id] {
                            View& view = views[key];
                            if (view.selected == id && view.expanded == id) {
                                view.picking = true;
                                return;
                            }
                            view.selected = id;
                            view.pickNext = true;
                        }));
                        if (hasChange) {
                            items.push_back(ActionMenuItem::make("撤销更改", 0xf0e2,
                                                                 [key, change] { ModManager::unstage(key, change); }));
                        }
                        items.push_back(
                                ActionMenuItem::make("在文件夹中显示", 0xf07c, [file] { Platform::reveal(file); }));
                        if (homepage.rfind("http", 0) == 0) {
                            items.push_back(ActionMenuItem::make("打开主页", 0xf35d,
                                                                 [homepage] { Platform::openUrl(homepage); }));
                        }
                        items.push_back(ActionMenuItem::make("删除", 0xf2ed, [key, file] { Mods::remove(key, file); })
                                                ->dangerous()
                                                ->confirmFirst());
                        return items;
                    },
                    x, y, 160.0f);
        }

        void drawNode(const std::string& key, const Mods::List& list, const Node& node, float nx, float ny, float zoom,
                      bool hovered, bool selected, bool dim, float appear, float raster) {
            float w = NODE_W * zoom;
            float h = NODE_H * zoom;
            float radius = 9.0f * zoom;
            float saved = UiRender::alphaMul;
            const Mods::Mod* mod = node.kind == Kind::Local ? &list.mods[(size_t) node.mod] : nullptr;
            const Item* item = itemOf(node);
            float alpha = (dim ? 0.3f : 1.0f) * appear;
            if ((mod != nullptr && !mod->info.enabled) || (item != nullptr && item->tone == Tone::Muted)) {
                alpha *= 0.5f;
            }
            UiRender::alphaMul = saved * alpha;
            const ModManager::Change* change = mod != nullptr ? ModManager::changeFor(key, mod->info.file) : nullptr;
            uint32_t accent = UiTheme::accent();
            bool root = item != nullptr && item->tone == Tone::Root;
            uint32_t fill = missing(node)              ? withAlpha(UiTheme::danger(), 0.08f)
                    : node.kind == Kind::Added || root ? withAlpha(accent, 0.16f)
                                                       : UiTheme::surfaceRaised();
            UiRender::roundRect(nx, ny, w, h, radius, fill);
            if (hovered) {
                UiRender::roundRect(nx, ny, w, h, radius, UiTheme::overlay(14));
            }
            uint32_t border = selected                                      ? accent
                    : missing(node)                                         ? withAlpha(UiTheme::danger(), 0.6f)
                    : node.kind == Kind::Added || change != nullptr || root ? withAlpha(accent, 0.6f)
                                                                            : UiTheme::border(UiTheme::outlineSoft());
            UiRender::outline(nx, ny, w, h, radius, border, 0xF, selected ? 1.6f : 1.0f);
            if (mod != nullptr && !mod->missing.empty()) {
                UiRender::circle(nx + w - 6.0f * zoom, ny + 6.0f * zoom, 3.2f * zoom, UiTheme::danger());
            }
            float pad = 9.0f * zoom;
            float icon = 20.0f * zoom;
            float iconX = nx + pad;
            float iconY = ny + (h - icon) * 0.5f;
            uint32_t texture = mod != nullptr ? Textures::of(mod->icon) : 0;
            if (item != nullptr && !item->iconUrl.empty() && icon >= 11.0f) {
                texture = Textures::of(Addons::icon(item->iconUrl));
            }
            if (texture != 0) {
                roundIcon(texture, iconX, iconY, icon, 5.0f * zoom);
            } else {
                int glyph = missing(node) ? 0xf071 : node.kind == Kind::Added ? 0xf067 : 0xf12e;
                uint32_t ink = missing(node)               ? UiTheme::danger()
                        : node.kind == Kind::Added || root ? UiTheme::accentText()
                                                           : UiTheme::textFaint();
                UiRender::roundRect(iconX, iconY, icon, icon, 5.0f * zoom, UiTheme::surface());
                scaledGlyph(glyph, iconX + icon * 0.5f, iconY + icon * 0.5f, ink, 10.0f, zoom);
            }
            const float textOffset = 37.0f;
            const float rightOffset = NODE_W - 9.0f;
            std::string version;
            uint32_t versionInk = UiTheme::textMuted();
            if (item != nullptr) {
                version = item->version;
                versionInk = missing(node) ? UiTheme::danger() : root ? UiTheme::accentText() : UiTheme::textMuted();
            } else if (node.kind == Kind::Missing) {
                version = "缺少";
                versionInk = UiTheme::danger();
            } else if (node.kind == Kind::Added) {
                for (const ModManager::Change& item : ModManager::changes(key)) {
                    if (item.file.empty() && item.projectId == node.projectId) {
                        version = item.target.number;
                    }
                }
                versionInk = UiTheme::accentText();
            } else if (change != nullptr) {
                version = "→ " + change->target.number;
                versionInk = UiTheme::accentText();
            } else {
                version = mod->info.version;
            }
            version =
                    UiFont::fitEnd(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, (rightOffset - textOffset) * 0.45f);
            float versionW = version.empty() ? 0.0f : UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!version.empty()) {
                zoomText(version, nx + (rightOffset - versionW) * zoom, ny, h, versionInk, UiWidgets::TEXT_SMALL, zoom,
                         raster);
            }
            std::string name = UiFont::fitEnd(nameOf(node), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                              std::max(0.0f, rightOffset - versionW - 6.0f - textOffset));
            zoomText(name, nx + textOffset * zoom, ny, h, UiTheme::textPrimary(), UiWidgets::TEXT_CONTROL, zoom,
                     raster);
            UiRender::alphaMul = saved;
        }

        std::string joinLinked(const View& view, int focus, Link link) {
            std::string out;
            for (const Edge& edge : view.edges) {
                if (edge.link != link || (edge.from != focus && edge.to != focus)) {
                    continue;
                }
                out += (out.empty() ? "" : "、") +
                        nameOf(view.nodes[(size_t) (edge.from == focus ? edge.to : edge.from)]);
            }
            return out;
        }

        std::string joinNames(const View& view, int focus, bool preds) {
            std::string out;
            for (const Edge& edge : view.edges) {
                if (edge.link == Link::Integration || edge.link == Link::Incompatible) {
                    continue;
                }
                int other = preds ? (edge.to == focus ? edge.from : -1) : (edge.from == focus ? edge.to : -1);
                if (other < 0) {
                    continue;
                }
                out += (out.empty() ? "" : "、") + nameOf(view.nodes[(size_t) other]);
            }
            return out.empty() ? std::string("无") : out;
        }

        struct Detail {
            std::vector<std::string> about;
            std::vector<std::pair<std::string, std::string>> rows;
            bool toggle = false;
            bool buttons = false;
            float height = 0.0f;
        };

        std::string sizeText(std::uint64_t bytes) {
            char text[32];
            if (bytes >= (1ull << 20)) {
                std::snprintf(text, sizeof(text), "%.1f MB", (double) bytes / 1048576.0);
            } else {
                std::snprintf(text, sizeof(text), "%.0f KB", (double) bytes / 1024.0);
            }
            return text;
        }

        std::string platformName(blb::modpack::mod_platform platform) {
            switch (platform) {
                case blb::modpack::mod_platform::fabric: return "Fabric";
                case blb::modpack::mod_platform::quilt: return "Quilt";
                case blb::modpack::mod_platform::forge: return "Forge";
                case blb::modpack::mod_platform::neoforge: return "NeoForge";
                case blb::modpack::mod_platform::legacy_forge: return "旧版 Forge";
                case blb::modpack::mod_platform::liteloader: return "LiteLoader";
            }
            return {};
        }

        std::string joined(const std::vector<std::string>& items) {
            std::string out;
            for (const std::string& item : items) {
                if (!item.empty()) {
                    out += (out.empty() ? "" : "、") + item;
                }
            }
            return out;
        }

        bool link(const std::string& url) {
            return url.rfind("http", 0) == 0;
        }

        void morphNode(const std::string& key, const Mods::List& list, const Node& node, const Area& row, float nx,
                       float ny, float zoom, float k) {
            const Mods::Mod& mod = list.mods[(size_t) node.mod];
            float saved = UiRender::alphaMul;
            if (!mod.info.enabled) {
                UiRender::alphaMul = saved * 0.5f;
            }
            float base = UiRender::alphaMul;
            float x = UiRender::lerp(row.x, nx, k);
            float y = UiRender::lerp(row.y, ny, k);
            float w = UiRender::lerp(row.w, NODE_W * zoom, k);
            float h = UiRender::lerp(row.h, NODE_H * zoom, k);
            float radius = UiRender::lerp(10.0f, 9.0f * zoom, k);
            float flight = std::sin(k * 3.14159265f);
            UiRender::alphaMul = base * flight * 0.8f;
            UiRender::shadow(x, y, w, h, radius, 14.0f, 5.0f, UiTheme::shadow());
            UiRender::alphaMul = base;
            float solid = std::max(k, flight);
            UiRender::roundRect(x, y, w, h, radius, withAlpha(UiTheme::surfaceRaised(), solid));
            UiRender::outline(x, y, w, h, radius, withAlpha(UiTheme::border(UiTheme::outlineSoft()), solid));

            float iconSize = UiRender::lerp(24.0f, 20.0f * zoom, k);
            float iconX = UiRender::lerp(row.x + 14.0f, nx + 9.0f * zoom, k);
            float iconY = y + (h - iconSize) * 0.5f;
            if (uint32_t texture = Textures::of(mod.icon)) {
                roundIcon(texture, iconX, iconY, iconSize, UiRender::lerp(6.0f, 5.0f * zoom, k));
            } else {
                UiRender::roundRect(iconX, iconY, iconSize, iconSize, UiRender::lerp(6.0f, 5.0f * zoom, k),
                                    UiTheme::surface());
                scaledGlyph(0xf12e, iconX + iconSize * 0.5f, iconY + iconSize * 0.5f, UiTheme::textFaint(),
                            UiRender::lerp(11.0f, 10.0f * zoom, k), 1.0f);
            }

            float scale = UiRender::lerp(1.0f, zoom, k);
            float textX = UiRender::lerp(row.x + 48.0f, nx + 37.0f * zoom, k);
            float right = UiRender::lerp(row.x + row.w - 90.0f, nx + (NODE_W - 9.0f) * zoom, k);
            const ModManager::Change* change = ModManager::changeFor(key, mod.info.file);
            std::string version = change != nullptr ? "→ " + change->target.number : mod.info.version;
            uint32_t versionInk = change != nullptr ? UiTheme::accentText() : UiTheme::textMuted();
            float room = std::max(0.0f, (right - textX) / scale);
            version = UiFont::fitEnd(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, room * 0.45f);
            float versionW = UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            std::string name = UiFont::fitEnd(ModManager::displayName(mod), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                              std::max(0.0f, room - versionW - 8.0f));
            float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            zoomText(name, textX, y, h, UiTheme::textPrimary(), UiWidgets::TEXT_CONTROL, scale, scale);
            if (!version.empty()) {
                float versionX = UiRender::lerp(textX + (nameW + 8.0f) * scale, right - versionW * scale, k);
                zoomText(version, versionX, y, h, versionInk, UiWidgets::TEXT_SMALL, scale, scale);
            }
            UiRender::alphaMul = saved;
        }

        Detail detailOf(const Mods::List& list, const View& view, int focus) {
            Detail detail;
            const Node& node = view.nodes[(size_t) focus];
            const Mods::Mod* mod = node.kind == Kind::Local ? &list.mods[(size_t) node.mod] : nullptr;
            const Item* item = itemOf(node);
            if (item != nullptr) {
                std::string about = item->about;
                std::replace(about.begin(), about.end(), '\n', ' ');
                about = Ui::trim(about);
                if (!about.empty()) {
                    std::vector<std::string> lines =
                            UiFont::wrap(about, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, CARD_W - 28.0f);
                    if (lines.size() > (size_t) ABOUT_LINES) {
                        lines.resize(ABOUT_LINES);
                        lines.back() = UiFont::fitEnd(lines.back() + "…", UiFont::MEDIUM, UiWidgets::TEXT_SMALL,
                                                      CARD_W - 28.0f);
                    }
                    detail.about = lines;
                }
                detail.rows = item->rows;
            }
            if (mod != nullptr) {
                std::string about = blb::strip_formatting(mod->info.description);
                std::replace(about.begin(), about.end(), '\n', ' ');
                std::replace(about.begin(), about.end(), '\r', ' ');
                about = Ui::trim(about);
                if (!about.empty()) {
                    std::vector<std::string> lines =
                            UiFont::wrap(about, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, CARD_W - 28.0f);
                    if (lines.size() > (size_t) ABOUT_LINES) {
                        std::string tail = lines[ABOUT_LINES - 1];
                        for (size_t i = ABOUT_LINES; i < lines.size(); i++) {
                            tail += " " + lines[i];
                        }
                        lines.resize(ABOUT_LINES);
                        lines.back() = UiFont::fitEnd(tail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, CARD_W - 28.0f);
                    }
                    detail.about = lines;
                }
                std::string chinese = ModManager::chineseName(*mod);
                if (!chinese.empty()) {
                    if (ModNames::original()) {
                        detail.rows.push_back({"中文名", chinese});
                    } else {
                        detail.rows.push_back({"原名", mod->info.name});
                    }
                }
                std::string authors = joined(mod->info.authors);
                if (!authors.empty()) {
                    detail.rows.push_back({"作者", authors});
                }
                if (!mod->info.id.empty()) {
                    detail.rows.push_back({"ID", mod->info.id});
                }
                std::vector<std::string> platforms;
                for (blb::modpack::mod_platform platform : mod->info.platforms) {
                    platforms.push_back(platformName(platform));
                }
                if (!platforms.empty()) {
                    detail.rows.push_back({"加载器", joined(platforms)});
                }
                if (!mod->info.license.empty()) {
                    detail.rows.push_back({"许可", mod->info.license});
                }
            }
            detail.rows.push_back({"依赖", joinNames(view, focus, true)});
            detail.rows.push_back({"被依赖", joinNames(view, focus, false)});
            if (std::string linked = joinLinked(view, focus, Link::Integration); !linked.empty()) {
                detail.rows.push_back({"联动", linked});
            }
            if (std::string linked = joinLinked(view, focus, Link::Incompatible); !linked.empty()) {
                detail.rows.push_back({"不兼容", linked});
            }
            if (mod != nullptr) {
                detail.rows.push_back({"文件", mod->info.file_name + "  " + sizeText(mod->info.size)});
            }
            detail.toggle = mod != nullptr;
            detail.buttons =
                    node.kind == Kind::Remote ? openable(node) : node.kind != Kind::Missing || !node.projectId.empty();
            detail.height = CARD_HEAD + 8.0f + (float) detail.about.size() * ABOUT_LINE +
                    (detail.about.empty() ? 0.0f : 6.0f) +
                    (float) (detail.rows.size() + (detail.toggle ? 1 : 0)) * CARD_ROW +
                    (detail.buttons ? 8.0f + Page::FIELD_H : 0.0f) + 14.0f;
            return detail;
        }

        void cardHeader(const std::string& key, const Mods::List& list, const Node& node, const Area& area,
                        bool picking, bool blocked, View& view) {
            float left = area.x + 14.0f;
            float right = area.x + CARD_W - 14.0f;
            const Mods::Mod* mod = node.kind == Kind::Local ? &list.mods[(size_t) node.mod] : nullptr;
            const Item* item = itemOf(node);
            float iconSize = 26.0f;
            float iconY = area.y + (CARD_HEAD - iconSize) * 0.5f;
            uint32_t texture = mod != nullptr ? Textures::of(mod->icon) : 0;
            if (item != nullptr && !item->iconUrl.empty()) {
                texture = Textures::of(Addons::icon(item->iconUrl));
            }
            if (texture != 0) {
                roundIcon(texture, left, iconY, iconSize, 7.0f);
            } else {
                int glyph = node.kind == Kind::Missing ? 0xf071 : node.kind == Kind::Added ? 0xf067 : 0xf12e;
                uint32_t ink = node.kind == Kind::Missing ? UiTheme::danger()
                        : node.kind == Kind::Added        ? UiTheme::accentText()
                                                          : UiTheme::textFaint();
                UiRender::roundRect(left, iconY, iconSize, iconSize, 7.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(glyph), left + iconSize * 0.5f, iconY + iconSize * 0.5f, ink, 12.0f);
            }
            float textX = left + iconSize + 10.0f;
            float limit = right;
            if (picking) {
                float closeX = right - 26.0f;
                if (Page::iconButton("mods#graph#card#close", 0xf00d, "收起", closeX,
                                     area.y + (CARD_HEAD - 28.0f) * 0.5f, blocked)) {
                    view.picking = false;
                }
                limit = closeX - 8.0f;
            } else {
                std::string version;
                uint32_t versionInk = UiTheme::textMuted();
                if (item != nullptr) {
                    version = item->version;
                    versionInk = missing(node) ? UiTheme::danger() : UiTheme::textMuted();
                } else if (node.kind == Kind::Missing) {
                    version = "缺少";
                    versionInk = UiTheme::danger();
                } else if (mod != nullptr) {
                    const ModManager::Change* change = ModManager::changeFor(key, mod->info.file);
                    version = change != nullptr ? mod->info.version + " → " + change->target.number : mod->info.version;
                    versionInk = change != nullptr ? UiTheme::accentText() : UiTheme::textMuted();
                } else {
                    for (const ModManager::Change& item : ModManager::changes(key)) {
                        if (item.file.empty() && item.projectId == node.projectId) {
                            version = "新增 " + item.target.number;
                        }
                    }
                    versionInk = UiTheme::accentText();
                }
                float versionW = std::min(UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL), 150.0f);
                UiRender::textY(UiFont::fitEnd(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, versionW),
                                right - versionW, area.y, CARD_HEAD, versionInk, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                limit = right - versionW - 10.0f;
            }
            UiRender::textY(UiFont::fitEnd(nameOf(node), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, limit - textX), textX,
                            area.y, CARD_HEAD, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::hLine(area.x + 12.0f, area.x + CARD_W - 12.0f, area.y + CARD_HEAD, UiTheme::outlineSoft());
        }

        void cardDetails(const std::string& key, const Mods::List& list, View& view, int focus, const Detail& detail,
                         const Area& area, bool blocked) {
            const Node& node = view.nodes[(size_t) focus];
            const Mods::Mod* mod = node.kind == Kind::Local ? &list.mods[(size_t) node.mod] : nullptr;
            float left = area.x + 14.0f;
            float right = area.x + CARD_W - 14.0f;
            float y = area.y + CARD_HEAD + 8.0f;
            for (const std::string& line : detail.about) {
                UiRender::textY(line, left, y, ABOUT_LINE, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                y += ABOUT_LINE;
            }
            if (!detail.about.empty()) {
                y += 6.0f;
            }
            if (detail.toggle && mod != nullptr) {
                UiRender::textY("启用", left, y, CARD_ROW, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                std::filesystem::path file = mod->info.file;
                bool enabled = mod->info.enabled;
                auto flag = std::make_shared<FlagBinding>(
                        "mods#graph#card#enabled", [enabled] { return enabled; },
                        [key, file](bool on) { Mods::setEnabled(key, file, on); });
                ToggleWidget toggle("mods#graph#card#enabled", flag, true);
                float toggleX = left + LABEL_W;
                float toggleY = y + (CARD_ROW - 18.0f) * 0.5f;
                toggle.bounds(toggleX, toggleY, 30.0f, 18.0f)
                        .hitArea(toggleX - 4.0f, y + 2.0f, 38.0f, CARD_ROW - 4.0f)
                        .block(blocked);
                toggle.paint();
                y += CARD_ROW;
            }
            for (const auto& row : detail.rows) {
                UiRender::textY(row.first, left, y, CARD_ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                float valueX = left + LABEL_W;
                std::string fitted = UiFont::fitEnd(row.second, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, right - valueX);
                UiRender::textY(fitted, valueX, y, CARD_ROW, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                if (fitted != row.second && !blocked && NewUi::hit(valueX, y, right - valueX, CARD_ROW)) {
                    Page::tooltip(row.second, valueX, y, right - valueX, CARD_ROW);
                }
                y += CARD_ROW;
            }
            if (!detail.buttons) {
                return;
            }
            float buttonY = y + 8.0f;
            if (node.kind == Kind::Remote) {
                std::string label = "查看详情";
                float buttonW = Page::buttonWidth(label, 0xf05a);
                if (Page::button("mods#graph#card#open", label, left, buttonY, buttonW, true, blocked, 0xf05a)) {
                    model->open(node.id);
                }
                return;
            }
            if (node.kind == Kind::Missing) {
                std::string label = "添加前置";
                float buttonW = Page::buttonWidth(label, 0xf067);
                bool busy = ModManager::adding(key, node.projectId);
                if (Page::button("mods#graph#card#add", label, left, buttonY, buttonW, true, blocked || busy, 0xf067)) {
                    ModManager::addProject(key, node.projectId);
                }
                return;
            }
            if (node.kind == Kind::Added) {
                std::string label = "撤销";
                float buttonW = Page::buttonWidth(label, 0xf0e2);
                if (Page::button("mods#graph#card#undo", label, left, buttonY, buttonW, false, blocked, 0xf0e2)) {
                    ModManager::Change change;
                    change.projectId = node.projectId;
                    ModManager::unstage(key, change);
                }
                return;
            }
            std::string pick = "切换版本";
            float pickW = Page::buttonWidth(pick, 0xf021);
            if (Page::button("mods#graph#card#pick", pick, left, buttonY, pickW, true, blocked, 0xf021)) {
                view.picking = true;
            }
            float linkY = buttonY + (Page::FIELD_H - 28.0f) * 0.5f;
            float linkX = right - 26.0f;
            if (Page::iconButton("mods#graph#card#reveal", 0xf07c, "在文件夹中显示", linkX, linkY, blocked)) {
                Platform::reveal(mod->info.file);
            }
            if (long long wiki = ModManager::wikiId(*mod); wiki > 0) {
                linkX -= 30.0f;
                if (Page::iconButton("mods#graph#card#wiki", 0xf02d, "在 MC 百科上查看", linkX, linkY, blocked)) {
                    Platform::openUrl("https://www.mcmod.cn/class/" + std::to_string(wiki) + ".html");
                }
            }
            const std::tuple<std::string, int, const char*> links[] = {{mod->info.homepage, 0xf0ac, "主页"},
                                                                       {mod->info.sources, 0xf121, "源代码"},
                                                                       {mod->info.issues, 0xf188, "问题反馈"}};
            for (const auto& [url, glyph, hint] : links) {
                if (!link(url)) {
                    continue;
                }
                linkX -= 30.0f;
                if (Page::iconButton(std::string("mods#graph#card#link#") + hint, glyph, hint, linkX, linkY, blocked)) {
                    Platform::openUrl(url);
                }
            }
        }

        float cardHeight(const Detail& detail, float pick) {
            return UiRender::lerp(detail.height, CARD_HEAD + PICK_H, pick);
        }

        void detailCard(const std::string& key, const Mods::List& list, View& view, int focus, const Detail& detail,
                        const Area& area, float reveal, float pick, bool blocked) {
            const Node& node = view.nodes[(size_t) focus];
            float radius = UiRender::lerp(9.0f * view.zoom, 12.0f, reveal);
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * reveal;
            UiRender::shadow(area.x, area.y, area.w, area.h, radius, 16.0f, 6.0f, UiTheme::shadow());
            UiRender::alphaMul = saved;
            UiRender::roundRect(area.x, area.y, area.w, area.h, radius, UiTheme::surface());
            UiRender::outline(area.x, area.y, area.w, area.h, radius,
                              UiRender::mix(UiTheme::accent(), UiTheme::border(UiTheme::outlineSoft()), reveal));
            float content = std::clamp((reveal - 0.35f) / 0.65f, 0.0f, 1.0f);
            if (content <= 0.001f) {
                return;
            }
            blocked = blocked || reveal < 0.9f;
            UiRender::scissor(area.x, area.y, area.w, area.h);
            UiRender::alphaMul = saved * content;
            cardHeader(key, list, node, area, view.picking && pick > 0.5f, blocked, view);
            if (pick < 0.999f) {
                UiRender::alphaMul = saved * content * (1.0f - pick);
                cardDetails(key, list, view, focus, detail, area, blocked || pick > 0.001f);
            }
            if (pick > 0.001f && node.kind == Kind::Local) {
                UiRender::alphaMul = saved * content * pick;
                const Mods::Mod& mod = list.mods[(size_t) node.mod];
                if (ModVersionPicker::list("mods#graph#versions", key, mod, area.x + 12.0f, area.y + CARD_HEAD + 10.0f,
                                           CARD_W - 24.0f, PICK_H - 22.0f, blocked || pick < 0.9f)) {
                    view.picking = false;
                }
            }
            UiRender::unscissor();
            UiRender::alphaMul = saved;
        }

        void message(const std::string& text, float x, float y, float w, float h, bool loading) {
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float lead = loading ? 24.0f : 0.0f;
            float left = x + (w - textW - lead) * 0.5f;
            float top = y + h * 0.4f;
            if (loading) {
                Page::spinner(left + 8.0f, top + 12.0f, 8.0f, UiTheme::accent());
            }
            UiRender::textY(text, left + lead, top, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }
    }

    void render(const std::string& key, const Instances::Entry* entry, const Mods::List& list, const Model* source,
                const std::string& query, const Frame& frame, bool blocked) {
        const Area& clip = frame.clip;
        float originX = frame.layout.x;
        float originY = frame.layout.y;
        Area fitArea{frame.layout.x, frame.layout.y + frame.top, frame.layout.w,
                     frame.layout.h - frame.top - frame.bottom};
        float centreX = frame.focused ? frame.focusX : fitArea.x + fitArea.w * 0.5f;
        float centreY = frame.focused ? frame.focusY : fitArea.y + fitArea.h * 0.5f;
        float reach = 1.0f;
        for (float px : {clip.x, clip.x + clip.w}) {
            for (float py : {clip.y, clip.y + clip.h}) {
                reach = std::max(reach, std::hypot(px - centreX, py - centreY));
            }
        }
        if (frame.chrome > 0.001f) {
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * frame.chrome;
            Page::card(clip.x, clip.y, clip.w, clip.h);
            UiRender::alphaMul = saved;
        }
        if (source != nullptr && source->items.empty()) {
            return;
        }
        if (source == nullptr && list.mods.empty()) {
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * frame.nodes;
            message(list.loading ? "正在读取模组…" : "还没有模组", fitArea.x, fitArea.y, fitArea.w, fitArea.h,
                    list.loading);
            UiRender::alphaMul = saved;
            return;
        }
        View& view = views[key];
        if (!view.loaded) {
            if (entry != nullptr) {
                load(view, *entry);
            } else {
                view.loaded = true;
            }
        }
        view.originX = originX;
        view.originY = originY;
        std::string signature = source != nullptr ? source->signature : signatureOf(key, list);
        if (signature != view.signature) {
            view.signature = signature;
            if (source != nullptr) {
                buildModel(*source, view);
            } else {
                build(key, list, view);
            }
            if (!view.fitted || (source != nullptr && !view.touched)) {
                fit(view, fitArea.x - originX, fitArea.y - originY, fitArea.w, fitArea.h, !view.fitted);
                view.fitted = true;
            }
        }
        if (query != view.lastQuery) {
            view.lastQuery = query;
            int only = -1;
            int found = 0;
            for (size_t i = 0; i < view.nodes.size() && !query.empty() && found < 2; i++) {
                if (matches(list, view.nodes[i], query)) {
                    only = (int) i;
                    found++;
                }
            }
            if (found == 1) {
                const Node& node = view.nodes[(size_t) only];
                float level = std::clamp(std::max(view.zoomTarget, 1.0f), ZOOM_MIN, ZOOM_MAX);
                view.zoomTarget = level;
                view.panTargetX = fitArea.x - originX + fitArea.w * 0.5f - (node.x + NODE_W * 0.5f) * level;
                view.panTargetY = fitArea.y - originY + fitArea.h * 0.5f - (node.y + NODE_H * 0.5f) * level;
            }
        }
        if (frame.nodes < 0.999f) {
            view.selected.clear();
            view.dragCluster = -1;
            view.panning = false;
        }
        int focus = -1;
        for (size_t i = 0; i < view.nodes.size(); i++) {
            if (view.nodes[i].id == view.selected) {
                focus = (int) i;
            }
        }
        if (focus < 0) {
            view.selected.clear();
        }

        float t = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f);
        ease(view.zoom, view.zoomTarget, t, 0.0005f);
        ease(view.panX, view.panTargetX, t, 0.05f);
        ease(view.panY, view.panTargetY, t, 0.05f);
        if (view.zoom != view.zoomTarget || view.panX != view.panTargetX || view.panY != view.panTargetY) {
            UiState::keepAnimating();
        }

        float zoom = view.zoom;
        auto screenX = [&view, originX](float wx) { return originX + view.panX + wx * view.zoom; };
        auto screenY = [&view, originY](float wy) { return originY + view.panY + wy * view.zoom; };
        float nodeW = NODE_W * zoom;
        float nodeH = NODE_H * zoom;

        std::vector<float> appear(view.clusters.size(), 1.0f);
        for (size_t i = 0; i < view.clusters.size(); i++) {
            if (frame.nodes >= 0.999f) {
                break;
            }
            const Cluster& cluster = view.clusters[i];
            float distance = std::hypot(screenX(cluster.x + cluster.w * 0.5f) - centreX,
                                        screenY(cluster.y + cluster.h * 0.5f) - centreY);
            float local = std::clamp((frame.nodes - std::min(1.0f, distance / reach) * 0.45f) / 0.55f, 0.0f, 1.0f);
            appear[i] = 1.0f - (1.0f - local) * (1.0f - local) * (1.0f - local);
        }
        std::map<std::string, size_t> sources;
        for (size_t i = 0; i < frame.rows.size(); i++) {
            sources["file:" + stemOf(frame.rows[i].file)] = i;
        }
        std::vector<int> sourceOf(view.nodes.size(), -1);
        std::vector<float> landing(view.nodes.size(), 1.0f);
        if (frame.morph < 0.999f) {
            for (size_t i = 0; i < view.nodes.size(); i++) {
                auto found = sources.find(view.nodes[i].id);
                if (found == sources.end()) {
                    continue;
                }
                sourceOf[i] = (int) found->second;
                float start = 0.06f + (float) found->second * 0.02f;
                landing[i] = easeInOut(std::clamp((frame.morph - start) / 0.42f, 0.0f, 1.0f));
            }
        }
        auto appearOf = [&](int node) {
            int cluster = view.nodes[(size_t) node].cluster;
            return cluster >= 0 && cluster < (int) appear.size() ? appear[(size_t) cluster] : frame.nodes;
        };

        std::string openId = "mods#graph#open#" + key;
        std::string pickId = "mods#graph#pick#" + key;
        if (!view.selected.empty() && view.selected != view.expanded) {
            view.expanded = view.selected;
            view.picking = view.pickNext;
            view.pickNext = false;
            UiState::remove(openId);
            UiState::remove(pickId);
        }
        int shown = -1;
        float open = 0.0f;
        for (size_t i = 0; i < view.nodes.size() && !view.expanded.empty(); i++) {
            if (view.nodes[i].id == view.expanded) {
                shown = (int) i;
            }
        }
        if (shown >= 0) {
            open = UiState::motionFrom(openId, view.selected.empty() ? 0.0f : 1.0f, 16.0f, 0.0f);
            if (view.selected.empty() && open < 0.02f) {
                view.expanded.clear();
                view.picking = false;
                shown = -1;
            }
        } else {
            view.expanded.clear();
        }
        if (shown >= 0 && view.nodes[(size_t) shown].kind != Kind::Local) {
            view.picking = false;
        }
        float reveal = 1.0f - (1.0f - open) * (1.0f - open) * (1.0f - open);
        float pickMotion = UiState::motionFrom(pickId, view.picking ? 1.0f : 0.0f, 14.0f, 0.0f);
        float pick = pickMotion * pickMotion * (3.0f - 2.0f * pickMotion);
        Detail info;
        Area card;
        if (shown >= 0) {
            info = detailOf(list, view, shown);
            const Node& node = view.nodes[(size_t) shown];
            float nx = screenX(node.x);
            float ny = screenY(node.y);
            float height = cardHeight(info, pick);
            float lowX = fitArea.x + 12.0f;
            float lowY = fitArea.y + 12.0f;
            float tx = std::clamp(nx, lowX, std::max(lowX, fitArea.x + fitArea.w - 12.0f - CARD_W));
            float ty = std::clamp(ny, lowY, std::max(lowY, fitArea.y + fitArea.h - 12.0f - height));
            card = Area{UiRender::lerp(nx, tx, reveal), UiRender::lerp(ny, ty, reveal),
                        UiRender::lerp(nodeW, CARD_W, reveal), UiRender::lerp(nodeH, height, reveal)};
        }

        float toolsW = 5.0f * TOOL_STEP - 4.0f + 8.0f;
        float toolsH = 36.0f;
        float toolsX = fitArea.x + fitArea.w - 14.0f - toolsW;
        float toolsY = fitArea.y + fitArea.h - 14.0f - toolsH;
        float mx = NewUi::mouseX;
        float my = NewUi::mouseY;
        bool inHole = false;
        for (const Area& hole : frame.holes) {
            inHole = inHole || NewUi::rawHit(hole.x, hole.y, hole.w, hole.h);
        }
        bool overTools = NewUi::rawHit(toolsX, toolsY, toolsW, toolsH);
        bool overCard = shown >= 0 && NewUi::rawHit(card.x, card.y, card.w, card.h);
        bool canvasHover = !blocked && NewUi::hit(clip.x, clip.y, clip.w, clip.h) && my >= frame.inputTop && !inHole &&
                !overTools && !overCard;

        int hovered = view.dragNode >= 0 && view.dragNode < (int) view.nodes.size() ? view.dragNode : -1;
        int hoveredCluster = view.dragCluster;
        if (canvasHover && !view.panning && view.dragCluster < 0 && view.dragNode < 0) {
            for (int i = (int) view.nodes.size() - 1; i >= 0; i--) {
                const Node& node = view.nodes[(size_t) i];
                if (NewUi::rawHit(screenX(node.x), screenY(node.y), nodeW, nodeH)) {
                    hovered = i;
                    break;
                }
            }
            if (hovered < 0 && movable) {
                for (int i = (int) view.clusters.size() - 1; i >= 0; i--) {
                    const Cluster& cluster = view.clusters[(size_t) i];
                    if (NewUi::rawHit(screenX(cluster.x), screenY(cluster.y), cluster.w * zoom, cluster.h * zoom)) {
                        hoveredCluster = i;
                        break;
                    }
                }
            }
        }
        int highlight = focus >= 0 ? focus : hovered;
        std::vector<bool> related(view.nodes.size(), false);
        if (focus >= 0) {
            for (const Edge& edge : view.edges) {
                if (edge.from == focus || edge.to == focus) {
                    related[(size_t) edge.from] = true;
                    related[(size_t) edge.to] = true;
                }
            }
        }

        UiRender::scissor(clip.x + 1.0f, clip.y + 1.0f, clip.w - 2.0f, clip.h - 2.0f);
        grid(view, originX, originY, clip, centreX, centreY, frame.grid);

        for (size_t i = 0; i < view.clusters.size(); i++) {
            const Cluster& cluster = view.clusters[i];
            if (appear[i] <= 0.001f) {
                continue;
            }
            float cx = screenX(cluster.x);
            float cy = screenY(cluster.y);
            float cw = cluster.w * zoom;
            float ch = cluster.h * zoom;
            if (cx + cw < clip.x || cx > clip.x + clip.w || cy + ch < clip.y || cy > clip.y + clip.h) {
                continue;
            }
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * appear[i];
            bool absent = cluster.hub >= 0 && missing(view.nodes[(size_t) cluster.hub]);
            float radius = CLUSTER_RADIUS * zoom;
            UiRender::roundRect(cx, cy, cw, ch, radius, withAlpha(UiTheme::textPrimary(), 0.035f));
            if ((int) i == hoveredCluster) {
                UiRender::roundRect(cx, cy, cw, ch, radius, withAlpha(UiTheme::textPrimary(), 0.03f));
            }
            UiRender::outline(cx, cy, cw, ch, radius,
                              absent ? withAlpha(UiTheme::danger(), 0.45f) : UiTheme::border(UiTheme::outlineSoft()));
            if (cluster.hub < 0) {
                std::string title = (cluster.title.empty() ? std::string("无前置依赖") : cluster.title) + "  " +
                        std::to_string(cluster.members.size());
                zoomText(UiFont::fitEnd(title, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, cluster.w - 2.0f * PAD),
                         cx + (PAD + 4.0f) * zoom, cy + PAD * zoom, TITLE_H * zoom, UiTheme::textMuted(),
                         UiWidgets::TEXT_SMALL, zoom, view.zoomTarget);
            } else if (cluster.lanes >= 2) {
                std::string caption =
                        "被 " + std::to_string(view.nodes[(size_t) cluster.hub].dependents) + " 个模组依赖";
                float captionOffset = PAD + NODE_W + 14.0f;
                zoomText(
                        UiFont::fitEnd(caption, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, cluster.w - captionOffset - PAD),
                        cx + captionOffset * zoom, cy + PAD * zoom, NODE_H * zoom, UiTheme::textMuted(),
                        UiWidgets::TEXT_SMALL, zoom, view.zoomTarget);
            }
            UiRender::alphaMul = saved;
        }

        struct Joint {
            float x = 0.0f;
            float y = 0.0f;
            ImU32 ink = 0;
        };
        std::vector<Joint> joints;
        if (highlight >= 0 && frame.nodes > 0.001f) {
            UiRender::flush();
            ImDrawList* drawList = UiRender::drawList();
            float unit = UiRender::unit();
            bool strong = focus >= 0;
            float head = UiRender::lerp(nodeH, CARD_HEAD, reveal);
            auto bounds = [&](int index, float& left, float& width, float& anchor) {
                if (index == shown) {
                    left = card.x;
                    width = card.w;
                    anchor = card.y + head * 0.5f;
                    return;
                }
                const Node& node = view.nodes[(size_t) index];
                left = screenX(node.x);
                width = nodeW;
                anchor = screenY(node.y) + nodeH * 0.5f;
            };
            float dot = UiRender::transformW(std::max(2.0f, 3.0f * zoom)) * unit;
            for (const Edge& edge : view.edges) {
                if (edge.from != highlight && edge.to != highlight) {
                    continue;
                }
                float ax;
                float aw;
                float ay;
                float bx;
                float bw;
                float by;
                bounds(edge.from, ax, aw, ay);
                bounds(edge.to, bx, bw, by);
                float gap = 8.0f * zoom;
                float x0;
                float x3;
                float c0;
                float c3;
                if (bx >= ax + aw + gap) {
                    x0 = ax + aw;
                    x3 = bx;
                    float bend = std::max((x3 - x0) * 0.5f, std::min(30.0f * zoom, x3 - x0));
                    c0 = x0 + bend;
                    c3 = x3 - bend;
                } else if (bx + bw <= ax - gap) {
                    x0 = ax;
                    x3 = bx + bw;
                    float bend = std::max((x0 - x3) * 0.5f, std::min(30.0f * zoom, x0 - x3));
                    c0 = x0 - bend;
                    c3 = x3 + bend;
                } else if (bx + bw * 0.5f >= ax + aw * 0.5f) {
                    x0 = ax + aw;
                    x3 = bx + bw;
                    c0 = std::max(x0, x3) + 30.0f * zoom + std::fabs(by - ay) * 0.2f;
                    c3 = c0;
                } else {
                    x0 = ax;
                    x3 = bx;
                    c0 = std::min(x0, x3) - 30.0f * zoom - std::fabs(by - ay) * 0.2f;
                    c3 = c0;
                }
                const Node& from = view.nodes[(size_t) edge.from];
                bool dependency = edge.to == highlight;
                uint32_t color = edge.link == Link::Integration            ? UiRender::color(91, 181, 219)
                        : edge.link == Link::Incompatible || missing(from) ? UiTheme::danger()
                        : dependency                                       ? UiTheme::accent()
                                                                           : UiTheme::textSecondary();
                bool solid = edge.required || edge.link == Link::Integration || edge.link == Link::Incompatible;
                float alpha = (strong ? 0.9f : 0.55f) * (solid ? 1.0f : 0.5f) * frame.nodes;
                ImU32 ink = imColor(withAlpha(color, alpha));
                float thickness = UiRender::transformW((strong ? 1.8f : 1.3f) * std::clamp(zoom, 0.6f, 1.2f)) * unit;
                drawList->AddBezierCubic(point(x0, ay), point(c0, ay), point(c3, by), point(x3, by), ink, thickness);
                if (edge.to == shown) {
                    joints.push_back(Joint{x3, by, ink});
                } else {
                    drawList->AddCircleFilled(point(x3, by), dot, ink);
                }
                if (edge.from == shown) {
                    joints.push_back(Joint{x0, ay, ink});
                }
            }
        }

        for (size_t i = 0; i < view.nodes.size(); i++) {
            const Node& node = view.nodes[i];
            float grow = sourceOf[i] >= 0 ? (landing[i] >= 0.999f ? 1.0f : 0.0f) : appearOf((int) i);
            if (grow <= 0.001f || (int) i == shown) {
                continue;
            }
            float scale = 0.85f + 0.15f * grow;
            float nx = screenX(node.x) + nodeW * (1.0f - scale) * 0.5f;
            float ny = screenY(node.y) + nodeH * (1.0f - scale) * 0.5f;
            if (nx + nodeW < clip.x || nx > clip.x + clip.w || ny + nodeH < clip.y || ny > clip.y + clip.h) {
                continue;
            }
            bool dim = !matches(list, node, query) || (focus >= 0 && !related[i] && (int) i != focus);
            drawNode(key, list, node, nx, ny, zoom * scale, (int) i == hovered, node.id == view.selected, dim, grow,
                     view.zoomTarget);
        }

        for (size_t i = 0; i < view.nodes.size(); i++) {
            if (sourceOf[i] < 0 || landing[i] >= 0.999f) {
                continue;
            }
            const Node& node = view.nodes[i];
            morphNode(key, list, node, frame.rows[(size_t) sourceOf[i]].area, screenX(node.x), screenY(node.y), zoom,
                      landing[i]);
        }

        double now = ImGui::GetTime();
        view.pending.erase(std::remove_if(view.pending.begin(), view.pending.end(),
                                          [now](const Pending& item) { return now - item.since > 15.0; }),
                           view.pending.end());
        for (const Pending& item : view.pending) {
            float gx = screenX(item.x);
            float gy = screenY(item.y);
            float radius = 9.0f * zoom;
            UiRender::roundRect(gx, gy, nodeW, nodeH, radius, withAlpha(UiTheme::surfaceRaised(), 0.75f));
            UiRender::outline(gx, gy, nodeW, nodeH, radius, withAlpha(UiTheme::accent(), 0.7f));
            Page::spinner(gx + 19.0f * zoom, gy + nodeH * 0.5f, std::max(3.0f, 6.0f * zoom), UiTheme::accent());
            std::string name = UiFont::fitEnd(item.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, NODE_W - 46.0f);
            zoomText(name, gx + 37.0f * zoom, gy, nodeH, UiTheme::textSecondary(), UiWidgets::TEXT_CONTROL, zoom,
                     view.zoomTarget);
        }
        if (!view.pending.empty()) {
            UiState::keepAnimating();
        }

        float savedTools = UiRender::alphaMul;
        UiRender::alphaMul = savedTools * frame.nodes;
        UiRender::shadow(toolsX, toolsY, toolsW, toolsH, 10.0f, 12.0f, 4.0f, UiTheme::shadow());
        UiRender::roundRect(toolsX, toolsY, toolsW, toolsH, 10.0f, UiTheme::surface());
        UiRender::outline(toolsX, toolsY, toolsW, toolsH, 10.0f, UiTheme::border(UiTheme::outlineSoft()));
        float toolX = toolsX + 4.0f;
        float toolY = toolsY + 4.0f;
        float middleX = fitArea.x - originX + fitArea.w * 0.5f;
        float middleY = fitArea.y - originY + fitArea.h * 0.5f;
        if (Page::iconButton("mods#graph#out", 0xf068, "缩小", toolX, toolY, blocked)) {
            zoomAt(view, view.zoomTarget / 1.25f, middleX, middleY);
            view.touched = true;
        }
        if (Page::iconButton("mods#graph#in", 0xf067, "放大", toolX + TOOL_STEP, toolY, blocked)) {
            zoomAt(view, view.zoomTarget * 1.25f, middleX, middleY);
            view.touched = true;
        }
        if (Page::iconButton("mods#graph#fit", 0xf065, "适应视图", toolX + 2.0f * TOOL_STEP, toolY, blocked)) {
            fit(view, fitArea.x - originX, fitArea.y - originY, fitArea.w, fitArea.h, false);
            view.touched = false;
        }
        if (Page::iconButton("mods#graph#layout", 0xf0e2, "重新排列", toolX + 3.0f * TOOL_STEP, toolY, blocked)) {
            view.touched = false;
            view.moved.clear();
            view.pinned.clear();
            view.pending.clear();
            view.signature.clear();
            view.fitted = false;
            touch(view);
        }
        if (Page::iconButton("mods#graph#lock", movable ? 0xf3c1 : 0xf023, movable ? "锁定分组" : "允许拖动分组",
                             toolX + 4.0f * TOOL_STEP, toolY, blocked)) {
            movable = !movable;
        }
        UiRender::alphaMul = savedTools;
        if (shown >= 0) {
            detailCard(key, list, view, shown, info, card, reveal, pick, blocked || view.selected.empty());
            if (!joints.empty()) {
                UiRender::flush();
                ImDrawList* drawList = UiRender::drawList();
                float radius = UiRender::transformW(3.5f) * UiRender::unit();
                for (const Joint& joint : joints) {
                    drawList->AddCircleFilled(point(joint.x, joint.y), radius, joint.ink);
                }
            }
        }
        UiRender::unscissor();

        if (canvasHover && NewUi::wheel() != 0.0f) {
            zoomAt(view, view.zoomTarget * std::pow(1.15f, NewUi::wheel()), mx - originX, my - originY);
            view.touched = true;
        }
        if (canvasHover && NewUi::pendingClick && !NewUi::clickConsumed) {
            NewUi::clickConsumed = true;
            std::string pressed = hovered >= 0 ? view.nodes[(size_t) hovered].id : std::string();
            bool twice = !pressed.empty() && pressed == view.lastPressed && now - view.lastPress < DOUBLE_CLICK;
            view.lastPress = twice ? -1.0 : now;
            view.lastPressed = pressed;
            view.pressX = view.lastX = mx;
            view.pressY = view.lastY = my;
            view.dragged = false;
            if (hovered >= 0 && view.pinned.count(view.nodes[(size_t) hovered].id) != 0 && !twice) {
                view.dragNode = hovered;
            } else if (hovered >= 0) {
                const Node& node = view.nodes[(size_t) hovered];
                view.selected = node.id;
                if (twice && node.kind == Kind::Local) {
                    view.picking = true;
                } else if (twice && openable(node)) {
                    model->open(node.id);
                }
            } else if (hoveredCluster >= 0) {
                view.dragCluster = hoveredCluster;
            } else {
                view.panning = true;
            }
        }
        if (view.dragNode >= 0) {
            if (view.dragNode >= (int) view.nodes.size()) {
                view.dragNode = -1;
            } else if (NewUi::mouseDown) {
                if (!view.dragged && (std::fabs(mx - view.pressX) > 3.0f || std::fabs(my - view.pressY) > 3.0f)) {
                    view.dragged = true;
                }
                if (view.dragged) {
                    Node& node = view.nodes[(size_t) view.dragNode];
                    node.x += (mx - view.lastX) / zoom;
                    node.y += (my - view.lastY) / zoom;
                    view.pinned[node.id] = {node.x, node.y};
                    touch(view);
                }
                view.lastX = mx;
                view.lastY = my;
            } else {
                if (!view.dragged) {
                    view.selected = view.nodes[(size_t) view.dragNode].id;
                }
                view.dragNode = -1;
            }
        }
        if (view.dragCluster >= 0 || view.panning) {
            if (NewUi::mouseDown) {
                if (!view.dragged && (std::fabs(mx - view.pressX) > 3.0f || std::fabs(my - view.pressY) > 3.0f)) {
                    view.dragged = true;
                }
                if (view.dragged) {
                    view.touched = true;
                    float dx = mx - view.lastX;
                    float dy = my - view.lastY;
                    if (view.dragCluster >= 0 && view.dragCluster < (int) view.clusters.size()) {
                        Cluster& cluster = view.clusters[(size_t) view.dragCluster];
                        cluster.x += dx / zoom;
                        cluster.y += dy / zoom;
                        view.moved[cluster.id] = {cluster.x, cluster.y};
                        touch(view);
                        place(view);
                    } else if (view.panning) {
                        view.panX += dx;
                        view.panY += dy;
                        view.panTargetX = view.panX;
                        view.panTargetY = view.panY;
                    }
                }
                view.lastX = mx;
                view.lastY = my;
            } else {
                if (!view.dragged) {
                    view.selected.clear();
                }
                view.dragCluster = -1;
                view.panning = false;
            }
        }
        if (canvasHover && hovered >= 0 && NewUi::rightClick) {
            NewUi::rightClick = false;
            nodeMenu(key, list, view.nodes[(size_t) hovered], mx, my);
        }
        if (canvasHover && hovered >= 0) {
            UiCursor::hand();
        }
        if (std::fabs(view.panTargetX - view.lastPanX) > 0.01f || std::fabs(view.panTargetY - view.lastPanY) > 0.01f ||
            std::fabs(view.zoomTarget - view.lastZoom) > 0.0001f) {
            view.lastPanX = view.panTargetX;
            view.lastPanY = view.panTargetY;
            view.lastZoom = view.zoomTarget;
            touch(view);
        }
        bool idle = view.dragCluster < 0 && view.dragNode < 0 && !view.panning;
        if (entry != nullptr && view.dirty && idle && (ImGui::GetTime() - view.changedAt > 0.8 || frame.nodes < 1.0f)) {
            save(view, *entry);
        }
    }

    void draw(const Instances::Entry& entry, const std::string& query, const Frame& frame, bool blocked) {
        model = nullptr;
        render(entry.key, &entry, Mods::of(entry.key), nullptr, query, frame, blocked);
    }

    void drawModel(const std::string& key, const Model& source, const std::string& query, const Frame& frame,
                   bool blocked) {
        static const Mods::List none = [] {
            Mods::List list;
            list.loading = false;
            return list;
        }();
        model = &source;
        render(key, nullptr, none, &source, query, frame, blocked);
        model = nullptr;
    }

    void pin(const std::string& key, const std::vector<std::filesystem::path>& files, float x, float y) {
        View& view = views[key];
        double now = ImGui::GetTime();
        float zoom = std::max(0.05f, view.zoom);
        float worldX = (x - view.originX - view.panX) / zoom - NODE_W * 0.5f;
        float worldY = (y - view.originY - view.panY) / zoom - NODE_H * 0.5f;
        for (size_t i = 0; i < files.size(); i++) {
            Pending item;
            item.id = "file:" + stemOf(files[i]);
            item.name = blb::path_to_utf8(files[i].filename());
            item.x = worldX;
            item.y = worldY + (float) i * ROW_STEP;
            item.since = now;
            view.pending.erase(std::remove_if(view.pending.begin(), view.pending.end(),
                                              [&item](const Pending& old) { return old.id == item.id; }),
                               view.pending.end());
            view.pinned.erase(item.id);
            view.pending.push_back(std::move(item));
        }
        view.signature.clear();
    }

    void preview(const std::string& key, const std::string& label, float x, float y) {
        auto found = views.find(key);
        float zoom = found != views.end() ? found->second.zoom : 1.0f;
        float raster = found != views.end() ? found->second.zoomTarget : 1.0f;
        float w = NODE_W * zoom;
        float h = NODE_H * zoom;
        float left = x - w * 0.5f;
        float top = y - h * 0.5f;
        float radius = 9.0f * zoom;
        UiRender::shadow(left, top, w, h, radius, 14.0f, 5.0f, UiTheme::shadow());
        UiRender::roundRect(left, top, w, h, radius, withAlpha(UiTheme::surfaceRaised(), 0.85f));
        UiRender::outline(left, top, w, h, radius, UiTheme::accent(), 0xF, 1.4f);
        scaledGlyph(0xf56f, left + 19.0f * zoom, y, UiTheme::accentText(), 10.0f, zoom);
        std::string text = UiFont::fitEnd(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, NODE_W - 46.0f);
        zoomText(text, left + 37.0f * zoom, top, h, UiTheme::textPrimary(), UiWidgets::TEXT_CONTROL, zoom, raster);
    }

}
