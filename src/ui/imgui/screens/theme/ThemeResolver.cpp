#include "ui/imgui/screens/theme/ThemeResolver.h"
#include "ui/imgui/screens/theme/ContrastGuard.h"
#include "ui/imgui/screens/theme/ThemeStore.h"

#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/theme/ColorMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace {
    constexpr int MAX_DEPTH = 8;
    constexpr size_t MAX_STOPS = 8;
    const char* const COMPLETE[] = {"plain", "dark", "light", "iridium-dark", "iridium-light"};

    bool complete(const std::string& id) {
        return std::any_of(std::begin(COMPLETE), std::end(COMPLETE), [&](const char* name) { return id == name; });
    }

    std::string text(const nlohmann::json& doc, const char* key) {
        auto it = doc.find(key);
        return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    void mergeObject(nlohmann::json& target, const nlohmann::json& source, const char* key) {
        auto it = source.find(key);
        if (it == source.end() || !it->is_object()) {
            return;
        }
        nlohmann::json& into = target[key];
        if (!into.is_object()) {
            into = nlohmann::json::object();
        }
        for (auto entry = it->begin(); entry != it->end(); ++entry) {
            into[entry.key()] = entry.value();
        }
    }

    std::string trim(std::string_view value) {
        size_t start = value.find_first_not_of(" \t");
        size_t end = value.find_last_not_of(" \t");
        return start == std::string_view::npos ? std::string() : std::string(value.substr(start, end - start + 1));
    }

    bool literal(const nlohmann::json& value) {
        return value.is_number() ||
                (value.is_string() && !value.get<std::string>().empty() && value.get<std::string>()[0] == '#');
    }

    double number(const nlohmann::json& doc, const char* key, double fallback) {
        auto it = doc.find(key);
        return it != doc.end() && it->is_number() ? it->get<double>() : fallback;
    }

    void flag(const nlohmann::json& doc, const char* key, bool& out) {
        auto it = doc.find(key);
        if (it != doc.end() && it->is_boolean()) {
            out = it->get<bool>();
        }
    }

    void readStyle(const nlohmann::json& node, ThemeStyle::Style& style) {
        if (!node.is_object()) {
            return;
        }
        auto radius = node.find("radius");
        if (radius != node.end() && radius->is_number()) {
            style.radiusScale = std::clamp(radius->get<float>(), 0.0f, 4.0f);
        } else if (radius != node.end() && radius->is_object()) {
            style.radiusScale = std::clamp((float) number(*radius, "scale", style.radiusScale), 0.0f, 4.0f);
            auto absolute = radius->find("absolute");
            if (absolute != radius->end() && absolute->is_number()) {
                style.radiusAbsolute = std::clamp(absolute->get<float>(), 0.0f, 64.0f);
            } else if (absolute != radius->end() && absolute->is_null()) {
                style.radiusAbsolute = -1.0f;
            }
            style.windowRadius = std::clamp((float) number(*radius, "window", style.windowRadius), 0.0f, 24.0f);
            std::string pills = text(*radius, "pills");
            if (!pills.empty()) {
                style.squarePills = pills == "square";
            }
        }
        flag(node, "icons", style.icons);
        flag(node, "sounds", style.sounds);
        flag(node, "animations", style.animations);
        flag(node, "outlines", style.outlines);
        flag(node, "rowDividers", style.rowDividers);
    }

    const nlohmann::json& sideOf(const nlohmann::json& value, bool light) {
        if (value.is_object()) {
            auto side = value.find(light ? "light" : "dark");
            if (side != value.end()) {
                return *side;
            }
        }
        return value;
    }

    void own(nlohmann::json& value, const std::string& owner) {
        if (value.is_string()) {
            value = owner + ":" + value.get<std::string>();
            return;
        }
        if (value.is_object()) {
            for (const char* side : {"light", "dark"}) {
                auto it = value.find(side);
                if (it != value.end() && it->is_string()) {
                    *it = owner + ":" + it->get<std::string>();
                }
            }
        }
    }

    bool point(const nlohmann::json& doc, const char* key, float out[2]) {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
            return false;
        }
        out[0] = (*it)[0].get<float>();
        out[1] = (*it)[1].get<float>();
        return true;
    }
}

const std::string& ThemeResolver::id() const {
    return themeId;
}

bool ThemeResolver::followsSystem() const {
    return text(merged, "base") == "system";
}

const std::vector<ThemeSetting>& ThemeResolver::settings() const {
    return settingList;
}

const std::vector<std::string>& ThemeResolver::problems() const {
    return problemList;
}

const std::vector<std::string>& ThemeResolver::loadIssues() const {
    return loadProblems;
}

const std::map<std::string, uint32_t>& ThemeResolver::variables() const {
    return vars;
}

const ThemeStyle::Style& ThemeResolver::style() const {
    return styleResult;
}

const ShellLayout::Spec& ThemeResolver::layout() const {
    return layoutResult;
}

const ThemePaintSpec::Spec& ThemeResolver::paints() const {
    return paintResult;
}

std::string ThemeResolver::accentPicker() const {
    std::string mode = text(merged, "accentPicker");
    return mode == "hue" || mode == "none" ? mode : std::string("full");
}

bool ThemeResolver::hasPress() const {
    return pressScript != nullptr;
}

