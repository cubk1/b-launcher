#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "app/system/Platform.h"
#include "app/system/Catalog.h"
#include "app/system/Notices.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "app/instances/Covers.h"
#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/LoaderSelection.h"
#include "app/instances/Instances.h"
#include "app/instances/InstanceSettings.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <functional>
#include <map>
#include <set>
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
        std::set<loader_type> wanting;

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
            if (const Instances::Source* only = Instances::soleSource()) {
                return only;
            }
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

        constexpr float TILE_MIN_W = 116.0f;
        constexpr float TILE_GAP = 10.0f;
        constexpr float TILE_RATIO = 1.4f;
        const char* const APRIL_PREFIX = "april:";
        const char* const STAGE_PREFIX = "stage:";
        const char* const SNAPSHOT_GROUP = "snapshot";

        struct Major {
            std::string key;
            std::string label;
            std::vector<const blb::meta::manifest_version*> versions;
        };

        std::vector<std::string> familyVersions;

        std::string majorLabel(const std::string& key) {
            for (const char* prefix : {APRIL_PREFIX, STAGE_PREFIX}) {
                if (key.rfind(prefix, 0) == 0) {
                    return key.substr(std::char_traits<char>::length(prefix));
                }
            }
            if (key == SNAPSHOT_GROUP) {
                return SNAPSHOT;
            }
            return key.find('.') == std::string::npos ? key + ".x" : key;
        }

        const std::vector<std::string>& coverKeys(const Catalog::Versions& all) {
            static std::vector<std::string> cached;
            static std::string signature;
            std::string now = std::to_string((uintptr_t) all.list.data()) + "#" + std::to_string(all.list.size());
            if (now == signature) {
                return cached;
            }
            signature = now;
            cached.clear();
            std::string upcoming;
            for (const blb::meta::manifest_version& version : all.list) {
                std::string key = Covers::keyOf(version, upcoming);
                if (std::find(cached.begin(), cached.end(), key) == cached.end()) {
                    cached.push_back(key);
                }
            }
            return cached;
        }

        const std::vector<Major>& majors(const Catalog::Versions& all, const std::string& query) {
            static std::vector<Major> cached;
            static std::string signature;
            std::string now =
                    std::to_string((uintptr_t) all.list.data()) + "#" + std::to_string(all.list.size()) + "#" + query;
            for (const std::string& kind : kinds) {
                now += "#" + kind;
            }
            if (now == signature) {
                return cached;
            }
            signature = now;
            cached.clear();
            std::map<std::string, size_t> index;
            std::string upcoming;
            for (const blb::meta::manifest_version& version : all.list) {
                std::string key = Covers::keyOf(version, upcoming);
                if (std::find(kinds.begin(), kinds.end(), kindOf(version)) == kinds.end() ||
                    !blb::icontains(version.id, query)) {
                    continue;
                }
                auto found = index.find(key);
                if (found == index.end()) {
                    found = index.emplace(key, cached.size()).first;
                    cached.push_back(Major{key, majorLabel(key), {}});
                }
                cached[found->second].versions.push_back(&version);
            }
            return cached;
        }

        int tileColumns(float w) {
            return std::max(2, (int) ((w + TILE_GAP) / (TILE_MIN_W + TILE_GAP)));
        }

        float tileWidth(float w, int columns) {
            return (w - TILE_GAP * (float) (columns - 1)) / (float) columns;
        }

        float tileHeight(float tileW) {
            return tileW * TILE_RATIO;
        }

        uint32_t placeholderTint(const std::string& key, int alpha) {
            static const uint32_t palette[] = {0x5C8F4Fu, 0x4F7A99u, 0x9A6B47u, 0x7D5C9Bu,
                                               0x4E8C86u, 0x9B5361u, 0x8A8442u, 0x5466A0u};
            size_t hash = std::hash<std::string>{}(key);
            uint32_t rgb = palette[hash % (sizeof(palette) / sizeof(palette[0]))];
            return UiRender::color((int) ((rgb >> 16) & 0xFF), (int) ((rgb >> 8) & 0xFF), (int) (rgb & 0xFF), alpha);
        }

        bool majorTile(const Major& major, float x, float y, float w, float h, bool blocked) {
            std::string id = "new#major#" + major.key;
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            float left = x;
            float top = y;
            float width = w;
            float height = h;
            std::shared_ptr<const Pixels> cover = Covers::get(major.key);
            bool captioned = cover == nullptr || major.key.rfind(APRIL_PREFIX, 0) != 0;
            if (cover != nullptr) {
                float pixel = UiRender::pixelScale();
                float per = UiRender::transformW(1.0f) * pixel;
                float screenX = UiRender::transformX(x) * pixel;
                float screenY = UiRender::transformY(y) * pixel;
                left = x + (std::round(screenX) - screenX) / per;
                top = y + (std::round(screenY) - screenY) / per;
                if (std::fabs((float) cover->height - h * per) < 1.5f) {
                    width = (float) cover->width / per;
                    height = (float) cover->height / per;
                }
                UiRender::roundTexture(Textures::of(cover), left, top, width, height, CARD_RADIUS, 0.0f, 0u,
                                       (float) cover->width, (float) cover->height);
            } else {
                UiRender::roundRectGradientV(x, y, w, h, CARD_RADIUS, placeholderTint(major.key, 255),
                                             UiRender::mix(placeholderTint(major.key, 255), 0xFF000000u, 0.45f));
            }
            if (captioned) {
                UiRender::roundRectGradientV(left, top + height * 0.45f, width, height * 0.55f, CARD_RADIUS,
                                             0x00000000u, 0xB3000000u);
            }
            if (hover > 0.001f) {
                UiRender::roundRect(left, top, width, height, CARD_RADIUS,
                                    UiRender::color(255, 255, 255, Ui::round(24 * hover)));
            }
            if (captioned) {
                std::string count = std::to_string(major.versions.size()) + " 个版本";
                float countY = top + height - 10.0f - 16.0f;
                float labelY = countY - 22.0f;
                std::string label = UiFont::fitEnd(major.label, UiFont::MEDIUM, 17.0f, width - 21.0f);
                for (float shift : {0.0f, 0.7f}) {
                    UiRender::textY(label, left + 10.0f + shift, labelY, 22.0f, 0xFFFFFFFFu, UiFont::MEDIUM, 17.0f);
                }
                UiRender::textY(count, left + 10.0f, countY, 16.0f, 0xB3FFFFFFu, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (!blocked && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::PAGE);
                return true;
            }
            return false;
        }

        void chooseMajor(const Catalog::Versions& all, const Major& major) {
            std::string key = major.key;
            std::string preferred;
            for (const blb::meta::manifest_version* version : major.versions) {
                if (Catalog::kindOf(*version) == Catalog::Kind::Release) {
                    preferred = version->id;
                    break;
                }
            }
            if (preferred.empty() && !major.versions.empty()) {
                preferred = major.versions.front()->id;
            }
            familyVersions.clear();
            for (const Major& group : majors(all, std::string())) {
                if (group.key == key) {
                    for (const blb::meta::manifest_version* version : group.versions) {
                        familyVersions.push_back(version->id);
                    }
                }
            }
            if (!preferred.empty()) {
                chooseVersion(preferred);
            }
        }

        void switchMinecraft(const std::string& id) {
            if (id == minecraft) {
                return;
            }
            loaderSelection.reset(id);
            autoFabricApi = false;
            automaticFabricApi.clear();
            wanting.clear();
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
                        Platform::pickFiles(Platform::userDirectory(), L"导入整合包", L"整合包", L"*.zip;*.mrpack");
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
            float x = Page::left();
            float w = Page::width();
            int columns = tileColumns(w);
            float tileW = tileWidth(w, columns);
            float tileH = tileHeight(tileW);
            if (!Covers::ready(coverKeys(all),
                               (int) std::lround(tileH * UiRender::stableScale() * UiRender::pixelScale()))) {
                message("正在加载版本封面…", UiTheme::textMuted(), true);
                return;
            }
            const std::vector<Major>& groups = majors(all, query);
            if (groups.empty()) {
                message(query.empty() ? std::string("没有符合筛选的版本") : "没有找到“" + query + "”",
                        UiTheme::textMuted());
                return;
            }
            int rows = ((int) groups.size() + columns - 1) / columns;
            float contentH = 6.0f + rows * tileH + (rows - 1) * TILE_GAP + 8.0f;
            Page::Scroll body("new#versions", Page::TOP + 40.0f, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool listBlocked = outside || blocked;
            float y0 = body.top - body.offset + 6.0f;
            const Major* picked = nullptr;
            for (size_t i = 0; i < groups.size(); i++) {
                float tileX = x + (float) (i % (size_t) columns) * (tileW + TILE_GAP);
                float tileY = y0 + (float) (i / (size_t) columns) * (tileH + TILE_GAP);
                if (tileY + tileH < body.top - 8.0f || tileY > body.bottom + 8.0f) {
                    continue;
                }
                if (majorTile(groups[i], tileX, tileY, tileW, tileH, listBlocked)) {
                    picked = &groups[i];
                }
            }
            body.end();
            if (picked != nullptr) {
                chooseMajor(all, *picked);
            }
        }

        void installButton(float right, float bottom, const std::string& problem, bool waiting) {
            const char* action = editing.empty() ? "安装" : "应用";
            bool usable = problem.empty() && !waiting;
            float x = right - BUTTON_W;
            float y = bottom - BUTTON_H;
            bool hovered = !NewUi::overlayCoversMouse() && NewUi::hit(x, y, BUTTON_W, BUTTON_H);
            float hover = UiState::motion("new#install#hover", usable && hovered ? 1.0f : 0.0f, 18.0f);
            float enabled = UiState::motion("new#install#enabled", usable ? 1.0f : 0.0f, 14.0f);
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * UiRender::lerp(0.45f, 1.0f, enabled);
            UiRender::shadow(x, y, BUTTON_W, BUTTON_H, 10.0f, 12.0f, 4.0f, UiTheme::shadow());
            UiRender::roundRect(x, y, BUTTON_W, BUTTON_H, 10.0f,
                                UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.10f * hover));
            float labelW = UiFont::width(action, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            UiRender::textY(action, x + (BUTTON_W - labelW) * 0.5f, y, BUTTON_H, UiTheme::onAccent(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
            UiRender::alphaMul = savedAlpha;

            if (!problem.empty()) {
                float problemW = UiFont::width(problem, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(problem, x - 14.0f - problemW, y, BUTTON_H, UiTheme::danger(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else if (waiting) {
                std::string text = fabricApiRequested ? "正在检查 Fabric API 兼容性…" : "正在获取 Fabric API 版本…";
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(text, x - 14.0f - textW, y, BUTTON_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (!usable || !NewUi::click(x, y, BUTTON_W, BUTTON_H)) {
                return;
            }
            UiSound::request(UiSound::PULSE);
            if (!editing.empty()) {
                Installer::startModify(editing, loaderSelection.requests());
            } else if (const Instances::Source* source = targetSource()) {
                if (modpack) {
                    Installer::startModpack(source->key, modpack->pack, instanceName());
                } else {
                    Installer::start(source->key, minecraft, loaderSelection.requests(), typedName);
                }
            }
            if (Installer::running()) {
                reset();
                MainWindow::showTransfers();
            }
        }

        enum class RowAction { None, Open, Toggle };

        struct RowView {
            bool checked = false;
            bool busy = false;
            bool checkable = false;
            bool openable = false;
            bool dim = false;
            std::string status;
            uint32_t color = 0;
        };

        void checkbox(float x, float cy, bool on, bool busy, float hover) {
            float y = cy - 9.0f;
            if (busy) {
                UiRender::outline(x, y, 18.0f, 18.0f, 5.0f, UiTheme::outline(), 0xF, 1.4f);
                Page::spinner(x + 9.0f, cy, 5.0f, UiTheme::accent());
            } else if (on) {
                UiRender::roundRect(x, y, 18.0f, 18.0f, 5.0f,
                                    UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.12f * hover));
                UiWidgets::icon(Ui::glyph(0xf00c), x + 9.0f, cy, UiTheme::onAccent(), 10.0f);
            } else {
                UiRender::outline(x, y, 18.0f, 18.0f, 5.0f,
                                  UiRender::mix(UiTheme::outline(), UiTheme::textMuted(), hover), 0xF, 1.4f);
            }
        }

        RowAction loaderRow(const std::string& id, uint32_t image, const std::string& label, const RowView& view,
                            float rowY, bool blocked, int corners) {
            constexpr float BOX_AREA = 44.0f;
            float left = Page::left();
            float cy = rowY + Page::ROW_H * 0.5f;
            bool overBox = view.checkable && !blocked && NewUi::hit(left, rowY, BOX_AREA, Page::ROW_H);
            bool overRow = view.openable && !blocked && !overBox && NewUi::hit(left, rowY, Page::width(), Page::ROW_H);
            rowHover(id, left, rowY, Page::width(), Page::ROW_H, overBox || overRow, corners);
            float hover = UiState::motion(id + "#box", overBox ? 1.0f : 0.0f, 18.0f);
            float savedAlpha = UiRender::alphaMul;
            if (view.dim) {
                UiRender::alphaMul = savedAlpha * 0.5f;
            }
            checkbox(left + 16.0f, cy, view.checked, view.busy, hover);
            if (image != 0) {
                UiRender::texture(image, left + 46.0f, cy - 8.0f, 16.0f, 16.0f);
            } else {
                UiWidgets::icon(Ui::glyph(0xf1b2), left + 54.0f, cy, UiTheme::textMuted(), 12.0f);
            }
            UiRender::textY(label, left + 72.0f, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float markX = Page::controlRight() - 10.0f;
            float statusRight = view.openable ? markX - 18.0f : markX + 6.0f;
            float statusW = UiFont::width(view.status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(view.status, statusRight - statusW, rowY, Page::ROW_H, view.color, UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (view.openable) {
                UiWidgets::iconBeside(Ui::glyph(0xf054), markX, view.status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                      rowY, Page::ROW_H, UiTheme::textFaint(), 9.0f);
            }
            UiRender::alphaMul = savedAlpha;
            if (overBox) {
                UiCursor::hand();
                if (NewUi::click(left, rowY, BOX_AREA, Page::ROW_H)) {
                    UiSound::request(UiSound::TICK);
                    return RowAction::Toggle;
                }
            }
            if (overRow && NewUi::click(left, rowY, Page::width(), Page::ROW_H)) {
                UiSound::request(UiSound::PAGE);
                return RowAction::Open;
            }
            return RowAction::None;
        }

        const blb::install::loader_version* newest(const Catalog::LoaderVersions& versions) {
            for (const blb::install::loader_version& version : versions.list) {
                if (version.stable) {
                    return &version;
                }
            }
            return versions.list.empty() ? nullptr : &versions.list.front();
        }

        void choose(loader_type type, const std::string& version) {
            loaderSelection.select(type, version);
            autoFabricApi = editing.empty() && type == loader_type::fabric;
            fabricApiRequested = false;
            if (type == loader_type::fabric_api) {
                automaticFabricApi.clear();
            }
        }

        void drop(loader_type type) {
            loaderSelection.remove(type);
            if (type == loader_type::fabric_api || chosen.count(loader_type::fabric) == 0) {
                autoFabricApi = false;
                automaticFabricApi.clear();
            }
        }

        void settleWanted() {
            for (auto it = wanting.begin(); it != wanting.end();) {
                const Catalog::LoaderVersions& versions = Catalog::loader(*it, minecraft);
                if (versions.status == Catalog::Status::Loading) {
                    ++it;
                    continue;
                }
                loader_type type = *it;
                it = wanting.erase(it);
                std::string label = Installer::loaderName(type);
                if (const blb::install::loader_version* latest = newest(versions)) {
                    choose(type, latest->version);
                } else if (versions.status == Catalog::Status::Failed) {
                    Notices::add(Notices::Kind::Failed, "获取 " + label + " 版本失败：" + versions.error, 5000);
                } else {
                    Notices::add(Notices::Kind::Info, label + " 没有适用于 " + minecraft + " 的版本", 4000);
                }
            }
        }

        void toggle(const blb::instance::loader_choice& choice) {
            loader_type type = choice.type;
            if (chosen.count(type) != 0) {
                drop(type);
                return;
            }
            if (wanting.erase(type) != 0) {
                return;
            }
            for (int guard = 0; guard < 8; guard++) {
                std::vector<blb::instance::loader_choice> now = loaderSelection.choices();
                auto found =
                        std::find_if(now.begin(), now.end(), [type](const auto& item) { return item.type == type; });
                if (found == now.end() || found->state != blb::instance::choice_state::incompatible) {
                    break;
                }
                auto other = blb::install::parse_loader_type(found->related);
                if (!other || chosen.count(*other) == 0) {
                    break;
                }
                drop(*other);
            }
            const Catalog::LoaderVersions& versions = Catalog::loader(type, minecraft);
            if (versions.status == Catalog::Status::Failed) {
                Catalog::refreshLoader(type, minecraft);
            }
            const blb::install::loader_version* latest = newest(versions);
            if (versions.status == Catalog::Status::Ready && latest != nullptr) {
                choose(type, latest->version);
            } else {
                wanting.insert(type);
            }
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
            bool blocked = NewUi::overlayCoversMouse();
            bool localPack = modpack && !modpack->pack.file.empty();
            const char* backHint = !editing.empty() ? "返回实例设置"
                    : localPack                     ? "返回"
                    : modpack                       ? "返回下载附加件"
                                                    : "重新选择版本";
            if (Page::iconButton("new#back", 0xf053, backHint, Page::left() - 4.0f, Page::TOP - 2.0f, blocked)) {
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
            Page::title(!editing.empty()  ? std::string("修改加载器")
                                : modpack ? std::string("安装整合包")
                                          : "新建 " + minecraft + " 实例",
                        Page::left() + 30.0f, Page::TOP);
            if (!editing.empty()) {
                if (const Instances::Entry* entry = Instances::find(editing)) {
                    float nameX = Page::left() + 30.0f + UiFont::width("修改加载器", UiFont::MEDIUM, Page::TITLE_SIZE) +
                            10.0f;
                    UiRender::textY(UiFont::fitEnd(entry->info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                   Page::right() - nameX),
                                    nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                }
            }

            float buttonBottom = MainWindow::height() - BUTTON_MARGIN;
            float bodyTop = Page::TOP + 54.0f;
            float bodyBottom = buttonBottom - BUTTON_H - 12.0f;
            settings(bodyTop, bodyBottom, 0.0f, blocked || readingLoaders || !loaderReadError.empty());

            std::string problem;
            if (Installer::running()) {
                problem = "请等待当前安装完成";
            } else if (!editing.empty()) {
                const Instances::Entry* entry = Instances::find(editing);
                problem = readingLoaders                          ? std::string("正在读取已安装的加载器…")
                        : !loaderReadError.empty()                ? loaderReadError
                        : entry == nullptr                        ? std::string("实例已不存在")
                        : chosen == original && entry->info.valid ? std::string("没有改动")
                                                                  : std::string();
            } else {
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
            settleWanted();
            std::vector<blb::instance::loader_choice> choices = modpack
                    ? std::vector<blb::instance::loader_choice>()
                    : loaderSelection.choices();
            int loaderRows = modpack ? 4 : (int) choices.size();
            bool edit = !editing.empty();
            bool choosePlace = !edit && Instances::soleSource() == nullptr;
            bool versionRow = !modpack;
            int instanceRows = (edit ? 0 : 1) + (choosePlace ? 1 : 0) + (versionRow ? 1 : 0);
            float contentH = Page::CAPTION_H + instanceRows * Page::ROW_H + Page::GAP +
                    (loaderRows > 0 ? Page::CAPTION_H + loaderRows * Page::ROW_H : 0.0f);
            float savedAlpha = UiRender::alphaMul;
            float savedOffsetY = UiRender::offsetY;
            UiRender::alphaMul = savedAlpha * (1.0f - gone);
            UiRender::offsetY = savedOffsetY - gone * 24.0f * UiRender::uiScale;
            Page::Scroll body("new#setup", top, top + (bottom - top) * (1.0f - gone), contentH);
            bool outside = body.begin();
            bool rowsBlocked = outside || blocked;
            float y = body.top - body.offset;

            Page::caption("实例", y);
            y += Page::CAPTION_H;
            Page::card(y, instanceRows);
            if (!edit) {
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

                if (choosePlace) {
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
                    y += Page::ROW_H;
                }
            }
            if (versionRow) {
                Page::label("游戏版本", y);
                if (!edit && familyVersions.size() > 1) {
                    auto version = std::make_shared<ModeBinding>(
                            "new#minecraft", [] { return minecraft; },
                            [](const std::string& id) { switchMinecraft(id); }, [] { return familyVersions; });
                    SelectWidget select("new#minecraft", version);
                    select.bounds(Page::controlRight() - 180.0f, y + Page::FIELD_Y, 180.0f, FIELD_H).block(rowsBlocked);
                    select.paint();
                } else {
                    float versionW = UiFont::width(minecraft, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    UiRender::textY(minecraft, Page::controlRight() - versionW, y, Page::ROW_H,
                                    UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                }
                y += Page::ROW_H;
            }
            y += Page::GAP;

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

            if (loaderRows > 0) {
                Page::caption("加载器", y);
                y += Page::CAPTION_H;
                Page::card(y, loaderRows);
            }
            int rowIndex = 0;
            for (const blb::instance::loader_choice& choice : choices) {
                loader_type type = choice.type;
                RowView view;
                view.checked = chosen.count(type) != 0;
                view.busy = wanting.count(type) != 0;
                view.color = UiTheme::textMuted();
                bool needs = choice.state == blb::instance::choice_state::requires_loader;
                const Catalog::LoaderVersions* versions = needs ? nullptr : &Catalog::loader(type, minecraft);
                const blb::install::loader_version* latest = versions != nullptr ? newest(*versions) : nullptr;
                if (view.checked) {
                    view.status = chosen.at(type);
                    view.color = UiTheme::textSecondary();
                    view.checkable = true;
                    view.openable = true;
                } else if (needs) {
                    view.status = "需要先选择 " + loaderLabelOf(choice.related);
                    view.dim = true;
                } else if (versions->status == Catalog::Status::Ready && latest == nullptr) {
                    view.status = "不支持该版本";
                    view.dim = true;
                } else if (choice.state == blb::instance::choice_state::incompatible) {
                    view.status = "与 " + loaderLabelOf(choice.related) + " 不兼容";
                    view.checkable = true;
                } else {
                    view.status = view.busy                               ? std::string("正在获取版本…")
                            : versions->status == Catalog::Status::Failed ? std::string("获取版本失败")
                            : latest != nullptr                           ? "最新 " + latest->version
                                                                          : std::string();
                    view.color = UiTheme::textFaint();
                    view.checkable = true;
                    view.openable = true;
                }
                int index = rowIndex++;
                RowAction action = Page::shown(y) ? loaderRow("new#row#" + std::to_string((int) type),
                                                              Icons::loader(type), Installer::loaderName(type), view, y,
                                                              rowsBlocked, Page::rowCorners(index, loaderRows))
                                                  : RowAction::None;
                if (action == RowAction::Open) {
                    picking = type;
                    steps.go(STEP_LOADER);
                } else if (action == RowAction::Toggle) {
                    toggle(choice);
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
                wanting.erase(picking);
                choose(picking, versions.list[clicked].version);
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
        familyVersions.clear();
        wanting.clear();
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
        for (auto it = UiState::motionMap.begin(); it != UiState::motionMap.end();) {
            const std::string& id = it->first;
            if (id.rfind("new#install#", 0) == 0) {
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
