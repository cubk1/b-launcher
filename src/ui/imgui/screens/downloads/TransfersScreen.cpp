#include "ui/imgui/screens/downloads/TransfersScreen.h"
#include "app/downloads/Addons.h"
#include "app/instances/Covers.h"
#include "app/instances/Instances.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"
#include "app/system/Transfers.h"
#include "ui/imgui/gl/RenderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace TransfersScreen {

    namespace {
        using Transfers::Kind;
        using Transfers::State;
        using Transfers::Task;

        constexpr float CARD_RADIUS = 12.0f;
        constexpr float CARD_GAP = 12.0f;
        constexpr float HEAD_H = 78.0f;
        constexpr float STATS_H = 58.0f;
        constexpr float THUMB_W = 42.0f;
        constexpr float THUMB_H = 58.0f;
        constexpr float ROW_H = 60.0f;
        constexpr float SMALL_W = 30.0f;
        constexpr float SMALL_H = 42.0f;
        constexpr float BUTTON = 30.0f;
        constexpr float BAR_H = 4.0f;
        constexpr float PAD = 14.0f;
        constexpr double SWEEP_SECONDS = 1.4;
        constexpr size_t CHART_SLOTS = 120;

        std::string amount(uint64_t bytes) {
            char text[32];
            if (bytes >= (1ull << 30)) {
                std::snprintf(text, sizeof(text), "%.2f GB", bytes / 1073741824.0);
            } else if (bytes >= (1ull << 20)) {
                std::snprintf(text, sizeof(text), "%.1f MB", bytes / 1048576.0);
            } else if (bytes >= 1024) {
                std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
            } else {
                std::snprintf(text, sizeof(text), "%llu B", (unsigned long long) bytes);
            }
            return text;
        }

        std::string rate(double bytesPerSecond) {
            return amount((uint64_t) std::max(0.0, bytesPerSecond)) + "/s";
        }

        std::string span(double value) {
            int seconds = (int) std::lround(std::max(0.0, value));
            if (seconds < 60) {
                return std::to_string(seconds) + " 秒";
            }
            if (seconds < 3600) {
                return std::to_string(seconds / 60) + " 分 " + std::to_string(seconds % 60) + " 秒";
            }
            return std::to_string(seconds / 3600) + " 小时 " + std::to_string(seconds / 60 % 60) + " 分";
        }

        int glyphOf(Kind kind) {
            switch (kind) {
                case Kind::Java: return 0xf7b6;
                case Kind::File: return 0xf15b;
                case Kind::Theme: return 0xf1fc;
                case Kind::Server: return 0xf233;
                default: return 0xf1b2;
            }
        }

        std::string coverKey(const std::string& minecraft) {
            static std::map<std::string, std::string> known;
            auto found = known.find(minecraft);
            if (found != known.end()) {
                return found->second;
            }
            std::string key = Covers::keyOf(minecraft);
            if (!Catalog::minecraft().list.empty()) {
                known[minecraft] = key;
            }
            return key;
        }

        void thumb(const Task& task, float x, float y, float w, float h) {
            float radius = 6.0f;
            std::shared_ptr<const Pixels> art;
            std::string key = !task.cover.empty() ? task.cover
                    : !task.minecraft.empty()     ? coverKey(task.minecraft)
                                                  : std::string();
            if (!key.empty()) {
                int pixels = (int) std::lround(h * UiRender::stableScale() * UiRender::pixelScale());
                art = Covers::thumb(key, pixels);
            }
            if (art != nullptr) {
                Textures::sharp(art, x, y, w, h, radius);
                return;
            }
            float top = y + (h - w) * 0.5f;
            std::shared_ptr<const Pixels> icon = task.icon.empty() ? nullptr : Addons::icon(task.icon);
            if (icon != nullptr) {
                UiRender::texture(Textures::of(icon), x, top, w, w);
                return;
            }
            UiRender::roundRect(x, top, w, w, w * 0.22f, UiTheme::surfaceRaised());
            UiWidgets::icon(Ui::glyph(glyphOf(task.kind)), x + w * 0.5f, top + w * 0.5f, UiTheme::textMuted(),
                            w * 0.4f);
        }

        void bar(const Task& task, float x, float y, float w) {
            UiRender::roundRect(x, y, w, BAR_H, BAR_H * 0.5f, UiTheme::outline());
            float value = task.progress();
            if (value < 0.0f) {
                UiState::keepAnimating();
                float t = (float) (std::fmod(Transfers::now(), SWEEP_SECONDS) / SWEEP_SECONDS);
                float segment = w * 0.3f;
                float head = x - segment + (w + segment) * t;
                float from = std::max(x, head);
                float to = std::min(x + w, head + segment);
                if (to - from > 0.5f) {
                    UiRender::roundRect(from, y, std::max(BAR_H, to - from), BAR_H, BAR_H * 0.5f, UiTheme::accent());
                }
                return;
            }
            float shown = UiState::motion("transfers#bar#" + std::to_string(task.id), value, 10.0f);
            if (shown > 0.001f) {
                UiRender::roundRect(x, y, std::max(BAR_H, w * shown), BAR_H, BAR_H * 0.5f, UiTheme::accent());
            }
        }

        std::vector<ImVec2> smooth(const std::vector<ImVec2>& knots, int steps) {
            size_t n = knots.size();
            std::vector<float> secants(n - 1);
            for (size_t i = 0; i + 1 < n; i++) {
                float dx = knots[i + 1].x - knots[i].x;
                secants[i] = dx > 0.0f ? (knots[i + 1].y - knots[i].y) / dx : 0.0f;
            }
            std::vector<float> slopes(n);
            slopes[0] = secants[0];
            slopes[n - 1] = secants[n - 2];
            for (size_t i = 1; i + 1 < n; i++) {
                slopes[i] = secants[i - 1] * secants[i] <= 0.0f ? 0.0f : (secants[i - 1] + secants[i]) * 0.5f;
            }
            for (size_t i = 0; i + 1 < n; i++) {
                if (secants[i] == 0.0f) {
                    slopes[i] = 0.0f;
                    slopes[i + 1] = 0.0f;
                    continue;
                }
                float a = slopes[i] / secants[i];
                float b = slopes[i + 1] / secants[i];
                float length = a * a + b * b;
                if (length > 9.0f) {
                    float scale = 3.0f / std::sqrt(length);
                    slopes[i] = scale * a * secants[i];
                    slopes[i + 1] = scale * b * secants[i];
                }
            }
            std::vector<ImVec2> out;
            out.reserve((n - 1) * (size_t) steps + 1);
            out.push_back(knots[0]);
            for (size_t i = 0; i + 1 < n; i++) {
                float dx = knots[i + 1].x - knots[i].x;
                for (int k = 1; k <= steps; k++) {
                    float t = (float) k / (float) steps;
                    float t2 = t * t;
                    float t3 = t2 * t;
                    float value = (2.0f * t3 - 3.0f * t2 + 1.0f) * knots[i].y + (t3 - 2.0f * t2 + t) * dx * slopes[i] +
                            (3.0f * t2 - 2.0f * t3) * knots[i + 1].y + (t3 - t2) * dx * slopes[i + 1];
                    out.emplace_back(knots[i].x + dx * t, value);
                }
            }
            return out;
        }

        uint32_t shifted(uint32_t argb, float degrees) {
            float r = (float) ((argb >> 16) & 0xFF) / 255.0f;
            float g = (float) ((argb >> 8) & 0xFF) / 255.0f;
            float b = (float) (argb & 0xFF) / 255.0f;
            float high = std::max({r, g, b});
            float low = std::min({r, g, b});
            float delta = high - low;
            float hue = 0.0f;
            if (delta > 0.0f) {
                hue = high == r     ? 60.0f * std::fmod((g - b) / delta, 6.0f)
                        : high == g ? 60.0f * ((b - r) / delta + 2.0f)
                                    : 60.0f * ((r - g) / delta + 4.0f);
            }
            hue = std::fmod(hue + degrees + 720.0f, 360.0f);
            float chroma = high * (high > 0.0f ? delta / high : 0.0f);
            float second = chroma * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
            float floor = high - chroma;
            float parts[6][3] = {{chroma, second, 0.0f}, {second, chroma, 0.0f}, {0.0f, chroma, second},
                                 {0.0f, second, chroma}, {second, 0.0f, chroma}, {chroma, 0.0f, second}};
            const float* part = parts[std::min(5, (int) (hue / 60.0f))];
            auto channel = [floor](float value) { return (uint32_t) std::lround((value + floor) * 255.0f) & 0xFF; };
            return (argb & 0xFF000000u) | channel(part[0]) << 16 | channel(part[1]) << 8 | channel(part[2]);
        }

        ImU32 tint(uint32_t color, float alpha) {
            uint32_t level = (uint32_t) std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
            return RenderUtil::imColor(UiRender::applyAlpha((color & 0x00FFFFFFu) | level << 24));
        }

        void chart(const std::vector<float>& history, float x, float y, float w, float h, uint32_t color) {
            if (w <= 0.0f || h <= 0.0f) {
                return;
            }
            std::vector<float> values = history;
            if (values.empty()) {
                values.push_back(0.0f);
            }
            if (values.size() == 1) {
                values.insert(values.begin(), values.front());
            }
            size_t count = std::min(values.size(), CHART_SLOTS);
            size_t first = values.size() - count;
            float top = 1.0f;
            for (size_t i = first; i < values.size(); i++) {
                top = std::max(top, values[i]);
            }
            top *= 1.15f;
            float unit = UiRender::unit();
            float step = w / (float) (CHART_SLOTS - 1);
            float base = y + h;
            std::vector<ImVec2> knots;
            for (size_t i = first; i < values.size(); i++) {
                float px = std::max(x, x + w - (float) (values.size() - 1 - i) * step);
                float py = base - h * std::clamp(values[i] / top, 0.0f, 1.0f);
                knots.emplace_back(UiRender::transformX(px) * unit, UiRender::transformY(py) * unit);
            }
            std::vector<ImVec2> points = smooth(knots, 4);
            float ceiling = UiRender::transformY(y) * unit;
            float floor = UiRender::transformY(base) * unit;
            for (ImVec2& point : points) {
                point.y = std::clamp(point.y, ceiling, floor);
            }
            float start = UiRender::transformX(x) * unit;
            float fade = std::max(1.0f, (UiRender::transformX(x + w) * unit - start) * 0.5f);
            UiRender::flush();
            ImDrawList* list = UiRender::drawList();
            if (list == nullptr) {
                return;
            }
            ImVec2 uv = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
            size_t n = points.size();
            list->PrimReserve((int) (n - 1) * 6, (int) n * 2);
            ImDrawIdx at = (ImDrawIdx) list->_VtxCurrentIdx;
            for (const ImVec2& point : points) {
                float shown = std::clamp((point.x - start) / fade, 0.0f, 1.0f);
                list->PrimWriteVtx(point, uv, tint(color, 0.32f * shown));
                list->PrimWriteVtx(ImVec2(point.x, floor), uv, tint(color, 0.0f));
            }
            for (size_t i = 0; i + 1 < n; i++) {
                ImDrawIdx a = (ImDrawIdx) (at + i * 2);
                list->PrimWriteIdx(a);
                list->PrimWriteIdx((ImDrawIdx) (a + 2));
                list->PrimWriteIdx((ImDrawIdx) (a + 3));
                list->PrimWriteIdx(a);
                list->PrimWriteIdx((ImDrawIdx) (a + 3));
                list->PrimWriteIdx((ImDrawIdx) (a + 1));
            }
            float half = 0.75f * UiRender::transformW(1.0f) * unit;
            float feather = 1.0f;
            list->PrimReserve((int) (n - 1) * 18, (int) n * 4);
            at = (ImDrawIdx) list->_VtxCurrentIdx;
            for (size_t i = 0; i < n; i++) {
                const ImVec2& before = points[i == 0 ? 0 : i - 1];
                const ImVec2& after = points[std::min(i + 1, n - 1)];
                float dx = after.x - before.x;
                float dy = after.y - before.y;
                float length = std::max(1e-4f, std::sqrt(dx * dx + dy * dy));
                ImVec2 normal(-dy / length, dx / length);
                const ImVec2& point = points[i];
                float shown = std::clamp((point.x - start) / fade, 0.0f, 1.0f);
                ImU32 solid = tint(color, shown);
                ImU32 clear = tint(color, 0.0f);
                float outer = half + feather;
                list->PrimWriteVtx(ImVec2(point.x + normal.x * outer, point.y + normal.y * outer), uv, clear);
                list->PrimWriteVtx(ImVec2(point.x + normal.x * half, point.y + normal.y * half), uv, solid);
                list->PrimWriteVtx(ImVec2(point.x - normal.x * half, point.y - normal.y * half), uv, solid);
                list->PrimWriteVtx(ImVec2(point.x - normal.x * outer, point.y - normal.y * outer), uv, clear);
            }
            for (size_t i = 0; i + 1 < n; i++) {
                ImDrawIdx a = (ImDrawIdx) (at + i * 4);
                ImDrawIdx b = (ImDrawIdx) (a + 4);
                for (int band = 0; band < 3; band++) {
                    list->PrimWriteIdx((ImDrawIdx) (a + band));
                    list->PrimWriteIdx((ImDrawIdx) (b + band));
                    list->PrimWriteIdx((ImDrawIdx) (b + band + 1));
                    list->PrimWriteIdx((ImDrawIdx) (a + band));
                    list->PrimWriteIdx((ImDrawIdx) (b + band + 1));
                    list->PrimWriteIdx((ImDrawIdx) (a + band + 1));
                }
            }
        }

        bool square(const std::string& id, int glyph, const std::string& hint, float x, float y, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, BUTTON, BUTTON);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            UiRender::roundRect(x, y, BUTTON, BUTTON, 8.0f, UiTheme::surfaceRaised());
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, BUTTON, BUTTON, 8.0f, UiTheme::overlay(Ui::round(20 * hover)));
            }
            UiWidgets::icon(Ui::glyph(glyph), x + BUTTON * 0.5f, y + BUTTON * 0.5f,
                            hovered ? UiTheme::textPrimary() : UiTheme::textSecondary(), 12.0f);
            if (hovered) {
                UiCursor::hand();
                Page::tooltip(hint, x, y, BUTTON, BUTTON);
            }
            if (!blocked && NewUi::click(x, y, BUTTON, BUTTON)) {
                UiSound::request(UiSound::TICK);
                return true;
            }
            return false;
        }

        bool openable(const Task& task) {
            std::error_code error;
            return !task.folder.empty() && std::filesystem::exists(task.folder, error);
        }

        bool visitable(const Task& task) {
            return task.state == State::Done && !task.instance.empty() && Instances::find(task.instance) != nullptr;
        }

        void menu(const Task& task, float x, float y) {
            uint64_t id = task.id;
            bool finished = task.state != State::Running;
            bool folder = openable(task);
            bool visit = visitable(task);
            std::filesystem::path where = task.folder;
            std::string instance = task.instance;
            NewUi::openMenu(
                    "transfers#menu",
                    [id, finished, folder, visit, where, instance] {
                        MenuItems items;
                        if (visit) {
                            items.push_back(ActionMenuItem::make("前往实例", 0xf1b2, [instance] {
                                Instances::select(instance);
                                MainWindow::showHome();
                            }));
                        }
                        if (folder) {
                            items.push_back(ActionMenuItem::make("打开所在文件夹", 0xf07c,
                                                                 [where] { Platform::reveal(where); }));
                        }
                        if (finished) {
                            items.push_back(
                                    ActionMenuItem::make("从列表中移除", 0xf00d, [id] { Transfers::remove(id); }));
                        }
                        return items;
                    },
                    x, y, 168.0f);
        }

        std::string sizeText(const Task& task) {
            if (task.total > 0) {
                return amount(std::min(task.done, task.total)) + " / " + amount(task.total);
            }
            return task.done > 0 ? amount(task.done) : std::string();
        }

        void stat(const std::string& label, const std::string& value, float x, float y) {
            UiRender::textY(label, x, y + 9.0f, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(value, x, y + 27.0f, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM, 14.0f);
        }

        float runningCard(const Task& task, float x, float y, float w, bool blocked) {
            float h = HEAD_H + STATS_H;
            Page::card(x, y, w, h);
            thumb(task, x + PAD, y + (HEAD_H - THUMB_H) * 0.5f, THUMB_W, THUMB_H);
            std::string id = "transfers#" + std::to_string(task.id);
            float buttonY = y + (HEAD_H - BUTTON) * 0.5f;
            float buttonX = x + w - PAD;
            if (openable(task)) {
                buttonX -= BUTTON;
                if (square(id + "#more", 0xf141, "更多", buttonX, buttonY, blocked)) {
                    menu(task, buttonX, buttonY + BUTTON + 4.0f);
                }
                buttonX -= 8.0f;
            }
            if (task.cancellable) {
                buttonX -= BUTTON;
                if (square(id + "#cancel", 0xf00d, "取消", buttonX, buttonY, blocked)) {
                    Transfers::cancel(task.id);
                }
            }
            float left = x + PAD + THUMB_W + PAD;
            float right = buttonX - 18.0f;
            float room = std::max(40.0f, right - left);
            std::string title = UiFont::fitEnd(task.title, UiFont::MEDIUM, UiWidgets::TEXT_TITLE, room);
            UiRender::textY(title, left, y + 13.0f, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_TITLE);
            bar(task, left, y + 40.0f, room);
            float lineY = y + 50.0f;
            std::string size = sizeText(task);
            float sizeW = size.empty() ? 0.0f : UiFont::width(size, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!size.empty()) {
                UiRender::textY(size, left + room - sizeW, lineY, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            std::string stage = !task.stage.empty() ? task.stage : task.total > 0 ? "正在下载" : "正在准备";
            float statusRoom = room - (sizeW > 0.0f ? sizeW + 16.0f : 0.0f);
            stage = UiFont::fitEnd(stage, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, statusRoom);
            UiRender::textY(stage, left, lineY, 18.0f, UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            double remaining = task.remaining();
            if (remaining >= 0.0) {
                float stageW = UiFont::width(stage, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                std::string rest = UiFont::fitEnd("剩余 " + span(remaining), UiFont::MEDIUM, UiWidgets::TEXT_SMALL,
                                                  statusRoom - stageW - 10.0f);
                UiRender::textY(rest, left + stageW + 10.0f, lineY, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }

            float statsY = y + HEAD_H;
            float columnW = w / 3.0f;
            const struct {
                const char* label;
                const Transfers::Rate* rate;
                float hue;
            } columns[] = {{"下载", &task.download, 0.0f}, {"读取", &task.read, 45.0f}, {"写入", &task.write, 90.0f}};
            for (int i = 0; i < 3; i++) {
                float columnX = x + columnW * (float) i;
                float chartX = columnX + columnW * 0.36f;
                chart(columns[i].rate->history, chartX, statsY + 9.0f, columnX + columnW - 6.0f - chartX,
                      STATS_H - 20.0f, shifted(UiTheme::accent(), columns[i].hue));
                stat(columns[i].label, rate(columns[i].rate->speed), columnX + PAD, statsY);
            }
            return h;
        }

        void finishedRow(const Task& task, float x, float y, float w, bool blocked, int corners) {
            std::string id = "transfers#" + std::to_string(task.id);
            float moreX = x + w - PAD - 28.0f;
            float moreY = y + (ROW_H - 28.0f) * 0.5f;
            bool visit = visitable(task);
            bool hovered = visit && !blocked && NewUi::hit(x, y, w, ROW_H) && !NewUi::hit(moreX, moreY, 28.0f, 28.0f);
            float lift = UiState::motion(id + "#row", hovered ? 1.0f : 0.0f, 20.0f);
            if (lift > 0.001f) {
                UiRender::roundRect(x, y, w, ROW_H, CARD_RADIUS, UiTheme::overlay(Ui::round(14 * lift)), corners);
            }
            if (hovered) {
                UiCursor::hand();
            }
            thumb(task, x + PAD, y + (ROW_H - SMALL_H) * 0.5f, SMALL_W, SMALL_H);
            if (Page::iconButton(id + "#more", 0xf141, "更多", moreX, moreY, blocked)) {
                menu(task, moreX, y + (ROW_H + 28.0f) * 0.5f + 4.0f);
            }
            if (visit && !blocked && NewUi::click(x, y, w, ROW_H)) {
                Instances::select(task.instance);
                MainWindow::showHome();
            }
            float left = x + PAD + SMALL_W + PAD;
            float room = std::max(40.0f, moreX - 12.0f - left);
            UiRender::textY(UiFont::fitEnd(task.title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room), left, y + 11.0f,
                            20.0f, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            std::string status;
            uint32_t color = UiTheme::textMuted();
            std::string extra;
            switch (task.state) {
                case State::Done:
                    status = "已完成";
                    color = UiTheme::accentText();
                    extra = (task.done > 0 ? amount(task.done) + "  " : std::string()) + "用时 " + span(task.elapsed());
                    break;
                case State::Cancelled: status = "已取消"; break;
                default:
                    status = task.error.empty() ? std::string("失败") : "失败：" + task.error;
                    color = UiTheme::danger();
                    break;
            }
            status = UiFont::fitEnd(status, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, room);
            UiRender::textY(status, left, y + 31.0f, 18.0f, color, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!extra.empty()) {
                float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(UiFont::fitEnd(extra, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, room - statusW - 10.0f),
                                left + statusW + 10.0f, y + 31.0f, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
        }

        void empty() {
            std::string text = "没有正在进行的下载";
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(text, Page::left() + (Page::width() - w) * 0.5f, Page::top() + 110.0f, 24.0f,
                            UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }
    }

    void draw() {
        bool blocked = NewUi::overlayCoversMouse();
        Page::title("下载列表", Page::left(), Page::top());
        const std::vector<Task>& all = Transfers::list();
        std::vector<const Task*> running;
        std::vector<const Task*> finished;
        for (const Task& task : all) {
            (task.state == State::Running ? running : finished).push_back(&task);
        }
        std::reverse(finished.begin(), finished.end());
        if (!finished.empty()) {
            std::string text = "清除已完成";
            float w = Page::buttonWidth(text, 0xf2ed);
            if (Page::button("transfers#clear", text, Page::right() - w, Page::top() + 1.0f, w, false, blocked,
                             0xf2ed)) {
                Transfers::clear();
            }
        }
        if (all.empty()) {
            empty();
            return;
        }
        if (!running.empty()) {
            UiState::keepAnimating();
        }
        float captionH = Page::CAPTION_H;
        float contentH = 6.0f;
        if (!running.empty()) {
            contentH += (float) running.size() * (HEAD_H + STATS_H + CARD_GAP);
        }
        if (!finished.empty()) {
            contentH += (running.empty() ? 0.0f : 8.0f) + captionH + (float) finished.size() * ROW_H + 10.0f;
        }
        Page::Scroll body("transfers#body", Page::top() + 40.0f, MainWindow::height() - 14.0f, contentH);
        bool outside = body.begin();
        bool listBlocked = outside || blocked;
        float x = Page::left();
        float w = Page::width();
        float y = body.top - body.offset + 6.0f;
        for (const Task* task : running) {
            float h = HEAD_H + STATS_H;
            if (y + h >= body.top - 8.0f && y <= body.bottom + 8.0f) {
                runningCard(*task, x, y, w, listBlocked);
            }
            y += h + CARD_GAP;
        }
        if (!finished.empty()) {
            if (!running.empty()) {
                y += 8.0f;
            }
            Page::caption("已完成", y);
            y += captionH;
            Page::listCard(x, y, w, ROW_H, (int) finished.size());
            int count = (int) finished.size();
            for (int i = 0; i < count; i++) {
                if (y + ROW_H >= body.top - 8.0f && y <= body.bottom + 8.0f) {
                    finishedRow(*finished[i], x, y, w, listBlocked, Page::rowCorners(i, count));
                }
                y += ROW_H;
            }
        }
        body.end();
    }
}