std::optional<nlohmann::json> ThemeResolver::press(const nlohmann::json& event, std::string& error) {
    if (pressScript == nullptr) {
        return std::nullopt;
    }
    bool missing = false;
    auto output = pressScript->call("press", event, missing, error);
    pressScript->takeLogs();
    return output;
}

bool ThemeResolver::accentPinned() const {
    auto colors = merged.find("colors");
    return colors != merged.end() && colors->is_object() && colors->contains("accent");
}

ThemeFacts ThemeResolver::facts() const {
    ThemeFacts out;
    out.author = text(merged, "author");
    out.base = text(merged, "base");
    auto chain = merged.find("scripts");
    out.script = chain != merged.end() && chain->is_array() && !chain->empty() && text(chain->back(), "id") == themeId;
    out.animated = scriptRate() > 0.0 || paintResult.animated;
    for (const ThemePaintSpec::RegionSpec& region : paintResult.regions) {
        for (const ThemePaintSpec::Layer& layer : region.layers) {
            out.shader = out.shader || layer.kind == ThemePaintSpec::Kind::Shader;
        }
    }
    return out;
}

double ThemeResolver::scriptRate() const {
    auto animate = merged.find("animate");
    if (animate == merged.end() || !animate->is_object()) {
        return 0.0;
    }
    return std::clamp(number(*animate, "script", 0.0), 0.0, 60.0);
}

void ThemeResolver::problem(const std::string& text) {
    if (problemList.size() < 200) {
        problemList.push_back(text);
    }
}

bool ThemeResolver::load(const std::string& requested, bool systemDark) {
    loadProblems.clear();
    std::vector<nlohmann::json> chain;
    std::vector<std::string> ids;
    std::string id = requested;
    std::string base;
    for (int depth = 0; depth < MAX_DEPTH && !id.empty(); depth++) {
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            loadProblems.push_back(id + "：继承关系形成了循环");
            break;
        }
        std::optional<std::string> source = ThemeStore::read(id, "theme.json");
        if (!source) {
            loadProblems.push_back("找不到主题 " + id);
            break;
        }
        nlohmann::json doc = nlohmann::json::parse(*source, nullptr, false, true);
        if (!doc.is_object()) {
            loadProblems.push_back(id + "/theme.json 不是有效的 JSON");
            break;
        }
        if (base.empty()) {
            base = text(doc, "base");
        }
        std::string parent = text(doc, "extends");
        chain.push_back(std::move(doc));
        ids.push_back(id);
        if (parent.empty() && !complete(id)) {
            bool light = base == "light" || (base == "system" && !systemDark);
            parent = light ? "light" : "dark";
        }
        id = parent;
    }
    if (chain.empty()) {
        return false;
    }
    merged = nlohmann::json::object();
    settingList.clear();
    for (size_t i = chain.size(); i-- > 0;) {
        const nlohmann::json& doc = chain[i];
        for (const char* key : {"colors", "overrides", "tuning", "options", "contrast", "style", "animate"}) {
            mergeObject(merged, doc, key);
        }
        auto layout = doc.find("layout");
        if (layout != doc.end() && layout->is_object()) {
            merged["layout"].merge_patch(*layout);
        }
        auto paints = doc.find("paints");
        if (paints != doc.end() && paints->is_object()) {
            nlohmann::json& into = merged["paints"];
            if (!into.is_object()) {
                into = nlohmann::json::object();
            }
            for (auto entry = paints->begin(); entry != paints->end(); ++entry) {
                nlohmann::json region = entry.value();
                if (region.is_object()) {
                    auto layers = region.find("layers");
                    if (layers != region.end() && layers->is_array()) {
                        for (nlohmann::json& item : *layers) {
                            if (item.is_object() && item.contains("src")) {
                                own(item["src"], ids[i]);
                            }
                            auto textures = item.is_object() ? item.find("textures") : item.end();
                            if (textures != item.end() && textures->is_array()) {
                                for (nlohmann::json& texture : *textures) {
                                    own(texture, ids[i]);
                                }
                            }
                        }
                    }
                }
                into[entry.key()] = std::move(region);
            }
        }
        auto fonts = doc.find("fonts");
        if (fonts != doc.end() && fonts->is_object()) {
            nlohmann::json& into = merged["fonts"];
            if (!into.is_object()) {
                into = nlohmann::json::object();
            }
            for (auto entry = fonts->begin(); entry != fonts->end(); ++entry) {
                nlohmann::json value = entry.value();
                if (value.is_string()) {
                    own(value, ids[i]);
                } else if (value.is_object() && value.contains("file")) {
                    own(value["file"], ids[i]);
                }
                into[entry.key()] = std::move(value);
            }
        }
        auto sounds = doc.find("sounds");
        if (sounds != doc.end() && sounds->is_object()) {
            nlohmann::json& into = merged["sounds"];
            if (!into.is_object()) {
                into = nlohmann::json::object();
            }
            for (auto entry = sounds->begin(); entry != sounds->end(); ++entry) {
                nlohmann::json value = entry.value();
                if (value.is_string()) {
                    own(value, ids[i]);
                }
                into[entry.key()] = std::move(value);
            }
        }
        auto decorations = doc.find("decorations");
        if (decorations != doc.end() && decorations->is_array()) {
            nlohmann::json& into = merged["decorations"];
            if (!into.is_array()) {
                into = nlohmann::json::array();
            }
            for (const nlohmann::json& item : *decorations) {
                nlohmann::json entry = item;
                if (entry.is_object() && entry.contains("src")) {
                    own(entry["src"], ids[i]);
                }
                std::string name = entry.is_object() ? text(entry, "id") : std::string();
                auto same = name.empty() ? into.end()
                                         : std::find_if(into.begin(), into.end(), [&](const nlohmann::json& existing) {
                                               return existing.is_object() && text(existing, "id") == name;
                                           });
                if (same != into.end()) {
                    *same = std::move(entry);
                } else {
                    into.push_back(std::move(entry));
                }
            }
        }
        auto background = doc.find("background");
        if (background != doc.end() && background->is_object()) {
            nlohmann::json patchDoc = *background;
            if (patchDoc.contains("image")) {
                own(patchDoc["image"], ids[i]);
            }
            merged["background"].merge_patch(patchDoc);
        }
        for (const char* key : {"name", "base", "accentPicker"}) {
            auto it = doc.find(key);
            if (it != doc.end()) {
                merged[key] = *it;
            }
        }
        std::string scriptPath = text(doc, "script");
        if (!scriptPath.empty()) {
            merged["scripts"].push_back({{"id", ids[i]}, {"path", scriptPath}});
        }
        auto declared = doc.find("settings");
        if (declared != doc.end() && declared->is_array()) {
            for (const nlohmann::json& item : *declared) {
                ThemeSetting setting;
                setting.id = text(item, "id");
                setting.type = text(item, "type");
                setting.label = text(item, "label");
                bool known = setting.type == "toggle" || setting.type == "slider" || setting.type == "select" ||
                        setting.type == "color";
                if (setting.id.empty() || !known) {
                    loadProblems.push_back(ids[i] + "：设置项缺少 id 或类型不对");
                    continue;
                }
                auto fallback = item.find("default");
                setting.fallback = fallback != item.end() ? *fallback : nlohmann::json();
                setting.min = number(item, "min", 0.0);
                setting.max = number(item, "max", 1.0);
                setting.step = number(item, "step", 0.0);
                auto options = item.find("options");
                if (options != item.end() && options->is_array()) {
                    for (const nlohmann::json& option : *options) {
                        if (option.is_string()) {
                            setting.options.push_back(option.get<std::string>());
                        }
                    }
                }
                auto same = std::find_if(settingList.begin(), settingList.end(),
                                         [&](const ThemeSetting& existing) { return existing.id == setting.id; });
                if (same != settingList.end()) {
                    *same = std::move(setting);
                } else {
                    settingList.push_back(std::move(setting));
                }
            }
        }
    }
    for (const char* key : {"author", "version"}) {
        merged[key] = text(chain.front(), key);
    }
    themeId = requested;
    scripts.clear();
    pressScript = nullptr;
    return true;
}

