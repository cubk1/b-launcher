#include "ui/imgui/screens/shell/Drop.h"
#include "app/instances/Drops.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/instances/InstanceMods.h"
#include "ui/imgui/screens/instances/ModGraph.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"

#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Drop {

    namespace blb = b_launcher_base;

    namespace {
        std::string targetInstance() {
            std::string shown = MainWindow::shownInstance();
            return shown.empty() ? Instances::selected() : shown;
        }

        bool toInterface(int x, int y, float& outX, float& outY) {
            ImVec2 display = ImGui::GetIO().DisplaySize;
            if (display.x <= 0.0f || display.y <= 0.0f) {
                return false;
            }
            outX = (float) x * MainWindow::width() / display.x;
            outY = (float) y * MainWindow::height() / display.y;
            return true;
        }

        void placed(const std::string& key, const std::vector<std::filesystem::path>& mods, int x, int y) {
            float ux = 0.0f;
            float uy = 0.0f;
            if (!mods.empty() && InstanceMods::graphShown(key) && toInterface(x, y, ux, uy)) {
                ModGraph::pin(key, mods, ux, uy);
            }
        }

        void openModpack(const std::filesystem::path& file, const blb::modpack::pack_manifest& manifest) {
            NewInstanceScreen::openModpack(ModpackSetup::fromFile(file, manifest));
            MainWindow::showNewInstance();
        }
    }

    void attach(void* window) {
        Drops::attach(window, {targetInstance, openModpack, nullptr, placed});
    }

    void draw(float width, float height) {
        int px = 0;
        int py = 0;
        float ux = 0.0f;
        float uy = 0.0f;
        std::string key = targetInstance();
        int mods = Drops::modCount();
        bool placing = Drops::hovering() && mods > 0 && InstanceMods::graphShown(key) && Drops::point(px, py) &&
                toInterface(px, py, ux, uy) && Drops::promise().rfind("松开", 0) == 0;
        float pinned = UiState::motion("drop#pinned", placing ? 1.0f : 0.0f, 18.0f);
        if (placing) {
            UiState::keepAnimating();
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * pinned;
            ModGraph::preview(key, "添加 " + std::to_string(mods) + " 个模组", ux, uy);
            UiRender::alphaMul = savedAlpha;
            return;
        }
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
        bool login = text.rfind("松开以登录", 0) == 0;
        float cy = height * 0.5f;
        UiWidgets::icon(Ui::glyph(login            ? 0xf090
                                          : usable ? 0xf56f
                                                   : 0xf05e),
                        width * 0.5f, cy - 22.0f, usable ? accent : UiTheme::textMuted(), 26.0f);
        float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        UiRender::textY(text, (width - textW) * 0.5f, cy + 8.0f, 24.0f,
                        usable ? UiTheme::textPrimary() : UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        UiRender::alphaMul = savedAlpha;
    }
}
