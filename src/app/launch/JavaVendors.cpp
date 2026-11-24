#include "app/launch/JavaVendors.h"

#include <core/base/strings.hpp>

#include <array>
#include <string_view>
#include <vector>

namespace JavaVendors {

    namespace blb = b_launcher_base;

    namespace {
        struct Rule {
            const char* icon;
            const char* name;
            std::array<std::string_view, 3> keys;
        };

        const Rule RULES[] = {
                {"graalvm", "GraalVM CE", {"graalvm"}},
                {"azul", "Azul Zulu", {"azul", "zulu"}},
                {"eclipse", "Eclipse Temurin", {"adoptium", "temurin"}},
                {"eclipse", "AdoptOpenJDK", {"adoptopenjdk"}},
                {"amazon", "Amazon Corretto", {"amazon", "corretto"}},
                {"bellsoft", "BellSoft Liberica", {"bellsoft", "liberica"}},
                {"microsoft", "Microsoft OpenJDK", {"microsoft"}},
                {"ibm", "IBM Semeru", {"ibm", "semeru", "openj9"}},
                {"jetbrains", "JetBrains Runtime", {"jetbrains", "jbr"}},
                {"sap", "SAP SapMachine", {"sapmachine", "sap se"}},
                {"alibaba", "Alibaba Dragonwell", {"alibaba", "dragonwell"}},
                {"", "Tencent Kona", {"tencent"}},
                {"", "Huawei BiSheng", {"huawei", "bisheng"}},
                {"", "Red Hat OpenJDK", {"red hat"}},
                {"oracle", "Oracle JDK", {"oracle"}},
        };

        const Rule* match(const std::vector<std::string>& texts) {
            for (const Rule& rule : RULES) {
                for (std::string_view key : rule.keys) {
                    for (const std::string& text : texts) {
                        if (!key.empty() && text.find(key) != std::string::npos) {
                            return &rule;
                        }
                    }
                }
            }
            return nullptr;
        }

        std::vector<std::string> folders(const std::filesystem::path& home) {
            constexpr size_t DEPTH = 4;
            std::vector<std::string> out;
            for (const std::filesystem::path& part : home) {
                out.push_back(blb::to_lower(blb::path_to_utf8(part)));
            }
            if (out.size() > DEPTH) {
                out.erase(out.begin(), out.end() - DEPTH);
            }
            return out;
        }

        bool within(const std::vector<std::string>& path, std::string_view key) {
            for (const std::string& folder : path) {
                if (folder.find(key) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        Vendor make(const Rule& rule, const std::vector<std::string>& path) {
            Vendor vendor{rule.icon, rule.name};
            if (std::string_view(rule.icon) == "oracle") {
                if (within(path, "graalvm")) {
                    vendor.name = "Oracle GraalVM";
                } else if (within(path, "openjdk")) {
                    vendor.name = "Oracle OpenJDK";
                }
            }
            return vendor;
        }
    }

    Vendor of(const blb::java::java_installation& java) {
        std::vector<std::string> path = folders(java.home);
        if (const Rule* rule = match({blb::to_lower(java.vendor)})) {
            return make(*rule, path);
        }
        if (const Rule* rule = match(path)) {
            return make(*rule, path);
        }
        return Vendor{std::string(), "OpenJDK"};
    }

    std::string title(const blb::java::java_installation& java) {
        return of(java).name + " " + (java.version.empty() ? std::to_string(java.major) : java.version);
    }
}