nlohmann::json ThemeResolver::settingValues(const nlohmann::json& stored) const {
    nlohmann::json values = nlohmann::json::object();
    for (const ThemeSetting& setting : settingList) {
        auto it = stored.is_object() ? stored.find(setting.id) : stored.end();
        nlohmann::json value = setting.fallback;
        if (it != stored.end()) {
            bool fits = (setting.type == "toggle" && it->is_boolean()) ||
                    (setting.type == "slider" && it->is_number()) || (setting.type == "select" && it->is_string()) ||
                    (setting.type == "color" && (it->is_string() || it->is_number()));
            if (fits) {
                value = *it;
            }
        }
        if (setting.type == "slider" && value.is_number()) {
            value = std::clamp(value.get<double>(), std::min(setting.min, setting.max),
                               std::max(setting.min, setting.max));
        }
        if (setting.type == "color" && value.is_string()) {
            uint32_t argb = 0;
            value = ColorMath::parse(value.get<std::string>(), argb) ? nlohmann::json(argb) : nlohmann::json();
        }
        if (setting.type == "select" && value.is_string() && !setting.options.empty() &&
            std::find(setting.options.begin(), setting.options.end(), value.get<std::string>()) ==
                    setting.options.end()) {
            value = setting.options.front();
        }
        values[setting.id] = value;
    }
    return values;
}

bool ThemeResolver::reference(const std::string& name, const UiTheme::Palette& palette, uint32_t& result) const {
    if (!name.empty() && name[0] == '#') {
        return ColorMath::parse(name, result);
    }
    if (!name.empty() && name[0] == '$') {
        auto var = vars.find(name.substr(1));
        if (var == vars.end()) {
            return false;
        }
        result = var->second;
        return true;
    }
    UiTheme::Token token;
    if (!UiTheme::tokenOf(name, token)) {
        return false;
    }
    result = palette.tokens[token];
    return true;
}

bool ThemeResolver::colorOf(const nlohmann::json& value, const UiTheme::Palette& palette, uint32_t& out,
                            std::string& error) const {
    if (value.is_number()) {
        out = (uint32_t) value.get<double>();
        return true;
    }
    if (!value.is_string()) {
        error = "颜色值格式不对";
        return false;
    }
    std::string spec = trim(value.get<std::string>());
    if (!spec.empty() && spec[0] == '#') {
        if (ColorMath::parse(spec, out)) {
            return true;
        }
        error = "无法识别的颜色 " + spec;
        return false;
    }
    if (spec.rfind("alpha(", 0) == 0 && spec.back() == ')') {
        std::string inner = spec.substr(6, spec.size() - 7);
        size_t comma = inner.rfind(',');
        if (comma != std::string::npos) {
            std::string name = trim(std::string_view(inner).substr(0, comma));
            std::string amount = trim(std::string_view(inner).substr(comma + 1));
            char* end = nullptr;
            float fraction = std::strtof(amount.c_str(), &end);
            uint32_t baseColor = 0;
            if (end != amount.c_str() && *end == '\0' && reference(name, palette, baseColor)) {
                out = ColorMath::withAlpha(baseColor, Ui::round(std::clamp(fraction, 0.0f, 1.0f) * 255.0f));
                return true;
            }
        }
        error = "无法识别的颜色 " + spec;
        return false;
    }
    if (reference(spec, palette, out)) {
        return true;
    }
    error = "未知的颜色名 " + spec;
    return false;
}

