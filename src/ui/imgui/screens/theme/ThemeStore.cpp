#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/screens/theme/EmbeddedThemes.h"
#include "app/system/Customization.h"

#include <core/base/fs.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string_view>
#include <system_error>

namespace ThemeStore {

    namespace blb = b_launcher_base;

    namespace {
        const char* const ORDER[] = {"plain", "dark", "light", "iridium-dark", "iridium-light"};
        constexpr auto STALE_AFTER = std::chrono::seconds(2);

        std::vector<Entry> list;
        bool scanned = false;
        std::chrono::steady_clock::time_point scannedAt;

        const EmbeddedThemes::File* embedded(std::string_view path) {
            for (unsigned int i = 0; i < EmbeddedThemes::count; i++) {
                if (path == EmbeddedThemes::files[i].path) {
                    return &EmbeddedThemes::files[i];
                }
            }
            return nullptr;
        }

        std::string nameOf(const std::optional<std::string>& text, const std::string& id) {
            if (!text) {
                return id;
            }
            nlohmann::json doc = nlohmann::json::parse(*text, nullptr, false, true);
            if (doc.is_object()) {
                auto name = doc.find("name");
                if (name != doc.end() && name->is_string() && !name->get<std::string>().empty()) {
                    return name->get<std::string>();
                }
            }
            return id;
        }

        int rank(const std::string& id) {
            for (int i = 0; i < (int) (sizeof(ORDER) / sizeof(ORDER[0])); i++) {
                if (id == ORDER[i]) {
                    return i;
                }
            }
            return (int) (sizeof(ORDER) / sizeof(ORDER[0]));
        }
    }

    bool validId(std::string_view id) {
        if (id.empty() || id.size() > 64 || id[0] == '_' || id[0] == '.') {
            return false;
        }
        for (char c : id) {
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    std::filesystem::path folder() {
        return blb::launcher_data_directory() / "themes";
    }

    bool builtIn(const std::string& id) {
        const Entry* entry = find(id);
        return entry != nullptr && entry->builtIn;
    }

    std::optional<std::string> duplicate(const std::string& id) {
        const Entry* entry = find(id);
        if (entry == nullptr) {
            return std::nullopt;
        }
        std::string name = entry->name;
        bool embeddedTheme = entry->builtIn;
        std::error_code ec;
        std::string base = id + "-copy";
        std::string next = base;
        for (int n = 2; find(next) != nullptr || std::filesystem::exists(folder() / next, ec); n++) {
            next = base + "-" + std::to_string(n);
        }
        if (!validId(next)) {
            return std::nullopt;
        }
        std::filesystem::path target = folder() / next;
        std::filesystem::create_directories(target, ec);
        if (ec) {
            return std::nullopt;
        }
        if (embeddedTheme) {
            std::string prefix = id + "/";
            for (unsigned int i = 0; i < EmbeddedThemes::count; i++) {
                std::string_view path = EmbeddedThemes::files[i].path;
                if (path.substr(0, prefix.size()) != prefix) {
                    continue;
                }
                std::filesystem::path out = target / blb::path_from_utf8(std::string(path.substr(prefix.size())));
                std::filesystem::create_directories(out.parent_path(), ec);
                std::ofstream file(out, std::ios::binary);
                file.write((const char*) EmbeddedThemes::files[i].data, EmbeddedThemes::files[i].size);
            }
        } else {
            std::filesystem::copy(folder() / id, target, std::filesystem::copy_options::recursive, ec);
        }
        auto text = blb::fs::read_file(target / "theme.json");
        nlohmann::json doc = text ? nlohmann::json::parse(*text, nullptr, false, true) : nlohmann::json();
        if (doc.is_object()) {
            doc["name"] = name + " 副本";
            std::ofstream file(target / "theme.json", std::ios::binary | std::ios::trunc);
            file << doc.dump(1);
        }
        rescan();
        return next;
    }

    bool remove(const std::string& id) {
        const Entry* entry = find(id);
        if (entry == nullptr || entry->builtIn || !validId(id)) {
            return false;
        }
        std::error_code ec;
        std::filesystem::remove_all(folder() / id, ec);
        rescan();
        return !ec;
    }

    void rescan() {
        std::vector<Entry> builtIns;
        for (unsigned int i = 0; i < EmbeddedThemes::count; i++) {
            std::string_view path = EmbeddedThemes::files[i].path;
            size_t slash = path.find('/');
            if (slash == std::string_view::npos || path.substr(slash + 1) != "theme.json") {
                continue;
            }
            std::string id(path.substr(0, slash));
            if (!validId(id)) {
                continue;
            }
            const EmbeddedThemes::File& file = EmbeddedThemes::files[i];
            builtIns.push_back({id, nameOf(std::string((const char*) file.data, file.size), id), true});
        }
        std::stable_sort(builtIns.begin(), builtIns.end(),
                         [](const Entry& a, const Entry& b) { return rank(a.id) < rank(b.id); });

        std::vector<Entry> users;
        std::error_code ec;
        std::filesystem::path root = folder();
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) {
                continue;
            }
            std::string id = it->path().filename().string();
            if (!validId(id) ||
                std::any_of(builtIns.begin(), builtIns.end(), [&](const Entry& entry) { return entry.id == id; })) {
                continue;
            }
            if (!std::filesystem::is_regular_file(it->path() / "theme.json", ec)) {
                continue;
            }
            auto text = blb::fs::read_file(it->path() / "theme.json");
            users.push_back({id, nameOf(text ? std::optional<std::string>(*text) : std::nullopt, id), false});
        }
        std::sort(users.begin(), users.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });

        std::vector<Entry> packs;
        for (const std::string& file : Customization::files("themes/")) {
            std::string_view rest = std::string_view(file).substr(7);
            size_t slash = rest.find('/');
            if (slash == std::string_view::npos || rest.substr(slash + 1) != "theme.json") {
                continue;
            }
            std::string id(rest.substr(0, slash));
            if (!validId(id) ||
                std::any_of(builtIns.begin(), builtIns.end(), [&](const Entry& entry) { return entry.id == id; })) {
                continue;
            }
            users.erase(std::remove_if(users.begin(), users.end(), [&](const Entry& entry) { return entry.id == id; }),
                        users.end());
            packs.push_back({id, nameOf(Customization::read(file), id), true, true});
        }

        list = std::move(builtIns);
        list.insert(list.end(), packs.begin(), packs.end());
        list.insert(list.end(), users.begin(), users.end());
        scanned = true;
        scannedAt = std::chrono::steady_clock::now();
    }

    void rescanIfStale() {
        if (!scanned || std::chrono::steady_clock::now() - scannedAt > STALE_AFTER) {
            rescan();
        }
    }

    const std::vector<Entry>& entries() {
        if (!scanned) {
            rescan();
        }
        return list;
    }

    const Entry* find(const std::string& id) {
        for (const Entry& entry : entries()) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    }

    std::optional<Source> locate(const std::string& id, const std::string& path) {
        const Entry* entry = find(id);
        if (entry == nullptr || !blb::fs::safe_relative(path)) {
            return std::nullopt;
        }
        Source source;
        if (entry->packed) {
            std::optional<std::string> data = Customization::read("themes/" + id + "/" + path);
            if (!data) {
                return std::nullopt;
            }
            source.owned = std::make_shared<const std::string>(std::move(*data));
            return source;
        }
        if (entry->builtIn) {
            const EmbeddedThemes::File* file = embedded(id + "/" + path);
            if (file == nullptr) {
                return std::nullopt;
            }
            source.data = file->data;
            source.size = file->size;
            return source;
        }
        std::filesystem::path root = folder() / id;
        source.file = root / blb::path_from_utf8(path);
        if (!blb::fs::is_inside(root, source.file)) {
            return std::nullopt;
        }
        return source;
    }

    std::optional<std::string> contents(const Source& source) {
        if (source.owned) {
            return *source.owned;
        }
        if (source.data != nullptr) {
            return std::string((const char*) source.data, source.size);
        }
        auto text = blb::fs::read_file(source.file);
        if (!text) {
            return std::nullopt;
        }
        return *text;
    }

    std::optional<std::string> read(const std::string& id, const std::string& path) {
        std::optional<Source> source = locate(id, path);
        if (!source) {
            return std::nullopt;
        }
        return contents(*source);
    }
}
