#include "ui/imgui/screens/downloads/Prerequisites.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/downloads/Projects.h"
#include "app/instances/Instances.h"
#include "app/instances/ModManager.h"
#include "app/instances/Mods.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <algorithm>
#include <map>
#include <vector>

namespace Prerequisites {

    using Addons::Kind;
    using Addons::Status;

    namespace {
        constexpr const char* DIALOG = "downloads#prerequisites";
        constexpr float DIALOG_W = 460.0f;
        constexpr float DIALOG_H = 400.0f;
        constexpr float ROW = 40.0f;
        constexpr float ICON = 24.0f;

        Addons::Version root;
        std::string rootTitle;
        std::string rootIcon;
        std::string owner;
        bool saving = false;
        std::string gameVersion;
        std::string loader;
        std::map<std::string, bool> toggled;

        bool present(const Addons::Version& dependency) {
            return !saving && installed(owner, dependency.source, dependency.projectId, &dependency);
        }

        bool chosen(const Addons::Version& dependency) {
            auto found = toggled.find(dependency.projectId);
            return found != toggled.end() ? found->second : !present(dependency);
        }

        std::string preferred(const std::vector<std::string>& offered, const std::string& wanted, bool newest) {
            if (!wanted.empty() && std::find(offered.begin(), offered.end(), wanted) != offered.end()) {
                return wanted;
            }
            if (offered.empty()) {
                return std::string();
            }
            return newest ? offered.back() : offered.front();
        }

        void launch(std::vector<Addons::Version> dependencies) {
            if (!saving) {
                ProjectInstaller::install(root, Kind::Mod, rootTitle, owner, std::move(dependencies), rootIcon);
            } else if (dependencies.empty()) {
                ProjectInstaller::saveAs(root, rootTitle, rootIcon);
            } else {
                ProjectInstaller::saveAllAs(root, dependencies, rootTitle, rootIcon);
            }
        }

        void checkbox(float x, float y, bool on) {
            if (on) {
                UiRender::roundRect(x, y, 18.0f, 18.0f, 5.0f, UiTheme::accent());
                UiWidgets::icon(Ui::glyph(0xf00c), x + 9.0f, y + 9.0f, UiTheme::onAccent(), 10.0f);
            } else {
                UiRender::outline(x, y, 18.0f, 18.0f, 5.0f, UiTheme::outline(), 0xF, 1.4f);
            }
        }