void ThemeResolver::applyColors(UiTheme::Palette& palette, const nlohmann::json& colors) {
    if (!colors.is_object()) {
        return;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (auto entry = colors.begin(); entry != colors.end(); ++entry) {
            const nlohmann::json& value = sideOf(entry.value(), palette.light);
            if (literal(value) != (pass == 0)) {
                continue;
            }
            UiTheme::Token token;
            if (!UiTheme::tokenOf(entry.key(), token)) {
                problem("未知的颜色项 " + entry.key());
                continue;
            }
            uint32_t color = 0;
            std::string error;
            if (!colorOf(value, palette, color, error)) {
                problem(entry.key() + "：" + error);
                continue;
            }
            palette.tokens[token] = color;
        }
    }
}

bool ThemeResolver::readLayer(const nlohmann::json& node, const UiTheme::Palette& palette, const std::string& where,
                              ThemePaintSpec::Layer& layer) {
    using namespace ThemePaintSpec;
    if (!node.is_object()) {
        problem(where + "：图层应该是一个对象");
        return false;
    }
    layer.id = text(node, "id");
    std::string label = where + (layer.id.empty() ? std::string() : "/" + layer.id);
    std::string type = text(node, "type");
    auto color = [&](const nlohmann::json& value, uint32_t& out) {
        std::string error;
        if (colorOf(sideOf(value, palette.light), palette, out, error)) {
            return true;
        }
        problem(label + "：" + error);
        return false;
    };
    if (type == "solid") {
        layer.kind = Kind::Solid;
        uint32_t fill = 0;
        auto it = node.find("color");
        if (it == node.end() || !color(*it, fill)) {
            return false;
        }
        layer.stops.push_back({fill, 0.0f});
    } else if (type == "gradient") {
        std::string shape = text(node, "shape");
        if (!shape.empty() && shape != "linear" && shape != "radial") {
            problem(label + "：未知的渐变形状 " + shape);
        }
        layer.kind = shape == "radial" ? Kind::Radial : Kind::Linear;
        auto stops = node.find("stops");
        if (stops == node.end() || !stops->is_array() || stops->empty()) {
            problem(label + "：渐变缺少色标");
            return false;
        }
        if (stops->size() > MAX_STOPS) {
            problem(label + "：色标最多 8 个，多出的已忽略");
        }
        size_t count = std::min(stops->size(), (size_t) MAX_STOPS);
        float last = 0.0f;
        for (size_t k = 0; k < count; k++) {
            const nlohmann::json& item = (*stops)[k];
            const nlohmann::json* value = &item;
            float at = count > 1 ? (float) k / (float) (count - 1) : 0.0f;
            if (item.is_array() && item.size() == 2) {
                value = &item[0];
                if (item[1].is_number()) {
                    at = item[1].get<float>();
                }
            } else if (item.is_object() && item.contains("color")) {
                value = &item["color"];
                at = (float) number(item, "at", at);
            }
            uint32_t stop = 0;
            if (!color(*value, stop)) {
                return false;
            }
            at = std::max(last, std::clamp(at, 0.0f, 1.0f));
            last = at;
            layer.stops.push_back({stop, at});
        }
        layer.angle = (float) number(node, "angle", layer.angle);
        point(node, "center", layer.center);
        layer.radius = std::clamp((float) number(node, "radius", layer.radius), 0.01f, 4.0f);
        flag(node, "dither", layer.dither);
    } else if (type == "image") {
        layer.kind = Kind::Image;
        auto src = node.find("src");
        const nlohmann::json& picked = src != node.end() ? sideOf(*src, palette.light) : node;
        if (!picked.is_string()) {
            problem(label + "：贴图缺少 src");
            return false;
        }
        layer.source = picked.get<std::string>();
        std::string fit = text(node, "fit");
        if (fit == "contain") {
            layer.fit = Fit::Contain;
        } else if (fit == "stretch") {
            layer.fit = Fit::Stretch;
        } else if (fit == "tile") {
            layer.fit = Fit::Tile;
        } else if (!fit.empty() && fit != "cover") {
            problem(label + "：未知的 fit " + fit);
        }
        layer.scale = std::clamp((float) number(node, "scale", layer.scale), 0.05f, 20.0f);
        point(node, "offset", layer.offset);
        if (point(node, "align", layer.align)) {
            layer.align[0] = std::clamp(layer.align[0], 0.0f, 1.0f);
            layer.align[1] = std::clamp(layer.align[1], 0.0f, 1.0f);
        }
        auto tint = node.find("tint");
        if (tint != node.end() && !color(*tint, layer.tint)) {
            layer.tint = 0xFFFFFFFFu;
        }
        layer.angle = (float) number(node, "angle", 90.0);
    } else if (type == "shader") {
        layer.kind = Kind::Shader;
        auto src = node.find("src");
        if (src == node.end() || !src->is_string()) {
            problem(label + "：着色器缺少 src");
            return false;
        }
        layer.source = src->get<std::string>();
        auto textures = node.find("textures");
        if (textures != node.end() && textures->is_array()) {
            for (const nlohmann::json& texture : *textures) {
                if (texture.is_string() && layer.textures.size() < 4) {
                    layer.textures.push_back(texture.get<std::string>());
                }
            }
            if (textures->size() > 4) {
                problem(label + "：着色器最多 4 张贴图");
            }
        }
        flag(node, "halfRes", layer.halfRes);
        flag(node, "backdrop", layer.backdrop);
        auto fallback = node.find("fallback");
        uint32_t still = 0;
        if (fallback != node.end() && color(*fallback, still)) {
            layer.stops.push_back({still, 0.0f});
        }
    } else {
        problem(label + "：未知的图层类型 " + (type.empty() ? std::string("（空）") : type));
        return false;
    }
    layer.opacity = std::clamp((float) number(node, "opacity", 1.0), 0.0f, 1.0f);
    layer.inset = std::clamp((float) number(node, "inset", 0.0), 0.0f, 64.0f);
    std::string blend = text(node, "blend");
    if (blend == "multiply") {
        layer.blend = Blend::Multiply;
    } else if (blend == "screen") {
        layer.blend = Blend::Screen;
    } else if (blend == "add") {
        layer.blend = Blend::Add;
    } else if (!blend.empty() && blend != "normal") {
        problem(label + "：未知的混合模式 " + blend);
    }
    auto animate = node.find("animate");
    if (animate != node.end() && animate->is_object()) {
        std::string mode = text(*animate, "mode");
        if (mode == "shift") {
            layer.motion = Motion::Shift;
        } else if (mode == "rotate") {
            layer.motion = Motion::Rotate;
        } else if (mode == "hue") {
            layer.motion = Motion::Hue;
        } else if (!mode.empty()) {
            problem(label + "：未知的动画 " + mode);
        }
        layer.period = std::clamp((float) number(*animate, "period", layer.period), 0.5f, 3600.0f);
    }
    return true;
}

