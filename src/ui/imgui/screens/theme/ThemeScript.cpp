#include "ui/imgui/screens/theme/ThemeScript.h"
#include "ui/imgui/screens/theme/MaterialYou.h"

#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/theme/ColorMath.h"

#include "duktape.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct ThemeScript::State {
    size_t used = 0;
    bool running = false;
    std::chrono::steady_clock::time_point deadline;
    std::vector<std::string> logs;
};

namespace {
    constexpr size_t HEAP_LIMIT = 4 * 1024 * 1024;
    constexpr size_t LOG_LIMIT = 64;
    constexpr auto CALL_LIMIT = std::chrono::milliseconds(50);

    struct alignas(16) Header {
        size_t size;
    };

    ThemeScript::State* stateOf(duk_context* ctx) {
        duk_memory_functions functions;
        duk_get_memory_functions(ctx, &functions);
        return (ThemeScript::State*) functions.udata;
    }

    void* allocate(void* udata, duk_size_t size) {
        auto* state = (ThemeScript::State*) udata;
        if (state->used + size > HEAP_LIMIT) {
            return nullptr;
        }
        auto* header = (Header*) std::malloc(sizeof(Header) + size);
        if (header == nullptr) {
            return nullptr;
        }
        header->size = size;
        state->used += size;
        return header + 1;
    }

    void release(void* udata, void* pointer) {
        if (pointer == nullptr) {
            return;
        }
        auto* state = (ThemeScript::State*) udata;
        Header* header = (Header*) pointer - 1;
        state->used -= header->size;
        std::free(header);
    }

    void* reallocate(void* udata, void* pointer, duk_size_t size) {
        if (pointer == nullptr) {
            return allocate(udata, size);
        }
        if (size == 0) {
            release(udata, pointer);
            return nullptr;
        }
        auto* state = (ThemeScript::State*) udata;
        Header* header = (Header*) pointer - 1;
        size_t old = header->size;
        if (size > old && state->used + (size - old) > HEAP_LIMIT) {
            return nullptr;
        }
        auto* moved = (Header*) std::realloc(header, sizeof(Header) + size);
        if (moved == nullptr) {
            return nullptr;
        }
        moved->size = size;
        state->used = state->used - old + size;
        return moved + 1;
    }

    void fatal(void*, const char* message) {
        std::fprintf(stderr, "[ThemeScript] fatal: %s\n", message != nullptr ? message : "");
        std::abort();
    }

    bool absent(duk_context* ctx, duk_idx_t index) {
        return duk_get_top(ctx) <= index || duk_is_undefined(ctx, index);
    }

    uint32_t colorArg(duk_context* ctx, duk_idx_t index) {
        duk_require_number(ctx, index);
        return (uint32_t) duk_to_uint32(ctx, index);
    }

    duk_ret_t pushColor(duk_context* ctx, uint32_t argb) {
        duk_push_number(ctx, (double) argb);
        return 1;
    }

    duk_ret_t colorParse(duk_context* ctx) {
        const char* text = duk_require_string(ctx, 0);
        uint32_t argb = 0;
        if (!ColorMath::parse(text, argb)) {
            return duk_error(ctx, DUK_ERR_TYPE_ERROR, "invalid color: %s", text);
        }
        return pushColor(ctx, argb);
    }

    duk_ret_t colorFormat(duk_context* ctx) {
        uint32_t argb = colorArg(ctx, 0);
        char out[16] = {};
        {
            std::string text = ColorMath::format(argb);
            std::snprintf(out, sizeof(out), "%s", text.c_str());
        }
        duk_push_string(ctx, out);
        return 1;
    }

    duk_ret_t colorRgb(duk_context* ctx) {
        int r = duk_require_int(ctx, 0);
        int g = duk_require_int(ctx, 1);
        int b = duk_require_int(ctx, 2);
        int a = absent(ctx, 3) ? 255 : duk_require_int(ctx, 3);
        auto clamp = [](int v) { return (uint32_t) (v < 0 ? 0 : v > 255 ? 255 : v); };
        return pushColor(ctx, (clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b));
    }

