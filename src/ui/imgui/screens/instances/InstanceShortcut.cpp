#include "ui/imgui/screens/instances/InstanceShortcut.h"
#include "ui/imgui/screens/instances/InstancePage.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Platform.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/IconButtonWidget.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>
#include <core/meta/game_layout.hpp>

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace InstanceShortcut {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float SELECT_W = 180.0f;
        constexpr float PREVIEW = 22.0f;
        constexpr int PREVIEW_PIXELS = 64;
        constexpr int BASE_PIXELS = 256;
        constexpr int ICON_LAUNCHER = 0;
        constexpr int ICON_INSTANCE = 1;
        constexpr int ICON_CUSTOM = 2;
        const char* const ICON_LABELS[] = {"启动器图标", "实例图标", "自定义图片"};
        const int ICON_SIZES[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
        const char* const HEADLESS_DOC =
                "不显示启动器窗口，直接启动游戏；游戏启动后启动器自动退出，启动失败时才显示窗口。"
                "启动器已经打开时在后台启动，不切换到启动器窗口。";

        struct Draft {
            int icon = ICON_INSTANCE;
            std::filesystem::path custom;
            bool headless = false;
        };

        struct Image {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> rgba;
        };

        struct Preview {
            std::filesystem::path file;
            uint32_t texture = 0;
        };

        std::map<std::string, Draft> drafts;
        std::filesystem::path lastFolder;
        Preview preview;

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

        bool isIco(const std::filesystem::path& file) {
            return blb::iequals(blb::path_to_utf8(file.extension()), ".ico");
        }

        std::string safeName(const std::string& name) {
            std::string out;
            for (char c : name) {
                bool bad = (unsigned char) c < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos;
                out += bad ? '_' : c;
            }
            return out.empty() ? std::string("Minecraft") : out;
        }

        Image decode(const unsigned char* data, size_t size) {
            Image image;
            int channels = 0;
            unsigned char* pixels = stbi_load_from_memory(data, (int) size, &image.width, &image.height, &channels, 4);
            if (pixels != nullptr) {
                image.rgba.assign(pixels, pixels + (size_t) image.width * image.height * 4);
                stbi_image_free(pixels);
            }
            return image;
        }

        Image load(const std::filesystem::path& file) {
            auto bytes = blb::fs::read_file(file);
            if (!bytes || bytes->empty()) {
                return {};
            }
            return decode((const unsigned char*) bytes->data(), bytes->size());
        }

        std::vector<unsigned char> resample(const Image& image, int size) {
            std::vector<unsigned char> out((size_t) size * size * 4, 0);
            if (image.width <= 0 || image.height <= 0) {
                return out;
            }
            double scale = (double) size / std::max(image.width, image.height);
            double offsetX = (size - image.width * scale) * 0.5;
            double offsetY = (size - image.height * scale) * 0.5;
            double area = 1.0 / (scale * scale);
            for (int y = 0; y < size; y++) {
                double top = std::max(0.0, (y - offsetY) / scale);
                double bottom = std::min((double) image.height, (y + 1 - offsetY) / scale);
                if (bottom <= top) {
                    continue;
                }
                for (int x = 0; x < size; x++) {
                    double left = std::max(0.0, (x - offsetX) / scale);
                    double right = std::min((double) image.width, (x + 1 - offsetX) / scale);
                    if (right <= left) {
                        continue;
                    }
                    double sum[4] = {0.0, 0.0, 0.0, 0.0};
                    for (int sy = (int) top; sy < bottom; sy++) {
                        double wy = std::min(bottom, sy + 1.0) - std::max(top, (double) sy);
                        for (int sx = (int) left; sx < right; sx++) {
                            double wx = std::min(right, sx + 1.0) - std::max(left, (double) sx);
                            const unsigned char* p = &image.rgba[((size_t) sy * image.width + sx) * 4];
                            double weight = wx * wy * p[3];
                            sum[0] += p[0] * weight;
                            sum[1] += p[1] * weight;
                            sum[2] += p[2] * weight;
                            sum[3] += weight;
                        }
                    }
                    if (sum[3] <= 0.0) {
                        continue;
                    }
                    unsigned char* q = &out[((size_t) y * size + x) * 4];
                    q[0] = (unsigned char) std::lround(std::min(255.0, sum[0] / sum[3]));
                    q[1] = (unsigned char) std::lround(std::min(255.0, sum[1] / sum[3]));
                    q[2] = (unsigned char) std::lround(std::min(255.0, sum[2] / sum[3]));
                    q[3] = (unsigned char) std::lround(std::min(255.0, sum[3] / area));
                }
            }
            return out;
        }

        Image reduced(Image image) {
            if (image.width <= BASE_PIXELS && image.height <= BASE_PIXELS) {
                return image;
            }
            Image out;
            out.width = BASE_PIXELS;
            out.height = BASE_PIXELS;
            out.rgba = resample(image, BASE_PIXELS);
            return out;
        }

        void put16(std::string& out, uint32_t value) {
            out.push_back((char) (value & 0xFF));
            out.push_back((char) ((value >> 8) & 0xFF));
        }

        void put32(std::string& out, uint32_t value) {
            put16(out, value & 0xFFFF);
            put16(out, value >> 16);
        }

        std::string ico(const Image& source) {
            Image image = reduced(source);
            std::vector<std::string> images;
            for (int size : ICON_SIZES) {
                std::vector<unsigned char> rgba = resample(image, size);
                uint32_t stride = (uint32_t) ((size + 31) / 32) * 4;
                std::string dib;
                put32(dib, 40);
                put32(dib, (uint32_t) size);
                put32(dib, (uint32_t) size * 2);
                put16(dib, 1);
                put16(dib, 32);
                put32(dib, 0);
                put32(dib, (uint32_t) size * size * 4 + stride * size);
                put32(dib, 0);
                put32(dib, 0);
                put32(dib, 0);
                put32(dib, 0);
                for (int y = size - 1; y >= 0; y--) {
                    for (int x = 0; x < size; x++) {
                        const unsigned char* p = &rgba[((size_t) y * size + x) * 4];
                        dib.push_back((char) p[2]);
                        dib.push_back((char) p[1]);
                        dib.push_back((char) p[0]);
                        dib.push_back((char) p[3]);
                    }
                }
                for (int y = size - 1; y >= 0; y--) {
                    std::string row(stride, '\0');
                    for (int x = 0; x < size; x++) {
                        if (rgba[((size_t) y * size + x) * 4 + 3] == 0) {
                            row[x / 8] = (char) (row[x / 8] | (0x80 >> (x % 8)));
                        }
                    }
                    dib += row;
                }
                images.push_back(std::move(dib));
            }
            std::string out;
            put16(out, 0);
            put16(out, 1);
            put16(out, (uint32_t) images.size());
            uint32_t offset = 6 + 16 * (uint32_t) images.size();
            for (size_t i = 0; i < images.size(); i++) {
                int size = ICON_SIZES[i];
                out.push_back((char) (size >= 256 ? 0 : size));
                out.push_back((char) (size >= 256 ? 0 : size));
                out.push_back(0);
                out.push_back(0);
                put16(out, 1);
                put16(out, 32);
                put32(out, (uint32_t) images[i].size());
                put32(out, offset);
                offset += (uint32_t) images[i].size();
            }
            for (const std::string& image : images) {
                out += image;
            }
            return out;
        }

        Icons::Encoded instanceIcon(const Instances::Entry& entry) {
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                if (blb::install::is_primary_loader(loader.type)) {
                    if (Icons::Encoded found = Icons::encoded(loader.type); found.data != nullptr) {
                        return found;
                    }
                }
            }
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                if (Icons::Encoded found = Icons::encoded(loader.type); found.data != nullptr) {
                    return found;
                }
            }
            return Icons::encodedVanilla();
        }

        uint32_t customTexture(const std::filesystem::path& file) {
            if (file == preview.file) {
                return preview.texture;
            }
            if (preview.texture != 0) {
                GLuint old = preview.texture;
                UiGL::DeleteTextures(1, &old);
                preview.texture = 0;
            }
            preview.file = file;
            std::vector<unsigned char> rgba;
            if (isIco(file)) {
                rgba = Platform::iconPixels(file, PREVIEW_PIXELS);
            } else if (Image image = load(file); !image.rgba.empty()) {
                rgba = resample(reduced(std::move(image)), PREVIEW_PIXELS);
            }
            if (rgba.size() == (size_t) PREVIEW_PIXELS * PREVIEW_PIXELS * 4) {
                preview.texture = UiImages::upload(rgba.data(), PREVIEW_PIXELS, PREVIEW_PIXELS);
            }
            return preview.texture;
        }

        std::string iconBytes(const Instances::Entry& entry, const Draft& draft) {
            if (draft.icon == ICON_CUSTOM && isIco(draft.custom)) {
                auto read = blb::fs::read_file(draft.custom);
                return read ? std::move(*read) : std::string();
            }
            Image image;
            if (draft.icon == ICON_CUSTOM) {
                image = load(draft.custom);
            } else {
                Icons::Encoded encoded = instanceIcon(entry);
                image = decode(encoded.data, encoded.size);
            }
            return image.rgba.empty() ? std::string() : ico(image);
        }

        std::optional<std::filesystem::path> storeIcon(const Instances::Entry& entry, const std::string& bytes,
                                                       std::string& problem) {
            const Instances::Source* source = Instances::source(entry.source);
            if (source == nullptr) {
                problem = "找不到实例所在的游戏目录";
                return std::nullopt;
            }
            std::filesystem::path folder = blb::meta::game_layout(source->root).cache() / L"shortcuts";
            std::string name = blb::to_hex(blb::sha1::digest(bytes)).substr(0, 16) + ".ico";
            std::filesystem::path file = folder / blb::path_from_utf8(name);
            if (blb::fs::exists(file)) {
                return file;
            }
            auto made = blb::fs::create_directories(folder);
            auto written = made ? blb::fs::write_file_atomic(file, bytes) : made;
            if (!written) {
                problem = "无法保存图标：" + written.err().message;
                return std::nullopt;
            }
            return file;
        }

        bool generate(const Instances::Entry& entry, const Draft& draft) {
            if (draft.icon == ICON_CUSTOM && draft.custom.empty()) {
                Instances::notify("先选择图标文件", true);
                return false;
            }
            std::filesystem::path folder = lastFolder.empty() ? Platform::desktopDirectory() : lastFolder;
            std::optional<std::filesystem::path> target =
                    Platform::pickShortcut(folder / blb::path_from_utf8(safeName(entry.info.id) + ".lnk"));
            if (!target) {
                return false;
            }
            lastFolder = target->parent_path();
            Platform::Shortcut shortcut;
            shortcut.file = *target;
            shortcut.arguments = "--launch \"" + entry.key + "\"" + (draft.headless ? " --headless" : "");
            shortcut.description = "用 B Launcher 启动 " + entry.info.id;
            shortcut.relative = Platform::insideLauncher(*target);
            if (draft.icon != ICON_LAUNCHER) {
                std::string bytes = iconBytes(entry, draft);
                if (bytes.empty()) {
                    Instances::notify("无法读取图标 " + Platform::display(draft.custom), true);
                    return false;
                }
                std::string problem;
                std::optional<std::filesystem::path> icon = storeIcon(entry, bytes, problem);
                if (!icon) {
                    Instances::notify(problem, true);
                    return false;
                }
                shortcut.icon = *icon;
            }
            std::string problem = Platform::createShortcut(shortcut);
            if (!problem.empty()) {
                Instances::notify(problem, true);
                return false;
            }
            Instances::notify(std::string(shortcut.relative ? "已创建快捷方式（相对路径） " : "已创建快捷方式 ") +
                                      Platform::display(*target),
                              false);
            return true;
        }

        void iconRow(const Instances::Entry& entry, float rowY, bool blocked) {
            const std::string key = entry.key;
            Page::label("图标", rowY);
            float selectX = Page::controlRight() - SELECT_W;
            float previewX = selectX - 12.0f - PREVIEW;
            float previewY = rowY + (Page::ROW_H - PREVIEW) * 0.5f;
            const Draft& draft = drafts[key];
            if (draft.icon == ICON_LAUNCHER) {
                Logo::draw(previewX, previewY, PREVIEW);
            } else if (draft.icon == ICON_INSTANCE) {
                if (uint32_t texture = InstancePage::icon(entry)) {
                    UiRender::texture(texture, previewX, previewY, PREVIEW, PREVIEW);
                }
            } else if (!draft.custom.empty()) {
                if (uint32_t texture = customTexture(draft.custom)) {
                    UiRender::texture(texture, previewX, previewY, PREVIEW, PREVIEW);
                }
            }
            auto choice = std::make_shared<ModeBinding>(
                    "shortcut#icon#" + key, [key] { return std::string(ICON_LABELS[drafts[key].icon]); },
                    [key](const std::string& label) {
                        for (int i = 0; i < (int) std::size(ICON_LABELS); i++) {
                            if (label == ICON_LABELS[i]) {
                                drafts[key].icon = i;
                            }
                        }
                    },
                    [] { return std::vector<std::string>(std::begin(ICON_LABELS), std::end(ICON_LABELS)); });
            SelectWidget widget("shortcut#icon#" + key, choice);
            widget.bounds(selectX, rowY + Page::FIELD_Y, SELECT_W, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void fileRow(const std::string& key, float rowY, bool blocked) {
            const std::string label = "图标文件";
            Page::label(label, rowY);
            Draft& draft = drafts[key];
            float pickX = Page::controlRight() - 27.0f;
            if (iconButton("shortcut#pick#" + key, 0xf07c, "选择图标", pickX, rowY + 6.0f, blocked)) {
                std::filesystem::path start = draft.custom.empty() ? Platform::exeDirectory() : draft.custom;
                if (std::optional<std::filesystem::path> picked = Platform::pickIcon(start)) {
                    draft.custom = *picked;
                }
            }
            float right = pickX - 8.0f;
            float labelRight =
                    Page::left() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            std::string shown = draft.custom.empty()
                    ? std::string("未选择")
                    : UiFont::fitStart(Platform::display(draft.custom), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                       std::max(0.0f, right - labelRight));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void headlessRow(const std::string& key, float rowY, bool blocked) {
            Page::label("静默启动", rowY);
            std::string id = "shortcut#headless#" + key;
            auto flag = std::make_shared<FlagBinding>(
                    id, [key] { return drafts[key].headless; }, [key](bool on) { drafts[key].headless = on; });
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::left(), rowY, Page::width(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
            if (!blocked && NewUi::hit(Page::left(), rowY, Page::controlRight() - 60.0f - Page::left(), Page::ROW_H)) {
                NewUi::markDoc(id + "#doc", HEADLESS_DOC, Page::left() + 6.0f, rowY + Page::ROW_H + 10.0f);
            }
        }
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        bool back = iconButton("shortcut#back", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, covered);
        const char* title = "生成快捷方式";
        Page::title(title, Page::left() + 30.0f, Page::TOP);
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::right() - nameX),
                        nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

        bool custom = drafts[key].icon == ICON_CUSTOM;
        int rows = custom ? 3 : 2;
        float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
        Page::Scroll body("shortcut#body#" + key, Page::TOP + 40.0f, buttonTop - 14.0f,
                          Page::CAPTION_H + rows * Page::ROW_H);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y = body.top - body.offset;
        Page::caption("快捷方式", y);
        float rowY = y + Page::CAPTION_H;
        Page::card(rowY, rows);
        iconRow(entry, rowY, blocked);
        rowY += Page::ROW_H;
        if (custom) {
            fileRow(key, rowY, blocked);
            rowY += Page::ROW_H;
        }
        headlessRow(key, rowY, blocked);
        body.end();

        float w = std::max(88.0f, Page::buttonWidth("生成", 0xf108));
        if (Page::button("shortcut#go", "生成", Page::right() - w, buttonTop, w, true, covered, 0xf108) &&
            generate(entry, drafts[key])) {
            back = true;
        }
        return back;
    }
}
