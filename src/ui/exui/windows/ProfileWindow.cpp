#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ProfileWindow.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Profiles.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/ImageView.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"

#include <algorithm>
#include <optional>

namespace ForgottenMelody {
    namespace {
        namespace blb = b_launcher_base;
        constexpr Gdiplus::ARGB MUTED = 0x96000000, DANGER = 0xD2C00000, SUCCESS = 0xD2208020;
        constexpr const char* NO_CAPE = "none";
        const SectionedForm::Frame FRAME{.width = 780, .height = 590, .top = 75, .bottom = 535, .listWidth = 160};

        class ProfileWindow : public SectionedForm {
        public:
            enum : uint32_t {
                SKIN_PAGE = FIRST_CONTROL, CAPE_PAGE, OPTIFINE_PAGE, NAME_PAGE,
                STATUS, RELOAD, LOGIN, SKIN_GROUP, SKIN_INFO, UPLOAD, RESET_SKIN, SKIN_NOTE,
                CAPE_GROUP, CAPE_NOTE, CAPES, CAPE_IMAGE, APPLY_CAPE,
                OPTIFINE_GROUP, OPTIFINE_IMAGE, OPTIFINE_INFO, QUERY_OPTIFINE, MANAGE_OPTIFINE, BUY_OPTIFINE,
                NAME_GROUP, CURRENT_LABEL, CURRENT_NAME, NEW_LABEL, NEW_NAME, CHECK_NAME, RENAME, NAME_NOTE,
            };

            explicit ProfileWindow(std::string key) : key(std::move(key)) {}
            ~ProfileWindow() override { Profiles::forgetName(key); }

