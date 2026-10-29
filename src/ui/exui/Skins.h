#pragma once
#include <cstdint>
#include <span>

namespace exui {

    // The eleven images of the 2014 launcher, embedded at build time.
    enum class Skin {
        MainBackground, // 720x450 main window background
        DialogBackground, // background of the smaller dialogs and message boxes
        MsgBoxBorder, // message-box bottom strip
        MsgBoxSkin, // message-box body and icons
        DownloadCenterBackground, // 900x368 download-center header
        FrameAtlas, // caption buttons and window frame slices
        ComboButtonNormal,
        ComboButtonHover,
        RadioSprites, // 4 frames of 17x17
        CheckSprites, // 4 frames of 17x17
        MenuCheckmark,
    };

    std::span<const std::uint8_t> SkinBytes(Skin skin);

} // namespace exui
