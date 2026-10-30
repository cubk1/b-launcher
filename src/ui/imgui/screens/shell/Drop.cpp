#include "ui/imgui/screens/shell/Drop.h"
#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"

#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/strings.hpp>

#include <filesystem>
#include <string>

namespace Drop {

    namespace blb = b_launcher_base;

    namespace {
        std::string targetInstance() {
            std::string shown = MainWindow::shownInstance();
            return shown.empty() ? Instances::selected() : shown;
        }

        void openModpack(const std::filesystem::path& file, const blb::modpack::pack_manifest& manifest) {
            NewInstanceScreen::Modpack setup;
            setup.title = manifest.name.empty() ? blb::path_to_utf8(file.stem()) : manifest.name;
            setup.version = manifest.version.empty() ? std::string("未知") : manifest.version;
            setup.minecraft = manifest.minecraft;
            for (const blb::install::loader_request& loader : manifest.loaders) {
                setup.loaders.push_back(Installer::loaderName(loader.type) +
                                        (loader.version.empty() ? std::string() : " " + loader.version));
                setup.pack.loaders.push_back(loader.type);
            }
            setup.pack.minecraft = manifest.minecraft;
            setup.pack.file = file;
            NewInstanceScreen::openModpack(setup);
            MainWindow::showNewInstance();
        }
    }

    void attach(void* window) {
        Drops::attach(window, {targetInstance, openModpack, nullptr});
    }

    void draw(float width, float height) {
        float shown = UiState::motion("drop#shown", Drops::hovering() ? 1.0f : 0.0f, 16.0f);
        if (shown < 0.01f) {
            return;
        }
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * shown;
        uint32_t veil = (UiTheme::panel() & 0x00FFFFFFu) | 0xE0000000u;
        UiRender::rect(0, 0, width, height, veil);
        const float inset = 14.0f;
        uint32_t accent = UiTheme::accent();
        UiRender::dashedOutline(inset, inset, width - inset * 2.0f, height - inset * 2.0f, 14.0f, accent, 8.0f, 6.0f);
        std::string text = Drops::promise();
        bool usable = text.rfind("松开", 0) == 0;
        float cy = height * 0.5f;
        UiWidgets::icon(Ui::glyph(usable ? 0xf56f : 0xf05e), width * 0.5f, cy - 22.0f,
                        usable ? accent : UiTheme::textMuted(), 26.0f);
        float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        UiRender::textY(text, (width - textW) * 0.5f, cy + 8.0f, 24.0f,
                        usable ? UiTheme::textPrimary() : UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        UiRender::alphaMul = savedAlpha;
    }
}
