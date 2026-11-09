#include "app/system/Features.h"

#include "app/system/Config.h"
#include "app/system/Customization.h"

#include <algorithm>

namespace Features {

    namespace {
        bool showing = false;

        const std::vector<Info>& table() {
            static const std::vector<Info> list = {
                    {NEW_INSTANCE, "newInstance", "新建实例", GROUP_PAGES},
                    {DOWNLOADS, "downloads", "下载附加件", GROUP_PAGES},
                    {TRANSFERS, "transfers", "下载列表", GROUP_PAGES},
                    {ACCOUNTS, "accounts", "账户", GROUP_PAGES},
                    {MULTIPLAYER, "multiplayer", "联机", GROUP_PAGES},
                    {VERSIONS, "versions", "版本管理", GROUP_PAGES},
                    {INSTANCE_EDIT, "instanceEdit", "编辑实例", GROUP_ACTIONS},
                    {INSTANCE_DELETE, "instanceDelete", "删除实例", GROUP_ACTIONS},
                    {MOD_UPDATES, "modUpdates", "模组更新", GROUP_ACTIONS},
                    {TOOLBOX, "toolbox", "工具箱", GROUP_ACTIONS},
                    {JAVA, "java", "Java 管理", GROUP_ACTIONS},
                    {UPDATE_CHECK, "updateCheck", "启动器更新检查", GROUP_ACTIONS},
                    {THEMES, "themes", "主题管理", GROUP_ACTIONS},
                    {SECTION_LAUNCH, "section.launch", "全局启动项", GROUP_SETTINGS},
                    {SECTION_OPTIONS, "section.options", "全局游戏选项", GROUP_SETTINGS},
                    {SECTION_KEYS, "section.keys", "按键绑定", GROUP_SETTINGS},
                    {SECTION_PATCH, "section.patch", "补丁", GROUP_SETTINGS},
                    {SECTION_FOLDERS, "section.folders", "游戏目录", GROUP_SETTINGS},
                    {SECTION_LOOK, "section.look", "外观", GROUP_SETTINGS},
                    {SECTION_WINDOW, "section.window", "窗口", GROUP_SETTINGS},
                    {SECTION_HOME, "section.home", "主页", GROUP_SETTINGS},
                    {SECTION_NETWORK, "section.network", "下载与网络", GROUP_SETTINGS},
                    {SECTION_RESET, "section.reset", "引导与重置", GROUP_SETTINGS},
            };
            return list;
        }

        const char* keyOf(Feature feature) {
            for (const Info& info : table()) {
                if (info.id == feature) {
                    return info.key;
                }
            }
            return "";
        }

        bool listed(const nlohmann::json& list, const std::string& key) {
            return list.is_array() && std::any_of(list.begin(), list.end(), [&](const nlohmann::json& item) {
                       return item.is_string() && item.get<std::string>() == key;
                   });
        }

        bool packHides(Feature feature) {
            std::vector<std::string> hidden = Customization::hiddenFeatures();
            return std::find(hidden.begin(), hidden.end(), keyOf(feature)) != hidden.end();
        }

        bool userHides(Feature feature) {
            const nlohmann::json& doc = Config::data();
            auto it = doc.find("hiddenFeatures");
            if (it != doc.end() && it->is_array()) {
                return listed(*it, keyOf(feature));
            }
            return packHides(feature) && !Customization::featuresLocked();
        }
    }

    const std::vector<Info>& all() {
        return table();
    }

    bool locked(Feature feature) {
        return Customization::featuresLocked() && packHides(feature);
    }

    bool hidden(Feature feature) {
        return locked(feature) || userHides(feature);
    }

    bool enabled(Feature feature) {
        if (locked(feature)) {
            return false;
        }
        return showing || !userHides(feature);
    }

    void setHidden(Feature feature, bool hide) {
        if (locked(feature)) {
            return;
        }
        nlohmann::json list = nlohmann::json::array();
        for (const Info& info : table()) {
            bool now = info.id == feature ? hide : userHides(info.id);
            if (now) {
                list.push_back(info.key);
            }
        }
        Config::data()["hiddenFeatures"] = list;
        Config::save();
    }

    bool revealed() {
        return showing;
    }

    void toggleReveal() {
        showing = !showing;
    }

    bool anyHidden() {
        for (const Info& info : table()) {
            if (hidden(info.id)) {
                return true;
            }
        }
        return false;
    }
}
