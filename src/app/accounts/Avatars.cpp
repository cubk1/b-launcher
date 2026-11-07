#include "app/accounts/Avatars.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Looks.h"
#include "app/accounts/SkinRender.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Config.h"
#include "app/system/EmbeddedIcons.h"
#include "app/system/Platform.h"

#include <core/auth/yggdrasil.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <zlib.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Avatars {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;
        using Clock = std::chrono::steady_clock;

        constexpr int SIZE = 128;
        constexpr const char* MAGIC = "BLBAVTR1";
        constexpr size_t MAGIC_SIZE = 8;
        const char* const PLACEHOLDER = "#default";
        const char* const HEAD = "#head";
        const char* const STYLE_KEY = "accountAvatar";
        const char* const RENDER = "7";
        constexpr int FACE = 64;

        struct Entry {
            std::string signature;
            std::shared_ptr<const Pixels> pixels;
        };

        struct Look {
            std::string skin;
            std::string cape;
            int model = -1;
        };

        using Snapshot = std::vector<std::pair<std::string, Entry>>;

        AsyncTasks tasks;
        bool loaded = false;
        bool dirty = false;
        bool saving = false;
        bool placeholderTried = false;
        Clock::time_point changedAt;
        std::map<std::string, Entry> entries;
        std::set<std::string> requested;

        std::filesystem::path cacheFile() {
            std::filesystem::path cache = Platform::cacheDirectory();
            return cache.empty() ? cache : cache / "avatars.bin";
        }

        std::filesystem::path imageFile(const std::string& url) {
            std::filesystem::path cache = Platform::cacheDirectory();
            size_t slash = url.find_last_of('/');
            std::string hash = slash == std::string::npos ? url : url.substr(slash + 1);
            if (cache.empty() || hash.empty() || hash.size() > 128 ||
                hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
                return {};
            }
            return cache / "skins" / (hash + ".png");
        }

        std::string signatureOf(const Look& look) {
            return std::string(RENDER) + "|" + std::to_string(SIZE) + "|" + look.skin + "|" + look.cape + "|" +
                    std::to_string(look.model);
        }

        std::string placeholderSignature() {
            return std::string(RENDER) + "|" + std::to_string(SIZE) + "|default";
        }

        void changed() {
            dirty = true;
            changedAt = Clock::now();
        }

        void putNumber(std::string& out, uint32_t value, int bytes) {
            for (int i = 0; i < bytes; i++) {
                out.push_back((char) ((value >> (8 * i)) & 0xFF));
            }
        }

        struct Reader {
            const std::string& data;
            size_t at = 0;
            bool ok = true;

            uint32_t number(int bytes) {
                if (!ok || data.size() - at < (size_t) bytes) {
                    ok = false;
                    return 0;
                }
                uint32_t value = 0;
                for (int i = 0; i < bytes; i++) {
                    value |= (uint32_t) (unsigned char) data[at + i] << (8 * i);
                }
                at += (size_t) bytes;
                return value;
            }

            std::string bytes(size_t count) {
                if (!ok || data.size() - at < count) {
                    ok = false;
                    return std::string();
                }
                std::string out = data.substr(at, count);
                at += count;
                return out;
            }
        };

        std::string encode(const Snapshot& list) {
            std::string body;
            uint32_t count = 0;
            for (const auto& [key, entry] : list) {
                const Pixels& pixels = *entry.pixels;
                uLongf length = compressBound((uLong) pixels.rgba.size());
                std::string packed(length, '\0');
                if (compress2((Bytef*) packed.data(), &length, pixels.rgba.data(), (uLong) pixels.rgba.size(), 6) !=
                    Z_OK) {
                    continue;
                }
                packed.resize(length);
                putNumber(body, (uint32_t) key.size(), 2);
                body += key;
                putNumber(body, (uint32_t) entry.signature.size(), 2);
                body += entry.signature;
                putNumber(body, (uint32_t) pixels.width, 2);
                putNumber(body, (uint32_t) pixels.height, 2);
                putNumber(body, (uint32_t) packed.size(), 4);
                body += packed;
                count++;
            }
            std::string out(MAGIC, MAGIC_SIZE);
            putNumber(out, count, 4);
            return out + body;
        }

        std::map<std::string, Entry> decode(const std::string& data) {
            std::map<std::string, Entry> out;
            if (data.size() < MAGIC_SIZE + 4 || data.compare(0, MAGIC_SIZE, MAGIC) != 0) {
                return out;
            }
            Reader reader{data, MAGIC_SIZE};
            uint32_t count = reader.number(4);
            for (uint32_t i = 0; i < count && reader.ok; i++) {
                std::string key = reader.bytes(reader.number(2));
                std::string signature = reader.bytes(reader.number(2));
                int width = (int) reader.number(2);
                int height = (int) reader.number(2);
                std::string packed = reader.bytes(reader.number(4));
                if (!reader.ok || width <= 0 || height <= 0 || width > 1024 || height > 1024) {
                    break;
                }
                auto pixels = std::make_shared<Pixels>();
                pixels->width = width;
                pixels->height = height;
                pixels->rgba.resize((size_t) width * height * 4);
                uLongf length = (uLongf) pixels->rgba.size();
                if (uncompress(pixels->rgba.data(), &length, (const Bytef*) packed.data(), (uLong) packed.size()) !=
                            Z_OK ||
                    length != pixels->rgba.size()) {
                    continue;
                }
                out[key] = Entry{signature, pixels};
            }
            return out;
        }

        void write(const std::filesystem::path& file, const Snapshot& list) {
            if (blb::fs::create_directories(file.parent_path())) {
                (void) blb::fs::write_file_atomic(file, encode(list));
            }
        }

        void ensureLoaded() {
            if (loaded) {
                return;
            }
            loaded = true;
            std::filesystem::path file = cacheFile();
            if (file.empty()) {
                return;
            }
            if (auto data = blb::fs::read_file(file)) {
                entries = decode(*data);
            }
        }

        Snapshot snapshot() {
            std::set<std::string> live = {PLACEHOLDER};
            for (const blb::auth::account& account : Accounts::list()) {
                live.insert(Accounts::keyOf(account));
            }
            const std::string suffix = HEAD;
            Snapshot list;
            for (const auto& [key, entry] : entries) {
                bool head = key.size() > suffix.size() &&
                        key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0;
                std::string owner = head ? key.substr(0, key.size() - suffix.size()) : key;
                if (entry.pixels != nullptr && live.count(owner) != 0) {
                    list.push_back({key, entry});
                }
            }
            return list;
        }

        void save(bool now) {
            dirty = false;
            std::filesystem::path file = cacheFile();
            if (file.empty()) {
                return;
            }
            Snapshot list = snapshot();
            if (now) {
                write(file, list);
                saving = false;
                return;
            }
            saving = true;
            tasks.run(
                    [file, list](blb::cancellation_token) -> Continuation {
                        write(file, list);
                        return [] { saving = false; };
                    },
                    [](const std::string&) -> Continuation { return [] { saving = false; }; });
        }

        std::string profileUrl(const blb::auth::account& account) {
            std::string id = blb::auth::strip_uuid(account.uuid);
            if (id.empty()) {
                return std::string();
            }
            if (account.type == blb::auth::account_type::microsoft) {
                return "https://sessionserver.mojang.com/session/minecraft/profile/" + id;
            }
            if (!blb::auth::is_yggdrasil(account.type)) {
                return std::string();
            }
            std::string api = blb::auth::yggdrasil_api(account);
            while (!api.empty() && api.back() == '/') {
                api.pop_back();
            }
            return api.empty() ? std::string() : api + "/sessionserver/session/minecraft/profile/" + id;
        }

        std::optional<Look> lookup(const std::string& url, blb::cancellation_token token) {
            auto response = blb::net::http_get(url, token);
            if (!response) {
                return std::nullopt;
            }
            nlohmann::json doc = nlohmann::json::parse(*response, nullptr, false);
            auto properties = doc.is_object() ? doc.find("properties") : doc.end();
            if (!doc.is_object() || properties == doc.end() || !properties->is_array()) {
                return std::nullopt;
            }
            Look look;
            look.model = 0;
            for (const nlohmann::json& property : *properties) {
                if (!property.is_object() || blb::json_text(property, "name") != "textures") {
                    continue;
                }
                std::optional<std::string> decoded = blb::base64_decode(blb::json_text(property, "value"));
                nlohmann::json value = decoded ? nlohmann::json::parse(*decoded, nullptr, false) : nlohmann::json();
                auto textures = value.is_object() ? value.find("textures") : value.end();
                if (!value.is_object() || textures == value.end() || !textures->is_object()) {
                    return std::nullopt;
                }
                auto skin = textures->find("SKIN");
                if (skin != textures->end() && skin->is_object()) {
                    look.skin = blb::json_text(*skin, "url");
                    auto metadata = skin->find("metadata");
                    if (metadata != skin->end() && metadata->is_object() &&
                        blb::json_text(*metadata, "model") == "slim") {
                        look.model = 1;
                    }
                }
                auto cape = textures->find("CAPE");
                if (cape != textures->end() && cape->is_object()) {
                    look.cape = blb::json_text(*cape, "url");
                }
            }
            return look;
        }

        std::shared_ptr<const Pixels> image(const std::string& url, blb::cancellation_token token) {
            std::filesystem::path file = imageFile(url);
            if (!file.empty()) {
                if (auto cached = blb::fs::read_file(file)) {
                    if (auto pixels = Pixels::decode(cached->data(), cached->size())) {
                        return pixels;
                    }
                }
            }
            auto data = blb::net::http_get(url, token);
            if (!data) {
                return nullptr;
            }
            std::shared_ptr<const Pixels> pixels = Pixels::decode(data->data(), data->size());
            if (pixels != nullptr && !file.empty() && blb::fs::create_directories(file.parent_path())) {
                (void) blb::fs::write_file_atomic(file, *data);
            }
            return pixels;
        }

        std::shared_ptr<const Pixels> faceOf(const Pixels& skin) {
            if (skin.width < 64 || skin.width % 64 != 0 || skin.height * 2 < skin.width ||
                skin.rgba.size() < (size_t) skin.width * skin.height * 4) {
                return nullptr;
            }
            int k = skin.width / 64;
            auto alphaAt = [&](int x, int y) { return skin.rgba[((size_t) y * skin.width + x) * 4 + 3]; };
            bool hat = true;
            if (skin.height * 2 == skin.width) {
                hat = false;
                for (int y = 0; y < 32 * k && !hat; y++) {
                    for (int x = 32 * k; x < 64 * k && !hat; x++) {
                        hat = alphaAt(x, y) < 128;
                    }
                }
            }
            auto out = std::make_shared<Pixels>();
            out->width = FACE;
            out->height = FACE;
            out->rgba.resize((size_t) FACE * FACE * 4);
            for (int y = 0; y < FACE; y++) {
                for (int x = 0; x < FACE; x++) {
                    int sx = 8 * k + x * 8 * k / FACE;
                    int sy = 8 * k + y * 8 * k / FACE;
                    const unsigned char* face = &skin.rgba[((size_t) sy * skin.width + sx) * 4];
                    const unsigned char* cap = &skin.rgba[((size_t) sy * skin.width + sx + 32 * k) * 4];
                    float cover = hat ? cap[3] / 255.0f : 0.0f;
                    unsigned char* p = &out->rgba[((size_t) y * FACE + x) * 4];
                    for (int i = 0; i < 3; i++) {
                        p[i] = (unsigned char) (face[i] * (1.0f - cover) + cap[i] * cover + 0.5f);
                    }
                    p[3] = 255;
                }
            }
            return out;
        }

        void requestLocal(const std::string& key) {
            const Looks::Look& look = Looks::of(key);
            if (look.skin.empty()) {
                if (entries.erase(key) + entries.erase(key + HEAD) != 0) {
                    changed();
                }
                return;
            }
            std::string skin = look.skin;
            std::string cape = look.cape;
            Looks::Model model = look.model;
            std::string signature = std::string(RENDER) + "|" + std::to_string(SIZE) + "|local|" +
                    blb::to_hex(blb::sha1::digest(skin)) + "|" + blb::to_hex(blb::sha1::digest(cape)) + "|" +
                    std::to_string((int) model);
            auto found = entries.find(key);
            if (found != entries.end() && found->second.signature == signature) {
                return;
            }
            tasks.run(
                    [key, skin, cape, model, signature](blb::cancellation_token) -> Continuation {
                        std::shared_ptr<const Pixels> decoded = Pixels::decode(skin.data(), skin.size());
                        if (decoded == nullptr) {
                            return nullptr;
                        }
                        std::shared_ptr<const Pixels> mantle =
                                cape.empty() ? nullptr : Pixels::decode(cape.data(), cape.size());
                        SkinRender::Options options;
                        options.size = SIZE;
                        options.slim = model == Looks::Model::Slim ||
                                (model == Looks::Model::Auto && SkinRender::looksSlim(*decoded));
                        std::shared_ptr<const Pixels> pixels = SkinRender::render(*decoded, mantle.get(), options);
                        std::shared_ptr<const Pixels> face = faceOf(*decoded);
                        if (pixels == nullptr || face == nullptr) {
                            return nullptr;
                        }
                        return [key, signature, pixels, face] {
                            entries[key] = Entry{signature, pixels};
                            entries[key + HEAD] = Entry{signature, face};
                            changed();
                        };
                    },
                    [](const std::string&) -> Continuation { return nullptr; });
        }

        void request(const blb::auth::account& account, const std::string& key) {
            if (!requested.insert(key).second) {
                return;
            }
            if (account.type == blb::auth::account_type::offline) {
                requestLocal(key);
                return;
            }
            std::string url = profileUrl(account);
            std::string fallback = account.skin_url;
            auto found = entries.find(key);
            std::string cached = found != entries.end() ? found->second.signature : std::string();
            tasks.run(
                    [key, url, fallback, cached](blb::cancellation_token token) -> Continuation {
                        std::optional<Look> seen = url.empty() ? std::nullopt : lookup(url, token);
                        Look look = seen ? *seen : Look{fallback, std::string(), -1};
                        if (look.skin.empty()) {
                            return [key] {
                                if (entries.erase(key) + entries.erase(key + HEAD) != 0) {
                                    changed();
                                }
                            };
                        }
                        std::string signature = signatureOf(look);
                        if (signature == cached) {
                            return nullptr;
                        }
                        std::shared_ptr<const Pixels> skin = image(look.skin, token);
                        if (skin == nullptr) {
                            return nullptr;
                        }
                        std::shared_ptr<const Pixels> cape = look.cape.empty() ? nullptr : image(look.cape, token);
                        SkinRender::Options options;
                        options.size = SIZE;
                        options.slim = look.model == 1 || (look.model < 0 && SkinRender::looksSlim(*skin));
                        std::shared_ptr<const Pixels> pixels = SkinRender::render(*skin, cape.get(), options);
                        std::shared_ptr<const Pixels> face = faceOf(*skin);
                        if (pixels == nullptr || face == nullptr) {
                            return nullptr;
                        }
                        return [key, signature, pixels, face] {
                            entries[key] = Entry{signature, pixels};
                            entries[key + HEAD] = Entry{signature, face};
                            changed();
                        };
                    },
                    [](const std::string&) -> Continuation { return nullptr; });
        }

        std::shared_ptr<const Pixels> placeholder(bool head) {
            std::string key = std::string(PLACEHOLDER) + (head ? HEAD : "");
            auto found = entries.find(key);
            if (found != entries.end() && found->second.pixels != nullptr &&
                found->second.signature == placeholderSignature()) {
                return found->second.pixels;
            }
            if (placeholderTried) {
                return nullptr;
            }
            placeholderTried = true;
            std::shared_ptr<const Pixels> skin =
                    Pixels::decode(EmbeddedIcons::defaultSkin, EmbeddedIcons::defaultSkinSize);
            if (skin == nullptr) {
                return nullptr;
            }
            SkinRender::Options options;
            options.size = SIZE;
            options.slim = SkinRender::looksSlim(*skin);
            std::shared_ptr<const Pixels> pixels = SkinRender::render(*skin, nullptr, options);
            std::shared_ptr<const Pixels> face = faceOf(*skin);
            if (pixels == nullptr || face == nullptr) {
                return nullptr;
            }
            entries[PLACEHOLDER] = Entry{placeholderSignature(), pixels};
            entries[std::string(PLACEHOLDER) + HEAD] = Entry{placeholderSignature(), face};
            changed();
            return head ? face : pixels;
        }

        std::shared_ptr<const Pixels> avatar(const blb::auth::account& account, bool head) {
            ensureLoaded();
            std::string key = Accounts::keyOf(account);
            request(account, key);
            auto found = entries.find(head ? key + HEAD : key);
            if (found != entries.end() && found->second.pixels != nullptr) {
                return found->second.pixels;
            }
            return placeholder(head);
        }
    }

    Style style() {
        return blb::json_text(Config::data(), STYLE_KEY) == "head" ? Style::Head : Style::Bust;
    }

    void setStyle(Style value) {
        if (style() == value) {
            return;
        }
        Config::data()[STYLE_KEY] = value == Style::Head ? "head" : "bust";
        Config::save();
    }

    std::shared_ptr<const Pixels> bust(const blb::auth::account& account) {
        return avatar(account, false);
    }

    std::shared_ptr<const Pixels> head(const blb::auth::account& account) {
        return avatar(account, true);
    }

    void refresh(const std::string& key) {
        requested.erase(key);
    }

    void poll() {
        tasks.poll();
        if (dirty && !saving && Clock::now() - changedAt > std::chrono::seconds(2)) {
            save(false);
        }
    }

    void shutdown() {
        tasks.stop();
        if (dirty || saving) {
            save(true);
        }
    }
}
