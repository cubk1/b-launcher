#pragma once

#include <windows.h>

#include <core/modpack/modpack.hpp>

#include <filesystem>

namespace ForgottenMelody {

    // 下载中心: the kinds of things to download listed on the left, in the frame of the 2014 window; returns once it
    // is closed.
    void showDownloads(HWND parent);
    // 下载中心 on 新建实例, set up to install the modpack in file, which manifest describes.
    void showModpack(HWND parent, const std::filesystem::path& file,
                     const b_launcher_base::modpack::pack_manifest& manifest);
}
