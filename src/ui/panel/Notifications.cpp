#include "Notifications.h"
#include "UiCommon.h"
#include "UiFont.h"
#include "UiRender.h"
#include "../gl/ShaderUtil.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace Notifications {

    const Style INFO = {0xf05a, 0xFFC9C9C9u};
    const Style DONE = {0xf00c, 0xFF78C86Eu};
    const Style FAILED = {0xf071, 0xFFE74C4Cu};

    namespace {
        constexpr float SCALE = 1.5f;
        constexpr long long ENTER_MS = 380;
        constexpr long long EXIT_MS = 320;
        constexpr long long ROLL_MS = 260;
        constexpr size_t MAX_ITEMS = 20;
        constexpr float HEIGHT = 24.0f;
        constexpr float STEP = 30.0f;
        constexpr float BOTTOM = 16.0f;
        constexpr float MARGIN = 6.0f;
        constexpr float TEXT_SIZE = 10.5f;
        constexpr uint32_t TEXT_COLOR = 0xFFE4E4E8u;
        constexpr uint32_t FILL_COLOR = 0x90000000u;
        constexpr long long FOREVER = std::numeric_limits<long long>::max() / 4;
        const char32_t BRAILLE[] = {0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F};

        struct Item {
            std::string text;
            std::string previous;
            bool rolling = false;
            long long changedAt = 0;
            Style style;
            long long born = 0;
            long long duration = 0;
            float width = 0.0f;
            float shownY = std::numeric_limits<float>::quiet_NaN();
            float shownW = std::numeric_limits<float>::quiet_NaN();
            std::string id;
            bool live = false;
        };

        std::vector<Item> items;
        long long lastUpdate = 0;
        long long serial = 0;

        long long nowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
        }

        std::string spinner(long long now) {
            return Ui::encode(BRAILLE[(now / 80) % (long long) (sizeof(BRAILLE) / sizeof(BRAILLE[0]))]);
        }

        float widthOf(const std::string& text, bool live) {
            float content = UiFont::width(text, UiFont::MONO, TEXT_SIZE);
            if (live) {
                content += UiFont::width(Ui::encode(BRAILLE[0]) + " ", UiFont::MONO, TEXT_SIZE);
            }
            return content + 28.0f + 12.0f;
        }

        float easeOutExpo(float t) {
            if (t >= 1.0f) {
                return 1.0f;
            }
            if (t <= 0.0f) {
                return 0.0f;
            }
            return 1.0f - (float) std::pow(2.0, -10.0 * t);
        }

        float easeOutCubic(float t) {
            if (t <= 0.0f) {
                return 0.0f;
            }
            if (t >= 1.0f) {
                return 1.0f;
            }
            float inv = 1.0f - t;
            return 1.0f - inv * inv * inv;
        }

        uint32_t withAlpha(uint32_t argb, float a) {
            if (a <= 0.0f) {
                return argb & 0x00FFFFFFu;
            }
            if (a >= 1.0f) {
                return argb;
            }
            uint32_t alpha = (uint32_t) Ui::round(((argb >> 24) & 0xFF) * a);
            return (alpha << 24) | (argb & 0x00FFFFFFu);
        }

        size_t commonPrefix(const std::u32string& a, const std::u32string& b) {
            size_t limit = std::min(a.size(), b.size());
            size_t i = 0;
            while (i < limit && a[i] == b[i]) {
                i++;
            }
            while (i > 0 && a[i - 1] == U'§') {
                i--;
            }
            return i;
        }

        int findById(const std::string& id) {
            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].id == id) {
                    return (int) i;
                }
            }
            return -1;
        }

        void retarget(Item& item, const std::string& old, const std::string& text) {
            long long now = nowMs();
            std::string pending = item.rolling ? item.previous : std::string();
            long long elapsed = now - item.changedAt;
            item.previous = old;
            item.rolling = true;
            if (!(!pending.empty() && elapsed < ROLL_MS && pending == text)) {
                item.changedAt = now;
                return;
            }
            item.changedAt = now - std::max(0LL, ROLL_MS - elapsed);
        }

        Item& push(const Style& style, const std::string& text, long long duration, const std::string& id, bool live) {
            if (items.size() >= MAX_ITEMS) {
                items.erase(items.begin());
            }
            Item item;
            item.text = text;
            item.style = style;
            item.born = nowMs();
            item.duration = duration;
            item.width = widthOf(text, live);
            item.id = id;
            item.live = live;
            items.push_back(item);
            return items.back();
        }

        void drawText(const std::string& text, float x, float y, float alpha) {
            UiRender::textY(text, x, y, HEIGHT, withAlpha(TEXT_COLOR, alpha), UiFont::MONO, TEXT_SIZE);
        }

        void renderItems(float screenW, float screenH, float inset) {
            long long now = nowMs();
            long long elapsed = lastUpdate == 0 ? 16 : std::min(100LL, now - lastUpdate);
            lastUpdate = now;
            float yLerp = 1.0f - (float) std::exp(-elapsed * 0.018);
            float wLerp = 1.0f - (float) std::exp(-elapsed * 0.014);

            for (size_t i = 0; i < items.size(); i++) {
                Item& item = items[i];
                long long age = now - item.born;
                long long hold = item.duration;
                if (!item.live && age >= ENTER_MS + hold + EXIT_MS) {
                    items.erase(items.begin() + (long long) i);
                    i--;
                    continue;
                }

                float w = std::isnan(item.shownW) ? item.width : item.shownW + (item.width - item.shownW) * wLerp;
                item.shownW = w;
                float drawX;
                float opacity;
                if (item.live) {
                    float t = std::min(1.0f, age / (float) ENTER_MS);
                    drawX = screenW - easeOutExpo(t) * (w + MARGIN);
                    opacity = easeOutCubic(t);
                } else if (age <= ENTER_MS) {
                    float t = age / (float) ENTER_MS;
                    drawX = screenW - easeOutExpo(t) * (w + MARGIN);
                    opacity = easeOutCubic(t);
                } else if (age <= ENTER_MS + hold) {
                    drawX = screenW - w - MARGIN;
                    opacity = 1.0f;
                } else {
                    float t = (age - ENTER_MS - hold) / (float) EXIT_MS;
                    drawX = screenW - w - MARGIN;
                    opacity = 1.0f - easeOutCubic(t);
                }

                float targetY = screenH - BOTTOM - inset - (i + 1) * STEP;
                float cy = std::isnan(item.shownY) ? targetY : item.shownY + (targetY - item.shownY) * yLerp;
                item.shownY = cy;

                int blurAlpha = Ui::round(opacity * 255.0f);
                if (blurAlpha > 0) {
                    ShaderUtil::queueBlur(UiRender::transformX(drawX), UiRender::transformY(cy),
                                          UiRender::transformW(w), UiRender::transformW(HEIGHT),
                                          UiRender::transformW(5.0f), 10.0f, ((uint32_t) blurAlpha << 24) | 0xFFFFFFu,
                                          0xF, 6.0f);
                    ShaderUtil::flushBlur();
                }
                UiRender::roundRect(drawX, cy, w, HEIGHT, 5.0f, withAlpha(FILL_COLOR, opacity));
                UiRender::textY(Ui::encode((char32_t) item.style.icon), drawX + 10.0f, cy, HEIGHT,
                                withAlpha(item.style.color, opacity), UiFont::ICON, 10.0f);

                float textX = drawX + 28.0f;
                if (item.live) {
                    std::string frame = spinner(now);
                    drawText(frame, textX, cy, opacity);
                    textX += UiFont::width(frame + " ", UiFont::MONO, TEXT_SIZE);
                }

                UiRender::scissor(drawX, cy, std::max(0.0f, w - 6.0f), HEIGHT);
                long long sinceChange = now - item.changedAt;
                if (item.rolling && sinceChange >= ROLL_MS) {
                    item.rolling = false;
                    item.previous.clear();
                }
                if (!item.rolling) {
                    drawText(item.text, textX, cy, opacity);
                } else {
                    float t = easeOutCubic(sinceChange / (float) ROLL_MS);
                    std::u32string previous = Ui::decode(item.previous);
                    std::u32string current = Ui::decode(item.text);
                    size_t shared = commonPrefix(previous, current);
                    std::string stable = Ui::encode(current.substr(0, shared));
                    if (!stable.empty()) {
                        drawText(stable, textX, cy, opacity);
                    }
                    float tailX = textX + UiFont::width(stable, UiFont::MONO, TEXT_SIZE);
                    std::string oldTail = Ui::encode(previous.substr(std::min(shared, previous.size())));
                    std::string newTail = Ui::encode(current.substr(shared));
                    if (!oldTail.empty()) {
                        drawText(oldTail, tailX, cy - t * 6.5f, opacity * (1.0f - t));
                    }
                    if (!newTail.empty()) {
                        drawText(newTail, tailX, cy + (1.0f - t) * 6.5f, opacity * t);
                    }
                }
                UiRender::unscissor();
            }
        }
    }

    void add(const Style& style, const std::string& text, long long durationMs) {
        push(style, text, durationMs, std::string(), false);
    }

    void keyed(const std::string& key, const Style& style, const std::string& text, long long durationMs) {
        int existing = findById(key);
        if (existing >= 0) {
            Item& item = items[existing];
            if (item.text != text) {
                retarget(item, item.text, text);
            }
            item.text = text;
            item.style = style;
            item.width = widthOf(text, false);
            item.born = nowMs() - ENTER_MS;
            item.duration = durationMs;
            return;
        }
        push(style, text, durationMs, key, false);
    }

    std::string live(const Style& style, const std::string& text) {
        std::string id = "live_" + std::to_string(++serial);
        push(style, text, FOREVER, id, true);
        return id;
    }

    void update(const std::string& id, const std::string& text) {
        int index = findById(id);
        if (index < 0) {
            return;
        }
        Item& item = items[index];
        if (item.text == text) {
            return;
        }
        retarget(item, item.text, text);
        item.text = text;
        item.width = widthOf(text, item.live);
    }

    void finish(const std::string& id, const std::string& text, long long durationMs) {
        int index = findById(id);
        if (index < 0) {
            return;
        }
        finish(id, items[index].style, text, durationMs);
    }

    void finish(const std::string& id, const Style& style, const std::string& text, long long durationMs) {
        int index = findById(id);
        if (index < 0) {
            return;
        }
        Item& item = items[index];
        if (item.text != text) {
            retarget(item, item.text, text);
        }
        item.text = text;
        item.style = style;
        item.width = widthOf(text, false);
        item.born = nowMs() - ENTER_MS;
        item.duration = durationMs;
        item.id.clear();
        item.live = false;
    }

    void render(float layoutW, float layoutH, float bottomInset) {
        if (items.empty()) {
            lastUpdate = 0;
            return;
        }
        float savedUiScale = UiRender::uiScale;
        float savedScaleFactor = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        float savedOffsetX = UiRender::offsetX;
        float savedOffsetY = UiRender::offsetY;
        float savedAlphaMul = UiRender::alphaMul;
        UiRender::uiScale = savedUiScale * SCALE;
        UiRender::scaleFactor = 1.0f;
        UiRender::pivotX = 0.0f;
        UiRender::pivotY = 0.0f;
        UiRender::offsetX = 0.0f;
        UiRender::offsetY = 0.0f;
        UiRender::alphaMul = 1.0f;
        renderItems(layoutW / SCALE, layoutH / SCALE, bottomInset / SCALE);
        UiRender::uiScale = savedUiScale;
        UiRender::scaleFactor = savedScaleFactor;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
        UiRender::offsetX = savedOffsetX;
        UiRender::offsetY = savedOffsetY;
        UiRender::alphaMul = savedAlphaMul;
    }
}