        void row(const Addons::Version& dependency, float x, float y, float w, bool blocked) {
            bool on = chosen(dependency);
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW);
            if (hovered) {
                UiRender::roundRect(x, y + 2.0f, w, ROW - 4.0f, 8.0f, UiTheme::overlay(12));
            }
            float boxX = x + 10.0f;
            checkbox(boxX, y + (ROW - 18.0f) * 0.5f, on);
            const Projects::Brief* brief = Projects::brief(dependency.source, dependency.projectId);
            float iconX = boxX + 28.0f;
            DownloadsParts::projectIcon(brief != nullptr ? brief->iconUrl : std::string(), iconX,
                                        y + (ROW - ICON) * 0.5f, ICON, Kind::Mod);
            std::string detail = present(dependency) ? std::string("已安装")
                    : !dependency.number.empty()     ? dependency.number
                                                     : dependency.name;
            detail = UiFont::fitEnd(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, 150.0f);
            float detailW = UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(detail, x + w - 10.0f - detailW, y, ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            std::string name = brief != nullptr && !brief->title.empty()
                    ? DownloadsParts::displayTitle(brief->title, Kind::Mod)
                    : (!dependency.name.empty() ? dependency.name : dependency.projectId);
            float nameX = iconX + ICON + 10.0f;
            UiRender::textY(UiFont::fitEnd(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                           std::max(0.0f, x + w - 22.0f - detailW - nameX)),
                            nameX, y, ROW, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (!blocked && NewUi::click(x, y, w, ROW)) {
                toggled[dependency.projectId] = !on;
            }
        }

        void body(float x, float y, float w, float h) {
            const ProjectInstaller::Plan& plan = ProjectInstaller::plan(root, gameVersion, loader);
            if (!saving) {
                ModManager::prepare(owner);
            }
            UiRender::textY("前置模组", x + 20.0f, y + 16.0f, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            float top = y + 56.0f;
            float bottom = y + h - 64.0f;
            std::vector<Addons::Version> selected;
            if (plan.status == Status::Loading) {
                Page::spinner(x + 28.0f, top + 12.0f, 7.0f, UiTheme::accent());
                UiRender::textY("正在查找前置模组", x + 44.0f, top, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else if (plan.status == Status::Failed) {
                float lineY = top;
                for (const std::string& line :
                     UiFont::wrap(plan.error, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 40.0f)) {
                    if (lineY + 22.0f > bottom) {
                        break;
                    }
                    UiRender::textY(line, x + 20.0f, lineY, 22.0f, UiTheme::danger(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    lineY += 22.0f;
                }
            } else if (plan.dependencies.empty()) {
                UiRender::textY("没有需要安装的前置模组", x + 20.0f, top, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else {
                float listX = x + 12.0f;
                float listW = w - 24.0f - 10.0f;
                Page::Scroll scroll("downloads#prerequisites#scroll", top, bottom,
                                    (float) plan.dependencies.size() * ROW, listX, listW);
                bool outside = scroll.begin();
                for (size_t i = 0; i < plan.dependencies.size(); i++) {
                    float rowY = top - scroll.offset + (float) i * ROW;
                    if (rowY + ROW >= top && rowY <= bottom) {
                        row(plan.dependencies[i], listX, rowY, listW, outside);
                    }
                }
                scroll.end();
                for (const Addons::Version& dependency : plan.dependencies) {
                    if (chosen(dependency)) {
                        selected.push_back(dependency);
                    }
                }
            }
            float buttonY = y + h - 20.0f - Page::FIELD_H;
            std::string both = saving ? "一起另存为" : "一起安装";
            std::string only = saving ? "仅另存此模组" : "仅安装此模组";
            float bothW = std::max(96.0f, Page::buttonWidth(both));
            float bothX = x + w - 20.0f - bothW;
            float onlyW = Page::buttonWidth(only);
            float onlyX = bothX - 10.0f - onlyW;
            if (Page::button("downloads#prerequisites#only", only, onlyX, buttonY, onlyW, false, false)) {
                Dialog::close();
                launch({});
                return;
            }
            if (Page::button("downloads#prerequisites#both", both, bothX, buttonY, bothW, true, selected.empty())) {
                Dialog::close();
                launch(std::move(selected));
            }
        }
    }

    void context(const Addons::Version& version, const std::string& instanceKey, std::string& gameVersion,
                 std::string& loader) {
        std::string game = DownloadsParts::versionFilter();
        std::string tag = DownloadsParts::loaderFilter();
        if (const Instances::Entry* entry = Instances::find(instanceKey)) {
            game = entry->info.minecraft_version;
            tag = ProjectInstaller::loaderOf(instanceKey);
        }
        gameVersion = preferred(version.gameVersions, game, true);
        loader = preferred(version.loaders, tag, false);
    }

    bool installed(const std::string& instanceKey, Addons::Source source, const std::string& projectId,
                   const Addons::Version* version) {
        if (instanceKey.empty() || Instances::find(instanceKey) == nullptr) {
            return false;
        }
        if (version != nullptr && ProjectInstaller::installed(*version, Kind::Mod, instanceKey)) {
            return true;
        }
        if (projectId.empty()) {
            return false;
        }
        for (const Mods::Mod& mod : Mods::of(instanceKey).mods) {
            const ModManager::Remote* remote = ModManager::remote(mod, source);
            if (remote != nullptr && remote->projectId == projectId) {
                return true;
            }
        }
        return false;
    }

    bool needed(const Addons::Version& version) {
        return std::any_of(version.dependencies.begin(), version.dependencies.end(),
                           [](const Addons::Dependency& dependency) {
                               return dependency.required && !dependency.embedded && !dependency.incompatible;
                           });
    }

    void open(const Addons::Version& version, const std::string& title, bool saveAs, const std::string& instanceKey,
              const std::string& icon) {
        root = version;
        rootTitle = title;
        rootIcon = icon;
        owner = instanceKey;
        saving = saveAs;
        toggled.clear();
        if (saveAs) {
            context(version, std::string(), gameVersion, loader);
        } else {
            const Instances::Entry* entry = Instances::find(instanceKey);
            gameVersion = entry != nullptr ? entry->info.minecraft_version : std::string();
            loader = ProjectInstaller::loaderOf(instanceKey);
        }
        Dialog::open(DIALOG, DIALOG_W, DIALOG_H, body);
    }
}