            bool create(HWND parent) {
                const auto* account = accountOf();
                if (!account || !createFrame(parent, L"用户资料", Platform::widen(account->name),
                        {{L"皮肤", SKIN_PAGE}, {L"披风", CAPE_PAGE}, {L"OptiFine 披风", OPTIFINE_PAGE},
                         {L"用户名", NAME_PAGE}}, FRAME)) return false;
                using exui::ControlKind;
                const exui::ControlSpec controls[] = {
                        {STATUS, ControlKind::Label, {}},
                        {RELOAD, ControlKind::Button, {}, L"刷新"},
                        {LOGIN, ControlKind::Button, {}, L"重新登录"},
                        {SKIN_GROUP, ControlKind::Group, {}, L"Minecraft 皮肤"},
                        {SKIN_INFO, ControlKind::Label, {}},
                        {UPLOAD, ControlKind::Button, {}, L"上传皮肤"},
                        {RESET_SKIN, ControlKind::Button, {}, L"恢复默认皮肤"},
                        {SKIN_NOTE, ControlKind::Label, {}, L"支持 64×64 或 64×32 的 PNG 图片，手臂模型会自动识别。"},
                        {CAPE_GROUP, ControlKind::Group, {}, L"Minecraft 披风"},
                        {CAPE_NOTE, ControlKind::Label, {}, L"选中后点击应用；选择“不使用披风”可以隐藏当前披风。"},
                        {CAPES, ControlKind::List, {}},
                        {APPLY_CAPE, ControlKind::Button, {}, L"应用披风"},
                        {OPTIFINE_GROUP, ControlKind::Group, {}, L"OptiFine 披风"},
                        {OPTIFINE_INFO, ControlKind::Label, {}},
                        {QUERY_OPTIFINE, ControlKind::Button, {}, L"重新查询"},
                        {MANAGE_OPTIFINE, ControlKind::Button, {}, L"管理披风"},
                        {BUY_OPTIFINE, ControlKind::Button, {}, L"购买披风"},
                        {NAME_GROUP, ControlKind::Group, {}, L"Minecraft 用户名"},
                        {CURRENT_LABEL, ControlKind::Label, {}, L"当前用户名"},
                        {CURRENT_NAME, ControlKind::Label, {}},
                        {NEW_LABEL, ControlKind::Label, {}, L"新用户名"},
                        {NEW_NAME, ControlKind::Edit, {}},
                        {CHECK_NAME, ControlKind::Button, {}, L"检查可用性"},
                        {RENAME, ControlKind::Button, {}, L"修改用户名"},
                        {NAME_NOTE, ControlKind::Label, {}},
                };
                AddControls(controls);
                AddControl(CAPE_IMAGE, std::make_shared<exui::ImageView>());
                AddControl(OPTIFINE_IMAGE, std::make_shared<exui::ImageView>());
                for (uint32_t id : {STATUS, SKIN_NOTE, CAPE_NOTE, OPTIFINE_INFO, NAME_NOTE}) {
                    Control<exui::Label>(id).SetMultiline(true);
                    Control<exui::Label>(id).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                }
                auto& list = Control<exui::ListView>(CAPES);
                list.InsertColumn(L"披风", 270);
                list.InsertColumn(L"状态", 74);
                list.SetRowHeight(30);
                Control<exui::EditBox>(NEW_NAME).OnTextChanged = [this](const std::wstring&) {
                    Profiles::forgetName(key);
                    refresh();
                };
                FindControl(NEW_NAME)->SetTooltipText(L"3–16 位，只能使用英文字母、数字和下划线。");
                using exui::Layout;
                MoveInto(SKIN_PAGE, {SKIN_GROUP, SKIN_INFO, UPLOAD, RESET_SKIN, SKIN_NOTE});
                MoveInto(CAPE_PAGE, {CAPE_GROUP, CAPE_NOTE, CAPES, CAPE_IMAGE, APPLY_CAPE});
                MoveInto(OPTIFINE_PAGE, {OPTIFINE_GROUP, OPTIFINE_IMAGE, OPTIFINE_INFO,
                                        QUERY_OPTIFINE, MANAGE_OPTIFINE, BUY_OPTIFINE});
                MoveInto(NAME_PAGE, {NAME_GROUP, CURRENT_LABEL, CURRENT_NAME, NEW_LABEL, NEW_NAME,
                                    CHECK_NAME, RENAME, NAME_NOTE});
                place(SKIN_PAGE, group(SKIN_GROUP, {
                        cell(SKIN_INFO, 26), Layout::Row({cell(UPLOAD, 110), cell(RESET_SKIN, 130)}, 10, 30),
                        cell(SKIN_NOTE, 48),
                }));
                place(CAPE_PAGE, group(CAPE_GROUP, {
                        cell(CAPE_NOTE, 36), Layout::Row({cell(CAPES), cell(CAPE_IMAGE, 120)}, 12, 210),
                        Layout::Row({cell(APPLY_CAPE, 100)}, 0, 30),
                }));
                place(OPTIFINE_PAGE, group(OPTIFINE_GROUP, {
                        Layout::Row({cell(OPTIFINE_IMAGE, 120), Layout::Column({
                                cell(OPTIFINE_INFO, 52),
                                Layout::Row({cell(QUERY_OPTIFINE, 88), cell(MANAGE_OPTIFINE, 88),
                                             cell(BUY_OPTIFINE, 88)}, 8, 30),
                        }, 12)}, 16, 190),
                }));
                place(NAME_PAGE, group(NAME_GROUP, {
                        row(CURRENT_LABEL, {cell(CURRENT_NAME)}),
                        row(NEW_LABEL, {cell(NEW_NAME, 180, 26), cell(CHECK_NAME, 88), cell(RENAME, 104)}, 30),
                        cell(NAME_NOTE, 48),
                }));
                Profiles::forgetName(key);
                showSection(0);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const auto* account = accountOf();
                if (!account) { CloseWindow(); return; }
                bool expired = Accounts::expired(key), accountBusy = Accounts::busy(key);
                if (expired) needsReload = true;
                if (needsReload && !expired && !accountBusy) {
                    needsReload = false;
                    Profiles::reload(key);
                }
                const auto* profile = currentProfile();
                bool usable = ready();
                setSubject(Platform::widen(profile && !profile->name.empty() ? profile->name : account->name));
                std::wstring status;
                if (expired) status = L"登录已过期，请重新登录当前账号。";
                else if (accountBusy) status = L"正在刷新账号…";
                else if (!profile || profile->status == Profiles::Status::Loading) status = L"正在读取用户资料…";
                else if (profile->status == Profiles::Status::Failed) status = L"读取失败：" + Platform::widen(profile->error);
                else if (profile->working == "skin") status = L"正在更换皮肤…";
                else if (profile->working.starts_with("cape:")) status = L"正在更换披风…";
                else if (profile->working == "name") status = L"正在修改用户名…";
                else if (profile->working == "optifine") status = L"正在验证账号并打开 OptiFine 管理页面…";
                UpdateText(STATUS, status);
                FindControl(STATUS)->SetTooltipText(status);
                FindControl(RELOAD)->SetEnabled(canReload());
                UpdateText(RELOAD, profile && profile->status == Profiles::Status::Failed ? L"重试" : L"刷新");
                FindControl(LOGIN)->SetVisible(expired);
                FindControl(LOGIN)->SetEnabled(expired && Accounts::login() == Accounts::Login::Idle);
                UpdateText(LOGIN, Accounts::login() == Accounts::Login::Idle ? L"重新登录" : L"登录中…");
                std::vector<exui::Layout> actions{cell(STATUS), cell(RELOAD, 80)};
                if (expired) actions.push_back(cell(LOGIN, 96));
                setFooterActions(std::move(actions));
                for (uint32_t id : {UPLOAD, RESET_SKIN}) FindControl(id)->SetEnabled(usable);
                UpdateText(SKIN_INFO, !profile || profile->status != Profiles::Status::Ready ? L"读取资料后显示当前皮肤"
                           : profile->skinUrl.empty() ? L"当前使用默认皮肤"
                           : profile->slim ? L"当前皮肤：纤细模型" : L"当前皮肤：经典模型");
                updateCapes(profile, usable);
                updateOptiFine(profile, usable);
                if (!submittedName.empty() && profile && profile->status == Profiles::Status::Ready && profile->working.empty()) {
                    if (profile->name == submittedName && typedName() == submittedName) SetText(NEW_NAME, L"");
                    submittedName.clear();
                }
                UpdateText(CURRENT_NAME, Platform::widen(profile && !profile->name.empty() ? profile->name : account->name));
                updateName(profile, usable);
            }

