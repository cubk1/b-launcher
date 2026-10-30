#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/InstancesWindow.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/VersionSettingsWindow.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        // 更新游戏's list and button row, as in the account manager: what to open on the left, what to change on the
        // right. The game directories are listed beside it, as the default interface groups the instances by them.
        class InstancesWindow : public LiveForm {
        public:
            enum : uint32_t {
                DIRECTORIES = 1,
                ADD_DIRECTORY,
                REMOVE_DIRECTORY,
                LIST,
                CREATE,
                SETTINGS,
                FOLDER,
                RENAME,
                COPY,
                REMOVE,
                CURRENT
            };

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {DIRECTORIES, ControlKind::List, {18, 35, 160, 360}},
                        {ADD_DIRECTORY, ControlKind::Button, {18, 402, 77, 35}, L"添加目录"},
                        {REMOVE_DIRECTORY, ControlKind::Button, {101, 402, 77, 35}, L"移除目录"},
                        {LIST, ControlKind::List, {186, 35, 514, 360}},
                        // Over the list of a 当前目录 without .minecraft.
                        {.id = CREATE,
                         .kind = ControlKind::Button,
                         .bounds = {378, 195, 130, 35},
                         .caption = L"新建 .minecraft",
                         .visible = false},
                        {SETTINGS, ControlKind::Button, {186, 402, 76, 35}, L"版本设置"},
                        {FOLDER, ControlKind::Button, {268, 402, 76, 35}, L"打开目录"},
                        {RENAME, ControlKind::Button, {350, 402, 76, 35}, L"重命名"},
                        {COPY, ControlKind::Button, {432, 402, 76, 35}, L"复制"},
                        {REMOVE, ControlKind::Button, {542, 402, 76, 35}, L"删除"},
                        {CURRENT, ControlKind::Button, {624, 402, 76, 35}, L"设为当前"},
                };
                if (!createDialog(*this, parent, L"实例管理", 720, 450)) {
                    return false;
                }
                AddControls(CONTROLS);
                auto& directories = Control<exui::ListView>(DIRECTORIES);
                directories.InsertColumn(L"游戏目录", 100);
                directories.InsertColumn(L"实例", 46);
                auto& list = Control<exui::ListView>(LIST);
                list.InsertColumn(L"名称", 170);
                list.InsertColumn(L"版本", 100);
                list.InsertColumn(L"加载器", 155);
                list.InsertColumn(L"状态", 75);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const std::vector<Instances::Source>& sources = Instances::sources();
                const Instances::Entry* current = Instances::find(Instances::selected());
                if (!chosen && current != nullptr) {
                    showDirectory(current->source);
                }
                if (Instances::source(directory) == nullptr && !sources.empty()) {
                    showDirectory(sources.front().key);
                }
                Rows rows;
                std::vector<std::string> keys;
                for (const Instances::Source& source : sources) {
                    int count = 0;
                    for (const Instances::Entry& entry : Instances::list()) {
                        count += entry.source == source.key ? 1 : 0;
                    }
                    rows.push_back({Platform::widen(source.label),
                                    Instances::loading() || !source.exists ? L"" : std::to_wstring(count)});
                    keys.push_back(source.key);
                }
                auto& directories = Control<exui::ListView>(DIRECTORIES);
                fill(directories, shownDirectories, directoryKeys, std::move(rows), std::move(keys));
                if (LiveForm::selectedKey(directories, directoryKeys) != directory) {
                    auto at = std::find(directoryKeys.begin(), directoryKeys.end(), directory);
                    directories.SetSelectedRow(at != directoryKeys.end() ? (int) (at - directoryKeys.begin()) : -1);
                }

                rows.clear();
                keys.clear();
                for (const Instances::Entry& entry : Instances::list()) {
                    if (entry.source != directory) {
                        continue;
                    }
                    std::string loaders;
                    for (const auto& loader : entry.info.loaders) {
                        loaders += (loaders.empty() ? "" : "、") + Installer::loaderName(loader.type) +
                                (loader.version.empty() ? "" : " " + loader.version);
                    }
                    rows.push_back({Platform::widen(entry.info.id), Platform::widen(entry.info.minecraft_version),
                                    Platform::widen(loaders), statusOf(entry.key)});
                    keys.push_back(entry.key);
                }
                // Like the default interface, only 当前目录 offers to create its .minecraft.
                const Instances::Source* source = Instances::source(directory);
                bool missing = source != nullptr && !source->exists && directory != created && !Instances::loading();
                if (rows.empty()) {
                    rows.push_back({Instances::loading() ? L"正在读取…"
                                            : missing    ? L"没有找到 .minecraft"
                                                         : L"没有实例",
                                    L"", L"", L""});
                    keys.emplace_back();
                }
                fill(Control<exui::ListView>(LIST), shown, shownKeys, std::move(rows), std::move(keys));
                FindControl(CREATE)->SetVisible(missing && directory == "local");

                // An instance being copied, renamed or removed is left alone until that is done; the others wait for
                // it only to be copied, renamed or removed in turn, and one in play is not changed under the game.
                std::string key = selectedKey();
                bool usable = !key.empty() && Instances::activityOf(key).empty();
                bool free = usable && Instances::activity().empty() &&
                        (Game::state() == Game::State::Idle || Game::instance() != key);
                FindControl(SETTINGS)->SetEnabled(usable);
                // The instance's folder, or the directory's while no instance is chosen.
                FindControl(FOLDER)->SetEnabled(usable || (key.empty() && source != nullptr && source->exists));
                FindControl(RENAME)->SetEnabled(free);
                FindControl(COPY)->SetEnabled(free);
                FindControl(REMOVE)->SetEnabled(free);
                FindControl(CURRENT)->SetEnabled(usable && key != Instances::selected());
                // A directory goes with its instances, so not while one of them is worked on or played.
                bool playing = Game::state() != Game::State::Idle && Instances::find(Game::instance()) != nullptr &&
                        Instances::find(Game::instance())->source == directory;
                FindControl(REMOVE_DIRECTORY)
                        ->SetEnabled(source != nullptr && source->custom && Instances::activity().empty() && !playing);
            }

            // The list is rebuilt whenever the instances are scanned again, every few seconds, so no entry is kept
            // across a dialog: what one needs afterwards is copied before it opens.
            void OnAction(uint32_t id) override {
                switch (id) {
                    case ADD_DIRECTORY: addDirectory(); break;
                    case REMOVE_DIRECTORY: removeDirectory(); break;
                    case CREATE: createMinecraft(); break;
                }
                std::string key = selectedKey();
                const Instances::Entry* entry = Instances::find(key);
                if (id == FOLDER && entry == nullptr) {
                    if (const Instances::Source* source = Instances::source(directory)) {
                        Platform::open(source->root);
                    }
                }
                if (entry != nullptr) {
                    switch (id) {
                        case SETTINGS: showVersionSettings(GetHwnd(), key); break;
                        case FOLDER: Platform::open(entry->gameDirectory); break;
                        case RENAME: rename(*entry); break;
                        case COPY: copy(*entry); break;
                        case REMOVE: remove(key, Platform::widen(entry->info.id)); break;
                        case CURRENT: Instances::select(key); break;
                    }
                }
                refresh();
            }

            void OnSelection(uint32_t id, int) override {
                if (id == DIRECTORIES) {
                    std::string picked = LiveForm::selectedKey(Control<exui::ListView>(DIRECTORIES), directoryKeys);
                    if (!picked.empty()) {
                        chooseDirectory(picked);
                    }
                }
                refresh();
            }

        private:
            // The game directory whose instances are listed, and whether it was chosen here; until it is, the
            // directory of the current instance is, once the instances are read.
            std::string directory;
            bool chosen = false;
            // The directory whose .minecraft was made here, which the instances show only once they are read again.
            std::string created;
            Rows shownDirectories, shown;
            // The directory and the instance of each row; empty for the row that says there is none.
            std::vector<std::string> directoryKeys, shownKeys;

            // Lists the instances of another directory from the top.
            void showDirectory(const std::string& key) {
                if (key != directory) {
                    directory = key;
                    shown.clear();
                    shownKeys.clear();
                }
            }

            void chooseDirectory(const std::string& key) {
                chosen = true;
                showDirectory(key);
            }

            // 新建 .minecraft goes once the folder is made, so that it is not pressed again meanwhile; it stays when
            // that failed.
            void createMinecraft() {
                const Instances::Source* source = Instances::source(directory);
                if (source == nullptr) {
                    return;
                }
                std::filesystem::path root = source->root;
                Instances::createFolder(directory);
                std::error_code error;
                if (std::filesystem::is_directory(root, error)) {
                    created = directory;
                }
            }

            void addDirectory() {
                std::optional<std::filesystem::path> picked = Platform::pickFolder(Platform::exeDirectory());
                if (!picked) {
                    return;
                }
                if (std::string problem = Instances::addFolder(*picked); !problem.empty()) {
                    message(GetHwnd(), Platform::widen(problem), L"添加目录");
                    return;
                }
                chooseDirectory(Instances::sources().back().key);
            }

            // Asks first; the directory's neighbour in the list as it is after the question is listed instead.
            void removeDirectory() {
                const Instances::Source* source = Instances::source(directory);
                if (source == nullptr || !source->custom) {
                    return;
                }
                std::string key = source->key;
                if (message(GetHwnd(),
                            L"要移除游戏目录 " + Platform::widen(source->label) + L" 吗？文件夹里的文件不会被删除。",
                            L"实例管理", exui::MsgBoxButtons::OkCancel, {L"取消", L"移除"}) != 0) {
                    return;
                }
                const std::vector<Instances::Source>& sources = Instances::sources();
                auto at = std::find_if(sources.begin(), sources.end(),
                                       [&](const Instances::Source& each) { return each.key == key; });
                if (at == sources.end()) {
                    return;
                }
                std::string neighbour;
                if (at + 1 != sources.end()) {
                    neighbour = (at + 1)->key;
                } else if (at != sources.begin()) {
                    neighbour = (at - 1)->key;
                }
                Instances::removeFolder(key);
                chooseDirectory(neighbour);
            }

            std::string selectedKey() const {
                return LiveForm::selectedKey(Control<exui::ListView>(LIST), shownKeys);
            }

            static std::wstring statusOf(const std::string& key) {
                if (std::string activity = Instances::activityOf(key); !activity.empty()) {
                    return Platform::widen(activity);
                }
                if (Game::state() != Game::State::Idle && Game::instance() == key) {
                    return L"运行中";
                }
                return key == Instances::selected() ? L"当前" : L"";
            }

            void rename(const Instances::Entry& entry) {
                std::string key = entry.key;
                std::string source = entry.source;
                std::string id = entry.info.id;
                askName(GetHwnd(), L"重命名", L"输入新名字", Platform::widen(id),
                        [key, source, id](const std::wstring& typed) {
                            std::string to = trimmed(typed);
                            if (to == id) {
                                return std::wstring();
                            }
                            std::string problem = Instances::checkId(source, to);
                            if (problem.empty()) {
                                Instances::rename(key, to);
                            }
                            return Platform::widen(problem);
                        });
            }

            // Like the default interface: the copy gets a free name and becomes the current instance.
            static void copy(const Instances::Entry& entry) {
                std::string target = Instances::freeId(entry.source, entry.info.id + "-copy");
                std::string targetKey = Instances::keyOf(entry.source, target);
                Instances::duplicate(entry.key, target, [targetKey](const std::string& error) {
                    if (error.empty()) {
                        Instances::select(targetKey);
                    }
                });
            }

            // Asks first; a current instance hands over to its neighbour in the list as it is after the question.
            void remove(const std::string& key, const std::wstring& name) {
                if (message(GetHwnd(), L"要删除实例 " + name + L" 吗？", L"实例管理", exui::MsgBoxButtons::OkCancel,
                            {L"取消", L"删除"}) != 0) {
                    return;
                }
                const std::vector<Instances::Entry>& list = Instances::list();
                auto at = std::find_if(list.begin(), list.end(),
                                       [&](const Instances::Entry& entry) { return entry.key == key; });
                if (at == list.end()) {
                    return;
                }
                std::string neighbour;
                if (at + 1 != list.end()) {
                    neighbour = (at + 1)->key;
                } else if (at != list.begin()) {
                    neighbour = (at - 1)->key;
                }
                bool current = Instances::selected() == key;
                Instances::remove(key, [neighbour, current](const std::string& error) {
                    if (error.empty() && current) {
                        Instances::select(neighbour);
                    }
                });
            }
        };
    }

    void showInstances(HWND parent) {
        InstancesWindow window;
        if (window.create(parent)) {
            runModal(window);
        }
    }
}
