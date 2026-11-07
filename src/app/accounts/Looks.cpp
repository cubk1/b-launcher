#include "app/accounts/Looks.h"
#include "app/accounts/SkinRender.h"
#include "app/system/Pixels.h"
#include "app/system/Platform.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <span>

namespace Looks {

    namespace blb = b_launcher_base;

    namespace {
        const char* const STORE = "AccountLooks";
        constexpr size_t EMBED_BYTES = 1024 * 1024;

        std::map<std::string, Look> looks;
        bool loaded = false;

        const char* modelName(Model model) {
            return model == Model::Slim ? "slim" : model == Model::Classic ? "default" : "auto";
        }

        Model modelOf(const std::string& text) {
            return text == "slim" ? Model::Slim : text == "default" ? Model::Classic : Model::Auto;
        }

        std::string encode(const std::string& data) {
            return blb::base64_encode(std::span<const std::uint8_t>((const std::uint8_t*) data.data(), data.size()));
        }

        std::string restore(const nlohmann::json& value, const char* name, std::filesystem::path& file) {
            file = blb::path_from_utf8(blb::json_text(value, std::string(name) + "Path"));
            if (file.empty()) {
                return blb::base64_decode(blb::json_text(value, name)).value_or(std::string());
            }
            auto read = blb::fs::read_file(file);
            return read ? std::move(*read) : std::string();
        }

        void keep(nlohmann::json& entry, const char* name, const std::string& data, const std::filesystem::path& file) {
            if (!file.empty()) {
                entry[std::string(name) + "Path"] = blb::path_to_utf8(file);
            } else if (!data.empty()) {
                entry[name] = encode(data);
            }
        }

        bool empty(const Look& look) {
            return look.skin.empty() && look.cape.empty() && look.skinFile.empty() && look.capeFile.empty();
        }

        void load() {
            if (loaded) {
                return;
            }
            loaded = true;
            std::optional<std::string> stored = Platform::loadValue(STORE);
            if (!stored) {
                return;
            }
            nlohmann::json doc = nlohmann::json::parse(*stored, nullptr, false);
            if (!doc.is_object()) {
                return;
            }
            for (const auto& [key, value] : doc.items()) {
                if (!value.is_object()) {
                    continue;
                }
                Look look;
                look.skin = restore(value, "skin", look.skinFile);
                look.cape = restore(value, "cape", look.capeFile);
                look.model = modelOf(blb::json_text(value, "model"));
                if (!empty(look)) {
                    looks[key] = std::move(look);
                }
            }
        }

        void save() {
            nlohmann::json doc = nlohmann::json::object();
            for (const auto& [key, look] : looks) {
                if (empty(look)) {
                    continue;
                }
                nlohmann::json entry = {{"model", modelName(look.model)}};
                keep(entry, "skin", look.skin, look.skinFile);
                keep(entry, "cape", look.cape, look.capeFile);
                doc[key] = std::move(entry);
            }
            (void) Platform::storeValue(STORE, doc.dump(), true);
        }

        bool png(const std::string& data) {
            return data.size() > 8 && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
        }

        std::string readImage(const std::filesystem::path& file, std::string& data) {
            auto read = blb::fs::read_file(file);
            if (!read) {
                return "无法读取图片：" + read.err().message;
            }
            if (!png(*read)) {
                return "需要 PNG 图片";
            }
            data = std::move(*read);
            return std::string();
        }

        std::filesystem::path linked(const std::filesystem::path& file, const std::string& data) {
            if (data.size() <= EMBED_BYTES) {
                return std::filesystem::path();
            }
            std::error_code error;
            std::filesystem::path absolute = std::filesystem::absolute(file, error);
            return error ? file : absolute;
        }

