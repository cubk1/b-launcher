#pragma once

#include <core/installer/loader.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Installer {

    namespace blb = b_launcher_base;

    struct Part {
        enum class Status { Waiting, Running, Done, Failed };

        std::string label;
        Status status = Status::Waiting;
        float fraction = -1.0f;
        std::string detail = "等待中";
    };

    bool running();
    float progress();
    std::vector<Part> parts();
    std::string activity();
    double elapsed();

    void start(const std::string& source, const std::string& minecraft,
               const std::vector<blb::install::loader_request>& loaders, const std::string& id);

    struct Modpack {
        std::string minecraft;
        std::filesystem::path file;
        std::vector<std::string> urls;
        std::string sha1;
        long long size = -1;
        std::vector<blb::install::loader_type> loaders;
    };
    void startModpack(const std::string& source, const Modpack& pack, const std::string& id);
    void startModify(const std::string& instanceKey, const std::vector<blb::install::loader_request>& loaders);
    void cancel();
    void poll();
    void shutdown();
}