    duk_ret_t colorMix(duk_context* ctx) {
        return pushColor(ctx, UiRender::mix(colorArg(ctx, 0), colorArg(ctx, 1), (float) duk_require_number(ctx, 2)));
    }

    duk_ret_t colorAdd(duk_context* ctx) {
        return pushColor(ctx, ColorMath::add(colorArg(ctx, 0), duk_require_int(ctx, 1)));
    }

    duk_ret_t colorScale(duk_context* ctx) {
        return pushColor(ctx, ColorMath::scale(colorArg(ctx, 0), (float) duk_require_number(ctx, 1)));
    }

    duk_ret_t colorAlpha(duk_context* ctx) {
        return pushColor(ctx, ColorMath::withAlpha(colorArg(ctx, 0), duk_require_int(ctx, 1)));
    }

    duk_ret_t colorInk(duk_context* ctx) {
        return pushColor(ctx, UiTheme::inkOn(colorArg(ctx, 0)));
    }

    int alphaArg(duk_context* ctx, duk_idx_t index) {
        return absent(ctx, index) ? 255 : duk_require_int(ctx, index);
    }

    void putNumber(duk_context* ctx, const char* key, double value) {
        duk_push_number(ctx, value);
        duk_put_prop_string(ctx, -2, key);
    }

    duk_ret_t colorHct(duk_context* ctx) {
        return pushColor(
                ctx,
                MaterialYou::hct(duk_require_number(ctx, 0), duk_require_number(ctx, 1), duk_require_number(ctx, 2)));
    }

    duk_ret_t colorToHct(duk_context* ctx) {
        double h = 0.0;
        double c = 0.0;
        double t = 0.0;
        MaterialYou::toHct(colorArg(ctx, 0), h, c, t);
        duk_push_object(ctx);
        putNumber(ctx, "h", h);
        putNumber(ctx, "c", c);
        putNumber(ctx, "t", t);
        return 1;
    }

    duk_ret_t colorTone(duk_context* ctx) {
        return pushColor(
                ctx,
                MaterialYou::tone(duk_require_number(ctx, 0), duk_require_number(ctx, 1), duk_require_number(ctx, 2)));
    }

    duk_ret_t colorHarmonize(duk_context* ctx) {
        return pushColor(ctx, MaterialYou::harmonize(colorArg(ctx, 0), colorArg(ctx, 1)));
    }

    duk_ret_t colorOklch(duk_context* ctx) {
        return pushColor(ctx,
                         ColorMath::fromOklch((float) duk_require_number(ctx, 0), (float) duk_require_number(ctx, 1),
                                              (float) duk_require_number(ctx, 2), alphaArg(ctx, 3)));
    }

    duk_ret_t colorToOklch(duk_context* ctx) {
        ColorMath::Oklch value = ColorMath::toOklch(colorArg(ctx, 0));
        duk_push_object(ctx);
        putNumber(ctx, "l", value.l);
        putNumber(ctx, "c", value.c);
        putNumber(ctx, "h", value.h);
        return 1;
    }

    duk_ret_t colorHsl(duk_context* ctx) {
        return pushColor(ctx,
                         ColorMath::fromHsl(duk_require_number(ctx, 0), duk_require_number(ctx, 1),
                                            duk_require_number(ctx, 2), alphaArg(ctx, 3)));
    }

    duk_ret_t colorHsl2(duk_context* ctx) {
        return pushColor(ctx,
                         ColorMath::fromHsl2(duk_require_number(ctx, 0), duk_require_number(ctx, 1),
                                             duk_require_number(ctx, 2), alphaArg(ctx, 3)));
    }

    duk_ret_t colorToHsl(duk_context* ctx) {
        ColorMath::Hsl value = ColorMath::toHsl(colorArg(ctx, 0));
        duk_push_object(ctx);
        putNumber(ctx, "h", value.h);
        putNumber(ctx, "s", value.s);
        putNumber(ctx, "l", value.l);
        return 1;
    }

