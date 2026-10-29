#include "ui/exui/Skins.h"
#include "ui/exui/EmbeddedSkins.h"

namespace exui {

    std::span<const std::uint8_t> SkinBytes(Skin skin) {
        switch (skin) {
            case Skin::MainBackground: return {EmbeddedSkins::mainBackground, EmbeddedSkins::mainBackgroundSize};
            case Skin::DialogBackground: return {EmbeddedSkins::dialogBackground, EmbeddedSkins::dialogBackgroundSize};
            case Skin::MsgBoxBorder: return {EmbeddedSkins::msgBoxBorder, EmbeddedSkins::msgBoxBorderSize};
            case Skin::MsgBoxSkin: return {EmbeddedSkins::msgBoxSkin, EmbeddedSkins::msgBoxSkinSize};
            case Skin::DownloadCenterBackground:
                return {EmbeddedSkins::downloadCenterBackground, EmbeddedSkins::downloadCenterBackgroundSize};
            case Skin::FrameAtlas: return {EmbeddedSkins::frameAtlas, EmbeddedSkins::frameAtlasSize};
            case Skin::ComboButtonNormal:
                return {EmbeddedSkins::comboButtonNormal, EmbeddedSkins::comboButtonNormalSize};
            case Skin::ComboButtonHover: return {EmbeddedSkins::comboButtonHover, EmbeddedSkins::comboButtonHoverSize};
            case Skin::RadioSprites: return {EmbeddedSkins::radioSprites, EmbeddedSkins::radioSpritesSize};
            case Skin::CheckSprites: return {EmbeddedSkins::checkSprites, EmbeddedSkins::checkSpritesSize};
            case Skin::MenuCheckmark: return {EmbeddedSkins::menuCheckmark, EmbeddedSkins::menuCheckmarkSize};
        }
        return {};
    }
}
