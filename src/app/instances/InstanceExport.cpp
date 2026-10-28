#include "app/instances/InstanceExport.h"
#include "app/accounts/Accounts.h"
#include "app/system/Config.h"
#include "app/shell/MainWindow.h"
#include "app/shell/Page.h"
#include "app/system/Platform.h"

#include "ui/panel/NewUi.h"
#include "ui/panel/Notifications.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"
#include "ui/value/Bind.h"
#include "ui/widget/IconButtonWidget.h"
#include "ui/widget/TextFieldWidget.h"
#include "ui/widget/ToggleWidget.h"

#include <core/base/cancellation.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>
#include <core/network/mirror_provider.hpp>

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace InstanceExport {

    namespace blb = b_launcher_base;
    using Options = blb::modpack::export_options;

    namespace {
        constexpr float WIDE_FIELD = 300.0f;

        std::map<std::string, Options> remembered;

        std::thread worker;
        std::shared_ptr<blb::cancellation_source> cancelling;
        bool running = false;
        std::string runningKey;
        std::mutex lock;
        bool done = false;
        std::string failure;
        bool cancelled = false;
        std::filesystem::path written;
        size_t warnings = 0;
        std::string status;

        void tip(const std::string& text, float x, float y, float w, float h) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
            NewUi::tooltipText = text;
            NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
            NewUi::tooltipY = y + h + 6.0f;
        }

        bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y, bool blocked) {
            IconButtonWidget button(id, Ui::glyph(glyph), UiTheme::textSecondary(), UiTheme::textPrimary());
            button.bounds(x, y, 28, 28).block(blocked);
            button.paint();
            if (!blocked && button.hovered()) {
                tip(hint, x, y, 28, 28);
            }
            return button.pressed;
        }

        Options& optionsOf(const Instances::Entry& entry) {
            auto found = remembered.find(entry.key);
            if (found != remembered.end()) {
                return found->second;
            }
            Options options;
            options.name = entry.info.id;
            if (const blb::auth::account* account = Accounts::current()) {
                options.author = account->name;
            }
            const nlohmann::json& data = Config::data();
            auto saved = data.find("exportOptions");
            if (saved != data.end() && saved->is_object()) {
                auto mine = saved->find(entry.key);
                if (mine != saved->end() && mine->is_object()) {
                    auto parsed = blb::modpack::parse_export_options(mine->dump());
                    if (parsed) {
                        options = *parsed;
                    }
                }
            }
            return remembered[entry.key] = options;
        }

        void save(const std::string& key) {
            auto found = remembered.find(key);
            if (found == remembered.end()) {
                return;
            }
            nlohmann::json parsed =
                    nlohmann::json::parse(blb::modpack::serialize_export_options(found->second), nullptr, false);
            if (parsed.is_object()) {
                Config::data()["exportOptions"][key] = parsed;
                Config::save();
            }
        }

        void textRow(const std::string& key, const char* field, const char* label, const char* placeholder, float w,
                     std::string Options::* member, float rowY, bool blocked) {
            Page::label(label, rowY);
            auto value = std::make_shared<TextBinding>(
                    std::string("export#") + field + "#" + key,
                    [key, member] {
                        auto found = remembered.find(key);
                        return found != remembered.end() ? found->second.*member : std::string();
                    },
                    [key, member](const std::string& text) {
                        remembered[key].*member = Ui::trim(text);
                        save(key);
                    },
                    placeholder);
            value->limit = 200;
            float x = Page::controlRight() - w;
            float y = rowY + Page::FIELD_Y;
            TextFieldWidget widget(value);
            widget.box(x, y, w, Page::FIELD_H).bounds(x, y, w, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void toggleRow(const std::string& key, const char* field, const char* label, bool Options::* member, float rowY,
                       bool blocked, const char* doc = nullptr) {
            Page::label(label, rowY);
            std::string id = std::string("export#") + field + "#" + key;
            auto flag = std::make_shared<FlagBinding>(
                    id,
                    [key, member] {
                        auto found = remembered.find(key);
                        return found != remembered.end() && found->second.*member;
                    },
                    [key, member](bool on) {
                        remembered[key].*member = on;
                        save(key);
                    });
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::left(), rowY, Page::width(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
            if (doc != nullptr && !blocked &&
                NewUi::hit(Page::left(), rowY, Page::controlRight() - 60.0f - Page::left(), Page::ROW_H)) {
                NewUi::markDoc(id + "#doc", doc, Page::left() + 6.0f, rowY + Page::ROW_H + 10.0f);
            }
        }

        std::string fileName(const Options& options) {
            std::string base = options.name.empty() ? std::string("modpack") : options.name;
            if (!options.version.empty()) {
                base += "-" + options.version;
            }
            std::string out;
            for (char c : base) {
                if (std::string("\\/:*?\"<>|").find(c) == std::string::npos && (unsigned char) c >= 32) {
                    out += c;
                }
            }
            return out + ".mrpack";
        }

        void start(const Instances::Entry& entry, const Options& options, const std::filesystem::path& output) {
            const Instances::Source* source = Instances::source(entry.source);
            if (running || source == nullptr) {
                return;
            }
            if (worker.joinable()) {
                worker.join();
            }
            running = true;
            runningKey = entry.key;
            {
                std::lock_guard<std::mutex> guard(lock);
                done = false;
                failure.clear();
                cancelled = false;
                warnings = 0;
                status = "正在准备";
            }
            cancelling = std::make_shared<blb::cancellation_source>();
            blb::cancellation_token token = cancelling->token();
            std::filesystem::path root = source->root;
            std::string id = entry.info.id;
            std::filesystem::path gameDirectory = entry.gameDirectory;
            worker = std::thread([root, id, gameDirectory, output, options, token] {
                std::string error;
                size_t warned = 0;
                bool wasCancelled = false;
                try {
                    blb::function_progress_sink sink([](const blb::progress_event& event) {
                        char text[64];
                        if (event.task == "export:hash") {
                            std::snprintf(text, sizeof(text), "正在查找文件来源 %llu/%llu",
                                          (unsigned long long) event.done_items,
                                          (unsigned long long) event.total_items);
                        } else if (event.total_bytes > 0) {
                            std::snprintf(text, sizeof(text), "正在打包 %d%%",
                                          (int) (event.done_bytes * 100 / event.total_bytes));
                        } else {
                            std::snprintf(text, sizeof(text), "正在打包");
                        }
                        std::lock_guard<std::mutex> guard(lock);
                        status = text;
                    });
                    blb::install::install_options installOptions;
                    installOptions.mirror = blb::net::make_bmclapi_provider();
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions);
                    auto report =
                            blb::modpack::export_modpack(installer, id, gameDirectory, output, options, &sink, token);
                    if (report) {
                        warned = report->warnings.size();
                    } else {
                        error = report.err().message;
                        wasCancelled = report.err().code == blb::errc::cancelled;
                    }
                } catch (const std::exception& exception) {
                    error = exception.what();
                }
                if (!error.empty()) {
                    std::error_code removeError;
                    std::filesystem::remove(output, removeError);
                }
                std::lock_guard<std::mutex> guard(lock);
                failure = error;
                cancelled = wasCancelled || token.is_cancelled();
                written = output;
                warnings = warned;
                done = true;
            });
        }

        void exportButton(const Instances::Entry& entry, bool blocked) {
            float y = MainWindow::height() - 28.0f - Page::FIELD_H;
            bool mine = running && runningKey == entry.key;
            std::string label = mine ? "取消" : "导出";
            float w = std::max(88.0f, Page::buttonWidth(label, mine ? 0 : 0xf56e));
            float x = Page::right() - w;
            if (mine) {
                std::string now;
                {
                    std::lock_guard<std::mutex> guard(lock);
                    now = status;
                }
                float nowW = UiFont::width(now, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(now, x - 14.0f - nowW, y, Page::FIELD_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                Page::spinner(x - 14.0f - nowW - 14.0f, y + Page::FIELD_H * 0.5f, 6.0f, UiTheme::accent());
            } else if (running) {
                std::string busy = "另一个实例正在导出";
                float busyW = UiFont::width(busy, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(busy, x - 14.0f - busyW, y, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (Page::button("export#go", label, x, y, w, !mine, blocked || (running && !mine), mine ? 0 : 0xf56e)) {
                if (mine) {
                    if (cancelling) {
                        cancelling->cancel();
                    }
                    return;
                }
                const Options& options = optionsOf(entry);
                if (std::optional<std::filesystem::path> output =
                            Platform::pickSavePack(b_launcher_base::path_from_utf8(fileName(options)))) {
                    start(entry, options, *output);
                }
            }
        }
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        bool back = iconButton("export#back", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, covered);
        const char* title = "导出整合包";
        Page::title(title, Page::left() + 30.0f, Page::TOP);
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::right() - nameX),
                        nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        optionsOf(entry);

        const int packCount = 4;
        const int contentCount = 8;
        const int fileCount = 1;
        float contentH = 3 * Page::CAPTION_H + (packCount + contentCount + fileCount) * Page::ROW_H + 2 * Page::GAP;
        float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
        Page::Scroll body("export#body#" + key, Page::TOP + 40.0f, buttonTop - 14.0f, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered || (running && runningKey == key);
        float y = body.top - body.offset;

        if (Page::shown(y, Page::CAPTION_H + packCount * Page::ROW_H)) {
            Page::caption("整合包", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, packCount);
            textRow(key, "name", "名称", "整合包名称", WIDE_FIELD, &Options::name, rowY, blocked);
            textRow(key, "version", "版本", "1.0.0", 140.0f, &Options::version, rowY + Page::ROW_H, blocked);
            textRow(key, "author", "作者", "无", 200.0f, &Options::author, rowY + 2 * Page::ROW_H, blocked);
            textRow(key, "summary", "简介", "无", WIDE_FIELD, &Options::summary, rowY + 3 * Page::ROW_H, blocked);
        }
        y += Page::CAPTION_H + packCount * Page::ROW_H + Page::GAP;

        if (Page::shown(y, Page::CAPTION_H + contentCount * Page::ROW_H)) {
            Page::caption("包含的内容", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, contentCount);
            toggleRow(key, "mods", "模组", &Options::mods, rowY, blocked);
            toggleRow(key, "disabledMods", "已禁用的模组", &Options::disabled_mods, rowY + Page::ROW_H, blocked);
            toggleRow(key, "modSettings", "模组配置文件", &Options::mod_settings, rowY + 2 * Page::ROW_H, blocked,
                      "config 等文件夹里模组的设置。");
            toggleRow(key, "gameSettings", "游戏设置", &Options::game_settings, rowY + 3 * Page::ROW_H, blocked,
                      "options.txt 里的按键、视频等设置。");
            toggleRow(key, "resourcePacks", "资源包", &Options::resource_packs, rowY + 4 * Page::ROW_H, blocked);
            toggleRow(key, "shaderPacks", "光影包", &Options::shader_packs, rowY + 5 * Page::ROW_H, blocked);
            toggleRow(key, "saves", "存档", &Options::saves, rowY + 6 * Page::ROW_H, blocked);
            toggleRow(key, "servers", "服务器列表", &Options::servers, rowY + 7 * Page::ROW_H, blocked);
        }
        y += Page::CAPTION_H + contentCount * Page::ROW_H + Page::GAP;

        if (Page::shown(y, Page::CAPTION_H + fileCount * Page::ROW_H)) {
            Page::caption("文件", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, fileCount);
            toggleRow(key, "bundle", "全部打包进整合包", &Options::bundle_files, rowY, blocked,
                      "关闭时，能在 Modrinth "
                      "上找到的模组和资源只记下载地址，整合包更小，安装时再下载；打开则把文件都放进整合包。");
        }
        body.end();

        exportButton(entry, covered);
        return back;
    }

    void poll() {
        if (!running) {
            return;
        }
        std::string error;
        bool wasCancelled;
        std::filesystem::path file;
        size_t warned;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!done) {
                return;
            }
            error = failure;
            wasCancelled = cancelled;
            file = written;
            warned = warnings;
        }
        if (worker.joinable()) {
            worker.join();
        }
        running = false;
        if (wasCancelled) {
            return;
        }
        if (!error.empty()) {
            Notifications::add(Notifications::FAILED, "导出失败：" + error, 5000);
            return;
        }
        (void) warned;
        Notifications::add(Notifications::DONE, "已导出 " + blb::path_to_utf8(file.filename()), 4000);
        Platform::reveal(file);
    }

    void shutdown() {
        if (running && cancelling) {
            cancelling->cancel();
        }
        if (worker.joinable()) {
            worker.join();
        }
    }
}
