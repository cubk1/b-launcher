#include "ui/imgui/screens/accounts/ProfilePage.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Profiles.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

namespace ProfilePage {

    namespace blb = b_launcher_base;

    namespace {
        const char* const NAME_OWNER = "accounts#profile#name";
        constexpr float PAD = 14.0f;
        constexpr float TILE_W = 84.0f;
        constexpr float TILE_H = 100.0f;
        constexpr float TILE_GAP = 10.0f;
        constexpr float CAPE_W = 40.0f;
        constexpr float CAPE_H = 64.0f;
        constexpr float CAPE_RADIUS = 7.0f;
        constexpr float OPTIFINE_H = 92.0f;
        constexpr float NAME_FIELD_W = 180.0f;

        std::string key;
        std::string typed;

        uint32_t success() {
            return UiTheme::isLight() ? 0xFF2F9E5Cu : 0xFF5CC98Au;
        }

        uint32_t warning() {
            return UiTheme::isLight() ? 0xFFC27803u : 0xFFE8A33Du;
        }

        std::string fit(const std::string& text, float size, float room) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, room));
        }

        void pickSkin(const std::string& account) {
            std::vector<std::filesystem::path> files =
                    Platform::pickFiles(Platform::userDirectory(), L"选择皮肤图片", L"PNG 图片", L"*.png");
            if (!files.empty()) {
                Profiles::uploadSkin(account, files.front());
            }
        }

        void skinMenu(float x, float y) {
            std::string account = key;
            NewUi::openMenu(
                    "accounts#profile#skin#menu",
                    [account] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("上传皮肤", 0xf093, [account] { pickSkin(account); }));
                        items.push_back(ActionMenuItem::make("恢复默认皮肤", 0xf0e2,
                                                             [account] { Profiles::resetSkin(account); })
                                                ->dangerous()
                                                ->confirmFirst());
                        return items;
                    },
                    x, y, 200.0f);
        }

        float skinCard(const Profiles::Profile& profile, float y, bool blocked) {
            Page::caption("皮肤", y);
            y += Page::CAPTION_H;
            Page::card(y, 1);
            bool working = profile.working == "skin";
            if (Page::navRow("accounts#profile#skin", "更换皮肤", working ? std::string("正在更换…") : std::string(), y,
                             blocked || !profile.working.empty(), Page::rowCorners(0, 1), 0xf553)) {
                skinMenu(Page::controlRight() - 200.0f, y + Page::ROW_H + 4.0f);
            }
            return y + Page::ROW_H;
        }

        int capeColumns() {
            return std::max(1, (int) ((Page::width() - 2.0f * PAD + TILE_GAP) / (TILE_W + TILE_GAP)));
        }

        float capeHeight(const Profiles::Profile& profile) {
            if (profile.capes.empty()) {
                return Page::ROW_H;
            }
            int count = (int) profile.capes.size() + 1;
            int rows = (count + capeColumns() - 1) / capeColumns();
            return 2.0f * PAD + rows * TILE_H + (rows - 1) * TILE_GAP;
        }

        void capeTile(const std::string& id, const std::string& label, const std::string& url, bool active,
                      bool working, bool busy, float x, float y, bool blocked) {
            std::string widget = "accounts#profile#cape#" + (id.empty() ? std::string("none") : id);
            bool hovered = !blocked && NewUi::hit(x, y, TILE_W, TILE_H);
            float hover = UiState::motion(widget + "#hover", hovered && !active && !busy ? 1.0f : 0.0f, 20.0f);
            float lit = UiState::motion(widget + "#active", active ? 1.0f : 0.0f, 16.0f);
            float imageX = x + (TILE_W - CAPE_W) * 0.5f;
            float imageY = y + 8.0f;
            float cx = imageX + CAPE_W * 0.5f;
            float cy = imageY + CAPE_H * 0.5f;
            uint32_t texture = id.empty() ? 0 : Textures::of(Profiles::capeFront(url));
            if (texture != 0) {
                ShaderUtil::roundTex(UiRender::transformX(imageX), UiRender::transformY(imageY),
                                     UiRender::transformW(CAPE_W), UiRender::transformW(CAPE_H),
                                     UiRender::transformW(CAPE_RADIUS), texture, 0.0f, 0.0f, 1.0f, 1.0f,
                                     UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                UiRender::roundRect(imageX, imageY, CAPE_W, CAPE_H, CAPE_RADIUS, UiTheme::surfaceRaised());
                if (id.empty()) {
                    UiWidgets::icon(Ui::glyph(0xf05e), cx, cy, UiTheme::textFaint(), 16.0f);
                } else {
                    Page::spinner(cx, cy, 6.0f, UiTheme::accent());
                }
            }
            if (hover > 0.001f) {
                UiRender::roundRect(imageX, imageY, CAPE_W, CAPE_H, CAPE_RADIUS,
                                    UiTheme::overlay(Ui::round(30 * hover)));
            }
            if (lit > 0.001f) {
                UiRender::roundRect(imageX, imageY, CAPE_W, CAPE_H, CAPE_RADIUS,
                                    (UiTheme::accent() & 0x00FFFFFFu) | ((uint32_t) Ui::round(150.0f * lit) << 24));
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * lit;
                UiWidgets::check(cx, cy, 0xFFFFFFFFu, 14.0f);
                UiRender::alphaMul = savedAlpha;
            }
            if (working) {
                UiRender::roundRect(imageX, imageY, CAPE_W, CAPE_H, CAPE_RADIUS, 0x80000000u);
                Page::spinner(cx, cy, 7.0f, 0xFFFFFFFFu);
            }
            std::string shown = fit(label, UiWidgets::TEXT_SMALL, TILE_W - 10.0f);
            Page::centredText(shown, x + TILE_W * 0.5f, imageY + CAPE_H + 6.0f, 20.0f,
                              UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), lit),
                              UiWidgets::TEXT_SMALL);
            if (hovered && shown != label) {
                Page::tooltip(label, x, y, TILE_W, TILE_H);
            }
            if (!active && !busy && !blocked && NewUi::click(x, y, TILE_W, TILE_H)) {
                UiSound::request(UiSound::TICK);
                Profiles::setCape(key, id);
            }
        }

        float capeCard(const Profiles::Profile& profile, float y, bool blocked) {
            Page::caption("披风", y);
            y += Page::CAPTION_H;
            if (profile.capes.empty()) {
                Page::card(y, 1);
                UiRender::textY("这个账户还没有披风", Page::columnLeft() + 16.0f, y, Page::ROW_H, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                return y + Page::ROW_H;
            }
            float x = Page::left();
            float h = capeHeight(profile);
            Page::card(x, y, Page::width(), h);
            int columns = capeColumns();
            bool busy = !profile.working.empty();
            bool hidden = std::none_of(profile.capes.begin(), profile.capes.end(),
                                       [](const Profiles::Cape& cape) { return cape.active; });
            for (size_t i = 0; i <= profile.capes.size(); i++) {
                float tileX = x + PAD + (float) (i % columns) * (TILE_W + TILE_GAP);
                float tileY = y + PAD + (float) (i / columns) * (TILE_H + TILE_GAP);
                if (i == 0) {
                    capeTile(std::string(), "不显示", std::string(), hidden, profile.working == "cape:", busy, tileX,
                             tileY, blocked);
                    continue;
                }
                const Profiles::Cape& cape = profile.capes[i - 1];
                capeTile(cape.id, cape.alias.empty() ? std::string("披风") : cape.alias, cape.url, cape.active,
                         profile.working == "cape:" + cape.id, busy, tileX, tileY, blocked);
            }
            return y + h;
        }

        float optifineHeight(const Profiles::Profile& profile) {
            return profile.optifine == Profiles::OptiFine::Owned ? OPTIFINE_H : Page::ROW_H;
        }

        float optifineCard(const Profiles::Profile& profile, float y, bool blocked) {
            Page::caption("OptiFine 披风", y);
            y += Page::CAPTION_H;
            float textX = Page::columnLeft() + 16.0f;
            if (profile.optifine != Profiles::OptiFine::Owned) {
                Page::card(y, 1);
                bool checking = profile.optifine == Profiles::OptiFine::Checking;
                bool failed = profile.optifine == Profiles::OptiFine::Failed;
                std::string text = checking ? "正在查询…"
                        : failed            ? "无法查询 OptiFine 披风"
                                            : "这个账户没有 OptiFine 披风";
                UiRender::textY(text, textX, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (checking) {
                    Page::spinner(Page::controlRight() - 8.0f, y + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
                } else if (failed) {
                    std::string retry = "重试";
                    float retryW = std::max(64.0f, Page::buttonWidth(retry));
                    if (Page::button("accounts#profile#optifine#retry", retry, Page::controlRight() - retryW,
                                     y + Page::FIELD_Y, retryW, false, blocked)) {
                        Profiles::checkOptiFine(key);
                    }
                } else {
                    std::string buy = "购买 OptiFine 披风";
                    float buyW = Page::buttonWidth(buy, 0xf35d);
                    if (Page::button("accounts#profile#optifine#buy", buy, Page::controlRight() - buyW,
                                     y + Page::FIELD_Y, buyW, false, blocked, 0xf35d)) {
                        Platform::openUrl("https://optifine.net/donate");
                    }
                }
                return y + Page::ROW_H;
            }
            float x = Page::left();
            Page::card(x, y, Page::width(), OPTIFINE_H);
            float imageX = x + PAD + (TILE_W - CAPE_W) * 0.5f;
            float imageY = y + (OPTIFINE_H - CAPE_H) * 0.5f;
            if (uint32_t texture = Textures::of(Profiles::capeFront(profile.optifineUrl))) {
                ShaderUtil::roundTex(UiRender::transformX(imageX), UiRender::transformY(imageY),
                                     UiRender::transformW(CAPE_W), UiRender::transformW(CAPE_H),
                                     UiRender::transformW(CAPE_RADIUS), texture, 0.0f, 0.0f, 1.0f, 1.0f,
                                     UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                UiRender::roundRect(imageX, imageY, CAPE_W, CAPE_H, CAPE_RADIUS, UiTheme::surfaceRaised());
            }
            std::string manage = "管理披风";
            float manageW = Page::buttonWidth(manage, 0xf35d);
            float manageX = Page::controlRight() - manageW;
            float manageY = y + (OPTIFINE_H - Page::FIELD_H) * 0.5f;
            if (Page::button("accounts#profile#optifine", manage, manageX, manageY, manageW, false,
                             blocked || !profile.working.empty(), 0xf35d)) {
                Profiles::manageOptiFine(key);
            }
            if (profile.working == "optifine") {
                Page::spinner(manageX - 18.0f, manageY + Page::FIELD_H * 0.5f, 7.0f, UiTheme::accent());
            }
            return y + OPTIFINE_H;
        }

        std::string liveName() {
            return NewUi::textEditing == NAME_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : typed;
        }

        bool confirming() {
            const Profiles::NameCheck& check = Profiles::nameCheck(key);
            std::string now = Ui::trim(liveName());
            return !now.empty() && check.name == now && check.state == Profiles::Check::Available;
        }

        std::shared_ptr<TextBinding> nameValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        NAME_OWNER, [] { return typed; }, [](const std::string& text) { typed = Ui::trim(text); },
                        "新的用户名");
                binding->limit = 16;
                return binding;
            }();
            return value;
        }

        void nameNote(const Profiles::Profile& profile, float y) {
            const Profiles::NameCheck& check = Profiles::nameCheck(key);
            std::string now = Ui::trim(liveName());
            std::string text;
            uint32_t color = UiTheme::textMuted();
            if (!profile.renameAllowed) {
                text = profile.renameAfter.empty() ? std::string("最近改过名，30 天内不能再改")
                                                   : profile.renameAfter + " 之后才能再改名";
                color = warning();
            } else if (!now.empty() && check.name == now) {
                switch (check.state) {
                    case Profiles::Check::Checking: text = "正在检查…"; break;
                    case Profiles::Check::Available:
                        text = "可以使用";
                        color = success();
                        break;
                    case Profiles::Check::Taken:
                        text = "这个用户名已经有人用了";
                        color = UiTheme::danger();
                        break;
                    case Profiles::Check::Invalid:
                        text = "需要 3–16 位，只能用字母、数字和下划线";
                        color = UiTheme::danger();
                        break;
                    case Profiles::Check::Failed:
                        text = check.error;
                        color = UiTheme::danger();
                        break;
                    default: break;
                }
            }
            if (text.empty()) {
                return;
            }
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(text, Page::right() - 2.0f - textW, y + 2.0f, 16.0f, color, UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }

        void nameCard(const Profiles::Profile& profile, float y, bool blocked) {
            Page::caption("用户名", y);
            nameNote(profile, y);
            y += Page::CAPTION_H;
            bool sure = confirming();
            Page::card(y, sure ? 3 : 2);

            Page::label("当前用户名", y);
            float nameW = UiFont::width(profile.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(profile.name, Page::controlRight() - nameW, y, Page::ROW_H, UiTheme::textSecondary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            y += Page::ROW_H;

            Page::label("新用户名", y);
            std::string action = "修改";
            float actionW = std::max(72.0f, Page::buttonWidth(action));
            float actionX = Page::controlRight() - actionW;
            float fieldX = actionX - 8.0f - NAME_FIELD_W;
            bool working = profile.working == "name";
            TextFieldWidget field(nameValue());
            field.box(fieldX, y + Page::FIELD_Y, NAME_FIELD_W, Page::FIELD_H)
                    .bounds(fieldX, y + Page::FIELD_Y, NAME_FIELD_W, Page::FIELD_H)
                    .block(blocked || !profile.renameAllowed || working);
            field.paint();
            std::string now = Ui::trim(liveName());
            const Profiles::NameCheck& check = Profiles::nameCheck(key);
            bool checking = check.name == now && check.state == Profiles::Check::Checking;
            bool usable = profile.renameAllowed && profile.working.empty() && !now.empty() && now != profile.name &&
                    !sure && !checking;
            if (Page::button("accounts#profile#check", action, actionX, y + Page::FIELD_Y, actionW, false,
                             blocked || !usable)) {
                typed = now;
                if (NewUi::textEditing == NAME_OWNER) {
                    NewUi::cancelTextEdit();
                }
                Profiles::checkName(key, typed);
            }
            y += Page::ROW_H;
            if (!sure) {
                return;
            }

            std::string confirm = "确认修改";
            std::string cancel = "取消";
            float confirmW = std::max(84.0f, Page::buttonWidth(confirm));
            float cancelW = std::max(64.0f, Page::buttonWidth(cancel));
            float confirmX = Page::controlRight() - confirmW;
            float cancelX = confirmX - 8.0f - cancelW;
            std::string question = "把用户名从 " + profile.name + " 改为 " + now + "？改名后 30 天内不能再改";
            float textX = Page::columnLeft() + 16.0f;
            UiRender::textY(fit(question, UiWidgets::TEXT_CONTROL, cancelX - 12.0f - textX), textX, y, Page::ROW_H,
                            warning(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (working) {
                Page::spinner(cancelX - 18.0f, y + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
            }
            if (Page::button("accounts#profile#cancel", cancel, cancelX, y + Page::FIELD_Y, cancelW, false,
                             blocked || working)) {
                Profiles::forgetName(key);
            }
            if (Page::button("accounts#profile#confirm", confirm, confirmX, y + Page::FIELD_Y, confirmW, true,
                             blocked || working)) {
                Profiles::rename(key, now);
            }
        }
    }

    void open(const std::string& account) {
        key = account;
        typed.clear();
        if (NewUi::textEditing == NAME_OWNER) {
            NewUi::cancelTextEdit();
        }
        Profiles::forgetName(key);
        Profiles::reload(key);
    }

    bool draw(bool covered) {
        const blb::auth::account* account = Accounts::find(key);
        if (account == nullptr) {
            return false;
        }
        if (Page::iconButton("accounts#profile#back", 0xf053, "返回", Page::left() - 6.0f, Page::TOP - 2.0f, covered)) {
            if (NewUi::textEditing == NAME_OWNER) {
                NewUi::cancelTextEdit();
            }
            Profiles::forgetName(key);
            return false;
        }
        const char* title = "用户资料";
        Page::title(title, Page::left() + 28.0f, Page::TOP);
        float nameX = Page::left() + 28.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(fit(account->name, UiWidgets::TEXT_CONTROL, Page::right() - 40.0f - nameX), nameX, Page::TOP,
                        24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        const Profiles::Profile& profile = Profiles::of(key);
        if (Page::iconButton("accounts#profile#reload", 0xf2f1, "重新读取", Page::right() - 28.0f, Page::TOP - 2.0f,
                             covered || !profile.working.empty() || profile.status == Profiles::Status::Loading)) {
            Profiles::reload(key);
        }

        float top = Page::TOP + 40.0f;
        float bottom = MainWindow::height() - 14.0f;
        float cx = Page::left() + Page::width() * 0.5f;
        float middle = (top + bottom) * 0.5f;
        if (profile.status == Profiles::Status::Loading) {
            Page::spinner(cx, middle, 10.0f, UiTheme::accent());
            return true;
        }
        if (profile.status == Profiles::Status::Failed) {
            Page::centredText(profile.error, cx, middle - 30.0f, 24.0f, UiTheme::textMuted(), UiWidgets::TEXT_BODY);
            std::string retry = "重试";
            float retryW = std::max(72.0f, Page::buttonWidth(retry));
            if (Page::button("accounts#profile#retry", retry, cx - retryW * 0.5f, middle + 4.0f, retryW, false,
                             covered)) {
                Profiles::reload(key);
            }
            return true;
        }

        if (profile.working.empty() && NewUi::textEditing != NAME_OWNER && typed == profile.name) {
            typed.clear();
        }
        int nameRows = confirming() ? 3 : 2;
        float contentH = 3.0f * Page::CAPTION_H + Page::ROW_H + capeHeight(profile) + Page::CAPTION_H +
                optifineHeight(profile) + Page::GAP + nameRows * Page::ROW_H + 2.0f * Page::GAP + 8.0f;
        Page::Scroll body("accounts#profile", top, bottom, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y = body.top - body.offset;
        y = skinCard(profile, y, blocked) + Page::GAP;
        y = capeCard(profile, y, blocked) + Page::GAP;
        y = optifineCard(profile, y, blocked) + Page::GAP;
        nameCard(profile, y, blocked);
        body.end();
        return true;
    }
}