    duk_ret_t colorContrast(duk_context* ctx) {
        duk_push_number(ctx, ColorMath::contrast(colorArg(ctx, 0), colorArg(ctx, 1)));
        return 1;
    }

    duk_ret_t colorEnsureContrast(duk_context* ctx) {
        float ratio = absent(ctx, 2) ? 4.5f : (float) duk_require_number(ctx, 2);
        return pushColor(ctx, ColorMath::ensureContrast(colorArg(ctx, 0), colorArg(ctx, 1), ratio));
    }

    duk_ret_t colorOver(duk_context* ctx) {
        return pushColor(ctx, ColorMath::over(colorArg(ctx, 0), colorArg(ctx, 1)));
    }

    duk_ret_t colorRotate(duk_context* ctx) {
        return pushColor(ctx, ColorMath::rotate(colorArg(ctx, 0), (float) duk_require_number(ctx, 1)));
    }

    duk_ret_t colorLighten(duk_context* ctx) {
        return pushColor(ctx, ColorMath::lighten(colorArg(ctx, 0), (float) duk_require_number(ctx, 1)));
    }

    duk_ret_t colorDarken(duk_context* ctx) {
        return pushColor(ctx, ColorMath::lighten(colorArg(ctx, 0), -(float) duk_require_number(ctx, 1)));
    }

    duk_ret_t scriptLog(duk_context* ctx) {
        ThemeScript::State* state = stateOf(ctx);
        if (state->logs.size() < LOG_LIMIT) {
            state->logs.push_back(duk_safe_to_string(ctx, 0));
        }
        return 0;
    }

    const duk_function_list_entry COLOR_FUNCTIONS[] = {
            {"parse", colorParse, 1},
            {"format", colorFormat, 1},
            {"rgb", colorRgb, DUK_VARARGS},
            {"mix", colorMix, 3},
            {"add", colorAdd, 2},
            {"scale", colorScale, 2},
            {"alpha", colorAlpha, 2},
            {"ink", colorInk, 1},
            {"hct", colorHct, 3},
            {"toHct", colorToHct, 1},
            {"tone", colorTone, 3},
            {"harmonize", colorHarmonize, 2},
            {"oklch", colorOklch, DUK_VARARGS},
            {"toOklch", colorToOklch, 1},
            {"hsl", colorHsl, DUK_VARARGS},
            {"hsl2", colorHsl2, DUK_VARARGS},
            {"toHsl", colorToHsl, 1},
            {"contrast", colorContrast, 2},
            {"ensureContrast", colorEnsureContrast, DUK_VARARGS},
            {"over", colorOver, 2},
            {"rotate", colorRotate, 2},
            {"lighten", colorLighten, 2},
            {"darken", colorDarken, 2},
            {nullptr, nullptr, 0},
    };

    duk_ret_t install(duk_context* ctx, void*) {
        duk_push_global_object(ctx);
        duk_push_object(ctx);
        duk_put_function_list(ctx, -1, COLOR_FUNCTIONS);
        duk_put_prop_string(ctx, -2, "color");
        duk_push_c_function(ctx, scriptLog, 1);
        duk_put_prop_string(ctx, -2, "log");
        duk_pop(ctx);
        return 0;
    }

    struct Source {
        const std::string* text;
        const std::string* name;
    };

    duk_ret_t evaluate(duk_context* ctx, void* udata) {
        auto* source = (Source*) udata;
        duk_push_string(ctx, source->name->c_str());
        duk_compile_lstring_filename(ctx, 0, source->text->data(), source->text->size());
        duk_call(ctx, 0);
        duk_pop(ctx);
        return 0;
    }

    struct Invocation {
        const char* function;
        const std::string* env;
        bool missing = false;
    };

