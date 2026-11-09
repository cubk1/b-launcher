#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "app/system/Catalog.h"
#include "ui/imgui/screens/downloads/SourceMarks.h"
#include "ui/imgui/screens/home/WebImages.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

namespace DownloadsParts {

    namespace {
        long long daysFromCivil(int y, unsigned m, unsigned d) {
            y -= m <= 2;
            const int era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = (unsigned) (y - era * 400);
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return (long long) era * 146097 + (long long) doe - 719468;
        }

        bool parseTime(const std::string& iso, long long& seconds) {
            int y = 0;
            int mo = 0;
            int d = 0;
            int h = 0;
            int mi = 0;
            int s = 0;
            if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3 || mo < 1 || mo > 12) {
                return false;
            }
            seconds = daysFromCivil(y, (unsigned) mo, (unsigned) d) * 86400LL + h * 3600LL + mi * 60LL + s;
            return true;
        }
    }

    void webImage(const std::string& url, float x, float y, float w, float h, float radius, bool cover) {
        const WebImages::Image& image = WebImages::get(url);
        if (image.texture == 0 || image.width <= 0 || image.height <= 0 || w <= 0.0f || h <= 0.0f) {
            UiRender::roundRect(x, y, w, h, radius, UiTheme::surfaceRaised());
            return;
        }
        float nw = (float) image.width;
        float nh = (float) image.height;
        float dx = x;
        float dy = y;
        float dw = w;
        float dh = h;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (cover) {
            float s = std::max(w / nw, h / nh);
            float uSpan = w / (nw * s);
            float vSpan = h / (nh * s);
            u0 = (1.0f - uSpan) * 0.5f;
            u1 = u0 + uSpan;
            v0 = (1.0f - vSpan) * 0.5f;
            v1 = v0 + vSpan;
        } else {
            float s = std::min(w / nw, h / nh);
            dw = nw * s;
            dh = nh * s;
            dx = x + (w - dw) * 0.5f;
            dy = y + (h - dh) * 0.5f;
        }
        if (radius > 0.0f) {
            ShaderUtil::roundTex(UiRender::transformX(dx), UiRender::transformY(dy), UiRender::transformW(dw),
                                 UiRender::transformW(dh), UiRender::transformW(radius), image.texture, u0, v0, u1, v1,
                                 UiRender::applyAlpha(0xFFFFFFFFu));
        } else {
            UiRender::texture(image.texture, dx, dy, dw, dh, u0, v0, u1, v1);
        }
    }

    std::string relativeTime(const std::string& iso) {
        long long then = 0;
        if (!parseTime(iso, then)) {
            return shortDate(iso);
        }
        long long now =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                        .count();
        long long diff = std::max(0LL, now - then);
        if (diff < 60) {
            return "刚刚";
        }
        if (diff < 3600) {
            return std::to_string(diff / 60) + " 分钟前";
        }
        if (diff < 86400) {
            return std::to_string(diff / 3600) + " 小时前";
        }
        if (diff < 86400LL * 30) {
            return std::to_string(diff / 86400) + " 天前";
        }
        if (diff < 86400LL * 365) {
            return std::to_string(diff / (86400LL * 30)) + " 个月前";
        }
        return std::to_string(diff / (86400LL * 365)) + " 年前";
    }

    std::string shortDate(const std::string& iso) {
        return iso.substr(0, std::min<size_t>(10, iso.size()));
    }

    std::vector<std::string> gameRanges(const std::vector<std::string>& versions) {
        std::vector<std::string> out;
        if (versions.empty()) {
            return out;
        }
        std::vector<std::string> releases;
        for (const auto& version : Catalog::minecraft().list) {
            if (version.type == "release") {
                releases.push_back(version.id);
            }
        }
        std::map<std::string, int> index;
        for (size_t i = 0; i < releases.size(); i++) {
            index.emplace(releases[i], (int) i);
        }
        std::vector<int> found;
        for (const std::string& version : versions) {
            auto it = index.find(version);
            if (it != index.end()) {
                found.push_back(it->second);
            }
        }
        if (found.empty()) {
            out.push_back(versions.size() == 1 ? versions.front() : versions.front() + "–" + versions.back());
            return out;
        }
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        std::vector<std::pair<int, int>> groups;
        for (int i : found) {
            if (!groups.empty() && groups.back().second + 1 == i) {
                groups.back().second = i;
            } else {
                groups.emplace_back(i, i);
            }
        }
        for (const auto& [newest, oldest] : groups) {
            out.push_back(newest == oldest ? releases[(size_t) newest]
                                           : releases[(size_t) oldest] + "–" + releases[(size_t) newest]);
        }
        return out;
    }

    std::string gameRange(const std::vector<std::string>& versions, size_t limit) {
        if (versions.empty()) {
            return std::string();
        }
        std::vector<std::string> releases;
        for (const auto& version : Catalog::minecraft().list) {
            if (version.type == "release") {
                releases.push_back(version.id);
            }
        }
        std::map<std::string, int> index;
        for (size_t i = 0; i < releases.size(); i++) {
            index.emplace(releases[i], (int) i);
        }
        std::vector<int> found;
        for (const std::string& version : versions) {
            auto it = index.find(version);
            if (it != index.end()) {
                found.push_back(it->second);
            }
        }
        if (found.empty()) {
            return versions.size() == 1 ? versions.front() : versions.front() + "–" + versions.back();
        }
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        std::vector<std::pair<int, int>> groups;
        for (int i : found) {
            if (!groups.empty() && groups.back().second + 1 == i) {
                groups.back().second = i;
            } else {
                groups.emplace_back(i, i);
            }
        }
        std::string out;
        for (size_t g = 0; g < groups.size(); g++) {
            if (g == limit) {
                out += " 等";
                break;
            }
            if (g > 0) {
                out += "、";
            }
            const auto& [newest, oldest] = groups[g];
            out += newest == oldest ? releases[(size_t) newest]
                                    : releases[(size_t) oldest] + "–" + releases[(size_t) newest];
        }
        return out;
    }

    std::string sideLabel(const std::string& side) {
        if (side == "required") {
            return "必需";
        }
        if (side == "optional") {
            return "可选";
        }
        if (side == "unsupported") {
            return "不支持";
        }
        return "未知";
    }

    const char* versionTypeLabel(const std::string& type) {
        if (type == "beta") {
            return "测试版";
        }
        if (type == "alpha") {
            return "早期版";
        }
        return "正式版";
    }

    void typeTag(const std::string& type, float x, float centreY, float limit) {
        uint32_t ink = UiTheme::textSecondary();
        uint32_t fill = UiTheme::surfaceRaised();
        if (type == "beta" || type == "alpha") {
            ink = type == "beta" ? UiTheme::warningInk() : UiTheme::danger();
            fill = (ink & 0x00FFFFFFu) | 0x2E000000u;
        }
        tag(versionTypeLabel(type), x, centreY, limit, fill, ink);
    }

    namespace {
        bool segmentIconed(const Segment& item) {
            return item.glyph != 0 || item.mark != 0;
        }

        void segmentIcon(const Segment& item, float cx, float cy, uint32_t rest, float near) {
            if (item.mark == 0) {
                UiWidgets::icon(Ui::glyph(item.glyph), cx, cy, UiRender::mix(rest, UiTheme::accentText(), near), 11.0f);
                return;
            }
            const float size = 13.0f;
            float x = cx - size * 0.5f;
            float y = cy - size * 0.5f;
            if (item.mark == MARK_MCMOD) {
                SourceMarks::mcmod(x, y, size, rest);
                SourceMarks::mcmodColored(x, y, size, near);
                return;
            }
            Addons::Source source =
                    item.mark == MARK_CURSEFORGE ? Addons::Source::CurseForge : Addons::Source::Modrinth;
            SourceMarks::draw(source, x, y, size, UiRender::mix(rest, SourceMarks::brand(source), near));
        }

        float segmentWidth(const Segment& item, bool iconOnly = false) {
            if (iconOnly && segmentIconed(item)) {
                return 40.0f;
            }
            float w = 24.0f + UiFont::width(item.label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (segmentIconed(item)) {
                w += 20.0f;
            }
            if (item.count > 0) {
                w += 5.0f + UiFont::width(std::to_string(item.count), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            return w;
        }
    }

    float segmentsWidth(const std::vector<Segment>& items) {
        float total = 4.0f;
        for (const Segment& item : items) {
            total += segmentWidth(item);
        }
        return total;
    }

    int segments(const std::string& owner, int current, const std::vector<Segment>& items, float x, float y,
                 bool covered, float maxWidth) {
        if (items.empty()) {
            return current;
        }
        const float h = SEGMENT_H;
        bool compact = maxWidth > 0.0f && segmentsWidth(items) > maxWidth;
        std::vector<float> starts;
        std::vector<float> widths;
        float cursor = x + 2.0f;
        int index = 0;
        for (size_t i = 0; i < items.size(); i++) {
            float w = segmentWidth(items[i], compact && items[i].id != current);
            starts.push_back(cursor);
            widths.push_back(w);
            cursor += w;
            if (items[i].id == current) {
                index = (int) i;
            }
        }
        std::string base = "downloads#segments#" + owner;
        int count = (int) items.size();
        UiRender::roundRect(x, y, cursor + 2.0f - x, h, 8.0f, UiTheme::surfaceRaised());
        float slide = std::clamp(UiState::motion(base + "#slide", (float) index, 16.0f), 0.0f, (float) (count - 1));
        int lower = std::min((int) slide, count - 1);
        int upper = std::min(lower + 1, count - 1);
        float blend = slide - (float) lower;
        float pillX = UiRender::lerp(starts[(size_t) lower], starts[(size_t) upper], blend);
        float pillW = UiRender::lerp(widths[(size_t) lower], widths[(size_t) upper], blend);
        UiRender::roundRect(pillX, y + 2.0f, pillW, h - 4.0f, 6.0f, UiTheme::surface());
        UiRender::outline(pillX, y + 2.0f, pillW, h - 4.0f, 6.0f, UiTheme::border(UiTheme::outlineSoft()));
        int next = current;
        for (size_t i = 0; i < items.size(); i++) {
            const Segment& item = items[i];
            float sx = starts[i];
            float w = widths[i];
            bool hovered = !covered && NewUi::hit(sx, y, w, h);
            float near = 1.0f - std::clamp(std::fabs(slide - (float) i), 0.0f, 1.0f);
            uint32_t rest = hovered ? UiTheme::textSecondary() : UiTheme::textMuted();
            float tx = sx + 12.0f;
            bool iconOnly = compact && item.id != current && segmentIconed(item);
            if (iconOnly) {
                segmentIcon(item, sx + w * 0.5f, y + h * 0.5f, rest, near);
                if (hovered) {
                    Page::tooltip(item.label, sx, y, w, h);
                }
                if (!covered && NewUi::click(sx, y, w, h)) {
                    UiSound::request(UiSound::TICK);
                    next = item.id;
                }
                continue;
            }
            if (segmentIconed(item)) {
                segmentIcon(item, tx + 7.0f, y + h * 0.5f, rest, near);
                tx += 20.0f;
            }
            UiRender::textY(item.label, tx, y, h, UiRender::mix(rest, UiTheme::textPrimary(), near), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (item.count > 0) {
                float cx = tx + UiFont::width(item.label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 5.0f;
                UiRender::textY(std::to_string(item.count), cx, y, h, UiTheme::textFaint(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            if (!covered && item.id != current && NewUi::click(sx, y, w, h)) {
                UiSound::request(UiSound::TICK);
                next = item.id;
            }
        }
        return next;
    }

    float sectionTitle(const std::string& text, float x, float y, float w) {
        UiRender::textY(UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w), x, y, 24.0f,
                        UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        return 28.0f;
    }
}
