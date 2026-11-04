#pragma once
#include <cstdint>
#include <span>

namespace exui {

    // Images used by the 2014-style interface, embedded at build time.
    enum class Skin {
        MainBackground, // 720x450 main window background
        DialogBackground, // background of the smaller dialogs and message boxes
        MsgBoxBorder, // message-box bottom strip
        MsgBoxSkin, // message-box body and icons
        FrameAtlas, // caption buttons and window frame slices
        ComboButtonNormal,
        ComboButtonHover,
        RadioSprites, // 4 frames of 17x17
        CheckSprites, // 4 frames of 17x17
        MenuCheckmark,
    };

    std::span<const std::uint8_t> SkinBytes(Skin skin);

} // namespace exui