void ThemeResolver::readPaints(const UiTheme::Palette& palette, const nlohmann::json& tweaks) {
    using namespace ThemePaintSpec;
    paintResult = Spec();
    auto paints = merged.find("paints");
    if (paints != merged.end() && paints->is_object()) {
        for (auto entry = paints->begin(); entry != paints->end(); ++entry) {
            Region region;
            if (!regionOf(entry.key(), region)) {
                problem("未知的填充区域 " + entry.key());
                continue;
            }
            const nlohmann::json& node = entry.value();
            if (!node.is_object()) {
                problem(entry.key() + "：填充应该是一个对象");
                continue;
            }
            RegionSpec& spec = paintResult.regions[region];
            spec.blur = std::clamp((float) number(node, "blur", 0.0), 0.0f, 64.0f);
            auto layers = node.find("layers");
            if (layers == node.end()) {
                continue;
            }
            if (!layers->is_array()) {
                problem(entry.key() + "：layers 应该是数组");
                continue;
            }
            for (const nlohmann::json& item : *layers) {
                Layer layer;
                if (readLayer(item, palette, entry.key(), layer)) {
                    spec.layers.push_back(std::move(layer));
                }
            }
        }
    }
    if (tweaks.is_object()) {
        for (auto entry = tweaks.begin(); entry != tweaks.end(); ++entry) {
            const nlohmann::json& change = entry.value();
            if (!change.is_object()) {
                continue;
            }
            bool matched = false;
            for (RegionSpec& spec : paintResult.regions) {
                for (auto layer = spec.layers.begin(); layer != spec.layers.end();) {
                    if (layer->id != entry.key()) {
                        ++layer;
                        continue;
                    }
                    matched = true;
                    auto visible = change.find("visible");
                    if (visible != change.end() && visible->is_boolean() && !visible->get<bool>()) {
                        layer = spec.layers.erase(layer);
                        continue;
                    }
                    layer->opacity = std::clamp((float) number(change, "opacity", layer->opacity), 0.0f, 1.0f);
                    auto stops = change.find("stops");
                    if (stops != change.end() && stops->is_array()) {
                        for (size_t k = 0; k < stops->size() && k < layer->stops.size(); k++) {
                            uint32_t stop = 0;
                            std::string error;
                            if (colorOf((*stops)[k], palette, stop, error)) {
                                layer->stops[k].color = stop;
                            } else {
                                problem(entry.key() + "：" + error);
                            }
                        }
                    }
                    ++layer;
                }
            }
            if (!matched) {
                problem("ui() 修改了不存在的图层 " + entry.key());
            }
        }
    }
    for (const RegionSpec& spec : paintResult.regions) {
        for (const Layer& layer : spec.layers) {
            paintResult.animated = paintResult.animated || layer.motion != Motion::None || layer.kind == Kind::Shader;
        }
    }
    auto background = merged.find("background");
    if (background != merged.end() && background->is_object()) {
        BackgroundSpec& out = paintResult.background;
        auto image = background->find("image");
        if (image != background->end()) {
            const nlohmann::json& picked = sideOf(*image, palette.light);
            if (picked.is_string()) {
                out.image = picked.get<std::string>();
            }
        }
        std::string fit = text(*background, "fit");
        out.stretch = fit == "stretch";
        if (!fit.empty() && fit != "cover" && fit != "stretch") {
            problem("背景：未知的 fit " + fit);
        }
        auto blur = background->find("blur");
        if (blur != background->end() && blur->is_object()) {
            out.sidebarBlur = std::clamp((float) number(*blur, "sidebar", out.sidebarBlur), 0.0f, 80.0f);
            out.panelBlur = std::clamp((float) number(*blur, "panel", out.panelBlur), 0.0f, 80.0f);
        }
        auto fallback = background->find("default");
        if (fallback != background->end() && fallback->is_boolean()) {
            out.enabled = fallback->get<bool>() ? 1 : 0;
        }
    }
}