            void OnSelection(uint32_t id, int index) override {
                if (id < FIRST_CONTROL) SectionedForm::OnSelection(id, index);
                else if (id == CAPES) refresh();
            }

            void sectionShown(int) override { if (FindControl(STATUS)) refresh(); }

            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) { SectionedForm::OnAction(id); return; }
                if (id == LOGIN) {
                    if (accountOf() && Accounts::expired(key) && Accounts::login() == Accounts::Login::Idle) Accounts::startLogin();
                } else if (id == RELOAD) {
                    if (canReload()) { Profiles::forgetName(key); needsReload = true; }
                } else if (ready()) {
                    if (id == UPLOAD) {
                        auto files = Platform::pickFiles(Platform::exeDirectory(), L"选择皮肤图片", L"PNG 图片", L"*.png");
                        if (!files.empty() && ready()) Profiles::uploadSkin(key, files.front());
                    } else if (id == RESET_SKIN) {
                        if (message(GetHwnd(), L"要为当前账号恢复默认皮肤吗？", L"恢复默认皮肤",
                                    exui::MsgBoxButtons::OkCancel, {L"取消", L"恢复"}) == 0 && ready()) Profiles::resetSkin(key);
                    } else if (id == APPLY_CAPE) {
                        if (auto cape = selectedCape(); cape && !cape->active) Profiles::setCape(key, cape->id);
                    } else if (id == QUERY_OPTIFINE) {
                        if (currentProfile()->optifine != Profiles::OptiFine::Checking) Profiles::checkOptiFine(key);
                    } else if (id == MANAGE_OPTIFINE) {
                        if (currentProfile()->optifine == Profiles::OptiFine::Owned) Profiles::manageOptiFine(key);
                    } else if (id == BUY_OPTIFINE) {
                        if (currentProfile()->optifine == Profiles::OptiFine::None) Platform::openUrl("https://optifine.net/donate");
                    } else if (id == CHECK_NAME) {
                        std::string name = typedName();
                        if (canCheck(name)) Profiles::checkName(key, name);
                    } else if (id == RENAME) {
                        std::string name = typedName(), previous = currentProfile()->name;
                        if (canRename(name) && message(GetHwnd(), L"把用户名从 “" + Platform::widen(previous) + L"” 改为 “" +
                                Platform::widen(name) + L"” 吗？\n改名后 30 天内不能再次修改。", L"修改用户名",
                                exui::MsgBoxButtons::OkCancel, {L"取消", L"修改"}) == 0) {
                            if (canRename(name) && typedName() == name && currentProfile()->name == previous) {
                                submittedName = name;
                                Profiles::rename(key, name);
                            } else {
                                Notices::add(Notices::Kind::Info, "资料状态已变化，请重新检查用户名后再修改", 4000);
                            }
                        }
                    }
                }
                refresh();
            }

