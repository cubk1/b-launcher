#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace FileTree {

    enum class Mark { OFF, ON, PARTIAL };

    struct Node {
        std::string name;
        std::string path;
        bool directory = false;
    };

    using Rules = std::map<std::string, bool>;

    class Tree {
    public:
        void reset(const std::filesystem::path& root, const std::string& instanceId);
        void refresh();
        const std::filesystem::path& root() const;
        const std::string& instance() const;
        const std::vector<Node>& children(const std::string& path);
        bool open(const std::string& path) const;
        void setOpen(const std::string& path, bool open);
        void toggle(Rules& rules, const std::string& path);

    private:
        std::filesystem::path base;
        std::string id;
        std::map<std::string, std::vector<Node>> listed;
        std::set<std::string> opened;
    };

    Mark mark(const Rules& rules, const std::string& path);
}
