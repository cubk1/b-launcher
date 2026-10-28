#include "app/accounts/Migrators.h"

#include <core/base/strings.hpp>

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

namespace Migrators {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using MicrosoftClients::App;

    namespace {
        std::filesystem::path appData() {
            const wchar_t* folder = _wgetenv(L"APPDATA");
            return folder != nullptr && *folder != 0 ? std::filesystem::path(folder) : std::filesystem::path();
        }

        std::optional<std::string> readFile(const std::filesystem::path& path) {
            std::error_code error;
            if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
                return std::nullopt;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            std::ostringstream content;
            content << in.rdbuf();
            return content.str();
        }

        std::optional<std::string> unprotect(const std::string& data) {
            DATA_BLOB input = {(DWORD) data.size(), (BYTE*) data.data()};
            DATA_BLOB output = {};
            if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
                return std::nullopt;
            }
            std::string out((const char*) output.pbData, output.cbData);
            LocalFree(output.pbData);
            return out;
        }

        std::optional<std::string> registryText(HKEY root, const char* key, const char* name) {
            DWORD size = 0;
            if (RegGetValueA(root, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
                return std::nullopt;
            }
            std::string value(size, '\0');
            if (RegGetValueA(root, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
                return std::nullopt;
            }
            value.resize(std::strlen(value.c_str()));
            return value;
        }

        void officialLauncher(const std::filesystem::path& file, const std::string& launcher, std::vector<Found>& out) {
            std::optional<std::string> data = readFile(file);
            std::optional<std::string> plain = data ? unprotect(*data) : std::nullopt;
            if (!plain) {
                return;
            }
            json doc = json::parse(*plain, nullptr, false);
            auto credentials = doc.is_object() ? doc.find("credentials") : doc.end();
            if (doc.is_discarded() || !doc.is_object() || credentials == doc.end() || !credentials->is_object()) {
                return;
            }
            static const std::regex msaName(R"(^Xal\.\d+\.Production\.Msa\..+$)");
            for (const auto& [name, credential] : credentials->items()) {
                if (name.empty() ||
                    !std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
                    !credential.is_object()) {
                    continue;
                }
                for (const auto& [key, value] : credential.items()) {
                    if (!value.is_string() || !std::regex_match(key, msaName)) {
                        continue;
                    }
                    json token = json::parse(value.get<std::string>(), nullptr, false);
                    auto refresh = token.is_object() ? token.find("refresh_token") : token.end();
                    if (!token.is_discarded() && token.is_object() && refresh != token.end() && refresh->is_string()) {
                        out.push_back(Found{App::Official, launcher, std::string(), refresh->get<std::string>()});
                    }
                    break;
                }
            }
        }

        void hmcl(std::vector<Found>& out) {
            std::optional<std::string> data = readFile(appData() / ".hmcl" / "accounts.json");
            if (!data) {
                return;
            }
            json doc = json::parse(*data, nullptr, false);
            if (doc.is_discarded() || !doc.is_array()) {
                return;
            }
            for (const json& account : doc) {
                if (!account.is_object()) {
                    continue;
                }
                auto refresh = account.find("refreshToken");
                if (refresh == account.end() || !refresh->is_string() || refresh->get<std::string>().empty()) {
                    continue;
                }
                auto name = account.find("displayName");
                out.push_back(
                        Found{App::Hmcl, "HMCL",
                              name != account.end() && name->is_string() ? name->get<std::string>() : std::string(),
                              refresh->get<std::string>()});
            }
        }

        uint64_t pclHash(const std::string& text) {
            uint64_t value = 5381;
            for (unsigned char c : text) {
                value = (value << 5) ^ value ^ c;
            }
            return value ^ 12218072394304324399ull;
        }

        std::optional<std::string> pclIdentity() {
            std::optional<std::string> config =
                    registryText(HKEY_LOCAL_MACHINE, "SYSTEM\\HardwareConfig", "LastConfig");
            std::optional<std::string> identify = registryText(HKEY_CURRENT_USER, "Software\\PCL", "Identify");
            if (!config || !identify) {
                return std::nullopt;
            }
            std::string trimmed;
            for (char c : *config) {
                if (c != '{' && c != '}') {
                    trimmed += (char) std::toupper((unsigned char) c);
                }
            }
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%llX", (unsigned long long) pclHash(trimmed + *identify));
            std::string h = std::string(16 - std::strlen(hex), '7') + hex;
            return h.substr(4, 4) + "-" + h.substr(12, 4) + "-" + h.substr(0, 4) + "-" + h.substr(8, 4);
        }

        std::string pclSecret(const std::string& key) {
            if (key.empty()) {
                return "@;$ Abv2";
            }
            std::string number = std::to_string(pclHash(key));
            if (number.size() >= 8) {
                return number.substr(0, 8);
            }
            return std::string(8 - number.size(), 'X') + number;
        }

        std::optional<std::string> desDecrypt(const std::string& data, const std::string& key, const std::string& iv) {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_DES_ALGORITHM, nullptr, 0) < 0) {
                return std::nullopt;
            }
            std::optional<std::string> out;
            BCRYPT_KEY_HANDLE handle = nullptr;
            UCHAR keyBytes[8] = {};
            std::memcpy(keyBytes, key.data(), std::min<size_t>(8, key.size()));
            if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR) BCRYPT_CHAIN_MODE_CBC,
                                  sizeof(BCRYPT_CHAIN_MODE_CBC), 0) >= 0 &&
                BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, keyBytes, sizeof(keyBytes), 0) >= 0) {
                UCHAR vector[8] = {};
                std::memcpy(vector, iv.data(), std::min<size_t>(8, iv.size()));
                std::string input = data;
                ULONG size = 0;
                if (BCryptDecrypt(handle, (PUCHAR) input.data(), (ULONG) input.size(), nullptr, vector, sizeof(vector),
                                  nullptr, 0, &size, BCRYPT_BLOCK_PADDING) >= 0) {
                    std::string plain(size, '\0');
                    std::memcpy(vector, iv.data(), std::min<size_t>(8, iv.size()));
                    if (BCryptDecrypt(handle, (PUCHAR) input.data(), (ULONG) input.size(), nullptr, vector,
                                      sizeof(vector), (PUCHAR) plain.data(), size, &size, BCRYPT_BLOCK_PADDING) >= 0) {
                        plain.resize(size);
                        out = plain;
                    }
                }
                BCryptDestroyKey(handle);
            }
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return out;
        }

        void pcl(std::vector<Found>& out) {
            std::optional<std::string> identity = pclIdentity();
            std::optional<std::string> saved = registryText(HKEY_CURRENT_USER, "Software\\PCL", "LoginMsJson");
            if (!identity || !saved || saved->empty()) {
                return;
            }
            std::optional<std::string> bytes = blb::base64_decode(*saved);
            std::optional<std::string> plain =
                    bytes ? desDecrypt(*bytes, pclSecret("PCL" + *identity), "95168702") : std::nullopt;
            if (!plain) {
                return;
            }
            json doc = json::parse(*plain, nullptr, false);
            if (doc.is_discarded() || !doc.is_object()) {
                return;
            }
            for (const auto& [name, token] : doc.items()) {
                if (token.is_string() && !token.get<std::string>().empty()) {
                    out.push_back(Found{App::Pcl, "PCL", name, token.get<std::string>()});
                }
            }
        }
    }

    std::vector<Found> scan() {
        std::vector<Found> out;
        std::filesystem::path minecraft = appData() / ".minecraft";
        if (!appData().empty()) {
            officialLauncher(minecraft / "launcher_msa_credentials.bin", "Minecraft 启动器", out);
            officialLauncher(minecraft / "launcher_msa_credentials_microsoft_store.bin",
                             "Minecraft 启动器（Microsoft Store）", out);
            hmcl(out);
        }
        pcl(out);
        std::vector<Found> unique;
        for (Found& found : out) {
            if (std::none_of(unique.begin(), unique.end(),
                             [&](const Found& seen) { return seen.refreshToken == found.refreshToken; })) {
                unique.push_back(std::move(found));
            }
        }
        return unique;
    }
}