void ThemeResolver::readDecorations(bool light, const nlohmann::json& tweaks) {
    using namespace ThemePaintSpec;
    paintResult.decorations.clear();
    auto list = merged.find("decorations");
    if (list == merged.end() || !list->is_array()) {
        return;
    }
    static const char* const ANCHORS[] = {"top-left", "top",         "top-right", "left",        "center",
                                          "right",    "bottom-left", "bottom",    "bottom-right"};
    for (const nlohmann::json& node : *list) {
        if (!node.is_object()) {
            problem("装饰：每一项都应该是对象");
            continue;
        }
        Decoration item;
        item.id = text(node, "id");
        std::string label = "装饰" + (item.id.empty() ? std::string() : " " + item.id);
        bool visible = true;
        flag(node, "visible", visible);
        auto change = tweaks.is_object() && !item.id.empty() ? tweaks.find(item.id) : tweaks.end();
        bool changed = tweaks.is_object() && !item.id.empty() && change != tweaks.end() && change->is_object();
        if (changed) {
            flag(*change, "visible", visible);
        }
        if (!visible) {
            continue;
        }
        auto src = node.find("src");
        const nlohmann::json& picked = src != node.end() ? sideOf(*src, light) : node;
        if (!picked.is_string()) {
            problem(label + "：缺少 src");
            continue;
        }
        item.source = picked.get<std::string>();
        std::string region = text(node, "region");
        if (!region.empty()) {
            Region where;
            if (regionOf(region, where) && where <= CONTENT) {
                item.region = where;
            } else {
                problem(label + "：装饰只能放在 window、titlebar、sidebar 或 content");
            }
        }
        std::string anchor = text(node, "anchor");
        if (!anchor.empty()) {
            auto found = std::find_if(std::begin(ANCHORS), std::end(ANCHORS),
                                      [&](const char* name) { return anchor == name; });
            if (found == std::end(ANCHORS)) {
                problem(label + "：未知的锚点 " + anchor);
            } else {
                int index = (int) (found - std::begin(ANCHORS));
                item.anchor[0] = (index % 3) * 0.5f;
                item.anchor[1] = (index / 3) * 0.5f;
            }
        }
        point(node, "offset", item.offset);
        auto size = node.find("size");
        if (size != node.end() && size->is_array() && size->size() == 2) {
            for (int k = 0; k < 2; k++) {
                const nlohmann::json& value = (*size)[(size_t) k];
                if (value.is_number()) {
                    item.size[k] = std::clamp(value.get<float>(), 1.0f, 4096.0f);
                    item.extent[k] = Extent::Units;
                } else if (value.is_string() && value.get<std::string>() != "auto") {
                    std::string percent = value.get<std::string>();
                    char* end = nullptr;
                    float amount = std::strtof(percent.c_str(), &end);
                    if (end != percent.c_str() && std::string(end) == "%") {
                        item.size[k] = std::clamp(amount, 1.0f, 400.0f) / 100.0f;
                        item.extent[k] = Extent::Percent;
                    } else {
                        problem(label + "：无法识别的尺寸 " + percent);
                    }
                }
            }
        }
        std::string layer = text(node, "layer");
        item.above = layer == "above";
        if (!layer.empty() && layer != "above" && layer != "below") {
            problem(label + "：layer 只能是 above 或 below");
        }
        auto pages = node.find("pages");
        if (pages != node.end() && pages->is_array()) {
            for (const nlohmann::json& page : *pages) {
                if (page.is_string()) {
                    item.pages.push_back(page.get<std::string>());
                }
            }
        }
        item.opacity = std::clamp((float) number(node, "opacity", 1.0), 0.0f, 1.0f);
        if (changed) {
            item.opacity = std::clamp((float) number(*change, "opacity", item.opacity), 0.0f, 1.0f);
        }
        paintResult.decorations.push_back(std::move(item));
    }
}

void ThemeResolver::readSounds() {
    using namespace ThemePaintSpec;
    for (SoundSpec& sound : paintResult.sounds) {
        sound = SoundSpec();
    }
    paintResult.soundVolume = 1.0f;
    auto sounds = merged.find("sounds");
    if (sounds == merged.end() || !sounds->is_object()) {
        return;
    }
    static const char* const KINDS[SOUND_KINDS] = {"press", "tick", "release", "page", "pulse"};
    for (auto entry = sounds->begin(); entry != sounds->end(); ++entry) {
        const nlohmann::json& value = entry.value();
        if (entry.key() == "volume") {
            if (value.is_number()) {
                paintResult.soundVolume = std::clamp(value.get<float>(), 0.0f, 1.0f);
            } else {
                problem("sounds.volume 应该是 0 到 1 之间的数字");
            }
            continue;
        }
        auto kind =
                std::find_if(std::begin(KINDS), std::end(KINDS), [&](const char* name) { return entry.key() == name; });
        if (kind == std::end(KINDS)) {
            problem("未知的音效 " + entry.key());
            continue;
        }
        SoundSpec& out = paintResult.sounds[kind - std::begin(KINDS)];
        if (value.is_null()) {
            out.silent = true;
        } else if (value.is_string()) {
            out.source = value.get<std::string>();
        } else {
            problem("音效 " + entry.key() + " 应该是 WAV 文件名或 null");
        }
    }
}