        std::string written(const std::string& data) {
            if (data.empty()) {
                return std::string();
            }
            std::filesystem::path folder = Platform::cacheDirectory();
            std::error_code error;
            if (folder.empty()) {
                folder = std::filesystem::temp_directory_path(error);
            }
            folder /= std::filesystem::path("skins") / "local";
            blb::sha1_digest digest = blb::sha1::digest(data);
            std::filesystem::path file = folder / (blb::to_hex(digest) + ".png");
            if (!std::filesystem::exists(file, error) && blb::fs::create_directories(folder)) {
                (void) blb::fs::write_file_atomic(file, data);
            }
            return blb::path_to_utf8(file);
        }

        std::string source(const std::string& data, const std::filesystem::path& file) {
            std::error_code error;
            if (!data.empty() && !file.empty() && std::filesystem::is_regular_file(file, error)) {
                return blb::path_to_utf8(file);
            }
            return written(data);
        }
    }

    const Look& of(const std::string& key) {
        static const Look none;
        load();
        auto found = looks.find(key);
        return found == looks.end() ? none : found->second;
    }

    std::string setSkin(const std::string& key, const std::filesystem::path& file) {
        load();
        std::string data;
        std::string problem = readImage(file, data);
        if (!problem.empty()) {
            return problem;
        }
        std::shared_ptr<const Pixels> pixels = Pixels::decode(data.data(), data.size());
        if (pixels == nullptr || pixels->width < 64 || pixels->width % 64 != 0 ||
            (pixels->height != pixels->width && pixels->height * 2 != pixels->width)) {
            return "皮肤图片需要是 64×64 或 64×32（或它们的整数倍）的 PNG";
        }
        Look& look = looks[key];
        look.skinFile = linked(file, data);
        look.skin = std::move(data);
        look.model = SkinRender::looksSlim(*pixels) ? Model::Slim : Model::Classic;
        save();
        return std::string();
    }

    std::string setCape(const std::string& key, const std::filesystem::path& file) {
        load();
        std::string data;
        std::string problem = readImage(file, data);
        if (!problem.empty()) {
            return problem;
        }
        std::shared_ptr<const Pixels> pixels = Pixels::decode(data.data(), data.size());
        if (pixels == nullptr || pixels->width < 22 || pixels->height < 17) {
            return "披风图片尺寸不正确";
        }
        Look& look = looks[key];
        look.capeFile = linked(file, data);
        look.cape = std::move(data);
        save();
        return std::string();
    }

    void clearSkin(const std::string& key) {
        load();
        auto found = looks.find(key);
        if (found != looks.end() && (!found->second.skin.empty() || !found->second.skinFile.empty())) {
            found->second.skin.clear();
            found->second.skinFile.clear();
            save();
        }
    }

    void clearCape(const std::string& key) {
        load();
        auto found = looks.find(key);
        if (found != looks.end() && (!found->second.cape.empty() || !found->second.capeFile.empty())) {
            found->second.cape.clear();
            found->second.capeFile.clear();
            save();
        }
    }

    void move(const std::string& from, const std::string& to) {
        load();
        auto found = looks.find(from);
        if (found == looks.end() || from == to) {
            return;
        }
        Look look = std::move(found->second);
        looks.erase(found);
        looks[to] = std::move(look);
        save();
    }

    void forget(const std::string& key) {
        load();
        if (looks.erase(key) != 0) {
            save();
        }
    }

    void apply(const std::string& key, Patches::Plan& plan) {
        const Look& look = of(key);
        if (look.skin.empty() && look.cape.empty()) {
            return;
        }
        plan.player.clear();
        plan.values.erase(std::remove_if(plan.values.begin(), plan.values.end(),
                                         [](const auto& value) {
                                             return value.first == "skin.sources" ||
                                                     value.first.rfind("skin.template.", 0) == 0;
                                         }),
                          plan.values.end());
        plan.values.emplace_back("skin.sources", "template");
        plan.values.emplace_back("skin.template.skin", source(look.skin, look.skinFile));
        plan.values.emplace_back("skin.template.cape", source(look.cape, look.capeFile));
        plan.values.emplace_back("skin.template.model", modelName(look.model));
    }
}
