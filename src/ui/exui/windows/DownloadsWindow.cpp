#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/DownloadsWindow.h"
#include "app/downloads/Addons.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Instances.h"
#include "app/system/Catalog.h"
#include "app/system/Config.h"
#include "app/system/Pixels.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/ScrollBar.h"
#include "ui/exui/windows/Dialogs.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ForgottenMelody {

    namespace {
        using Addons::Kind;
        using Addons::Source;
        using exui::ControlKind;

        enum : uint32_t {
            NEW_PANEL = SectionedForm::FIRST_CONTROL,
            NEW_LATER,
            MOD_PANEL,
            MODPACK_PANEL,
            RESOURCE_PACK_PANEL,
            SHADER_PANEL,
            DATAPACK_PANEL,
            // The search, the same for every kind, and a project's page in its place.
            BROWSE,
            SEARCH_VIEW,
            PROJECT_VIEW,
            SOURCE,
            SEARCH,
            INSTANCE,
            VERSION,
            LOADER,
            CATEGORY,
            SORT,
            COUNT,
            RESULTS,
            RETRY,
            PREVIOUS,
            PAGE,
            NEXT,
            BACK,
            HEADER,
            WEBSITE,
            PROJECT_INSTANCE,
            PROJECT_VERSION,
            PROJECT_LOADER,
            VERSIONS,
            VERSIONS_RETRY,
            STATUS,
            ACTION,
        };

        struct KindInfo {
            Kind kind;
            const wchar_t* label;
            uint32_t panel;
        };
        // In the order of the default interface, after 新建实例.
        const KindInfo KINDS[] = {
                {Kind::Mod, L"模组", MOD_PANEL},
                {Kind::Modpack, L"整合包", MODPACK_PANEL},
                {Kind::ResourcePack, L"资源包", RESOURCE_PACK_PANEL},
                {Kind::Shader, L"光影包", SHADER_PANEL},
                {Kind::Datapack, L"数据包", DATAPACK_PANEL},
        };

        struct SortOption {
            Addons::Sort sort;
            const wchar_t* label;
        };
        const std::vector<SortOption> MODRINTH_SORTS = {
                {Addons::Sort::Relevance, L"相关度"}, {Addons::Sort::Downloads, L"下载量"},
                {Addons::Sort::Follows, L"关注数"},   {Addons::Sort::Newest, L"最新发布"},
                {Addons::Sort::Updated, L"最近更新"},
        };
        const std::vector<SortOption> CURSE_SORTS = {
                {Addons::Sort::Popularity, L"热门"},  {Addons::Sort::Downloads, L"下载量"},
                {Addons::Sort::Updated, L"最近更新"}, {Addons::Sort::Newest, L"最新发布"},
                {Addons::Sort::Name, L"名称"},
        };

        // LiveForm's refresh timer is 1.
        constexpr UINT_PTR SEARCH_TIMER = 2;
        // The search waits for a pause in the typing, as in the default interface.
        constexpr UINT TYPING_PAUSE_MS = 400;
        // The section shown last, for the rest of the run; 模组 at first.
        int lastSection = 1;

        std::unique_ptr<Gdiplus::Bitmap> bitmapOf(const Pixels& pixels) {
            if (pixels.width <= 0 || pixels.height <= 0 ||
                pixels.rgba.size() < (size_t) pixels.width * pixels.height * 4) {
                return nullptr;
            }
            auto bitmap = std::make_unique<Gdiplus::Bitmap>(pixels.width, pixels.height, PixelFormat32bppARGB);
            Gdiplus::Rect all(0, 0, pixels.width, pixels.height);
            Gdiplus::BitmapData data;
            if (bitmap->LockBits(&all, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
                return nullptr;
            }
            for (int y = 0; y < pixels.height; y++) {
                auto* row = (uint8_t*) data.Scan0 + (ptrdiff_t) y * data.Stride;
                for (int x = 0; x < pixels.width; x++) {
                    const unsigned char* p = &pixels.rgba[((size_t) y * pixels.width + x) * 4];
                    row[x * 4] = p[2];
                    row[x * 4 + 1] = p[1];
                    row[x * 4 + 2] = p[0];
                    row[x * 4 + 3] = p[3];
                }
            }
            bitmap->UnlockBits(&data);
            return bitmap;
        }

        // The results of a search, a project a row in the manner of the default interface: its icon, its title with
        // the author and the tags, the description below and the downloads at the right.
        class ProjectList : public exui::Widget {
        public:
            struct Row {
                std::wstring title, byline, description, downloads;
                std::string icon;
            };

            std::function<void(int)> OnOpen;

            ProjectList(int x, int y, int width, int height) {
                SetBounds(x, y, width, height);
                SetTabStop(true);
                bar = &Add<exui::ScrollBar>();
                bar->SetVisible(false);
                bar->SetLineStep(ROW_H / 2);
                bar->OnScroll = [this](int position) {
                    scroll = position;
                    Invalidate();
                };
            }

            void setRows(std::vector<Row> next) {
                rows = std::move(next);
                scroll = 0;
                hovered = pressed = -1;
                layout();
                Invalidate();
            }

            // What the list says while it has no rows.
            void setMessage(const std::wstring& text) {
                if (text != message) {
                    message = text;
                    Invalidate();
                }
            }

            // Rows that the next search will replace look faded.
            void setStale(bool value) {
                if (value != stale) {
                    stale = value;
                    Invalidate();
                }
            }

            // Takes in the icons that arrived since the last call.
            void updateIcons() {
                bool arrived = false;
                for (const Row& row : rows) {
                    if (row.icon.empty()) {
                        continue;
                    }
                    auto& icon = icons[row.icon];
                    if (icon == nullptr) {
                        if (std::shared_ptr<const Pixels> pixels = Addons::icon(row.icon)) {
                            icon = bitmapOf(*pixels);
                            arrived = arrived || icon != nullptr;
                        }
                    }
                }
                if (arrived) {
                    Invalidate();
                }
            }

            void OnPaint(Gdiplus::Graphics& g) override {
                using namespace Gdiplus;
                layout();
                SolidBrush backing(Color(0xD2FFFFFF));
                g.FillRectangle(&backing, 0, 0, GetWidth(), GetHeight());
                Pen border(Color(0x50000000));
                g.DrawRectangle(&border, 0, 0, GetWidth() - 1, GetHeight() - 1);
                const Rect view(INSET, INSET, GetWidth() - 2 * INSET - (bar->IsVisible() ? BAR_W : 0),
                                GetHeight() - 2 * INSET);
                g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                // 微软雅黑 rather than the 宋体 of the window: the descriptions are mostly English.
                FontFamily family(L"微软雅黑");
                Font title(&family, 13, FontStyleBold, UnitPixel);
                Font text(&family, 12, FontStyleRegular, UnitPixel);
                Font* body = &text;
                StringFormat line;
                line.SetFormatFlags(StringFormatFlagsNoWrap);
                line.SetTrimming(StringTrimmingEllipsisCharacter);
                line.SetLineAlignment(StringAlignmentCenter);
                StringFormat right;
                right.SetFormatFlags(StringFormatFlagsNoWrap);
                right.SetLineAlignment(StringAlignmentCenter);
                right.SetAlignment(StringAlignmentFar);
                const auto saved = g.Save();
                g.SetClip(view, CombineModeIntersect);
                if (rows.empty()) {
                    StringFormat centered;
                    centered.SetAlignment(StringAlignmentCenter);
                    centered.SetLineAlignment(StringAlignmentCenter);
                    SolidBrush muted(Color(0x96000000));
                    g.DrawString(message.c_str(), -1, body,
                                 RectF((REAL) view.X, (REAL) view.Y, (REAL) view.Width, (REAL) view.Height), &centered,
                                 &muted);
                    g.Restore(saved);
                    return;
                }
                const BYTE fade = stale ? 0x60 : 0xD2;
                SolidBrush ink(Color(fade, 0, 0, 0));
                SolidBrush muted(Color(stale ? 0x40 : 0x96, 0, 0, 0));
                SolidBrush hover(Color(0x18000000));
                SolidBrush placeholder(Color(0x18000000));
                Pen divider(Color(0x14000000));
                int first = scroll / ROW_H;
                for (int i = first; i < (int) rows.size(); i++) {
                    const Row& row = rows[i];
                    int y = view.Y + i * ROW_H - scroll;
                    if (y >= view.GetBottom()) {
                        break;
                    }
                    if (i == hovered && !stale) {
                        g.FillRectangle(&hover, view.X, y, view.Width, ROW_H);
                    }
                    if (i > 0) {
                        g.DrawLine(&divider, view.X + 8, y, view.GetRight() - 8, y);
                    }
                    RectF icon((REAL) view.X + 8, (REAL) y + 6, ICON, ICON);
                    auto found = icons.find(row.icon);
                    if (found != icons.end() && found->second != nullptr) {
                        ImageAttributes attributes;
                        ColorMatrix matrix = {{{1, 0, 0, 0, 0},
                                               {0, 1, 0, 0, 0},
                                               {0, 0, 1, 0, 0},
                                               {0, 0, 0, stale ? 0.4f : 1.0f, 0},
                                               {0, 0, 0, 0, 1}}};
                        attributes.SetColorMatrix(&matrix);
                        g.DrawImage(found->second.get(), icon, 0, 0, (REAL) found->second->GetWidth(),
                                    (REAL) found->second->GetHeight(), UnitPixel, &attributes);
                    } else {
                        g.FillRectangle(&placeholder, icon);
                    }
                    REAL textX = (REAL) view.X + 8 + ICON + 10;
                    REAL rightEdge = (REAL) view.GetRight() - 10;
                    RectF downloads(rightEdge - 90, (REAL) y + 5, 90, 20);
                    g.DrawString(row.downloads.c_str(), -1, body, downloads, &right, &muted);
                    RectF heading(textX, (REAL) y + 5, rightEdge - 96 - textX, 20);
                    RectF titleSize;
                    g.MeasureString(row.title.c_str(), -1, &title, heading, &line, &titleSize);
                    g.DrawString(row.title.c_str(), -1, &title, heading, &line, &ink);
                    RectF byline(textX + titleSize.Width + 6, (REAL) y + 5,
                                 std::max<REAL>(0, heading.GetRight() - textX - titleSize.Width - 6), 20);
                    g.DrawString(row.byline.c_str(), -1, body, byline, &line, &muted);
                    RectF description(textX, (REAL) y + 28, rightEdge - textX, 18);
                    g.DrawString(row.description.c_str(), -1, body, description, &line, &muted);
                }
                g.Restore(saved);
            }

            bool OnMouseMove(int, int y, UINT) override {
                int at = rowAt(y);
                if (at != hovered) {
                    hovered = at;
                    Invalidate();
                }
                return true;
            }

            void OnMouseLeave() override {
                hovered = -1;
                Invalidate();
                Widget::OnMouseLeave();
            }

            bool OnMouseDown(int, int y, UINT button) override {
                pressed = button == VK_LBUTTON && !stale ? rowAt(y) : -1;
                return true;
            }

            bool OnMouseUp(int, int y, UINT button) override {
                if (button == VK_LBUTTON && pressed >= 0 && rowAt(y) == pressed && OnOpen) {
                    OnOpen(pressed);
                }
                pressed = -1;
                return true;
            }

            bool OnMouseWheel(int delta, int, int) override {
                if (maxScroll() <= 0) {
                    return false;
                }
                scroll -= delta * ROW_H / WHEEL_DELTA;
                layout();
                Invalidate();
                return true;
            }

        private:
            static constexpr int ROW_H = 52, ICON = 40, INSET = 2, BAR_W = 10;

            exui::ScrollBar* bar = nullptr;
            std::vector<Row> rows;
            // The icons by address; empty until they arrive.
            std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> icons;
            std::wstring message;
            bool stale = false;
            int scroll = 0, hovered = -1, pressed = -1;

            int viewHeight() const {
                return std::max(0, GetHeight() - 2 * INSET);
            }

            int maxScroll() const {
                return std::max(0, (int) rows.size() * ROW_H - viewHeight());
            }

            void layout() {
                int most = maxScroll();
                scroll = std::clamp(scroll, 0, most);
                bar->SetVisible(most > 0);
                bar->SetBounds(GetWidth() - INSET - BAR_W, INSET, BAR_W, viewHeight());
                bar->SetRange(0, most, viewHeight());
                bar->SetPosition(scroll);
            }

            int rowAt(int y) const {
                if (y < INSET || y >= INSET + viewHeight()) {
                    return -1;
                }
                int at = (y - INSET + scroll) / ROW_H;
                return at < (int) rows.size() ? at : -1;
            }
        };

        // The top of a project's page: its icon, its title, the author and the downloads, and its description.
        class ProjectHeader : public exui::Widget {
        public:
            ProjectHeader(int x, int y, int width, int height) {
                SetBounds(x, y, width, height);
            }

            void show(const Addons::Hit& hit) {
                title = Platform::widen(hit.title);
                std::string line = hit.author.empty() ? std::string() : "by " + hit.author + "  ·  ";
                byline = Platform::widen(line + Addons::count(hit.downloads) + " 次下载");
                description = Platform::widen(hit.description);
                iconUrl = hit.iconUrl;
                icon.reset();
                Invalidate();
            }

            void updateIcon() {
                if (icon == nullptr && !iconUrl.empty()) {
                    if (std::shared_ptr<const Pixels> pixels = Addons::icon(iconUrl)) {
                        icon = bitmapOf(*pixels);
                        Invalidate();
                    }
                }
            }

            void OnPaint(Gdiplus::Graphics& g) override {
                using namespace Gdiplus;
                g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                RectF iconBox(0, 4, 40, 40);
                if (icon != nullptr) {
                    g.DrawImage(icon.get(), iconBox);
                } else {
                    SolidBrush placeholder(Color(0x18000000));
                    g.FillRectangle(&placeholder, iconBox);
                }
                FontFamily family(L"微软雅黑");
                Font big(&family, 16, FontStyleBold, UnitPixel);
                Font text(&family, 12, FontStyleRegular, UnitPixel);
                StringFormat line;
                line.SetFormatFlags(StringFormatFlagsNoWrap);
                line.SetTrimming(StringTrimmingEllipsisCharacter);
                line.SetLineAlignment(StringAlignmentCenter);
                SolidBrush ink(Color(0xD2000000)), muted(Color(0x96000000));
                REAL width = (REAL) GetWidth() - 50;
                g.DrawString(title.c_str(), -1, &big, RectF(50, 0, width, 22), &line, &ink);
                g.DrawString(byline.c_str(), -1, &text, RectF(50, 22, width, 16), &line, &muted);
                g.DrawString(description.c_str(), -1, &text, RectF(50, 38, width, 16), &line, &muted);
            }

        private:
            std::wstring title, byline, description;
            std::string iconUrl;
            std::unique_ptr<Gdiplus::Bitmap> icon;
        };

        const wchar_t* versionTypeLabel(const std::string& type) {
            return type == "beta" ? L"测试版" : type == "alpha" ? L"早期版" : L"正式版";
        }

        // The default interface's short names of versions: without what all of them start with, nor the title.
        bool separator(char c) {
            return c == ' ' || c == '-' || c == '_' || c == ':' || c == '|' || c == '/' || c == '(' || c == '[';
        }

        size_t sameStart(const std::string& text, const std::string& head) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; };
            size_t same = 0;
            while (same < head.size() && same < text.size() && lower(text[same]) == lower(head[same])) {
                same++;
            }
            return same;
        }

        std::string dropStart(const std::string& name, const std::string& head) {
            if (head.empty() || sameStart(name, head) != head.size()) {
                return name;
            }
            if (head.size() < name.size() && !separator(head.back()) && !separator(name[head.size()])) {
                return name;
            }
            size_t start = head.size();
            while (start < name.size() && separator(name[start])) {
                start++;
            }
            return name.substr(start);
        }

        std::string sharedPrefix(const std::vector<Addons::Version>& versions) {
            std::string prefix;
            if (versions.size() >= 2) {
                prefix = versions.front().name;
                for (const Addons::Version& version : versions) {
                    prefix.resize(sameStart(version.name, prefix));
                    if (prefix.empty()) {
                        break;
                    }
                }
            }
            size_t cut = prefix.size();
            while (cut > 0 && !separator(prefix[cut - 1])) {
                cut--;
            }
            prefix.resize(cut);
            return prefix;
        }

        std::string shortName(const Addons::Version& version, const std::string& prefix, const std::string& title) {
            std::string name = dropStart(dropStart(version.name, prefix), title);
            return !name.empty() ? name : version.number.empty() ? version.name : version.number;
        }

        // 下载中心 in the frame of the 2014 window, its sections those of the default interface: 新建实例, then a
        // search of Modrinth or CurseForge for each kind of thing, which can follow the instance it is for, and the
        // page of a project with its versions.
        class DownloadsWindow : public SectionedForm {
        public:
            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {NEW_LATER, ControlKind::Label, {210, 295, 675, 30}, L"新建实例还在开发中"},
                        // The search
                        {SOURCE, ControlKind::Combo, {225, 85, 110, 28}},
                        {SEARCH, ControlKind::Edit, {341, 86, 300, 26}},
                        {INSTANCE, ControlKind::Combo, {647, 85, 222, 28}},
                        {VERSION, ControlKind::Combo, {225, 119, 120, 28}},
                        {LOADER, ControlKind::Combo, {351, 119, 120, 28}},
                        {CATEGORY, ControlKind::Combo, {477, 119, 150, 28}},
                        {SORT, ControlKind::Combo, {633, 119, 110, 28}},
                        {COUNT, ControlKind::Label, {749, 120, 120, 26}},
                        {PREVIOUS, ControlKind::Button, {411, 509, 80, 28}, L"上一页"},
                        {PAGE, ControlKind::Label, {497, 510, 100, 26}},
                        {NEXT, ControlKind::Button, {603, 509, 80, 28}, L"下一页"},
                        {.id = RETRY,
                         .kind = ControlKind::Button,
                         .bounds = {507, 340, 80, 30},
                         .caption = L"重试",
                         .visible = false},
                        // A project
                        {BACK, ControlKind::Button, {225, 85, 60, 28}, L"返回"},
                        {WEBSITE, ControlKind::Hyperlink, {749, 88, 120, 22}},
                        {PROJECT_INSTANCE, ControlKind::Combo, {225, 142, 222, 28}},
                        {PROJECT_VERSION, ControlKind::Combo, {453, 142, 120, 28}},
                        {PROJECT_LOADER, ControlKind::Combo, {579, 142, 120, 28}},
                        {VERSIONS, ControlKind::List, {225, 176, 645, 323}},
                        {.id = VERSIONS_RETRY,
                         .kind = ControlKind::Button,
                         .bounds = {507, 350, 80, 30},
                         .caption = L"重试",
                         .visible = false},
                        {STATUS, ControlKind::Label, {225, 510, 540, 26}},
                        {ACTION, ControlKind::Button, {779, 509, 90, 28}, L"安装"},
                };
                std::vector<Section> sections = {{L"新建实例", NEW_PANEL}};
                for (const KindInfo& info : KINDS) {
                    sections.push_back({info.label, info.panel});
                }
                if (!createFrame(parent, L"下载中心", L"", sections)) {
                    return false;
                }
                AddControl(BROWSE, std::make_shared<exui::Panel>(210, 75, 675, 470));
                AddControl(SEARCH_VIEW, std::make_shared<exui::Panel>(210, 75, 675, 470));
                AddControl(PROJECT_VIEW, std::make_shared<exui::Panel>(210, 75, 675, 470));
                auto list = std::make_shared<ProjectList>(225, 153, 645, 350);
                results = list.get();
                results->OnOpen = [this](int index) { openProject(index); };
                AddControl(RESULTS, list);
                auto top = std::make_shared<ProjectHeader>(295, 80, 448, 56);
                header = top.get();
                AddControl(HEADER, top);
                AddControls(CONTROLS);
                MoveInto(NEW_PANEL, {NEW_LATER});
                Control<exui::Label>(NEW_LATER).SetAlignment(Gdiplus::StringAlignmentCenter,
                                                             Gdiplus::StringAlignmentCenter);
                MoveInto(SEARCH_VIEW,
                         {SOURCE, SEARCH, INSTANCE, VERSION, LOADER, CATEGORY, SORT, COUNT, RESULTS, RETRY, PREVIOUS,
                          PAGE, NEXT});
                MoveInto(PROJECT_VIEW,
                         {BACK, HEADER, WEBSITE, PROJECT_INSTANCE, PROJECT_VERSION, PROJECT_LOADER, VERSIONS,
                          VERSIONS_RETRY, STATUS, ACTION});
                MoveInto(BROWSE, {SEARCH_VIEW, PROJECT_VIEW});
                Control<exui::Label>(COUNT).SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
                Control<exui::Label>(PAGE).SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
                Control<exui::Hyperlink>(WEBSITE).SetAlignment(Gdiplus::StringAlignmentFar,
                                                               Gdiplus::StringAlignmentCenter);
                Control<exui::EditBox>(SEARCH).OnTextChanged = [this](const std::wstring&) {
                    SetTimer(GetHwnd(), SEARCH_TIMER, TYPING_PAUSE_MS, nullptr);
                };
                FindControl(SEARCH)->SetTooltipText(L"按名称搜索");
                for (uint32_t id : {INSTANCE, PROJECT_INSTANCE}) {
                    FindControl(id)->SetTooltipText(L"下载的东西装进这个实例；搜索也跟着它的游戏版本和加载器");
                }
                auto& versions = Control<exui::ListView>(VERSIONS);
                versions.InsertColumn(L"版本", 220);
                versions.InsertColumn(L"类型", 56);
                versions.InsertColumn(L"游戏版本", 100);
                versions.InsertColumn(L"加载器", 100);
                versions.InsertColumn(L"发布日期", 80);
                versions.InsertColumn(L"状态", 75);
                setOptions(SOURCE, {L"Modrinth", L"CurseForge"}, 0);

                const nlohmann::json& doc = Config::data();
                auto saved = doc.find("downloadInstance");
                target = saved != doc.end() && saved->is_string() ? saved->get<std::string>() : Instances::selected();
                showView(false);
                showSection(lastSection);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                followTarget();
                if (viewingProject) {
                    showProject();
                } else {
                    fillFilters();
                    showResults();
                }
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_TIMER && wp == SEARCH_TIMER) {
                    search();
                    return 0;
                }
                if (msg == WM_KEYDOWN && wp == VK_RETURN && GetFocusedWidget() == FindControl(SEARCH)) {
                    search();
                    return 0;
                }
                return SectionedForm::HandleMessage(msg, wp, lp);
            }

            void OnSelection(uint32_t id, int index) override {
                if (id < FIRST_CONTROL) {
                    SectionedForm::OnSelection(id, index);
                    return;
                }
                if (index < 0) {
                    return;
                }
                switch (id) {
                    case SOURCE: switchSource(index == 0 ? Source::Modrinth : Source::CurseForge); break;
                    case INSTANCE:
                    case PROJECT_INSTANCE:
                        chooseTarget(index > 0 && index <= (int) instanceKeys.size() ? instanceKeys[index - 1]
                                                                                     : std::string());
                        versionFilter = query.gameVersion;
                        loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
                        break;
                    case VERSION:
                        query.gameVersion = index < (int) versionChoices.size() ? versionChoices[index] : "";
                        query.page = 0;
                        break;
                    case PROJECT_VERSION:
                        versionFilter = index < (int) versionChoices.size() ? versionChoices[index] : "";
                        break;
                    case LOADER:
                        query.loaders.clear();
                        if (index > 0 && index <= (int) loaderTags().size()) {
                            query.loaders.push_back(loaderTags()[index - 1]);
                        }
                        query.page = 0;
                        break;
                    case PROJECT_LOADER:
                        loaderFilter = index > 0 && index <= (int) loaderTags().size() ? loaderTags()[index - 1] : "";
                        break;
                    case CATEGORY:
                        query.categories.clear();
                        if (index > 0 && index <= (int) categoryKeys.size()) {
                            query.categories.push_back(categoryKeys[index - 1]);
                        }
                        query.page = 0;
                        break;
                    case SORT:
                        if (index < (int) sorts().size()) {
                            query.sort = sorts()[index].sort;
                            query.page = 0;
                        }
                        break;
                }
                refresh();
            }

            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) {
                    SectionedForm::OnAction(id);
                    return;
                }
                switch (id) {
                    case PREVIOUS: query.page = std::max(0, query.page - 1); break;
                    case NEXT: query.page++; break;
                    case RETRY: Addons::refresh(source, query); break;
                    case BACK: showView(false); break;
                    case VERSIONS_RETRY: Addons::refreshVersions(source, project.id); break;
                    case ACTION: act(); break;
                }
                refresh();
            }

            void sectionShown(int index) override {
                lastSection = index;
                FindControl(BROWSE)->SetVisible(index > 0);
                if (index > 0) {
                    switchKind(KINDS[index - 1].kind);
                }
            }

        private:
            ProjectList* results = nullptr;
            ProjectHeader* header = nullptr;
            Source source = Source::Modrinth;
            Addons::Query query;
            // The instance things are downloaded for, and whether the search follows it yet.
            std::string target;
            bool followed = false;
            Addons::Results shown;
            // The project shown instead of the search, and the filters of its versions.
            bool viewingProject = false;
            Addons::Hit project;
            std::string versionFilter, loaderFilter;
            Rows shownVersions;
            std::vector<std::string> versionKeys;
            // What the combo boxes offer, behind their labels.
            std::vector<std::string> instanceKeys, versionChoices, categoryKeys;
            std::map<uint32_t, std::vector<std::wstring>> offered;

            static bool usesInstance(Kind kind) {
                return kind != Kind::Modpack;
            }

            bool hasLoaders() const {
                if (source == Source::CurseForge) {
                    return query.kind == Kind::Mod || query.kind == Kind::Modpack;
                }
                return query.kind == Kind::Mod || query.kind == Kind::Modpack || query.kind == Kind::Shader;
            }

            std::vector<std::string> loaderTags() const {
                if (source == Source::CurseForge) {
                    if (query.kind == Kind::Modpack) {
                        return {"forge", "fabric", "neoforge", "quilt"};
                    }
                    return {"forge", "fabric", "neoforge", "quilt", "liteloader"};
                }
                if (query.kind == Kind::Shader) {
                    return {"iris", "optifine", "canvas", "vanilla"};
                }
                if (query.kind == Kind::Modpack) {
                    return {"fabric", "forge", "neoforge", "quilt"};
                }
                return {"fabric", "forge", "neoforge", "quilt", "legacy-fabric", "liteloader"};
            }

            const std::vector<SortOption>& sorts() const {
                return source == Source::CurseForge ? CURSE_SORTS : MODRINTH_SORTS;
            }

            // Fills a combo box only when its choices change, so that an open one stays as it is.
            void setOptions(uint32_t id, const std::vector<std::wstring>& options, int selected) {
                auto& combo = Control<exui::ComboBox>(id);
                if (offered[id] != options) {
                    offered[id] = options;
                    combo.ClearItems();
                    for (const std::wstring& option : options) {
                        combo.AddItem(option);
                    }
                }
                if (combo.GetSelectedIndex() != selected) {
                    combo.SetSelectedIndex(selected);
                }
            }

            void setText(uint32_t id, const std::wstring& text) {
                if (Text(id) != text) {
                    SetText(id, text);
                }
            }

            // The search and a project take turns in the same place.
            void showView(bool projectView) {
                viewingProject = projectView;
                FindControl(SEARCH_VIEW)->SetVisible(!projectView);
                FindControl(PROJECT_VIEW)->SetVisible(projectView);
            }

            // Like the default interface: the search takes the game version of the instance, and for mods its loader.
            void followInstance() {
                const Instances::Entry* entry = Instances::find(target);
                if (entry == nullptr) {
                    return;
                }
                if (query.kind != Kind::Modpack) {
                    query.gameVersion = entry->info.minecraft_version;
                }
                if (query.kind == Kind::Mod) {
                    std::string loader = ProjectInstaller::loaderOf(target);
                    query.loaders.clear();
                    if (!loader.empty()) {
                        query.loaders.push_back(loader);
                    }
                }
                query.page = 0;
            }

            void followTarget() {
                if (!followed && Instances::find(target) != nullptr) {
                    followed = true;
                    followInstance();
                }
                if (!target.empty() && Instances::find(target) == nullptr && !Instances::loading()) {
                    chooseTarget(std::string());
                }
            }

            void chooseTarget(const std::string& key) {
                target = key;
                Config::data()["downloadInstance"] = target;
                Config::save();
                followInstance();
            }

            void switchKind(Kind kind) {
                showView(false);
                if (query.kind == kind) {
                    return;
                }
                query.kind = kind;
                query.categories.clear();
                query.loaders.clear();
                query.gameVersion.clear();
                query.client = false;
                query.server = false;
                shown = Addons::Results();
                results->setRows({});
                followInstance();
                refresh();
            }

            void switchSource(Source next) {
                if (next == source) {
                    return;
                }
                Kind kind = query.kind;
                source = next;
                query = Addons::Query();
                query.kind = kind;
                query.sort = next == Source::CurseForge ? Addons::Sort::Popularity : Addons::Sort::Relevance;
                SetText(SEARCH, L"");
                KillTimer(GetHwnd(), SEARCH_TIMER);
                shown = Addons::Results();
                results->setRows({});
                followInstance();
            }

            void search() {
                KillTimer(GetHwnd(), SEARCH_TIMER);
                std::string text = trimmed(Text(SEARCH));
                if (text != query.text) {
                    query.text = text;
                    query.page = 0;
                    refresh();
                }
            }

            // The instances after 不指定实例, the same two places alike.
            void fillInstances(uint32_t id) {
                std::vector<std::wstring> options = {L"不指定实例"};
                instanceKeys.clear();
                int selected = 0;
                const std::vector<Instances::Entry>& list = Instances::list();
                for (const Instances::Entry& entry : list) {
                    bool shared = std::count_if(list.begin(), list.end(), [&](const Instances::Entry& other) {
                                      return other.info.id == entry.info.id;
                                  }) > 1;
                    std::wstring label = Platform::widen(entry.info.id);
                    if (shared) {
                        const Instances::Source* where = Instances::source(entry.source);
                        label += L"（" + Platform::widen(where != nullptr ? where->label : entry.source) + L"）";
                    }
                    options.push_back(label);
                    instanceKeys.push_back(entry.key);
                    selected = entry.key == target ? (int) instanceKeys.size() : selected;
                }
                setOptions(id, options, selected);
            }

            // The released game versions after 全部版本, with chosen among them even when it is not one.
            void fillVersions(uint32_t id, const std::string& chosen) {
                versionChoices = {""};
                std::vector<std::wstring> options = {L"全部版本"};
                for (const auto& version : Catalog::minecraft().list) {
                    if (version.type == "release") {
                        versionChoices.push_back(version.id);
                    }
                }
                if (!chosen.empty() &&
                    std::find(versionChoices.begin(), versionChoices.end(), chosen) == versionChoices.end()) {
                    versionChoices.insert(versionChoices.begin() + 1, chosen);
                }
                for (size_t i = 1; i < versionChoices.size(); i++) {
                    options.push_back(Platform::widen(versionChoices[i]));
                }
                setOptions(id, options,
                           (int) (std::find(versionChoices.begin(), versionChoices.end(), chosen) -
                                  versionChoices.begin()));
            }

            void fillLoaders(uint32_t id, const std::string& chosen) {
                std::vector<std::wstring> options = {L"全部加载器"};
                int selected = 0;
                std::vector<std::string> tags = loaderTags();
                for (size_t i = 0; i < tags.size(); i++) {
                    options.push_back(Platform::widen(Modrinth::label(tags[i])));
                    selected = chosen == tags[i] ? (int) i + 1 : selected;
                }
                setOptions(id, options, selected);
            }

            // The filters of the kind and the source, laid out in a row without the ones that do not apply.
            void fillFilters() {
                bool instance = usesInstance(query.kind);
                FindControl(INSTANCE)->SetVisible(instance);
                FindControl(SEARCH)->SetBounds(131, 11, instance ? 300 : 528, 26);
                if (instance) {
                    fillInstances(INSTANCE);
                }
                fillVersions(VERSION, query.gameVersion);
                int x = 15;
                FindControl(VERSION)->SetBounds(x, 44, 120, 28);
                x += 126;
                bool loaders = hasLoaders();
                FindControl(LOADER)->SetVisible(loaders);
                if (loaders) {
                    fillLoaders(LOADER, query.loaders.size() == 1 ? query.loaders.front() : std::string());
                    FindControl(LOADER)->SetBounds(x, 44, 120, 28);
                    x += 126;
                }

                std::vector<std::wstring> options = {L"全部分类"};
                categoryKeys.clear();
                int selected = 0;
                for (const Addons::Category& category : Addons::categories(source, query.kind)) {
                    // Modrinth knows its categories by name, CurseForge by id.
                    std::string key = source == Source::CurseForge ? category.id : category.name;
                    categoryKeys.push_back(key);
                    options.push_back(Platform::widen(source == Source::Modrinth ? Modrinth::label(category.name)
                                                                                 : category.name));
                    if (query.categories.size() == 1 && query.categories.front() == key) {
                        selected = (int) categoryKeys.size();
                    }
                }
                setOptions(CATEGORY, options, selected);
                FindControl(CATEGORY)->SetBounds(x, 44, 150, 28);
                x += 156;

                options.clear();
                selected = 0;
                for (size_t i = 0; i < sorts().size(); i++) {
                    options.push_back(sorts()[i].label);
                    selected = sorts()[i].sort == query.sort ? (int) i : selected;
                }
                setOptions(SORT, options, selected);
                FindControl(SORT)->SetBounds(x, 44, 110, 28);
            }

            void showResults() {
                const Addons::Results& found = Addons::search(source, query);
                bool loading = found.status == Addons::Status::Loading;
                bool failed = found.status == Addons::Status::Failed;
                if (found.status == Addons::Status::Ready &&
                    (found.total != shown.total || found.hits.size() != shown.hits.size() ||
                     !std::equal(found.hits.begin(), found.hits.end(), shown.hits.begin(),
                                 [](const Addons::Hit& a, const Addons::Hit& b) { return a.id == b.id; }))) {
                    shown = found;
                    std::vector<ProjectList::Row> rows;
                    for (const Addons::Hit& hit : shown.hits) {
                        std::string byline = hit.author.empty() ? std::string() : "by " + hit.author;
                        for (const std::string& category : hit.categories) {
                            byline += "  · " + (source == Source::Modrinth ? Modrinth::label(category) : category);
                        }
                        rows.push_back({Platform::widen(hit.title), Platform::widen(byline),
                                        Platform::widen(hit.description),
                                        L"下载 " + Platform::widen(Addons::count(hit.downloads)), hit.iconUrl});
                    }
                    results->setRows(std::move(rows));
                }
                std::wstring sourceName = Platform::widen(Addons::sourceName(source));
                results->setMessage(failed ? L"搜索失败：" + Platform::widen(found.error)
                                            : loading ? L"正在搜索 " + sourceName + L"…"
                                                      : L"没有找到符合条件的内容");
                results->setStale(loading || failed);
                results->updateIcons();
                FindControl(RETRY)->SetVisible(failed && shown.hits.empty());

                bool counted = !shown.hits.empty() || found.status == Addons::Status::Ready;
                setText(COUNT, counted ? L"共 " + std::to_wstring(shown.total) + L" 个" : L"");
                int pages = std::max(1, (shown.total + Addons::PAGE_SIZE - 1) / Addons::PAGE_SIZE);
                setText(PAGE,
                        L"第 " + std::to_wstring(std::min(query.page, pages - 1) + 1) + L" / " +
                                std::to_wstring(pages) + L" 页");
                FindControl(PREVIOUS)->SetEnabled(!loading && query.page > 0);
                FindControl(NEXT)->SetEnabled(!loading && query.page + 1 < pages);
            }

            void openProject(int index) {
                if (index < 0 || index >= (int) shown.hits.size()) {
                    return;
                }
                project = shown.hits[index];
                versionFilter = query.gameVersion;
                loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
                header->show(project);
                auto& website = Control<exui::Hyperlink>(WEBSITE);
                website.SetTargetUrl(Platform::widen(project.pageUrl));
                SetText(WEBSITE, L"在 " + Platform::widen(Addons::sourceName(source)) + L" 上查看");
                website.SetVisible(!project.pageUrl.empty());
                shownVersions.clear();
                versionKeys.clear();
                Control<exui::ListView>(VERSIONS).ClearRows();
                showView(true);
                refresh();
            }

            // The versions that pass the filters, in the order of the source.
            std::vector<const Addons::Version*> matching(const Addons::Versions& versions) const {
                std::vector<const Addons::Version*> out;
                for (const Addons::Version& version : versions.list) {
                    bool game = versionFilter.empty() ||
                            std::find(version.gameVersions.begin(), version.gameVersions.end(), versionFilter) !=
                                    version.gameVersions.end();
                    bool loader = loaderFilter.empty() ||
                            std::find(version.loaders.begin(), version.loaders.end(), loaderFilter) !=
                                    version.loaders.end();
                    if (game && loader) {
                        out.push_back(&version);
                    }
                }
                return out;
            }

            const Addons::Version* selectedVersion() const {
                std::string id = selectedKey(Control<exui::ListView>(VERSIONS), versionKeys);
                for (const Addons::Version& version : Addons::versions(source, project.id).list) {
                    if (!id.empty() && version.id == id) {
                        return &version;
                    }
                }
                return nullptr;
            }

            void showProject() {
                header->updateIcon();
                bool instance = usesInstance(query.kind);
                FindControl(PROJECT_INSTANCE)->SetVisible(instance);
                int x = 15;
                if (instance) {
                    fillInstances(PROJECT_INSTANCE);
                    x += 228;
                }
                fillVersions(PROJECT_VERSION, versionFilter);
                FindControl(PROJECT_VERSION)->SetBounds(x, 67, 120, 28);
                x += 126;
                FindControl(PROJECT_LOADER)->SetVisible(hasLoaders());
                if (hasLoaders()) {
                    fillLoaders(PROJECT_LOADER, loaderFilter);
                    FindControl(PROJECT_LOADER)->SetBounds(x, 67, 120, 28);
                }

                const Addons::Versions& versions = Addons::versions(source, project.id);
                std::string prefix = sharedPrefix(versions.list);
                Rows rows;
                std::vector<std::string> keys;
                for (const Addons::Version* version : matching(versions)) {
                    std::string games = versionFilter;
                    if (games.empty() && !version->gameVersions.empty()) {
                        games = version->gameVersions.back() + (version->gameVersions.size() > 1 ? " 等" : "");
                    }
                    std::string loaders;
                    for (const std::string& tag : version->loaders) {
                        loaders += (loaders.empty() ? "" : "、") + Modrinth::label(tag);
                    }
                    std::wstring state;
                    switch (ProjectInstaller::state(version->id)) {
                        case ProjectInstaller::State::Running:
                            state = L"安装中 " +
                                    std::to_wstring((int) (ProjectInstaller::progress(version->id) * 100)) + L"%";
                            break;
                        case ProjectInstaller::State::Done: state = L"已完成"; break;
                        case ProjectInstaller::State::Failed: state = L"失败"; break;
                        default: break;
                    }
                    rows.push_back({Platform::widen(shortName(*version, prefix, project.title)),
                                    versionTypeLabel(version->type), Platform::widen(games), Platform::widen(loaders),
                                    Platform::widen(version->published.substr(0, 10)), state});
                    keys.push_back(version->id);
                }
                bool failed = versions.status == Addons::Status::Failed;
                if (rows.empty()) {
                    std::wstring none = versions.status == Addons::Status::Loading ? L"正在获取版本列表…"
                            : failed ? L"获取版本失败：" + Platform::widen(versions.error)
                                     : L"没有符合条件的版本";
                    rows.push_back({none, L"", L"", L"", L"", L""});
                    keys.emplace_back();
                }
                fill(Control<exui::ListView>(VERSIONS), shownVersions, versionKeys, std::move(rows), std::move(keys));
                FindControl(VERSIONS_RETRY)->SetVisible(failed);

                const Addons::Version* version = selectedVersion();
                bool intoInstance = !ProjectInstaller::folder(query.kind).empty();
                bool usable = !intoInstance || Instances::find(target) != nullptr;
                ProjectInstaller::State state =
                        version != nullptr ? ProjectInstaller::state(version->id) : ProjectInstaller::State::Idle;
                bool installed = version != nullptr &&
                        ((intoInstance && usable && ProjectInstaller::installed(*version, query.kind, target)) ||
                         (query.kind == Kind::Modpack && state == ProjectInstaller::State::Done));
                std::wstring status;
                if (version == nullptr) {
                    status = rows.size() > 1 || !versionKeys.front().empty() ? L"选择一个版本" : L"";
                } else if (state == ProjectInstaller::State::Running) {
                    status = L"正在安装 " + std::to_wstring((int) (ProjectInstaller::progress(version->id) * 100)) +
                            L"%";
                } else if (installed) {
                    status = L"已安装";
                } else if (!usable) {
                    status = L"先在上方选择要安装到的实例";
                }
                setText(STATUS, status);
                setText(ACTION, query.kind == Kind::Datapack ? L"下载" : L"安装");
                FindControl(ACTION)->SetEnabled(version != nullptr && usable && !installed &&
                                                state != ProjectInstaller::State::Running);
            }

            // Installs the chosen version as the default interface does: a data pack is saved where one picks.
            void act() {
                const Addons::Version* chosen = selectedVersion();
                if (chosen == nullptr) {
                    return;
                }
                // The list may be fetched again under a dialog, so the version is copied before one opens.
                const Addons::Version version = *chosen;
                switch (query.kind) {
                    case Kind::Modpack:
                        message(GetHwnd(), L"整合包的安装会和新建实例一起做好，现在还在开发中", L"下载中心");
                        break;
                    case Kind::Datapack: {
                        const Instances::Entry* entry = Instances::find(target);
                        std::filesystem::path start =
                                entry != nullptr ? entry->gameDirectory / "saves" : Platform::exeDirectory();
                        if (std::optional<std::filesystem::path> folder = Platform::pickFolder(start)) {
                            ProjectInstaller::saveTo(version, *folder, project.title);
                        }
                        break;
                    }
                    default: ProjectInstaller::install(version, query.kind, project.title, target); break;
                }
            }
        };
    }

    void showDownloads(HWND parent) {
        DownloadsWindow window;
        if (window.create(parent)) {
            runModal(window);
        }
    }
}