void ThemeResolver::readFonts() {
    using namespace ThemePaintSpec;
    for (FontSpec& font : paintResult.fonts) {
        font = FontSpec();
    }
    auto fonts = merged.find("fonts");
    if (fonts == merged.end() || !fonts->is_object()) {
        return;
    }
    static const char* const ROLES[FONT_ROLES] = {"text", "bold", "display", "mono", "code"};
    bool given[FONT_ROLES] = {};
    for (auto entry = fonts->begin(); entry != fonts->end(); ++entry) {
        auto role =
                std::find_if(std::begin(ROLES), std::end(ROLES), [&](const char* name) { return entry.key() == name; });
        if (role == std::end(ROLES)) {
            problem("未知的字体角色 " + entry.key());
            continue;
        }
        int index = (int) (role - std::begin(ROLES));
        FontSpec& out = paintResult.fonts[index];
        const nlohmann::json& value = entry.value();
        if (value.is_string()) {
            out.source = value.get<std::string>();
        } else if (value.is_object()) {
            out.source = text(value, "file");
            out.face = std::clamp((int) number(value, "face", 0.0), 0, 255);
            out.weight = std::clamp((int) number(value, "weight", 0.0), 0, 1000);
            auto system = value.find("system");
            if (system != value.end() && system->is_string()) {
                out.system.push_back(system->get<std::string>());
            } else if (system != value.end() && system->is_array()) {
                for (const nlohmann::json& name : *system) {
                    if (name.is_string()) {
                        out.system.push_back(name.get<std::string>());
                    }
                }
            }
        }
        if (out.source.empty() && out.system.empty()) {
            problem("字体 " + entry.key() + " 缺少 file 或 system");
            continue;
        }
        given[index] = true;
    }
    if (given[FONT_TEXT]) {
        for (int role : {FONT_BOLD, FONT_DISPLAY, FONT_MONO}) {
            if (!given[role]) {
                paintResult.fonts[role] = paintResult.fonts[FONT_TEXT];
            }
        }
    }
}

nlohmann::json ThemeResolver::environment(const UiTheme::Palette& palette, const nlohmann::json& options) const {
    nlohmann::json base = nlohmann::json::object();
    for (int i = 0; i < UiTheme::TOKEN_COUNT; i++) {
        base[UiTheme::tokenName((UiTheme::Token) i)] = palette.tokens[i];
    }
    nlohmann::json system = {{"accent", nullptr}, {"dark", nullptr}};
    nlohmann::json image = nullptr;
    nlohmann::json values = nlohmann::json::object();
    if (current != nullptr) {
        if (current->system.accentKnown) {
            system["accent"] = current->system.accent;
        }
        if (current->system.darkKnown) {
            system["dark"] = current->system.dark;
        }
        image = current->image.json();
        values = settingValues(current->settings);
    }
    nlohmann::json opts = options.is_object() ? options : nlohmann::json::object();
    return {{"accent", palette.tokens[UiTheme::ACCENT]},
            {"light", palette.light},
            {"options", std::move(opts)},
            {"system", std::move(system)},
            {"image", std::move(image)},
            {"settings", std::move(values)},
            {"time", current != nullptr ? current->time : 0.0},
            {"base", std::move(base)}};
}

void ThemeResolver::applyOutput(UiTheme::Palette& palette, const nlohmann::json& output, const std::string& source) {
    if (output.is_null()) {
        return;
    }
    if (!output.is_object()) {
        problem(source + "：colors() 应该返回一个对象");
        return;
    }
    for (auto entry = output.begin(); entry != output.end(); ++entry) {
        uint32_t color = 0;
        const nlohmann::json& value = entry.value();
        if (value.is_number()) {
            color = (uint32_t) value.get<double>();
        } else if (!value.is_string() || !ColorMath::parse(value.get<std::string>(), color)) {
            problem(source + "：" + entry.key() + " 的值不是颜色");
            continue;
        }
        const std::string& key = entry.key();
        if (!key.empty() && key[0] == '$') {
            vars[key.substr(1)] = color;
            continue;
        }
        UiTheme::Token token;
        if (!UiTheme::tokenOf(key, token)) {
            problem(source + "：未知的颜色项 " + key);
            continue;
        }
        palette.tokens[token] = color;
    }
}

std::optional<nlohmann::json> ThemeResolver::run(ThemeScript& runner, const std::string& source,
                                                 const UiTheme::Palette& palette, const nlohmann::json& options) {
    bool missing = false;
    std::string error;
    auto output = runner.call("colors", environment(palette, options), missing, error);
    for (const std::string& line : runner.takeLogs()) {
        problem(source + "：" + line);
    }
    if (!error.empty()) {
        problem(source + "：" + error);
        return std::nullopt;
    }
    return output;
}

