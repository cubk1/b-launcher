#include "ui/imgui/screens/accounts/ProfilePage.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Avatars.h"
#include "app/accounts/LittleSkin.h"
#include "app/accounts/Looks.h"
#include "app/accounts/Profiles.h"
#include "app/launch/Patches.h"
#include "app/system/Notices.h"
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

#include <core/auth/offline.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ProfilePage {

    namespace blb = b_launcher_base;

    namespace {
        const char* const NAME_OWNER = "accounts#profile#name";
        const char* const UUID_OWNER = "accounts#profile#uuid";
        constexpr float PAD = 14.0f;
        constexpr float TILE_W = 84.0f;
        constexpr float TILE_H = 100.0f;
        constexpr float TILE_GAP = 10.0f;
        constexpr float CAPE_W = 40.0f;
        constexpr float CAPE_H = 64.0f;
        constexpr float SKIN_W = 64.0f;
        constexpr float CAPE_RADIUS = 7.0f;
        constexpr float OPTIFINE_H = 92.0f;
        constexpr float NAME_FIELD_W = 180.0f;
        constexpr float UUID_FIELD_W = 290.0f;

        std::string key;
        std::string typed;
        std::string typedUuid;
        std::string renameProblem;
        std::string uuidProblem;
        float measured = 0.0f;

        uint32_t success() {
            return UiTheme::success();
        }

        uint32_t warning() {
            return UiTheme::warning();
        }

        std::string fit(const std::string& text, float size, float room) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, room));
        }

        void stopEditing() {
            if (NewUi::textEditing == NAME_OWNER || NewUi::textEditing == UUID_OWNER) {
                NewUi::cancelTextEdit();
            }
        }

        std::filesystem::path pickPng(const wchar_t* title) {
            std::vector<std::filesystem::path> files =
                    Platform::pickFiles(Platform::userDirectory(), title, L"PNG 图片", L"*.png");
            return files.empty() ? std::filesystem::path() : files.front();
        }

        void note(const std::string& text, uint32_t color, float y) {
            if (text.empty()) {
                return;
            }
            std::string shown = fit(text, UiWidgets::TEXT_SMALL, Page::width() - 120.0f);
            float textW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(shown, Page::right() - 2.0f - textW, y + 2.0f, 16.0f, color, UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }

        float emptyCard(const std::string& text, float y) {
            Page::card(y, 1);
            UiRender::textY(text, Page::columnLeft() + 16.0f, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            return y + Page::ROW_H;
        }

        float bodyTop() {
            return Page::top() + 40.0f;
        }

        float bodyBottom() {
            return MainWindow::height() - 14.0f;
        }

        void centredSpinner() {
            Page::spinner(Page::left() + Page::width() * 0.5f, (bodyTop() + bodyBottom()) * 0.5f, 10.0f,
                          UiTheme::accent());
        }

        int notice(const std::string& id, const std::string& text, const std::vector<std::string>& buttons, int primary,
                   bool covered) {
            float cx = Page::left() + Page::width() * 0.5f;
            float middle = (bodyTop() + bodyBottom()) * 0.5f;
            Page::centredText(fit(text, UiWidgets::TEXT_BODY, Page::width()), cx, middle - 30.0f, 24.0f,
                              UiTheme::textMuted(), UiWidgets::TEXT_BODY);
            std::vector<float> widths;
            float total = 0.0f;
            for (const std::string& label : buttons) {
                widths.push_back(std::max(72.0f, Page::buttonWidth(label)));
                total += widths.back() + (widths.size() > 1 ? 8.0f : 0.0f);
            }
            float x = cx - total * 0.5f;
            int clicked = -1;
            for (size_t i = 0; i < buttons.size(); i++) {
                if (Page::button(id + "#" + std::to_string(i), buttons[i], x, middle + 4.0f, widths[i],
                                 (int) i == primary, covered)) {
                    clicked = (int) i;
                }
                x += widths[i] + 8.0f;
            }
            return clicked;
        }

        int tileColumns() {
            return std::max(1, (int) ((Page::width() - 2.0f * PAD + TILE_GAP) / (TILE_W + TILE_GAP)));
        }

        float gridHeight(int count) {
            int rows = (count + tileColumns() - 1) / tileColumns();
            return 2.0f * PAD + rows * TILE_H + (rows - 1) * TILE_GAP;
        }

        float grid(int count, float y, const std::function<void(int, float, float)>& each) {
            float x = Page::left();
            float h = gridHeight(count);
            Page::card(x, y, Page::width(), h);
            int columns = tileColumns();
            for (int i = 0; i < count; i++) {
                float tileX = x + PAD + (float) (i % columns) * (TILE_W + TILE_GAP);
                float tileY = y + PAD + (float) (i / columns) * (TILE_H + TILE_GAP);
                if (Page::shown(tileY, TILE_H)) {
                    each(i, tileX, tileY);
                }
            }
            return y + h;
        }

        bool tile(const std::string& widget, const std::string& label, uint32_t texture, float imageW, bool none,
                  bool active, bool working, bool busy, float x, float y, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, TILE_W, TILE_H);
            float hover = UiState::motion(widget + "#hover", hovered && !active && !busy ? 1.0f : 0.0f, 20.0f);
            float lit = UiState::motion(widget + "#active", active ? 1.0f : 0.0f, 16.0f);
            float imageX = x + (TILE_W - imageW) * 0.5f;
            float imageY = y + 8.0f;
            float cx = imageX + imageW * 0.5f;
            float cy = imageY + CAPE_H * 0.5f;
            if (texture == 0 || imageW != CAPE_W) {
                UiRender::roundRect(imageX, imageY, imageW, CAPE_H, CAPE_RADIUS, UiTheme::surfaceRaised());
            }
            if (texture != 0) {
                ShaderUtil::roundTex(UiRender::transformX(imageX), UiRender::transformY(imageY),
                                     UiRender::transformW(imageW), UiRender::transformW(CAPE_H),
                                     UiRender::transformW(CAPE_RADIUS), texture, 0.0f, 0.0f, 1.0f, 1.0f,
                                     UiRender::applyAlpha(0xFFFFFFFFu));
            } else if (none) {
                UiWidgets::icon(Ui::glyph(0xf05e), cx, cy, UiTheme::textFaint(), 16.0f);
            } else {
                Page::spinner(cx, cy, 6.0f, UiTheme::accent());
            }
            if (hover > 0.001f) {
                UiRender::roundRect(imageX, imageY, imageW, CAPE_H, CAPE_RADIUS,
                                    UiTheme::overlay(Ui::round(30 * hover)));
            }
            if (lit > 0.001f) {
                UiRender::roundRect(imageX, imageY, imageW, CAPE_H, CAPE_RADIUS,
                                    (UiTheme::accent() & 0x00FFFFFFu) | ((uint32_t) Ui::round(150.0f * lit) << 24));
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * lit;
                UiWidgets::check(cx, cy, UiTheme::onAccent(), 14.0f);
                UiRender::alphaMul = savedAlpha;
            }
            if (working) {
                UiRender::roundRect(imageX, imageY, imageW, CAPE_H, CAPE_RADIUS, 0x80000000u);
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
                return true;
            }
            return false;
        }

        void pickSkin(const std::string& account) {
            std::filesystem::path file = pickPng(L"选择皮肤图片");
            if (!file.empty()) {
                Profiles::uploadSkin(account, file);
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

        float capeHeight(const Profiles::Profile& profile) {
            return profile.capes.empty() ? Page::ROW_H : gridHeight((int) profile.capes.size() + 1);
        }

        float capeCard(const Profiles::Profile& profile, float y, bool blocked) {
            Page::caption("披风", y);
            y += Page::CAPTION_H;
            if (profile.capes.empty()) {
                return emptyCard("这个账户还没有披风", y);
            }
            bool busy = !profile.working.empty();
            bool hidden = std::none_of(profile.capes.begin(), profile.capes.end(),
                                       [](const Profiles::Cape& cape) { return cape.active; });
            return grid((int) profile.capes.size() + 1, y, [&](int i, float x, float tileY) {
                if (i == 0) {
                    if (tile("accounts#profile#cape#none", "不显示", 0, CAPE_W, true, hidden,
                             profile.working == "cape:", busy, x, tileY, blocked)) {
                        Profiles::setCape(key, std::string());
                    }
                    return;
                }
                const Profiles::Cape& cape = profile.capes[(size_t) i - 1];
                if (tile("accounts#profile#cape#" + cape.id, cape.alias.empty() ? std::string("披风") : cape.alias,
                         Textures::of(Profiles::capeFront(cape.url)), CAPE_W, false, cape.active,
                         profile.working == "cape:" + cape.id, busy, x, tileY, blocked)) {
                    Profiles::setCape(key, cape.id);
                }
            });
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

        std::string liveUuid() {
            return NewUi::textEditing == UUID_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : typedUuid;
        }

        bool confirming() {
            const Profiles::NameCheck& check = Profiles::nameCheck(key);
            std::string now = Ui::trim(liveName());
            return !now.empty() && check.name == now && check.state == Profiles::Check::Available;
        }

        std::shared_ptr<TextBinding> nameValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        NAME_OWNER, [] { return typed; },
                        [](const std::string& text) {
                            typed = Ui::trim(text);
                            renameProblem.clear();
                        },
                        "新的用户名");
                binding->limit = 16;
                return binding;
            }();
            return value;
        }

        std::shared_ptr<TextBinding> uuidValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        UUID_OWNER, [] { return typedUuid; },
                        [](const std::string& text) {
                            typedUuid = Ui::trim(text);
                            uuidProblem.clear();
                        },
                        "新的 UUID");
                binding->limit = 36;
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
            note(text, color, y);
        }

        void nameField(float x, float y, bool blocked) {
            TextFieldWidget field(nameValue());
            field.box(x, y + Page::FIELD_Y, NAME_FIELD_W, Page::FIELD_H)
                    .bounds(x, y + Page::FIELD_Y, NAME_FIELD_W, Page::FIELD_H)
                    .block(blocked);
            field.paint();
        }

        void currentValue(const std::string& label, const std::string& value, float y) {
            Page::label(label, y);
            float valueW = UiFont::width(value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(value, Page::controlRight() - valueW, y, Page::ROW_H, UiTheme::textSecondary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }

        void nameCard(const Profiles::Profile& profile, float y, bool blocked) {
            Page::caption("用户名", y);
            nameNote(profile, y);
            y += Page::CAPTION_H;
            bool sure = confirming();
            Page::card(y, sure ? 3 : 2);

            currentValue("当前用户名", profile.name, y);
            y += Page::ROW_H;

            Page::label("新用户名", y);
            std::string action = "修改";
            float actionW = std::max(72.0f, Page::buttonWidth(action));
            float actionX = Page::controlRight() - actionW;
            float fieldX = actionX - 8.0f - NAME_FIELD_W;
            bool working = profile.working == "name";
            nameField(fieldX, y, blocked || !profile.renameAllowed || working);
            std::string now = Ui::trim(liveName());
            const Profiles::NameCheck& check = Profiles::nameCheck(key);
            bool checking = check.name == now && check.state == Profiles::Check::Checking;
            bool usable = profile.renameAllowed && profile.working.empty() && !now.empty() && now != profile.name &&
                    !sure && !checking;
            if (Page::button("accounts#profile#check", action, actionX, y + Page::FIELD_Y, actionW, false,
                             blocked || !usable)) {
                typed = now;
                stopEditing();
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

        float renameCard(const std::string& current, bool working, float y, bool blocked,
                         const std::function<void(const std::string&)>& submit) {
            Page::caption("用户名", y);
            note(renameProblem, UiTheme::danger(), y);
            y += Page::CAPTION_H;
            Page::card(y, 2);

            currentValue("当前用户名", current, y);
            y += Page::ROW_H;

            Page::label("新用户名", y);
            std::string action = "修改";
            float actionW = std::max(72.0f, Page::buttonWidth(action));
            float actionX = Page::controlRight() - actionW;
            float fieldX = actionX - 8.0f - NAME_FIELD_W;
            nameField(fieldX, y, blocked || working);
            if (working) {
                Page::spinner(fieldX - 18.0f, y + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
            }
            std::string now = Ui::trim(liveName());
            bool usable = !working && !now.empty() && now != current;
            if (Page::button("accounts#profile#rename", action, actionX, y + Page::FIELD_Y, actionW, false,
                             blocked || !usable)) {
                typed = now;
                stopEditing();
                submit(now);
            }
            return y + Page::ROW_H;
        }

        bool microsoft(bool covered) {
            const Profiles::Profile& profile = Profiles::of(key);
            if (Page::iconButton("accounts#profile#reload", 0xf2f1, "重新读取", Page::right() - 28.0f,
                                 Page::top() - 2.0f,
                                 covered || !profile.working.empty() || profile.status == Profiles::Status::Loading)) {
                Profiles::reload(key);
            }
            if (profile.status == Profiles::Status::Loading) {
                centredSpinner();
                return true;
            }
            if (profile.status == Profiles::Status::Failed) {
                if (notice("accounts#profile#retry", profile.error, {"重试"}, -1, covered) == 0) {
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
            Page::Scroll body("accounts#profile", bodyTop(), bodyBottom(), contentH);
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

        void uploadLook(const std::string& account, bool cape) {
            std::filesystem::path file = pickPng(cape ? L"选择披风图片" : L"选择皮肤图片");
            if (file.empty()) {
                return;
            }
            std::string problem = cape ? Looks::setCape(account, file) : Looks::setSkin(account, file);
            if (!problem.empty()) {
                Notices::add(Notices::Kind::Failed, problem, 5000);
                return;
            }
            Avatars::refresh(account);
            if (cape) {
                Notices::add(Notices::Kind::Done, "已更换披风", 3000);
            } else {
                Notices::add(Notices::Kind::Done,
                             Looks::of(account).model == Looks::Model::Slim ? "已更换皮肤（纤细模型）"
                                                                            : "已更换皮肤（经典模型）",
                             3000);
            }
        }

        void clearLook(const std::string& account, bool cape) {
            if (cape) {
                Looks::clearCape(account);
            } else {
                Looks::clearSkin(account);
            }
            Avatars::refresh(account);
            Notices::add(Notices::Kind::Done, cape ? "已移除披风" : "已恢复默认皮肤", 3000);
        }

        void lookMenu(const std::string& id, bool cape, bool set, float x, float y) {
            std::string account = key;
            NewUi::openMenu(
                    id,
                    [account, cape, set] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make(cape ? "上传披风" : "上传皮肤", 0xf093,
                                                             [account, cape] { uploadLook(account, cape); }));
                        if (set) {
                            items.push_back(ActionMenuItem::make(cape ? "移除披风" : "恢复默认皮肤",
                                                                 cape ? 0xf2ed : 0xf0e2,
                                                                 [account, cape] { clearLook(account, cape); })
                                                    ->dangerous()
                                                    ->confirmFirst());
                        }
                        return items;
                    },
                    x, y, 200.0f);
        }

        float lookCard(bool cape, float y, bool blocked) {
            const Looks::Look& look = Looks::of(key);
            bool held = !(cape ? look.cape : look.skin).empty();
            bool set = held || !(cape ? look.capeFile : look.skinFile).empty();
            Page::caption(cape ? "披风" : "皮肤", y);
            if (!cape && !Patches::flag("skin.authlib")) {
                note("需要开启补丁中的自定义皮肤", warning(), y);
            }
            y += Page::CAPTION_H;
            Page::card(y, 1);
            std::string id = cape ? "accounts#profile#cape" : "accounts#profile#skin";
            std::string value = held ? "自定义" : set ? "找不到图片" : cape ? "不显示" : "默认";
            if (Page::navRow(id, cape ? "更换披风" : "更换皮肤", value, y, blocked, Page::rowCorners(0, 1),
                             cape ? 0xf024 : 0xf553)) {
                lookMenu(id + "#menu", cape, set, Page::controlRight() - 200.0f, y + Page::ROW_H + 4.0f);
            }
            return y + Page::ROW_H;
        }

        std::string randomUuid() {
            std::string hex = blb::random_bytes_hex(16);
            hex[12] = '4';
            hex[16] = "89ab"[std::stoi(hex.substr(16, 1), nullptr, 16) % 4];
            return blb::auth::format_uuid(hex);
        }

        void moved(const std::string& updated, const std::string& message) {
            key = updated;
            Avatars::refresh(key);
            Notices::add(Notices::Kind::Done, message, 3000);
        }

        float uuidCard(const blb::auth::account& account, float y, bool blocked) {
            Page::caption("UUID", y);
            note(uuidProblem, UiTheme::danger(), y);
            y += Page::CAPTION_H;
            Page::card(y, 3);

            currentValue("当前 UUID", blb::auth::format_uuid(account.uuid), y);
            y += Page::ROW_H;

            Page::label("新 UUID", y);
            std::string action = "修改";
            float actionW = std::max(72.0f, Page::buttonWidth(action));
            float actionX = Page::controlRight() - actionW;
            float fieldW = std::min(UUID_FIELD_W, actionX - 8.0f - Page::columnLeft() - 100.0f);
            float fieldX = actionX - 8.0f - fieldW;
            TextFieldWidget field(uuidValue());
            field.box(fieldX, y + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .bounds(fieldX, y + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .block(blocked);
            field.paint();
            std::string now = Ui::trim(liveUuid());
            bool usable = !now.empty() && blb::auth::strip_uuid(now) != account.uuid;
            bool apply = Page::button("accounts#profile#uuid#apply", action, actionX, y + Page::FIELD_Y, actionW, false,
                                      blocked || !usable);
            y += Page::ROW_H;

            Page::label("生成 UUID", y);
            std::string name = account.name;
            std::vector<std::pair<std::string, std::function<std::string()>>> ways = {
                    {"标准", [name] { return blb::auth::format_uuid(blb::auth::offline_uuid(name)); }},
                    {"PCL", [name] { return blb::auth::format_uuid(blb::auth::pcl_offline_uuid(name)); }},
                    {"随机", [] { return randomUuid(); }},
            };
            float wayX = Page::controlRight();
            for (size_t i = ways.size(); i-- > 0;) {
                float wayW = std::max(72.0f, Page::buttonWidth(ways[i].first));
                wayX -= wayW;
                if (Page::button("accounts#profile#uuid#" + std::to_string(i), ways[i].first, wayX, y + Page::FIELD_Y,
                                 wayW, false, blocked)) {
                    stopEditing();
                    typedUuid = ways[i].second();
                    uuidProblem.clear();
                }
                wayX -= 8.0f;
            }
            y += Page::ROW_H;

            if (apply) {
                typedUuid = now;
                stopEditing();
                std::string updated;
                uuidProblem = Accounts::updateOffline(key, name, now, updated);
                if (uuidProblem.empty()) {
                    typedUuid.clear();
                    moved(updated, "UUID 已改为 " + blb::auth::format_uuid(blb::auth::strip_uuid(now)));
                }
            }
            return y;
        }

        bool offline(const blb::auth::account& account, bool covered) {
            Page::Scroll body("accounts#profile", bodyTop(), bodyBottom(), measured);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            float start = y;
            y = lookCard(false, y, blocked) + Page::GAP;
            y = lookCard(true, y, blocked) + Page::GAP;
            std::string uuid = account.uuid;
            y = renameCard(account.name, false, y, blocked, [uuid](const std::string& name) {
                    std::string updated;
                    renameProblem = Accounts::updateOffline(key, name, uuid, updated);
                    if (renameProblem.empty()) {
                        typed.clear();
                        moved(updated, "用户名已改为 " + name);
                    }
                }) + Page::GAP;
            y = uuidCard(account, y, blocked);
            measured = y - start + 8.0f;
            body.end();
            return true;
        }

        float closetCard(bool cape, const LittleSkin::State& state, float y, bool blocked) {
            Page::caption(cape ? "披风" : "皮肤", y);
            y += Page::CAPTION_H;
            const std::vector<LittleSkin::Texture>& items = cape ? state.capes : state.skins;
            if (items.empty()) {
                return emptyCard(cape ? "衣柜里还没有披风" : "衣柜里还没有皮肤", y);
            }
            bool busy = !state.working.empty();
            long long worn = cape ? state.cape : state.skin;
            float imageW = cape ? CAPE_W : SKIN_W;
            std::string type = cape ? "cape:" : "skin:";
            return grid((int) items.size() + 1, y, [&](int i, float x, float tileY) {
                if (i == 0) {
                    if (tile("accounts#profile#" + type + "none", cape ? "不显示" : "默认", 0, imageW, true, worn == 0,
                             state.working == type + "0", busy, x, tileY, blocked)) {
                        LittleSkin::wear(key, cape, 0);
                    }
                    return;
                }
                const LittleSkin::Texture& texture = items[(size_t) i - 1];
                std::string tid = std::to_string(texture.tid);
                uint32_t image = Textures::of(cape ? Profiles::capeFront(LittleSkin::textureUrl(texture.hash))
                                                   : LittleSkin::preview(texture));
                if (tile("accounts#profile#" + type + tid, texture.name, image, imageW, false, worn == texture.tid,
                         state.working == type + tid, busy, x, tileY, blocked)) {
                    LittleSkin::wear(key, cape, texture.tid);
                }
            });
        }

        float littleSkinCard(bool busy, float y, bool blocked) {
            Page::caption("LittleSkin", y);
            y += Page::CAPTION_H;
            Page::card(y, 2);
            if (Page::navRow("accounts#profile#ls#upload", "上传皮肤或披风", std::string(), y, blocked,
                             Page::rowCorners(0, 2), 0xf093)) {
                Platform::openUrl(LittleSkin::uploadUrl());
            }
            y += Page::ROW_H;
            if (Page::navRow("accounts#profile#ls#signout", "取消授权", std::string(), y, blocked || busy,
                             Page::rowCorners(1, 2), 0xf2f5)) {
                LittleSkin::signOut(key);
            }
            return y + Page::ROW_H;
        }

        bool littleskin(bool covered) {
            const LittleSkin::State& state = LittleSkin::of(key);
            bool loaded = state.status == LittleSkin::Status::Ready || state.status == LittleSkin::Status::Failed;
            if (Page::iconButton("accounts#profile#reload", 0xf2f1, "重新读取", Page::right() - 28.0f,
                                 Page::top() - 2.0f, covered || !loaded || !state.working.empty())) {
                LittleSkin::reload(key);
            }
            switch (state.status) {
                case LittleSkin::Status::Loading: centredSpinner(); return true;
                case LittleSkin::Status::Authorizing:
                    if (notice("accounts#profile#ls#waiting", "在浏览器中完成授权", {"取消"}, -1, covered) == 0) {
                        LittleSkin::cancel();
                    }
                    return true;
                case LittleSkin::Status::Unauthorized:
                    if (notice("accounts#profile#ls#grant",
                               state.error.empty() ? std::string("授权后才能在启动器中管理 LittleSkin 角色")
                                                   : state.error,
                               {"授权 LittleSkin"}, 0, covered) == 0) {
                        LittleSkin::authorize(key);
                    }
                    return true;
                case LittleSkin::Status::Failed: {
                    int picked = notice("accounts#profile#ls#failed", state.error, {"重新授权", "重试"}, -1, covered);
                    if (picked == 0) {
                        LittleSkin::signOut(key);
                        LittleSkin::authorize(key);
                    } else if (picked == 1) {
                        LittleSkin::reload(key);
                    }
                    return true;
                }
                default: break;
            }

            if (state.working.empty() && NewUi::textEditing != NAME_OWNER && typed == state.player) {
                typed.clear();
            }
            bool busy = !state.working.empty();
            Page::Scroll body("accounts#profile", bodyTop(), bodyBottom(), measured);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            float start = y;
            y = closetCard(false, state, y, blocked) + Page::GAP;
            y = closetCard(true, state, y, blocked) + Page::GAP;
            y = renameCard(state.player, state.working == "name", y, blocked || busy, [](const std::string& name) {
                    LittleSkin::rename(key, name);
                }) + Page::GAP;
            y = littleSkinCard(busy, y, blocked);
            measured = y - start + 8.0f;
            body.end();
            return true;
        }
    }

    void open(const std::string& account) {
        stopEditing();
        key = account;
        typed.clear();
        typedUuid.clear();
        renameProblem.clear();
        uuidProblem.clear();
        measured = 0.0f;
        const blb::auth::account* found = Accounts::find(key);
        if (found == nullptr) {
            return;
        }
        if (found->type == blb::auth::account_type::microsoft) {
            Profiles::forgetName(key);
            Profiles::reload(key);
        } else if (LittleSkin::supports(*found) && LittleSkin::of(key).status == LittleSkin::Status::Ready) {
            LittleSkin::reload(key);
        }
    }

    bool draw(bool covered) {
        const blb::auth::account* account = Accounts::find(key);
        if (account == nullptr) {
            return false;
        }
        if (Page::iconButton("accounts#profile#back", 0xf053, "返回", Page::left() - 6.0f, Page::top() - 2.0f,
                             covered)) {
            stopEditing();
            Profiles::forgetName(key);
            return false;
        }
        Page::title("用户资料", Page::left() + 28.0f, Page::top());
        if (account->type == blb::auth::account_type::microsoft) {
            return microsoft(covered);
        }
        if (account->type == blb::auth::account_type::offline) {
            return offline(*account, covered);
        }
        if (LittleSkin::supports(*account)) {
            return littleskin(covered);
        }
        notice("accounts#profile#unsupported", "这个认证服务器不支持在启动器中修改资料", {}, -1, covered);
        return true;
    }
}
