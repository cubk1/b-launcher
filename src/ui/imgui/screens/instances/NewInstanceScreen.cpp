#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "app/system/Platform.h"
#include "app/system/Catalog.h"
#include "app/system/Notices.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/LoaderSelection.h"
#include "app/instances/Instances.h"
#include "app/instances/InstanceSettings.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/value/Values.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/instance/loader_editor.hpp>
#include <core/base/strings.hpp>
#include <core/modpack/mods.hpp>

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace NewInstanceScreen {

    namespace blb = b_launcher_base;
    using blb::install::loader_type;

    namespace {
        constexpr int STEP_VERSIONS = 0;
        constexpr int STEP_SETUP = 1;
        constexpr int STEP_LOADER = 2;
        constexpr float LIST_ROW = 36.0f;
        using Page::FIELD_H;
        constexpr float BUTTON_W = 96.0f;
        constexpr float BUTTON_H = 36.0f;
        constexpr float BUTTON_MARGIN = 28.0f;
        constexpr float SQUARE = 52.0f;
        constexpr float TAU = 6.28318531f;

        const char* const RELEASE = "正式版";
        const char* const SNAPSHOT = "快照版";
        const char* const APRIL_FOOLS = "愚人节版";
        const char* const OLD = "远古版";

        PageTransition steps(STEP_VERSIONS);
        std::vector<std::string> kinds = {RELEASE};
        LoaderSelection loaderSelection;
        const std::string& minecraft = loaderSelection.minecraft();
        const auto& chosen = loaderSelection.chosen();
        loader_type picking = loader_type::forge;
        std::string typedName;
        std::string target;
        std::optional<ModpackSetup> modpack;
        std::string editing;
        std::map<loader_type, std::string> original;
        bool readingLoaders = false;
        std::string loaderReadError;
        unsigned loaderReadGeneration = 0;
        // A Fabric install without Fabric API leaves most modpacks broken, so picking Fabric picks the API too.
        bool autoFabricApi = false;
        bool fabricApiRequested = false;
        std::string automaticFabricApi;

        std::string kindOf(const blb::meta::manifest_version& version) {
            switch (Catalog::kindOf(version)) {
                case Catalog::Kind::Release: return RELEASE;
                case Catalog::Kind::AprilFools: return APRIL_FOOLS;
                case Catalog::Kind::Old: return OLD;
                default: return SNAPSHOT;
            }
        }

        class KindChoice final : public Choice {
        public:
            KindChoice() : Choice("new#kinds") {}

            std::vector<std::string> options() override {
                return {RELEASE, SNAPSHOT, APRIL_FOOLS, OLD};
            }

            bool multi() override {
                return true;
            }

            bool selected(const std::string& option) override {
                return std::find(kinds.begin(), kinds.end(), option) != kinds.end();
            }

            void choose(const std::string& option) override {
                auto found = std::find(kinds.begin(), kinds.end(), option);
                if (found != kinds.end()) {
                    kinds.erase(found);
                } else {
                    kinds.push_back(option);
                }
            }

            std::string label() override {
                std::string text;
                for (const std::string& option : options()) {
                    if (selected(option)) {
                        text += (text.empty() ? "" : "、") + option;
                    }
                }
                return text.empty() ? std::string("不显示") : text;
            }
        };

        const char* const SEARCH_OWNER = "new#search";
        std::string searchText;

        std::shared_ptr<TextBinding> searchValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        SEARCH_OWNER, [] { return searchText; }, [](const std::string& text) { searchText = text; },
                        "搜索版本");
                binding->limit = 40;
                return binding;
            }();
            return value;
        }

        std::string day(const std::string& time) {
            return time.size() >= 10 ? time.substr(0, 10) : time;
        }

        float tag(const std::string& text, float x, float rowY, float rowH, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            float h = 18.0f;
            float y = rowY + (rowH - h) * 0.5f;
            UiRender::roundRect(x, y, w, h, 6.0f, fill);
            UiRender::textY(text, x + 6.0f, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        constexpr float CARD_RADIUS = 12.0f;

        void rowHover(const std::string& id, float x, float y, float w, float h, bool hovered, int corners) {
            float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (amount > 0.001f) {
                UiRender::roundRect(x, y, w, h, CARD_RADIUS, UiTheme::overlay(Ui::round(14 * amount)), corners);
            }
        }

        float message(const std::string& text, uint32_t color, bool loading = false) {
            float lead = loading ? 24.0f : 0.0f;
            float room = std::max(120.0f, Page::width() - 80.0f) - lead;
            std::vector<std::string> lines = UiFont::wrap(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room);
            float y = Page::TOP + 110.0f;
            for (size_t i = 0; i < lines.size(); i++) {
                float w = UiFont::width(lines[i], UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float shift = i == 0 ? lead : 0.0f;
                float x = Page::left() + (Page::width() - w - shift) * 0.5f;
                if (loading && i == 0) {
                    Page::spinner(x + 8.0f, y + 12.0f, 8.0f, UiTheme::accent());
                }
                UiRender::textY(lines[i], x + shift, y, 24.0f, color, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                y += 22.0f;
            }
            return y;
        }

        bool retryButton(const std::string& id, float y, bool blocked) {
            const std::string text = "重试";
            float w = std::max(88.0f, Page::buttonWidth(text, 0xf2f1));
            return Page::button(id, text, Page::left() + (Page::width() - w) * 0.5f, y + 14.0f, w, false, blocked,
                                0xf2f1);
        }

        std::string loaderLabelOf(const std::string& coreName) {
            if (auto type = blb::install::parse_loader_type(coreName)) {
                return Installer::loaderName(*type);
            }
            return coreName;
        }

        std::string generatedName() {
            if (modpack) {
                return InstanceSettings::modpackName(modpack->title);
            }
            return loaderSelection.defaultName();
        }

        std::string instanceName() {
            return typedName.empty() ? generatedName() : typedName;
        }

        std::string sourceLabel(const Instances::Source& source) {
            const auto& sources = Instances::sources();
            bool duplicate = std::count_if(sources.begin(), sources.end(), [&](const auto& other) {
                return other.label == source.label;
            }) > 1;
            return duplicate ? source.label + "（" + Platform::display(source.root) + "）" : source.label;
        }

        const Instances::Source* targetSource() {
            return Instances::installSource(target);
        }

        void chooseVersion(const std::string& id) {
            loaderSelection.reset(id);
            automaticFabricApi.clear();
            typedName.clear();
            steps.go(STEP_SETUP);
        }

        template <typename Row>
        int list(const std::string& id, float top, float bottom, size_t count, Row row) {
            float contentH = count * LIST_ROW;
            Page::Scroll body(id, top, bottom, contentH);
            bool outside = body.begin();
            bool blocked = outside || NewUi::overlayCoversMouse();
            float y0 = body.top - body.offset;
            Page::card(Page::left(), y0, Page::width(), contentH);
            int first = std::max(0, (int) std::floor((body.top - y0) / LIST_ROW));
            int last = std::min((int) count, (int) std::ceil((body.bottom - y0) / LIST_ROW) + 1);
            int clicked = -1;
            for (int i = first; i < last; i++) {
                float rowY = y0 + i * LIST_ROW;
                if (i > 0) {
                    UiRender::hLine(Page::left() + 14.0f, Page::right() - 14.0f, rowY, UiTheme::outlineSoft());
                }
                bool hovered = !blocked && NewUi::hit(Page::left(), rowY, Page::width(), LIST_ROW);
                rowHover(id + "#" + std::to_string(i), Page::left(), rowY, Page::width(), LIST_ROW, hovered,
                         Page::rowCorners(i, (int) count));
                row(i, rowY);
                if (!blocked && NewUi::click(Page::left(), rowY, Page::width(), LIST_ROW)) {
                    UiSound::request(UiSound::PAGE);
                    clicked = i;
                }
            }
            body.end();
            return clicked;
        }

        void versionsStep() {
            bool blocked = NewUi::overlayCoversMouse();
            Page::title("新建实例", Page::left(), Page::TOP);
            float refreshX = Page::right() - 28.0f;
            if (Page::iconButton("new#refresh", 0xf2f1, "刷新", refreshX, Page::TOP - 2.0f, blocked)) {
                Catalog::refreshMinecraft();
            }
            float importX = refreshX - 32.0f;
            if (Page::iconButton("new#import", 0xf56f, "导入整合包", importX, Page::TOP - 2.0f, blocked)) {
                std::vector<std::filesystem::path> picked =
                        Platform::pickFiles(Platform::exeDirectory(), L"导入整合包", L"整合包", L"*.zip;*.mrpack");
                if (!picked.empty()) {
                    Drops::importPack(picked.front());
                }
            }
            float filterX = importX - 8.0f - 170.0f;
            SelectWidget filter("new#kinds", std::make_shared<KindChoice>());
            filter.bounds(filterX, Page::TOP + 1.0f, 170.0f, FIELD_H).block(blocked);
            filter.paint();
            const float searchW = 170.0f;
            float searchX = filterX - 8.0f - searchW;
            TextFieldWidget search(searchValue());
            search.box(searchX, Page::TOP + 1.0f, searchW, FIELD_H)
                    .bounds(searchX, Page::TOP + 1.0f, searchW, FIELD_H)
                    .block(blocked);
            search.paint();
            std::string query =
                    Ui::trim(NewUi::textEditing == SEARCH_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : searchText);

            const Catalog::Versions& all = Catalog::minecraft();
            if (all.list.empty()) {
                if (all.status == Catalog::Status::Failed) {
                    float bottom = message("获取版本列表失败：" + all.error, UiTheme::danger());
                    if (retryButton("new#retry", bottom, blocked)) {
                        Catalog::refreshMinecraft();
                    }
                } else {
                    message("正在获取版本列表…", UiTheme::textMuted(), true);
                }
                return;
            }
            std::vector<const blb::meta::manifest_version*> shown;
            for (const blb::meta::manifest_version& version : all.list) {
                if (std::find(kinds.begin(), kinds.end(), kindOf(version)) != kinds.end() &&
                    blb::icontains(version.id, query)) {
                    shown.push_back(&version);
                }
            }
            if (shown.empty()) {
                message(query.empty() ? std::string("没有符合筛选的版本") : "没有找到“" + query + "”",
                        UiTheme::textMuted());
                return;
            }
            int clicked = list("new#versions", Page::TOP + 40.0f, MainWindow::height() - 14.0f, shown.size(),
                               [&](int i, float rowY) {
                                   const blb::meta::manifest_version& version = *shown[i];
                                   UiRender::texture(Icons::vanilla(), Page::left() + 16.0f,
                                                     rowY + (LIST_ROW - 16.0f) * 0.5f, 16.0f, 16.0f);
                                   UiRender::textY(version.id, Page::left() + 44.0f, rowY, LIST_ROW,
                                                   UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                                   float tagX = Page::left() + 44.0f +
                                           UiFont::width(version.id, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 8.0f;
                                   std::string kind = kindOf(version);
                                   bool release = kind == RELEASE;
                                   tag(kind, tagX, rowY, LIST_ROW,
                                       release ? UiTheme::accentSoft(255) : UiTheme::surfaceRaised(),
                                       release ? UiTheme::accentText() : UiTheme::textSecondary());
                                   std::string date = day(version.release_time);
                                   float dateW = UiFont::width(date, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                                   UiRender::textY(date, Page::right() - 16.0f - dateW, rowY, LIST_ROW,
                                                   UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                               });
            if (clicked >= 0) {
                chooseVersion(shown[clicked]->id);
            }
        }

        float easeOut(float t) {
            float inv = 1.0f - t;
            return 1.0f - inv * inv * inv;
        }

        void ring(float cx, float cy, float radius, float value, uint32_t color) {
            float x = UiRender::transformX(cx);
            float y = UiRender::transformY(cy);
            float r = UiRender::transformW(radius);
            float stroke = UiRender::transformW(2.6f);
            float known = UiState::motion("new#ring#known", value >= 0.0f ? 1.0f : 0.0f, 8.0f);
            float shown = UiState::motion("new#ring#fraction", std::max(0.0f, value), 10.0f);
            ShaderUtil::arc(x, y, r, stroke, 0.0f, TAU, UiRender::applyAlpha((color & 0x00FFFFFFu) | 0x47000000u));
            float spinFrom = 0.0f;
            float spinTo = 0.0f;
            Page::spinnerArc(spinFrom, spinTo);
            float from = UiRender::lerp(spinFrom - TAU * 0.25f, -TAU * 0.25f, known);
            float to = UiRender::lerp(spinTo - TAU * 0.25f, -TAU * 0.25f + std::max(0.02f, shown) * TAU, known);
            ShaderUtil::arc(x, y, r, stroke, from, to, UiRender::applyAlpha(color));
        }

        void installButton(float right, float bottom, const std::string& problem, bool waiting) {
            const char* action = editing.empty() ? "安装" : "应用";
            bool installing = Installer::running();
            bool usable = installing || (problem.empty() && !waiting);
            float morph = easeOut(UiState::motion("new#install#morph", installing ? 1.0f : 0.0f, 10.0f));
            float w = UiRender::lerp(BUTTON_W, SQUARE, morph);
            float h = UiRender::lerp(BUTTON_H, SQUARE, morph);
            float radius = UiRender::lerp(10.0f, 14.0f, morph);
            float x = right - w;
            float y = bottom - h;
            bool hovered = !NewUi::overlayCoversMouse() && NewUi::hit(x, y, w, h);
            float hover = UiState::motion("new#install#hover", usable && hovered ? 1.0f : 0.0f, 18.0f);
            float enabled = UiState::motion("new#install#enabled", usable ? 1.0f : 0.0f, 14.0f);
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * UiRender::lerp(0.45f, 1.0f, enabled);
            UiRender::shadow(x, y, w, h, radius, 12.0f, 4.0f, UiTheme::shadow());
            UiRender::roundRect(x, y, w, h, radius, UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.10f * hover));
            uint32_t ink = UiTheme::onAccent();
            float cx = x + w * 0.5f;
            float cy = y + h * 0.5f;
            if (morph < 0.99f) {
                float alpha = UiRender::alphaMul;
                UiRender::alphaMul = alpha * (1.0f - morph);
                float labelW = UiFont::width(action, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                UiRender::textY(action, cx - labelW * 0.5f, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                UiRender::alphaMul = alpha;
            }
            if (morph > 0.01f) {
                float alpha = UiRender::alphaMul;
                UiRender::alphaMul = alpha * morph;
                ring(cx, cy, 11.0f * UiRender::lerp(0.4f, 1.0f, morph), Installer::progress(), ink);
                UiRender::alphaMul = alpha;
            }
            UiRender::alphaMul = savedAlpha;

            if (!problem.empty() && !installing && morph < 0.01f) {
                float problemW = UiFont::width(problem, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(problem, x - 14.0f - problemW, y, h, UiTheme::danger(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else if (waiting && !installing && morph < 0.01f) {
                std::string text = fabricApiRequested ? "正在检查 Fabric API 兼容性…" : "正在获取 Fabric API 版本…";
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(text, x - 14.0f - textW, y, h, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (installing && hovered) {
                std::string hint = editing.empty() ? "取消安装" : "取消修改";
                float tipW = UiFont::width(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
                NewUi::tooltipText = hint;
                NewUi::tooltipX = x + w - tipW;
                NewUi::tooltipY = y - 34.0f;
            }
            if (usable && NewUi::click(x, y, w, h)) {
                UiSound::request(installing ? UiSound::RELEASE : UiSound::PULSE);
                if (installing) {
                    Installer::cancel();
                } else if (!editing.empty()) {
                    Installer::startModify(editing, loaderSelection.requests());
                } else if (const Instances::Source* source = targetSource()) {
                    if (modpack) {
                        Installer::startModpack(source->key, modpack->pack, instanceName());
                    } else {
                        Installer::start(source->key, minecraft, loaderSelection.requests(), typedName);
                    }
                }
            }
        }

        std::vector<Installer::Part> last;
        std::string lastActivity;
        double lastElapsed = 0.0;

        std::string clock(double seconds) {
            int whole = (int) seconds;
            char text[16];
            std::snprintf(text, sizeof(text), "%d:%02d", whole / 60, whole % 60);
            return text;
        }

        void progressBars(float top, float shown) {
            std::vector<Installer::Part> parts = Installer::parts();
            if (!parts.empty()) {
                last = parts;
                lastActivity = Installer::activity();
                lastElapsed = Installer::elapsed();
            }
            if (last.empty()) {
                return;
            }
            float savedAlpha = UiRender::alphaMul;
            float savedOffsetY = UiRender::offsetY;
            UiRender::alphaMul = savedAlpha * shown;
            UiRender::offsetY = savedOffsetY + (1.0f - shown) * 16.0f * UiRender::uiScale;
            const float rowH = 36.0f;
            const float barH = 4.0f;
            const float detailW = 150.0f;
            float barX = Page::left() + 110.0f;
            float detailRight = Page::right();
            float barW = detailRight - detailW - 12.0f - barX;
            double time = ImGui::GetTime();

            const float statusH = 30.0f;
            bool complete = std::all_of(last.begin(), last.end(), [](const Installer::Part& part) {
                return part.status == Installer::Part::Status::Done;
            });
            if (complete) {
                UiWidgets::check(Page::left() + 8.0f, top + statusH * 0.5f, UiTheme::accent(), 10.0f);
            } else {
                Page::spinner(Page::left() + 8.0f, top + statusH * 0.5f, 7.0f, UiTheme::accent());
            }
            UiRender::textY(lastActivity, Page::left() + 24.0f, top, statusH, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
            std::string taken = "已用 " + clock(lastElapsed);
            float takenW = UiFont::width(taken, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(taken, Page::right() - takenW, top, statusH, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            top += statusH + 10.0f;

            for (size_t i = 0; i < last.size(); i++) {
                const Installer::Part& part = last[i];
                float rowY = top + i * rowH;
                uint32_t detailColor = part.status == Installer::Part::Status::Failed ? UiTheme::danger()
                        : part.status == Installer::Part::Status::Running             ? UiTheme::textSecondary()
                                                                                      : UiTheme::textMuted();
                std::string detail =
                        UiFont::fitEnd(part.detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, detailW - 16.0f);
                float shownW = UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(detail, detailRight - shownW, rowY, rowH, detailColor, UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                float markX = detailRight - shownW - 9.0f;
                UiRender::textY(part.label, Page::left(), rowY, rowH, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                float barY = rowY + (rowH - barH) * 0.5f;
                UiRender::roundRect(barX, barY, barW, barH, barH * 0.5f, UiTheme::outline());
                bool finished = part.status == Installer::Part::Status::Done;
                bool failed = part.status == Installer::Part::Status::Failed;
                uint32_t fill = failed ? UiTheme::danger() : UiTheme::accent();
                std::string id = "new#part#" + std::to_string(i);
                float target = finished ? 1.0f : std::max(0.0f, part.fraction);
                float amount = UiState::motion(id + "#fill", target, 8.0f);
                bool measured = finished || failed || part.fraction >= 0.0f;
                if (measured) {
                    if (amount > 0.002f) {
                        UiRender::roundRect(barX, barY, std::max(barH, barW * amount), barH, barH * 0.5f, fill);
                    }
                } else if (part.status == Installer::Part::Status::Running) {
                    float segment = barW * 0.25f;
                    float phase = (float) std::fmod(time / 1.3 + i * 0.17, 1.0);
                    float segX = barX - segment + (barW + segment) * phase;
                    UiRender::scissor(barX, barY, barW, barH);
                    UiRender::roundRect(segX, barY, segment, barH, barH * 0.5f, fill);
                    UiRender::unscissor();
                }
                if (finished) {
                    UiWidgets::check(markX, rowY + rowH * 0.5f, UiTheme::accent(), 9.0f);
                } else if (failed) {
                    UiWidgets::icon(Ui::glyph(0xf00d), markX, rowY + rowH * 0.5f, UiTheme::danger(), 9.0f);
                }
            }
            UiRender::alphaMul = savedAlpha;
            UiRender::offsetY = savedOffsetY;
        }

        enum class RowAction { None, Open, Clear };

        RowAction loaderRow(const std::string& id, uint32_t image, int glyph, const std::string& label,
                            const std::string& status, uint32_t statusColor, bool enabled, bool clearable, float rowY,
                            bool blocked, int corners) {
            bool hovered = enabled && !blocked && NewUi::hit(Page::left(), rowY, Page::width(), Page::ROW_H);
            rowHover(id, Page::left(), rowY, Page::width(), Page::ROW_H, hovered, corners);
            float savedAlpha = UiRender::alphaMul;
            if (!enabled) {
                UiRender::alphaMul = savedAlpha * 0.5f;
            }
            if (image != 0) {
                UiRender::texture(image, Page::left() + 16.0f, rowY + (Page::ROW_H - 16.0f) * 0.5f, 16.0f, 16.0f);
            } else {
                UiWidgets::iconBeside(Ui::glyph(glyph), Page::left() + 24.0f, label, UiFont::MEDIUM,
                                      UiWidgets::TEXT_CONTROL, rowY, Page::ROW_H, UiTheme::textMuted(), 12.0f);
            }
            UiRender::textY(label, Page::left() + 44.0f, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float markX = Page::controlRight() - 10.0f;
            float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(status, markX - 18.0f - statusW, rowY, Page::ROW_H, statusColor, UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            bool cleared = false;
            if (clearable) {
                bool over = !blocked && NewUi::hit(markX - 12.0f, rowY + 8.0f, 24.0f, Page::ROW_H - 16.0f);
                UiWidgets::iconBeside(Ui::glyph(0xf00d), markX, status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, rowY,
                                      Page::ROW_H, over ? UiTheme::textPrimary() : UiTheme::textFaint(), 9.0f);
                cleared = !blocked && NewUi::click(markX - 12.0f, rowY + 8.0f, 24.0f, Page::ROW_H - 16.0f);
            } else if (enabled) {
                UiWidgets::iconBeside(Ui::glyph(0xf054), markX, status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, rowY,
                                      Page::ROW_H, UiTheme::textFaint(), 9.0f);
            }
            UiRender::alphaMul = savedAlpha;
            if (cleared) {
                UiSound::request(UiSound::RELEASE);
                return RowAction::Clear;
            }
            if (enabled && !blocked && NewUi::click(Page::left(), rowY, Page::width(), Page::ROW_H)) {
                UiSound::request(UiSound::PAGE);
                return RowAction::Open;
            }
            return RowAction::None;
        }

        void settings(float top, float bottom, float gone, bool blocked);

        // Check the newest API once per selection, including its embedded mods, before adding it.
        void attachFabricApi() {
            if (!autoFabricApi) {
                return;
            }
            if (chosen.count(loader_type::fabric) == 0) {
                autoFabricApi = false;
                return;
            }
            auto selected = chosen.find(loader_type::fabric_api);
            if (selected != chosen.end()) {
                if (automaticFabricApi.empty() || selected->second != automaticFabricApi) {
                    autoFabricApi = false;
                    return;
                }
                loaderSelection.remove(loader_type::fabric_api);
                automaticFabricApi.clear();
            }
            const Catalog::LoaderVersions& api = Catalog::loader(loader_type::fabric_api, minecraft);
            if (api.status == Catalog::Status::Loading) {
                return;
            }
            if (api.status != Catalog::Status::Ready || api.list.empty()) {
                autoFabricApi = false;
                return;
            }
            const blb::install::loader_version* latest = nullptr;
            for (const blb::install::loader_version& version : api.list) {
                if (version.stable) {
                    latest = &version;
                    break;
                }
            }
            if (latest == nullptr) {
                latest = &api.list.front();
            }
            const auto& requirements = Catalog::fabricApiRequirements(*latest, !fabricApiRequested);
            fabricApiRequested = true;
            if (requirements.status == Catalog::Status::Loading) {
                return;
            }
            autoFabricApi = false;
            if (requirements.status == Catalog::Status::Failed) {
                Notices::add(Notices::Kind::Failed, "未自动选择 Fabric API：无法检查兼容性，" + requirements.error,
                             5000);
                return;
            }
            const std::string& fabric = chosen.at(loader_type::fabric);
            std::string problem;
            for (const std::string& range : requirements.required) {
                if (!blb::modpack::version_in_range(fabric, range)) {
                    problem = "需要 Fabric Loader " + range;
                    break;
                }
            }
            for (const std::string& range : requirements.incompatible) {
                if (blb::modpack::version_in_range(fabric, range)) {
                    problem = "不兼容 Fabric Loader " + range;
                    break;
                }
            }
            if (!problem.empty()) {
                Notices::add(Notices::Kind::Info,
                             "未自动选择 Fabric API " + latest->version + "：" + problem +
                                     "。请调整 Loader 版本或手动选择兼容的 API。",
                             6000);
                return;
            }
            loaderSelection.select(loader_type::fabric_api, latest->version);
            automaticFabricApi = latest->version;
        }

        void setupStep() {
            attachFabricApi();
            bool installing = Installer::running();
            float phase = easeOut(UiState::motion("new#install#phase", installing ? 1.0f : 0.0f, 8.0f));
            bool blocked = NewUi::overlayCoversMouse();
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * (1.0f - phase);
            bool localPack = modpack && !modpack->pack.file.empty();
            const char* backHint = !editing.empty() ? "返回实例设置"
                    : localPack                     ? "返回"
                    : modpack                       ? "返回下载附加件"
                                                    : "重新选择版本";
            if (phase < 0.99f &&
                Page::iconButton("new#back", 0xf053, backHint, Page::left() - 4.0f, Page::TOP - 2.0f,
                           blocked || installing)) {
                if (!editing.empty()) {
                    std::string key = editing;
                    reset();
                    MainWindow::openVersionSettings(key);
                } else if (localPack) {
                    reset();
                    MainWindow::showHome();
                } else if (modpack) {
                    reset();
                    MainWindow::showDownloads();
                } else {
                    steps.go(STEP_VERSIONS);
                }
            }
            UiRender::alphaMul = savedAlpha;
            Page::title(!editing.empty()  ? "修改加载器"
                                : modpack ? "安装整合包"
                                          : "新建实例",
                        Page::left() + 30.0f * (1.0f - phase), Page::TOP);
            if (!editing.empty()) {
                if (const Instances::Entry* entry = Instances::find(editing)) {
                    float nameX = Page::left() + 30.0f * (1.0f - phase) +
                            UiFont::width("修改加载器", UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
                    UiRender::textY(UiFont::fitEnd(entry->info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                   Page::right() - nameX),
                                    nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                }
            }

            float buttonBottom = MainWindow::height() - BUTTON_MARGIN;
            float bodyTop = Page::TOP + 54.0f;
            float bodyBottom = buttonBottom - BUTTON_H - 12.0f;
            if (phase < 0.99f) {
                settings(bodyTop, bodyBottom, phase,
                         blocked || installing || readingLoaders || !loaderReadError.empty());
            }
            if (phase > 0.01f) {
                progressBars(bodyTop, phase);
            }

            std::string problem;
            if (!installing && !editing.empty()) {
                const Instances::Entry* entry = Instances::find(editing);
                problem = readingLoaders                          ? std::string("正在读取已安装的加载器…")
                        : !loaderReadError.empty()                ? loaderReadError
                        : entry == nullptr                        ? std::string("实例已不存在")
                        : chosen == original && entry->info.valid ? std::string("没有改动")
                                                                  : std::string();
            } else if (!installing) {
                const Instances::Source* source = targetSource();
                problem = source == nullptr ? std::string("没有可用的游戏目录")
                                            : Instances::checkInstallId(source->key, instanceName());
            }
            installButton(Page::right(), buttonBottom, problem, autoFabricApi);
        }

        void infoRow(const char* label, const std::string& value, float y) {
            Page::label(label, y);
            float available = Page::controlRight() - Page::left() - 140.0f;
            std::string shown = UiFont::fitEnd(value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, available);
            float w = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, Page::controlRight() - w, y, Page::ROW_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void modpackRows(float y) {
            infoRow("整合包", modpack->title, y);
            y += Page::ROW_H;
            infoRow("版本", modpack->version, y);
            y += Page::ROW_H;
            infoRow("游戏版本", modpack->minecraft.empty() ? std::string("未知") : "Minecraft " + modpack->minecraft,
                    y);
            y += Page::ROW_H;
            std::string loaders;
            for (const std::string& loader : modpack->loaders) {
                loaders += (loaders.empty() ? "" : "、") + loader;
            }
            infoRow("加载器", loaders.empty() ? std::string("无") : loaders, y);
        }

        void settings(float top, float bottom, float gone, bool blocked) {
            std::vector<blb::instance::loader_choice> choices = modpack
                    ? std::vector<blb::instance::loader_choice>()
                    : loaderSelection.choices();
            int loaderRows = modpack ? 4 : 1 + (int) choices.size();
            bool edit = !editing.empty();
            float contentH = edit ? Page::CAPTION_H + loaderRows * Page::ROW_H
                                  : 2 * Page::CAPTION_H + (2 + loaderRows) * Page::ROW_H + Page::GAP;
            float savedAlpha = UiRender::alphaMul;
            float savedOffsetY = UiRender::offsetY;
            UiRender::alphaMul = savedAlpha * (1.0f - gone);
            UiRender::offsetY = savedOffsetY - gone * 24.0f * UiRender::uiScale;
            Page::Scroll body("new#setup", top, top + (bottom - top) * (1.0f - gone), contentH);
            bool outside = body.begin();
            bool rowsBlocked = outside || blocked;
            float y = body.top - body.offset;

            if (!edit) {
                Page::caption("实例", y);
                y += Page::CAPTION_H;
                Page::card(y, 2);
                Page::label("名称", y);
                std::string generated = generatedName();
                auto nameValue = std::make_shared<TextBinding>(
                        "new#name", [] { return instanceName(); },
                        [generated](const std::string& value) {
                            typedName = value == generated ? std::string() : value;
                        },
                        generated);
                nameValue->limit = 128;
                float fieldW = 300.0f;
                float fieldX = Page::controlRight() - fieldW;
                float fieldY = y + Page::FIELD_Y;
                TextFieldWidget field(nameValue);
                field.box(fieldX, fieldY, fieldW, FIELD_H).bounds(fieldX, fieldY, fieldW, FIELD_H).block(rowsBlocked);
                field.paint();
                y += Page::ROW_H;

                Page::label("安装位置", y);
                auto place = std::make_shared<ModeBinding>(
                        "new#place",
                        [] {
                            const Instances::Source* source = targetSource();
                            return source != nullptr ? sourceLabel(*source) : std::string();
                        },
                        [](const std::string& option) {
                            for (const Instances::Source& source : Instances::sources()) {
                                if (sourceLabel(source) == option) {
                                    target = source.key;
                                    return;
                                }
                            }
                        },
                        [] {
                            std::vector<std::string> options;
                            for (const Instances::Source& source : Instances::sources()) {
                                options.push_back(sourceLabel(source));
                            }
                            return options;
                        });
                SelectWidget select("new#place", place);
                select.bounds(Page::controlRight() - 180.0f, y + Page::FIELD_Y, 180.0f, FIELD_H).block(rowsBlocked);
                select.paint();
                y += Page::ROW_H + Page::GAP;
            }

            if (modpack) {
                Page::caption("整合包", y);
                y += Page::CAPTION_H;
                Page::card(y, loaderRows);
                modpackRows(y);
                body.end();
                UiRender::alphaMul = savedAlpha;
                UiRender::offsetY = savedOffsetY;
                return;
            }

            Page::caption("加载器", y);
            y += Page::CAPTION_H;
            Page::card(y, loaderRows);
            if (edit) {
                if (Page::shown(y)) {
                    UiRender::texture(Icons::vanilla(), Page::left() + 16.0f, y + (Page::ROW_H - 16.0f) * 0.5f, 16.0f,
                                      16.0f);
                    UiRender::textY("Minecraft", Page::left() + 44.0f, y, Page::ROW_H, UiTheme::textPrimary(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    float versionW = UiFont::width(minecraft, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    UiRender::textY(minecraft, Page::controlRight() - 28.0f - versionW, y, Page::ROW_H,
                                    UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                }
            } else if (Page::shown(y) &&
                       loaderRow("new#row#minecraft", Icons::vanilla(), 0, "Minecraft", minecraft,
                                 UiTheme::textSecondary(), true, false, y, rowsBlocked,
                                 Page::rowCorners(0, loaderRows)) == RowAction::Open) {
                steps.go(STEP_VERSIONS);
            }
            y += Page::ROW_H;
            int rowIndex = 1;
            for (const blb::instance::loader_choice& choice : choices) {
                loader_type type = choice.type;
                bool picked = chosen.count(type) != 0;
                std::string status;
                uint32_t color = UiTheme::textMuted();
                bool enabled = picked || choice.state == blb::instance::choice_state::available;
                if (picked) {
                    status = chosen.at(type);
                    color = UiTheme::textSecondary();
                } else if (choice.state == blb::instance::choice_state::incompatible) {
                    status = "与 " + loaderLabelOf(choice.related) + " 不兼容";
                } else if (choice.state == blb::instance::choice_state::requires_loader) {
                    status = "需要先选择 " + loaderLabelOf(choice.related);
                } else {
                    status = "未选择";
                }
                int index = rowIndex++;
                RowAction action = Page::shown(y)
                        ? loaderRow("new#row#" + std::to_string((int) type), Icons::loader(type), 0xf1b2,
                                    Installer::loaderName(type), status, color, enabled, picked, y, rowsBlocked,
                                    Page::rowCorners(index, loaderRows))
                        : RowAction::None;
                if (action == RowAction::Open) {
                    picking = type;
                    steps.go(STEP_LOADER);
                } else if (action == RowAction::Clear) {
                    loaderSelection.remove(type);
                    if (type == loader_type::fabric_api) {
                        autoFabricApi = false;
                        automaticFabricApi.clear();
                    }
                }
                y += Page::ROW_H;
            }
            body.end();
            UiRender::alphaMul = savedAlpha;
            UiRender::offsetY = savedOffsetY;
        }

        void loaderStep() {
            bool blocked = NewUi::overlayCoversMouse();
            if (Page::iconButton("new#back2", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, blocked)) {
                steps.go(STEP_SETUP);
            }
            std::string label = Installer::loaderName(picking);
            Page::title(label + " 版本", Page::left() + 30.0f, Page::TOP);
            float refreshX = Page::right() - 28.0f;
            if (Page::iconButton("new#refresh2", 0xf2f1, "刷新", refreshX, Page::TOP - 2.0f, blocked)) {
                Catalog::refreshLoader(picking, minecraft);
            }

            const Catalog::LoaderVersions& versions = Catalog::loader(picking, minecraft);
            if (versions.list.empty()) {
                if (versions.status == Catalog::Status::Failed) {
                    float bottom = message("获取 " + label + " 版本失败：" + versions.error, UiTheme::danger());
                    if (retryButton("new#retryLoader", bottom, blocked)) {
                        Catalog::refreshLoader(picking, minecraft);
                    }
                } else if (versions.status == Catalog::Status::Loading) {
                    message("正在获取 " + label + " 版本…", UiTheme::textMuted(), true);
                } else {
                    message(label + " 没有适用于 " + minecraft + " 的版本", UiTheme::textMuted());
                }
                return;
            }
            uint32_t image = Icons::loader(picking);
            const std::string current = chosen.count(picking) != 0 ? chosen.at(picking) : std::string();
            int clicked = list(
                    "new#loader#" + std::to_string((int) picking), Page::TOP + 40.0f, MainWindow::height() - 14.0f,
                    versions.list.size(), [&](int i, float rowY) {
                        const blb::install::loader_version& version = versions.list[i];
                        if (image != 0) {
                            UiRender::texture(image, Page::left() + 16.0f, rowY + (LIST_ROW - 16.0f) * 0.5f, 16.0f,
                                              16.0f);
                        } else {
                            UiWidgets::iconBeside(Ui::glyph(0xf1b2), Page::left() + 24.0f, version.version,
                                                  UiFont::MEDIUM, UiWidgets::TEXT_BODY, rowY, LIST_ROW,
                                                  UiTheme::textMuted(), 12.0f);
                        }
                        UiRender::textY(version.version, Page::left() + 44.0f, rowY, LIST_ROW, UiTheme::textPrimary(),
                                        UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                        float tagX = Page::left() + 44.0f +
                                UiFont::width(version.version, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 8.0f;
                        if (!version.stable) {
                            tag("测试版", tagX, rowY, LIST_ROW, UiTheme::surfaceRaised(), UiTheme::textSecondary());
                        }
                        float rightX = Page::right() - 16.0f;
                        if (version.version == current) {
                            UiWidgets::check(rightX - 6.0f, rowY + LIST_ROW * 0.5f, UiTheme::accent(), 10.0f);
                            rightX -= 24.0f;
                        }
                        std::string date = day(version.release_time);
                        if (!date.empty()) {
                            float dateW = UiFont::width(date, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                            UiRender::textY(date, rightX - dateW, rowY, LIST_ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                                            UiWidgets::TEXT_CONTROL);
                        }
                    });
            if (clicked >= 0) {
                loaderSelection.select(picking, versions.list[clicked].version);
                autoFabricApi = editing.empty() && picking == loader_type::fabric;
                fabricApiRequested = false;
                if (picking == loader_type::fabric_api) {
                    automaticFabricApi.clear();
                }
                steps.go(STEP_SETUP);
            }
        }
    }

    void openModpack(const ModpackSetup& value) {
        reset();
        modpack = value;
        loaderSelection.reset(value.minecraft.empty() ? std::string("modpack") : value.minecraft);
        steps.page = STEP_SETUP;
        steps.mix = 1.0f;
    }

    void leaveModpack() {
        if (modpack || !editing.empty()) {
            reset();
        }
    }

    void openLoaderEdit(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr || entry->info.minecraft_version.empty() || Installer::running()) {
            return;
        }
        reset();
        editing = instanceKey;
        LoaderSelection::Choices installed;
        for (const blb::install::loader_info& loader : entry->info.loaders) {
            installed[loader.type] = loader.version;
        }
        loaderSelection.reset(entry->info.minecraft_version, std::move(installed));
        original = chosen;
        readingLoaders = true;
        unsigned generation = loaderReadGeneration;
        Instances::readLoaders(instanceKey, [instanceKey, generation](auto result) {
            if (generation != loaderReadGeneration || editing != instanceKey) return;
            readingLoaders = false;
            if (!result) {
                loaderReadError = "读取加载器失败，请重新打开此页面";
                Instances::notify(result.err().message, true);
                return;
            }
            LoaderSelection::Choices installed;
            for (const auto& loader : *result)
                installed[loader.type] = loader.version;
            loaderSelection.reset(minecraft, std::move(installed));
            original = chosen;
        });
        steps.page = STEP_SETUP;
        steps.mix = 1.0f;
    }

    void reset() {
        loaderSelection.reset();
        autoFabricApi = false;
        fabricApiRequested = false;
        automaticFabricApi.clear();
        typedName.clear();
        modpack.reset();
        editing.clear();
        original.clear();
        ++loaderReadGeneration;
        readingLoaders = false;
        loaderReadError.clear();
        steps.go(STEP_VERSIONS);
        steps.mix = 1.0f;
        last.clear();
        for (auto it = UiState::motionMap.begin(); it != UiState::motionMap.end();) {
            const std::string& id = it->first;
            if (id.rfind("new#install#", 0) == 0 || id.rfind("new#ring#", 0) == 0 || id.rfind("new#part#", 0) == 0) {
                it = UiState::motionMap.erase(it);
            } else {
                ++it;
            }
        }
    }

    void draw() {
        if (steps.page != STEP_VERSIONS && minecraft.empty()) {
            steps.go(STEP_VERSIONS);
        }
        steps.begin();
        switch (steps.page) {
            case STEP_SETUP: setupStep(); break;
            case STEP_LOADER: loaderStep(); break;
            default: versionsStep(); break;
        }
        steps.end();
    }
}