UiTheme::Palette ThemeResolver::resolve(const ThemeInputs& inputs) {
    current = &inputs;
    problemList = loadProblems;
    vars.clear();
    styleResult = ThemeStyle::Style();
    auto declaredStyle = merged.find("style");
    if (declaredStyle != merged.end()) {
        readStyle(*declaredStyle, styleResult);
    }
    nlohmann::json uiStyle;
    nlohmann::json uiLayout;
    nlohmann::json uiPaints;
    nlohmann::json uiDecorations;
    layoutResult = ShellLayout::Spec();
    auto declaredLayout = merged.find("layout");
    if (declaredLayout != merged.end()) {
        ShellLayout::read(*declaredLayout, layoutResult);
    }
    std::string picker = text(merged, "accentPicker");
    if (!picker.empty() && picker != "full" && picker != "hue" && picker != "none") {
        problem("accentPicker 只能是 full、hue 或 none");
    }
    UiTheme::Palette palette;
    std::string base = text(merged, "base");
    palette.light = base == "light" || (base == "system" && inputs.system.darkKnown && !inputs.system.dark);
    auto colors = merged.find("colors");
    bool pinned = colors != merged.end() && colors->is_object() && colors->contains("accent");
    if (!pinned) {
        palette.tokens[UiTheme::ACCENT] = inputs.accent;
    }
    if (colors != merged.end()) {
        applyColors(palette, *colors);
    }
    auto tuning = merged.find("tuning");
    if (tuning != merged.end() && tuning->is_object()) {
        for (auto entry = tuning->begin(); entry != tuning->end(); ++entry) {
            UiTheme::Scalar scalar;
            if (!UiTheme::scalarOf(entry.key(), scalar)) {
                problem("未知的调整项 " + entry.key());
                continue;
            }
            if (!entry.value().is_number()) {
                problem(entry.key() + " 应该是数字");
                continue;
            }
            palette.scalars[scalar] = entry.value().get<float>();
        }
    }
    auto backdrop = merged.find("background");
    if (backdrop != merged.end() && backdrop->is_object()) {
        auto dim = backdrop->find("dim");
        if (dim != backdrop->end() && dim->is_object()) {
            palette.scalars[UiTheme::SIDEBAR_SHADE] =
                    std::clamp((float) number(*dim, "sidebar", palette.scalars[UiTheme::SIDEBAR_SHADE]), 0.0f, 1.0f);
            palette.scalars[UiTheme::PANEL_SHADE] =
                    std::clamp((float) number(*dim, "panel", palette.scalars[UiTheme::PANEL_SHADE]), 0.0f, 1.0f);
        }
    }
    pressScript = nullptr;
    auto chain = merged.find("scripts");
    if (chain != merged.end() && chain->is_array()) {
        auto options = merged.find("options");
        const nlohmann::json& opts = options != merged.end() ? *options : nlohmann::json::object();
        for (const nlohmann::json& ref : *chain) {
            std::string owner = text(ref, "id");
            std::string path = text(ref, "path");
            std::string name = owner + "/" + path;
            LoadedScript& entry = scripts[name];
            if (!entry.runner && entry.error.empty()) {
                std::optional<std::string> source = ThemeStore::read(owner, path);
                auto loaded = std::make_unique<ThemeScript>();
                std::string error;
                if (!source) {
                    entry.error = "找不到脚本 " + name;
                } else if (!loaded->load(*source, path, error)) {
                    entry.error = name + "：" + error;
                } else {
                    entry.runner = std::move(loaded);
                }
            }
            if (!entry.runner) {
                problem(entry.error);
                continue;
            }
            if (entry.runner->has("press")) {
                pressScript = entry.runner.get();
            }
            if (auto output = run(*entry.runner, name, palette, opts)) {
                applyOutput(palette, *output, name);
            }
            bool missing = false;
            std::string error;
            auto tweaks = entry.runner->call("ui", environment(palette, opts), missing, error);
            if (!error.empty()) {
                problem(name + "：" + error);
                continue;
            }
            if (!tweaks || !tweaks->is_object()) {
                continue;
            }
            auto take = [&](const char* key, nlohmann::json& into) {
                auto found = tweaks->find(key);
                if (found != tweaks->end()) {
                    into.merge_patch(*found);
                }
            };
            take("style", uiStyle);
            take("layout", uiLayout);
            take("paints", uiPaints);
            take("decorations", uiDecorations);
        }
    }
    readStyle(uiStyle, styleResult);
    ShellLayout::read(uiLayout, layoutResult);
    auto overrides = merged.find("overrides");
    if (overrides != merged.end()) {
        applyColors(palette, *overrides);
    }
    auto contrast = merged.find("contrast");
    if (contrast != merged.end() && contrast->is_object()) {
        auto guard = contrast->find("guard");
        if (guard != contrast->end() && guard->is_boolean() && guard->get<bool>()) {
            std::vector<std::string> exclude;
            auto skip = contrast->find("exclude");
            if (skip != contrast->end() && skip->is_array()) {
                for (const nlohmann::json& item : *skip) {
                    if (item.is_string()) {
                        exclude.push_back(item.get<std::string>());
                    }
                }
            } else {
                exclude.push_back("textFaint");
            }
            ContrastGuard::apply(palette, (float) std::clamp(number(*contrast, "min", 4.5), 1.0, 21.0), exclude);
        }
    }
    readPaints(palette, uiPaints);
    readDecorations(palette.light, uiDecorations);
    readFonts();
    readSounds();
    current = nullptr;
    return palette;
}
