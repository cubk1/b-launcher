#include "ui/imgui/screens/home/Homepage.h"
#include "app/accounts/Accounts.h"
#include "app/system/About.h"
#include "app/system/Config.h"
#include "app/launch/Game.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Platform.h"
#include "app/system/Server.h"
#include "ui/imgui/screens/home/WebImages.h"
#include "ui/imgui/screens/home/XamlView.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Homepage {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = std::function<void()>;

        struct Preset {
            const char* name;
            const char* url;
        };

        const Preset PRESETS[] = {
                {"Minecraft 新闻", "https://news.bugjump.net/"},
                {"每日整合包推荐", "https://pclsub.sodamc.com/"},
                {"Minecraft 皮肤推荐", "https://forgepixel.com/pcl_sub_file"},
                {"整合包推荐引擎", "https://qawsedrftgyhujiko.fun/pcl2/Custom.xaml"},
        };
        constexpr int PRESET_COUNT = (int) (sizeof(PRESETS) / sizeof(PRESETS[0]));
        const char* const NONE_CHOICE = "无";
        const char* const ANNOUNCEMENT_CHOICE = "公告";
        const std::string ANNOUNCEMENT_URL = Server::file("announcement.xaml");
        const char* const FILE_CHOICE = "本地文件";
        const char* const URL_CHOICE = "自定义网址";

        int preset() {
            std::string name = Config::data().value("homepagePreset", std::string());
            for (int i = 0; i < PRESET_COUNT; i++) {
                if (name == PRESETS[i].name) {
                    return i;
                }
            }
            return 0;
        }
        constexpr float HEADER_H = 36.0f;
        constexpr float TOP_PAD = 12.0f;
        constexpr float TOP_GAP = 10.0f;
        constexpr float QUICK_W = 360.0f;
        constexpr float QUICK_GAP = 28.0f;
        constexpr float LOGO_MIN = 96.0f;
        constexpr double URL_SETTLE = 0.8;

        struct View {
            std::string id;
            std::string title;
            std::string location;
            std::string text;
            std::string error;
            bool loading = false;
            std::unique_ptr<XamlView::Document> doc;
        };

        struct Modal {
            bool open = false;
            std::string title;
            std::string text;
            std::string confirm;
            std::function<void()> action;
        };

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;

        std::vector<std::unique_ptr<View>> views;
        bool started = false;
        int serial = 0;
        int homeRequest = 0;
        double reloadAt = -1.0;
        std::map<std::string, std::string> variables;
        bool variablesLoaded = false;
        Modal modal;

        void perform(const std::vector<XamlView::Action>& actions);

        void notify(const std::string& text, bool error) {
            Notifications::add(error ? Notifications::FAILED : Notifications::INFO, text, error ? 5000 : 2500);
        }

        void run(std::function<Continuation(blb::cancellation_token)> work) {
            blb::cancellation_token token = stopping.token();
            workers.emplace_back([work, token] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    std::string text = error.what();
                    next = [text] { notify("主页出错：" + text, true); };
                }
                if (next) {
                    std::lock_guard<std::mutex> guard(lock);
                    finished.push_back(std::move(next));
                }
            });
        }

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return text;
        }

        bool remote(const std::string& address) {
            std::string head = lower(address.substr(0, 8));
            return head.rfind("http://", 0) == 0 || head.rfind("https://", 0) == 0;
        }

        std::filesystem::path cacheFile(const std::string& address) {
            std::filesystem::path cache = Platform::cacheDirectory();
            if (cache.empty()) {
                return {};
            }
            return cache / "homepage" / (blb::to_hex(blb::sha1::digest(address)) + ".xaml");
        }

        std::optional<std::string> fetch(const std::string& address, blb::cancellation_token token,
                                         std::string& error) {
            blb::net::http_request request;
            request.url = address;
            request.headers.push_back({"User-Agent", Platform::USER_AGENT});
            request.timeout = std::chrono::seconds(30);
            auto response = blb::net::http_send(request, token);
            if (!response) {
                error = response.err().message;
                return std::nullopt;
            }
            if (!response->ok()) {
                error = "HTTP " + std::to_string(response->status);
                return std::nullopt;
            }
            return std::move(response->body);
        }

        void loadVariables() {
            if (variablesLoaded) {
                return;
            }
            variablesLoaded = true;
            const nlohmann::json& doc = Config::data();
            if (auto it = doc.find("homepageVariables"); it != doc.end() && it->is_object()) {
                for (auto entry = it->begin(); entry != it->end(); ++entry) {
                    if (entry.value().is_string()) {
                        variables[entry.key()] = entry.value().get<std::string>();
                    }
                }
            }
        }

        void saveVariables() {
            Config::data()["homepageVariables"] = variables;
            Config::save();
        }

        std::string escape(const std::string& value) {
            std::string out;
            for (char c : value) {
                switch (c) {
                    case '&': out += "&amp;"; break;
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '"': out += "&quot;"; break;
                    case '\'': out += "&apos;"; break;
                    default: out += c; break;
                }
            }
            return out;
        }

        std::optional<std::string> mark(const std::string& inner) {
            if (inner.rfind("variable:", 0) == 0) {
                std::string rest = inner.substr(9);
                size_t colon = rest.find(':');
                std::string name = rest.substr(0, colon);
                std::string fallback = colon == std::string::npos ? std::string() : rest.substr(colon + 1);
                auto found = variables.find(name);
                return found != variables.end() ? found->second : fallback;
            }
            if (inner == "user") {
                const blb::auth::account* account = Accounts::current();
                return account != nullptr ? account->name : std::string();
            }
            if (inner == "cur_version" || inner == "version") {
                const Instances::Entry* entry = Instances::find(Instances::selected());
                return entry != nullptr ? entry->info.id : std::string();
            }
            if (inner == "date" || inner == "time") {
                std::time_t now = std::time(nullptr);
                std::tm local{};
                localtime_s(&local, &now);
                char text[32];
                std::strftime(text, sizeof(text), inner == "date" ? "%Y/%m/%d" : "%H:%M:%S", &local);
                return std::string(text);
            }
            if (inner == "path") {
                return blb::path_to_utf8(Platform::exeDirectory());
            }
            return std::nullopt;
        }

        std::string replaceMarks(const std::string& text) {
            std::string out;
            out.reserve(text.size());
            size_t i = 0;
            while (i < text.size()) {
                if (text[i] == '{') {
                    size_t end = text.find('}', i);
                    if (end != std::string::npos && end - i < 256) {
                        if (std::optional<std::string> value = mark(text.substr(i + 1, end - i - 1))) {
                            out += escape(*value);
                            i = end + 1;
                            continue;
                        }
                    }
                }
                out += text[i++];
            }
            return out;
        }

        void rebuild(View& view) {
            auto doc = std::make_unique<XamlView::Document>();
            view.error.clear();
            if (!doc->load(replaceMarks(view.text), view.id + "#" + std::to_string(++serial))) {
                view.error = (view.title.empty() ? "主页内容有误：" : "页面内容有误：") + doc->error();
                view.doc.reset();
                return;
            }
            doc->onActions = [](const std::vector<XamlView::Action>& actions) { perform(actions); };
            view.doc = std::move(doc);
        }

        View& home() {
            if (views.empty()) {
                views.push_back(std::make_unique<View>());
                views.front()->id = "home";
            }
            return *views.front();
        }

        void loadHome() {
            View& view = home();
            view.error.clear();
            view.loading = false;
            int request = ++homeRequest;
            int kind = source();
            if (kind == SOURCE_NONE) {
                view.text.clear();
                view.location.clear();
                view.doc.reset();
                return;
            }
            if (kind == SOURCE_FILE) {
                std::string path = file();
                view.location = path;
                if (path.empty()) {
                    view.doc.reset();
                    view.error = "还没有选择本地主页文件";
                    return;
                }
                auto data = blb::fs::read_file(blb::path_from_utf8(path));
                if (!data) {
                    view.doc.reset();
                    view.error = "无法读取 " + path;
                    return;
                }
                view.text = std::move(*data);
                rebuild(view);
                return;
            }
            std::string address = kind == SOURCE_PRESET ? std::string(PRESETS[preset()].url)
                    : kind == SOURCE_ANNOUNCEMENT       ? std::string(ANNOUNCEMENT_URL)
                                                        : Ui::trim(url());
            if (address != view.location) {
                view.doc.reset();
                view.text.clear();
            }
            view.location = address;
            if (address.empty()) {
                view.doc.reset();
                view.error = "还没有填写主页网址";
                return;
            }
            std::filesystem::path cached = cacheFile(address);
            if (!view.doc && !cached.empty()) {
                if (auto data = blb::fs::read_file(cached)) {
                    view.text = std::move(*data);
                    rebuild(view);
                }
            }
            view.loading = true;
            run([address, cached, request](blb::cancellation_token token) -> Continuation {
                std::string error;
                std::optional<std::string> data = fetch(address, token, error);
                if (data && !cached.empty() && blb::fs::create_directories(cached.parent_path())) {
                    (void) blb::fs::write_file_atomic(cached, *data);
                }
                return [data, error, request] {
                    if (request != homeRequest || views.empty()) {
                        return;
                    }
                    View& view = *views.front();
                    view.loading = false;
                    if (!data) {
                        if (!view.doc) {
                            view.error = "主页加载失败：" + error;
                        }
                        return;
                    }
                    if (*data != view.text || !view.doc) {
                        view.text = *data;
                        rebuild(view);
                    }
                };
            });
        }

        std::string pageUrl(const std::string& json) {
            size_t query = json.find('?');
            std::string path = json.substr(0, query);
            std::string rest = query == std::string::npos ? std::string() : json.substr(query);
            if (lower(path).size() > 5 && lower(path).compare(path.size() - 5, 5, ".json") == 0) {
                path = path.substr(0, path.size() - 5) + ".xaml";
            }
            return path + rest;
        }

        void openHelp(const std::string& target) {
            std::string address = Ui::trim(target);
            if (!remote(address)) {
                notify("暂不支持 PCL 内置的帮助页面：" + address, false);
                return;
            }
            auto view = std::make_unique<View>();
            view->id = "help" + std::to_string(++serial);
            view->title = "正在加载";
            view->location = address;
            view->loading = true;
            std::string id = view->id;
            views.push_back(std::move(view));
            std::string path = lower(address.substr(0, address.find('?')));
            bool json = path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0;
            run([address, id, json](blb::cancellation_token token) -> Continuation {
                std::string error;
                std::string title;
                std::vector<XamlView::Action> redirect;
                std::string page = address;
                if (json) {
                    if (std::optional<std::string> meta = fetch(address, token, error)) {
                        nlohmann::json doc = nlohmann::json::parse(*meta, nullptr, false);
                        if (doc.is_object()) {
                            title = doc.value("Title", std::string());
                            if (doc.value("IsEvent", false)) {
                                redirect.push_back(XamlView::Action{doc.value("EventType", std::string()),
                                                                    doc.value("EventData", std::string())});
                            }
                        }
                    }
                    page = pageUrl(address);
                }
                std::optional<std::string> data;
                if (redirect.empty()) {
                    data = fetch(page, token, error);
                }
                return [id, title, data, error, redirect] {
                    for (size_t i = 0; i < views.size(); i++) {
                        if (views[i]->id != id) {
                            continue;
                        }
                        if (!redirect.empty()) {
                            views.erase(views.begin() + (long long) i);
                            perform(redirect);
                            return;
                        }
                        View& view = *views[i];
                        view.loading = false;
                        view.title = title.empty() ? std::string("帮助") : title;
                        if (!data) {
                            view.error = "页面加载失败：" + error;
                            return;
                        }
                        view.text = *data;
                        rebuild(view);
                        return;
                    }
                };
            });
        }

        void showModal(const std::string& title, const std::string& text, const std::string& confirm,
                       std::function<void()> action) {
            modal.open = true;
            modal.title = title;
            modal.text = text;
            modal.confirm = confirm;
            modal.action = std::move(action);
        }

        void launch(const std::string& data) {
            std::string version = Ui::trim(data.substr(0, data.find('|')));
            if (Game::state() != Game::State::Idle) {
                notify("游戏正在启动或运行中", false);
                return;
            }
            const Instances::Entry* chosen = nullptr;
            const Instances::Entry* current = Instances::find(Instances::selected());
            if (version.empty() || (current != nullptr && current->info.id == version)) {
                chosen = current;
            }
            if (chosen == nullptr) {
                for (const Instances::Entry& entry : Instances::list()) {
                    if (entry.info.id == version) {
                        chosen = &entry;
                        break;
                    }
                }
            }
            if (chosen == nullptr) {
                notify("没有找到版本 " + version + "，请先安装它", true);
                return;
            }
            Instances::select(chosen->key);
            Game::launch(chosen->key);
        }

        void download(const std::string& data) {
            size_t bar = data.find('|');
            std::string address = Ui::trim(data.substr(0, bar));
            std::string name = bar == std::string::npos ? std::string() : Ui::trim(data.substr(bar + 1));
            if (name.empty()) {
                std::string path = address.substr(0, address.find('?'));
                size_t slash = path.find_last_of('/');
                name = slash == std::string::npos ? path : path.substr(slash + 1);
            }
            if (address.empty()) {
                return;
            }
            if (name.empty()) {
                name = "download";
            }
            std::optional<std::filesystem::path> target =
                    Platform::pickSaveFile(Platform::exeDirectory() / blb::path_from_utf8(name), L"保存文件");
            if (!target) {
                return;
            }
            notify("开始下载 " + name, false);
            std::filesystem::path path = *target;
            run([address, path](blb::cancellation_token token) -> Continuation {
                std::string error;
                std::optional<std::string> bytes = fetch(address, token, error);
                bool ok = bytes && blb::fs::write_file_atomic(path, *bytes);
                if (bytes && !ok) {
                    error = "无法写入文件";
                }
                std::string shown = Platform::display(path);
                return [ok, error, shown] { notify(ok ? "已保存到 " + shown : "下载失败：" + error, !ok); };
            });
        }

        void runFile(const std::string& data) {
            size_t bar = data.find('|');
            std::string path = Ui::trim(data.substr(0, bar));
            std::string args = bar == std::string::npos ? std::string() : Ui::trim(data.substr(bar + 1));
            if (path.empty()) {
                return;
            }
            showModal("运行程序", "主页请求运行：\n" + path + (args.empty() ? std::string() : " " + args), "运行",
                      [path, args] {
                          std::wstring file = blb::path_from_utf8(path).wstring();
                          std::wstring parameters = blb::path_from_utf8(args).wstring();
                          ShellExecuteW(nullptr, L"open", file.c_str(),
                                        parameters.empty() ? nullptr : parameters.c_str(), nullptr, SW_SHOWNORMAL);
                      });
        }

        std::string unescape(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); i++) {
                if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
                    out += '\n';
                    i++;
                } else {
                    out += text[i];
                }
            }
            return out;
        }

        void perform(const std::vector<XamlView::Action>& actions) {
            bool refresh = false;
            for (const XamlView::Action& action : actions) {
                std::string type = Ui::trim(action.type);
                const std::string& data = action.data;
                if (type == "打开网页") {
                    Platform::openUrl(Ui::trim(data));
                } else if (type == "复制文本") {
                    UiInput::setClipboard(data);
                    notify("已复制到剪贴板", false);
                } else if (type == "弹出窗口") {
                    size_t bar = data.find('|');
                    std::string title = bar == std::string::npos ? std::string("提示") : data.substr(0, bar);
                    std::string text = bar == std::string::npos ? data : data.substr(bar + 1);
                    showModal(unescape(title), unescape(text), std::string(), nullptr);
                } else if (type == "刷新主页") {
                    if (views.size() > 1) {
                        refresh = true;
                    } else {
                        loadHome();
                    }
                } else if (type == "刷新页面" || type == "刷新帮助") {
                    refresh = true;
                } else if (type == "修改变量" || type == "写入变量") {
                    size_t bar = data.find('|');
                    std::string name = Ui::trim(data.substr(0, bar));
                    if (!name.empty()) {
                        std::string rest = bar == std::string::npos ? std::string() : data.substr(bar + 1);
                        variables[name] = rest.substr(0, rest.find('|'));
                        saveVariables();
                    }
                } else if (type == "启动游戏") {
                    launch(data);
                } else if (type == "下载文件") {
                    download(data);
                } else if (type == "打开文件" || type == "执行命令") {
                    runFile(data);
                } else if (type == "打开帮助") {
                    openHelp(data);
                } else if (type == "切换页面") {
                    std::string page = Ui::trim(data.substr(0, data.find('|')));
                    if (page == "1") {
                        MainWindow::showDownloads();
                    }
                } else if (type == "内存优化" || type == "清理垃圾") {
                    notify(std::string(About::NAME) + " 没有" + type + "功能", false);
                } else if (!type.empty()) {
                    notify("暂不支持主页事件：" + type, false);
                }
            }
            if (refresh && !views.empty() && !views.back()->text.empty()) {
                rebuild(*views.back());
            }
        }

        void drawModal(bool blocked) {
            float panelX = MainWindow::sidebarWidth();
            float panelW = MainWindow::width() - panelX;
            UiRender::roundRect(panelX, 0.0f, panelW, MainWindow::height(), 14.0f, 0x66000000u, 1 | 4);
            float w = std::min(420.0f, panelW - 48.0f);
            std::vector<std::string> lines;
            size_t start = 0;
            while (start <= modal.text.size()) {
                size_t end = modal.text.find('\n', start);
                std::string line = modal.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                std::vector<std::string> wrapped = UiFont::wrap(line, UiFont::MEDIUM, UiWidgets::TEXT_BODY, w - 40.0f);
                if (wrapped.empty()) {
                    wrapped.push_back(std::string());
                }
                lines.insert(lines.end(), wrapped.begin(), wrapped.end());
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            const float lineH = 21.0f;
            float h = 20.0f + 24.0f + 8.0f + lines.size() * lineH + 18.0f + Page::FIELD_H + 18.0f;
            float x = panelX + (panelW - w) * 0.5f;
            float y = std::max(20.0f, (MainWindow::height() - h) * 0.5f);
            UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::surface());
            UiRender::outline(x, y, w, h, 12.0f, UiTheme::outlineSoft());
            UiRender::textY(modal.title, x + 20.0f, y + 20.0f, 24.0f, UiTheme::textPrimary(), UiFont::BOLD, 15.0f);
            float ty = y + 20.0f + 24.0f + 8.0f;
            for (const std::string& line : lines) {
                UiRender::textY(line, x + 20.0f, ty, lineH, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_BODY);
                ty += lineH;
            }
            float by = y + h - 18.0f - Page::FIELD_H;
            float right = x + w - 20.0f;
            bool close = false;
            if (modal.action) {
                float confirmW = std::max(72.0f, Page::buttonWidth(modal.confirm));
                float cancelW = std::max(72.0f, Page::buttonWidth("取消"));
                if (Page::button("homepage#modal#confirm", modal.confirm, right - confirmW, by, confirmW, true,
                                 blocked)) {
                    std::function<void()> action = modal.action;
                    close = true;
                    action();
                }
                if (Page::button("homepage#modal#cancel", "取消", right - confirmW - 8.0f - cancelW, by, cancelW, false,
                                 blocked)) {
                    close = true;
                }
            } else {
                float okW = std::max(72.0f, Page::buttonWidth("确定"));
                if (Page::button("homepage#modal#ok", "确定", right - okW, by, okW, true, blocked)) {
                    close = true;
                }
            }
            if (!blocked && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                close = true;
            }
            if (close) {
                modal = Modal();
            }
        }

        bool backButton(float x, float y, bool blocked) {
            const float size = 28.0f;
            bool hovered = !blocked && NewUi::hit(x, y, size, size);
            float lit = UiState::motion("homepage#back", hovered ? 1.0f : 0.0f, 18.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, size, size, 8.0f, UiTheme::overlay(Ui::round(14 * lit)));
            }
            UiWidgets::icon(Ui::glyph(0xf053), x + size * 0.5f, y + size * 0.5f,
                            UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), lit), 11.0f);
            return !blocked && NewUi::click(x, y, size, size);
        }
    }

    int source() {
        const nlohmann::json& doc = Config::data();
        std::string value = doc.value("homepage", std::string("none"));
        if (value == "none") {
            return SOURCE_NONE;
        }
        if (value == "file") {
            return SOURCE_FILE;
        }
        if (value == "url") {
            return SOURCE_URL;
        }
        if (value == "announcement") {
            return SOURCE_ANNOUNCEMENT;
        }
        return SOURCE_PRESET;
    }

    std::vector<std::string> choices() {
        std::vector<std::string> out = {NONE_CHOICE, ANNOUNCEMENT_CHOICE};
        for (const Preset& item : PRESETS) {
            out.push_back(item.name);
        }
        out.push_back(FILE_CHOICE);
        out.push_back(URL_CHOICE);
        return out;
    }

    std::string choice() {
        switch (source()) {
            case SOURCE_NONE: return NONE_CHOICE;
            case SOURCE_ANNOUNCEMENT: return ANNOUNCEMENT_CHOICE;
            case SOURCE_FILE: return FILE_CHOICE;
            case SOURCE_URL: return URL_CHOICE;
            default: return PRESETS[preset()].name;
        }
    }

    void choose(const std::string& name) {
        nlohmann::json& doc = Config::data();
        if (name == NONE_CHOICE) {
            doc["homepage"] = "none";
        } else if (name == ANNOUNCEMENT_CHOICE) {
            doc["homepage"] = "announcement";
        } else if (name == FILE_CHOICE) {
            doc["homepage"] = "file";
        } else if (name == URL_CHOICE) {
            doc["homepage"] = "url";
        } else {
            doc["homepage"] = "preset";
            doc["homepagePreset"] = name;
        }
        Config::save();
        reload();
    }

    std::string url() {
        return Config::data().value("homepageUrl", std::string());
    }

    void setUrl(const std::string& value) {
        if (value == url()) {
            return;
        }
        Config::data()["homepageUrl"] = value;
        Config::save();
        if (source() == SOURCE_URL) {
            reloadAt = ImGui::GetTime() + URL_SETTLE;
        }
    }

    std::string file() {
        return Config::data().value("homepageFile", std::string());
    }

    void setFile(const std::string& value) {
        Config::data()["homepageFile"] = value;
        Config::save();
        reload();
    }

    void reload() {
        reloadAt = -1.0;
        if (!started) {
            return;
        }
        views.resize(std::min<size_t>(views.size(), 1));
        modal = Modal();
        loadHome();
    }

    void draw(float left, float top, float right, float bottom, float reserve, bool blocked) {
        if (!started) {
            started = true;
            loadVariables();
            loadHome();
        }
        if (reloadAt >= 0.0 && ImGui::GetTime() >= reloadAt) {
            reload();
        }
        bool announcement = source() == SOURCE_ANNOUNCEMENT;
        if (announcement || (views.size() <= 1 && source() == SOURCE_NONE)) {
            float panelLeft = MainWindow::sidebarWidth();
            float panelRight = MainWindow::width();
            float listH = announcement ? 0.0f : HomeWidgets::height();
            float gap = listH > 0.0f ? QUICK_GAP : 0.0f;
            float cx = (panelLeft + panelRight) * 0.5f;
            bool covered = blocked || modal.open;
            if (listH > 0.0f && bottom - top - listH - gap < LOGO_MIN + TOP_PAD) {
                Page::Scroll body("homepage#widgets", top, bottom, listH + 2.0f * TOP_PAD, left, right - left);
                bool outside = body.begin();
                HomeWidgets::draw(cx - QUICK_W * 0.5f, body.top - body.offset + TOP_PAD, QUICK_W, covered || outside);
                body.end();
            } else {
                float size = std::clamp(std::min(panelRight - panelLeft, bottom - top - listH - gap) * 0.46f, LOGO_MIN,
                                        360.0f);
                float logoY = (top + bottom - size - gap - listH) * 0.5f - 8.0f;
                float faint = Appearance::backgroundImage() ? (UiTheme::isLight() ? 0.34f : 0.28f)
                                                            : (UiTheme::isLight() ? 0.12f : 0.09f);
                Logo::draw(cx - size * 0.5f, logoY, size, faint);
                if (listH > 0.0f) {
                    HomeWidgets::draw(cx - QUICK_W * 0.5f, logoY + size + gap, QUICK_W, covered);
                }
            }
            if (!announcement) {
                if (modal.open) {
                    drawModal(blocked);
                }
                return;
            }
        }
        if (views.empty()) {
            return;
        }
        bool covered = blocked || modal.open;
        View& view = *views.back();
        float contentTop = top + TOP_GAP;
        if (views.size() > 1) {
            if (backButton(left - 4.0f, top + 4.0f, covered)) {
                views.pop_back();
                return;
            }
            std::string title = UiFont::fitEnd(view.title, UiFont::MEDIUM, UiWidgets::TEXT_TITLE,
                                               std::max(0.0f, right - left - 40.0f));
            UiRender::textY(title, left + 32.0f, top + 4.0f, 28.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_TITLE);
            contentTop = top + HEADER_H + TOP_GAP;
        }
        float width = right - left;
        if (view.doc) {
            view.doc->glass = announcement;
            float contentH = view.doc->measure(width);
            Page::Scroll body("homepage#" + view.id, contentTop, bottom, contentH + TOP_PAD + reserve, left, width);
            bool outside = body.begin();
            view.doc->draw(left, body.top - body.offset + TOP_PAD, body.top, body.bottom, covered || outside);
            body.end();
        } else if (view.loading) {
            Page::spinner(left + width * 0.5f, (contentTop + bottom - reserve) * 0.5f, 10.0f, UiTheme::accent());
        } else if (!view.error.empty()) {
            float middle = (contentTop + bottom - reserve) * 0.5f;
            std::vector<std::string> lines =
                    UiFont::wrap(view.error, UiFont::MEDIUM, UiWidgets::TEXT_BODY, std::max(40.0f, width - 40.0f));
            float y = middle - lines.size() * 11.0f - 18.0f;
            for (const std::string& line : lines) {
                float lineW = UiFont::width(line, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                UiRender::textY(line, left + (width - lineW) * 0.5f, y, 22.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_BODY);
                y += 22.0f;
            }
            if (source() != SOURCE_NONE) {
                std::string retry = "重试";
                float w = std::max(72.0f, Page::buttonWidth(retry));
                if (Page::button("homepage#retry", retry, left + (width - w) * 0.5f, y + 12.0f, w, false, covered)) {
                    if (views.size() > 1) {
                        std::string location = view.location;
                        views.pop_back();
                        openHelp(location);
                    } else {
                        loadHome();
                    }
                }
            }
        }
        if (modal.open) {
            drawModal(blocked);
        }
    }

    void poll() {
        WebImages::poll();
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            next();
        }
    }

    void shutdown() {
        stopping.cancel();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
        WebImages::shutdown();
    }
}
