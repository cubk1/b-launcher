#include "app/instances/FileTree.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/modpack/modpack.hpp>

#include <algorithm>

namespace FileTree {

    namespace blb = b_launcher_base;

    namespace {
        std::string parentOf(const std::string& path) {
            size_t slash = path.rfind('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }

        bool inherited(const Rules& rules, const std::string& path) {
            std::string parent = parentOf(path);
            return !parent.empty() && blb::modpack::pick_state(rules, parent);
        }

        void clearBelow(Rules& rules, const std::string& path) {
            std::string prefix = path + "/";
            for (auto it = rules.lower_bound(prefix); it != rules.end() && it->first.rfind(prefix, 0) == 0;) {
                it = rules.erase(it);
            }
        }

        void set(Rules& rules, const std::string& path, bool on) {
            clearBelow(rules, path);
            rules.erase(path);
            if (inherited(rules, path) != on) {
                rules[path] = on;
            }
        }

    }

    void Tree::reset(const std::filesystem::path& root, const std::string& instanceId) {
        if (root == base && instanceId == id) {
            return;
        }
        base = root;
        id = instanceId;
        listed.clear();
        opened.clear();
    }

    void Tree::refresh() {
        listed.clear();
    }

    const std::filesystem::path& Tree::root() const {
        return base;
    }

    const std::string& Tree::instance() const {
        return id;
    }

    const std::vector<Node>& Tree::children(const std::string& path) {
        auto found = listed.find(path);
        if (found != listed.end()) {
            return found->second;
        }
        std::vector<Node> nodes;
        std::filesystem::path dir = path.empty() ? base : base / blb::path_from_utf8(path);
        for (const auto& [name, st] : blb::fs::list_directory(dir)) {
            std::string child = path.empty() ? name : path + "/" + name;
            if (st.is_link || blb::modpack::export_hidden(child, id)) {
                continue;
            }
            nodes.push_back({name, child, st.is_directory});
        }
        std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
            if (a.directory != b.directory) {
                return a.directory;
            }
            return blb::to_lower(a.name) < blb::to_lower(b.name);
        });
        return listed[path] = std::move(nodes);
    }

    bool Tree::open(const std::string& path) const {
        return opened.count(path) != 0;
    }

    void Tree::setOpen(const std::string& path, bool open) {
        if (open) {
            opened.insert(path);
        } else {
            opened.erase(path);
        }
    }

    void Tree::toggle(Rules& rules, const std::string& path) {
        bool on = mark(rules, path) != Mark::ON;
        set(rules, path, on);
        std::string parent = parentOf(path);
        while (!parent.empty()) {
            const std::vector<Node>& siblings = children(parent);
            bool uniform = std::all_of(siblings.begin(), siblings.end(), [&](const Node& node) {
                return mark(rules, node.path) == (on ? Mark::ON : Mark::OFF);
            });
            if (!uniform) {
                break;
            }
            set(rules, parent, on);
            parent = parentOf(parent);
        }
    }

    Mark mark(const Rules& rules, const std::string& path) {
        if (blb::modpack::picks_below(rules, path)) {
            return Mark::PARTIAL;
        }
        return blb::modpack::pick_state(rules, path) ? Mark::ON : Mark::OFF;
    }
}