        private:
            const std::string key;
            bool needsReload = true;
            std::string submittedName;
            Rows capeRows;
            std::vector<std::string> capeKeys;
            std::shared_ptr<const Pixels> capePixels, optifinePixels;

            const blb::auth::account* accountOf() const {
                const auto* account = Accounts::find(key);
                return account && account->type == blb::auth::account_type::microsoft ? account : nullptr;
            }
            const Profiles::Profile* currentProfile() const { return needsReload ? nullptr : &Profiles::of(key); }
            bool ready() const {
                const auto* profile = currentProfile();
                return accountOf() && !Accounts::expired(key) && !Accounts::busy(key) && profile &&
                        profile->status == Profiles::Status::Ready && profile->working.empty();
            }
            bool canReload() const {
                const auto* profile = currentProfile();
                return accountOf() && !Accounts::expired(key) && !Accounts::busy(key) &&
                        (!profile || (profile->status != Profiles::Status::Loading && profile->working.empty()));
            }
            std::string typedName() const { return trimmed(Text(NEW_NAME)); }
            bool canCheck(const std::string& name) const {
                const auto* profile = currentProfile();
                const auto& checked = Profiles::nameCheck(key);
                return ready() && profile->renameAllowed && Profiles::validName(name) && name != profile->name &&
                        !(checked.name == name && checked.state == Profiles::Check::Checking);
            }
            bool canRename(const std::string& name) const {
                const auto& checked = Profiles::nameCheck(key);
                return canCheck(name) && checked.name == name && checked.state == Profiles::Check::Available;
            }
            void place(uint32_t page, const exui::Layout& layout) {
                layout.Apply({15, 12, FindControl(page)->GetWidth() - 30, layout.Measure().Height});
            }
            std::optional<Profiles::Cape> selectedCape() const {
                const auto* profile = currentProfile();
                if (!profile || profile->status != Profiles::Status::Ready) return std::nullopt;
                std::string selected = selectedKey(Control<exui::ListView>(CAPES), capeKeys);
                if (selected == NO_CAPE) {
                    Profiles::Cape none;
                    none.active = std::none_of(profile->capes.begin(), profile->capes.end(), [](const auto& cape) { return cape.active; });
                    return none;
                }
                auto found = std::find_if(profile->capes.begin(), profile->capes.end(), [&](const auto& cape) { return selected == "cape:" + cape.id; });
                return found == profile->capes.end() ? std::nullopt : std::optional<Profiles::Cape>(*found);
            }
            void updateImage(uint32_t id, const std::string& url, const std::wstring& empty,
                             std::shared_ptr<const Pixels>& displayed) {
                auto pixels = url.empty() ? nullptr : Profiles::capeFront(url);
                if (pixels != displayed) {
                    displayed = pixels;
                    Control<exui::ImageView>(id).SetImage(pixels ? exui::BitmapFromRgba(pixels->width, pixels->height, pixels->rgba) : nullptr);
                }
                UpdateText(id, url.empty() ? empty : Profiles::capeFrontFailed(url) ? L"预览读取失败，点击上方刷新重试" : L"正在读取预览…");
            }
            void updateCapes(const Profiles::Profile* profile, bool usable) {
                Rows rows;
                std::vector<std::string> keys;
                int active = 0;
                if (profile && profile->status == Profiles::Status::Ready) {
                    rows.push_back({L"不使用披风", L""}); keys.push_back(NO_CAPE);
                    for (const auto& cape : profile->capes) {
                        if (cape.id.empty()) continue;
                        rows.push_back({Platform::widen(cape.alias.empty() ? "披风" : cape.alias), cape.active ? L"使用中" : L""});
                        keys.push_back("cape:" + cape.id);
                        if (cape.active) active = (int) keys.size() - 1;
                    }
                    if (active == 0) rows.front()[1] = L"使用中";
                }
                auto& list = Control<exui::ListView>(CAPES);
                // A reload keeps the existing selection until fresh cape IDs arrive.
                if (profile && profile->status == Profiles::Status::Ready)
                    fill(list, capeRows, capeKeys, std::move(rows), std::move(keys));
                if (list.GetSelectedRow() < 0 && !capeKeys.empty()) list.SetSelectedRow(active);
                auto selected = selectedCape();
                FindControl(APPLY_CAPE)->SetEnabled(usable && selected && !selected->active);
                UpdateText(APPLY_CAPE, selected && selected->id.empty() ? L"隐藏披风" : L"应用披风");
                if (FindControl(CAPE_PAGE)->IsVisible()) updateImage(CAPE_IMAGE, selected ? selected->url : "",
                        selected ? selected->id.empty() ? L"不使用披风" : L"暂无预览" : L"读取资料后选择披风", capePixels);
            }
            void updateOptiFine(const Profiles::Profile* profile, bool usable) {
                bool loaded = profile && profile->status == Profiles::Status::Ready;
                auto state = loaded ? profile->optifine : Profiles::OptiFine::Checking;
                std::wstring info = !loaded ? L"读取资料后查询 OptiFine 披风"
                        : state == Profiles::OptiFine::Checking ? L"正在查询 OptiFine 披风…"
                        : state == Profiles::OptiFine::Owned ? L"管理披风会在浏览器中打开 OptiFine 页面。"
                        : state == Profiles::OptiFine::None ? L"这个账号没有 OptiFine 披风。"
                                                         : L"查询失败，可以重新查询。";
                UpdateText(OPTIFINE_INFO, info);
                FindControl(QUERY_OPTIFINE)->SetEnabled(usable && state != Profiles::OptiFine::Checking);
                FindControl(MANAGE_OPTIFINE)->SetVisible(loaded && state == Profiles::OptiFine::Owned);
                FindControl(MANAGE_OPTIFINE)->SetEnabled(usable);
                FindControl(BUY_OPTIFINE)->SetVisible(loaded && state == Profiles::OptiFine::None);
                FindControl(BUY_OPTIFINE)->SetEnabled(usable);
                if (FindControl(OPTIFINE_PAGE)->IsVisible()) updateImage(OPTIFINE_IMAGE,
                        loaded && state == Profiles::OptiFine::Owned ? profile->optifineUrl : "",
                        loaded && state == Profiles::OptiFine::None ? L"尚未拥有" : L"暂无预览", optifinePixels);
            }
            void updateName(const Profiles::Profile* profile, bool usable) {
                std::string name = typedName();
                bool allowed = profile && profile->renameAllowed;
                FindControl(NEW_NAME)->SetEnabled(usable && allowed);
                FindControl(CHECK_NAME)->SetEnabled(canCheck(name));
                FindControl(RENAME)->SetEnabled(canRename(name));
                std::wstring note = L"3–16 位英文字母、数字或下划线；每 30 天可以修改一次。";
                Gdiplus::ARGB color = MUTED;
                if (profile && !profile->renameAllowed) {
                    note = profile->renameAfter.empty() ? L"最近修改过用户名，30 天内不能再次修改。"
                                                       : Platform::widen(profile->renameAfter) + L" 之后才能再次修改。";
                } else if (!name.empty() && !Profiles::validName(name)) {
                    note = L"用户名需要 3–16 位，只能使用英文字母、数字和下划线。"; color = DANGER;
                } else {
                    const auto& checked = Profiles::nameCheck(key);
                    if (!name.empty() && checked.name == name) {
                        switch (checked.state) {
                            case Profiles::Check::Checking: note = L"正在检查用户名…"; break;
                            case Profiles::Check::Available: note = L"用户名可以使用，点击“修改用户名”确认。"; color = SUCCESS; break;
                            case Profiles::Check::Taken: note = L"这个用户名已经有人使用。"; color = DANGER; break;
                            case Profiles::Check::Invalid: note = L"这个用户名不符合要求。"; color = DANGER; break;
                            case Profiles::Check::Failed: note = L"检查失败：" + Platform::widen(checked.error); color = DANGER; break;
                            default: break;
                        }
                    }
                }
                UpdateText(NAME_NOTE, note);
                if (FindControl(NAME_NOTE)->GetTextColor() != color) FindControl(NAME_NOTE)->SetTextColor(color);
                FindControl(NAME_NOTE)->SetTooltipText(note);
            }
        };
    }

    void showProfile(HWND parent, const std::string& accountKey) {
        ProfileWindow window(accountKey);
        if (window.create(parent)) runModal(window);
    }
}