    duk_ret_t invoke(duk_context* ctx, void* udata) {
        auto* invocation = (Invocation*) udata;
        duk_push_global_object(ctx);
        duk_get_prop_string(ctx, -1, invocation->function);
        if (!duk_is_function(ctx, -1)) {
            invocation->missing = true;
            duk_push_null(ctx);
            return 1;
        }
        duk_push_lstring(ctx, invocation->env->data(), invocation->env->size());
        duk_json_decode(ctx, -1);
        duk_call(ctx, 1);
        if (duk_is_undefined(ctx, -1)) {
            duk_pop(ctx);
            duk_push_null(ctx);
        }
        duk_json_encode(ctx, -1);
        return 1;
    }

    std::string describe(duk_context* ctx) {
        int line = 0;
        if (duk_is_error(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "lineNumber");
            if (duk_is_number(ctx, -1)) {
                line = (int) duk_get_number(ctx, -1);
            }
            duk_pop(ctx);
        }
        std::string text = duk_safe_to_string(ctx, -1);
        return line > 0 ? "第 " + std::to_string(line) + " 行：" + text : text;
    }
}

extern "C" int blb_duk_timeout(void* udata) {
    auto* state = (ThemeScript::State*) udata;
    return state != nullptr && state->running && std::chrono::steady_clock::now() > state->deadline ? 1 : 0;
}

ThemeScript::ThemeScript() : state(std::make_unique<State>()) {
    duk_context* ctx = duk_create_heap(allocate, reallocate, release, state.get(), fatal);
    if (ctx == nullptr) {
        return;
    }
    if (duk_safe_call(ctx, install, nullptr, 0, 1) != DUK_EXEC_SUCCESS) {
        duk_destroy_heap(ctx);
        return;
    }
    duk_pop(ctx);
    context = ctx;
}

ThemeScript::~ThemeScript() {
    if (context != nullptr) {
        duk_destroy_heap((duk_context*) context);
    }
}

bool ThemeScript::ready() const {
    return context != nullptr;
}

bool ThemeScript::load(const std::string& source, const std::string& name, std::string& error) {
    if (context == nullptr) {
        error = "无法创建脚本环境";
        return false;
    }
    auto* ctx = (duk_context*) context;
    Source input{&source, &name};
    state->running = true;
    state->deadline = std::chrono::steady_clock::now() + CALL_LIMIT;
    duk_int_t result = duk_safe_call(ctx, evaluate, &input, 0, 1);
    state->running = false;
    if (result != DUK_EXEC_SUCCESS) {
        error = describe(ctx);
        duk_pop(ctx);
        return false;
    }
    duk_pop(ctx);
    return true;
}

std::optional<nlohmann::json> ThemeScript::call(const char* function, const nlohmann::json& env, bool& missing,
                                                std::string& error) {
    missing = false;
    if (context == nullptr) {
        error = "无法创建脚本环境";
        return std::nullopt;
    }
    auto* ctx = (duk_context*) context;
    std::string text = env.dump();
    Invocation invocation{function, &text};
    state->running = true;
    state->deadline = std::chrono::steady_clock::now() + CALL_LIMIT;
    duk_int_t result = duk_safe_call(ctx, invoke, &invocation, 0, 1);
    state->running = false;
    if (result != DUK_EXEC_SUCCESS) {
        error = describe(ctx);
        duk_pop(ctx);
        return std::nullopt;
    }
    missing = invocation.missing;
    std::string output = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : "null";
    duk_pop(ctx);
    if (missing) {
        return std::nullopt;
    }
    nlohmann::json parsed = nlohmann::json::parse(output, nullptr, false);
    if (parsed.is_discarded()) {
        error = "脚本返回的内容无法识别";
        return std::nullopt;
    }
    return parsed;
}

bool ThemeScript::has(const char* function) {
    if (context == nullptr) {
        return false;
    }
    auto* ctx = (duk_context*) context;
    bool found = duk_get_global_string(ctx, function) != 0 && duk_is_function(ctx, -1);
    duk_pop(ctx);
    return found;
}

std::vector<std::string> ThemeScript::takeLogs() {
    std::vector<std::string> out;
    out.swap(state->logs);
    return out;
}
