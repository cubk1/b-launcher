#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "ShaderUtil.h"
#include "UiGL.h"
#include "IconShader.inl"
#include "../panel/UiRender.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

using namespace UiGL;

namespace ShaderUtil {

    std::vector<Prim> blurQueue;
    std::vector<Prim> bloomQueue;

    static std::vector<Prim> pendingPrims;
    static bool captureValid = false;
    static int captureFrame = -1;

    struct Shader {
        GLuint program = 0;
        GLuint vao = 0;
        std::unordered_map<std::string, GLint> loc;
        GLint operator[](const char* name) const {
            auto it = loc.find(name);
            return it == loc.end() ? -1 : it->second;
        }
    };

    static Shader blurShader;
    static bool blurFailed;
    static Shader roundRectShader;
    static bool roundRectFailed;
    static Shader roundTextureShader;
    static bool roundTextureFailed;
    static Shader roundTexShader;
    static bool roundTexFailed;
    static Shader roundRingShader;
    static bool roundRingFailed;
    static Shader shadowShader;
    static bool shadowFailed;
    static Shader pickerShader;
    static bool pickerFailed;
    static Shader bloomShader;
    static bool bloomFailed;
    static Shader dualDownShader;
    static Shader dualUpShader;
    static bool dualFailed;
    static Shader checkShader;
    static bool checkFailed;
    static Shader arcShader;
    static bool arcFailed;
    static Shader iconShader;
    static bool iconFailed;
    static Shader shellShader;
    static bool shellFailed;

    static GLuint captureTextureId;
    static int captureWidth;
    static int captureHeight;
    static int captureGeneration;
    static GLuint dualTextures[6];
    static GLuint dualFbos[6];
    static int dualWidths[6];
    static int dualHeights[6];
    static int dualGeneration = -1;
    static int dualIterations;
    static float dualOffset;
    static GLuint shapeTexture;

    static float mvpFloats[16];
    static int fbWidth;
    static int fbHeight;
    static GLint currentScissor[4];
    static bool scissorValid;

    static GLuint compileStage(GLenum type, const char* const* sources, int count) {
        GLuint shader = CreateShader(type);
        ShaderSource(shader, count, sources, nullptr);
        CompileShader(shader);
        GLint status = 0;
        GetShaderiv(shader, UGL_COMPILE_STATUS, &status);
        if (status == 0) {
            GLint length = 0;
            GetShaderiv(shader, UGL_INFO_LOG_LENGTH, &length);
            std::string log(length > 1 ? length : 1, '\0');
            GetShaderInfoLog(shader, length, nullptr, &log[0]);
            DeleteShader(shader);
            fprintf(stderr, "[ShaderUtil] Compile failed: %s\n", log.c_str());
            return 0;
        }
        return shader;
    }

    static bool compileShader(Shader& out, const char* const* vertSrc, int vertCount, const char* const* fragSrc,
                              int fragCount, std::initializer_list<const char*> uniforms) {
        GLuint vs = compileStage(UGL_VERTEX_SHADER, vertSrc, vertCount);
        if (vs == 0) {
            return false;
        }
        GLuint fs = compileStage(UGL_FRAGMENT_SHADER, fragSrc, fragCount);
        if (fs == 0) {
            DeleteShader(vs);
            return false;
        }
        GLuint program = CreateProgram();
        AttachShader(program, vs);
        AttachShader(program, fs);
        LinkProgram(program);
        GLint status = 0;
        GetProgramiv(program, UGL_LINK_STATUS, &status);
        if (status == 0) {
            GLint length = 0;
            GetProgramiv(program, UGL_INFO_LOG_LENGTH, &length);
            std::string log(length > 1 ? length : 1, '\0');
            GetProgramInfoLog(program, length, nullptr, &log[0]);
            fprintf(stderr, "[ShaderUtil] Link failed: %s\n", log.c_str());
            DeleteProgram(program);
            DeleteShader(vs);
            DeleteShader(fs);
            return false;
        }
        DetachShader(program, vs);
        DetachShader(program, fs);
        DeleteShader(vs);
        DeleteShader(fs);
        out.program = program;
        GenVertexArrays(1, &out.vao);
        for (const char* uniform : uniforms) {
            out.loc[uniform] = GetUniformLocation(program, uniform);
        }
        return true;
    }

    static bool compileShader(Shader& out, const char* vert, const char* frag,
                              std::initializer_list<const char*> uniforms) {
        return compileShader(out, &vert, 1, &frag, 1, uniforms);
    }

    static const char* RECT_VERT = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;

out vec2 v_uv;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );
    vec2 corner = corners[gl_VertexID];
    vec2 pos = u_rect.xy + corner * u_rect.zw;
    gl_Position = u_proj * vec4(pos, 0.0, 1.0);
    v_uv = corner;
}
)GLSL";

    static const char* ID_VERT = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;

out vec2 v_uv;

void main() {
    int id = gl_VertexID;
    vec2 corner = vec2(float((id == 1 || id == 2 || id == 4) ? 1 : 0), float((id == 2 || id == 4 || id == 5) ? 1 : 0));
    vec2 pos = u_rect.xy + corner * u_rect.zw;
    gl_Position = u_proj * vec4(pos, 0.0, 1.0);
    v_uv = corner;
}
)GLSL";

    static const char* FULLSCREEN_VERT = R"GLSL(#version 150 core

out vec2 v_uv;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 corner = corners[gl_VertexID];
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

    static bool ensureDualKawase() {
        if (dualDownShader.program != 0) {
            return true;
        }
        if (dualFailed) {
            return false;
        }
        const char* down = R"GLSL(#version 150 core

uniform sampler2D u_source;
uniform vec2  u_halfPixel;
uniform float u_offset;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 o = u_halfPixel * u_offset;
    vec4 sum = textureLod(u_source, v_uv, 0.0) * 4.0;
    sum += textureLod(u_source, v_uv - o, 0.0);
    sum += textureLod(u_source, v_uv + o, 0.0);
    sum += textureLod(u_source, v_uv + vec2(o.x, -o.y), 0.0);
    sum += textureLod(u_source, v_uv - vec2(o.x, -o.y), 0.0);
    fragColor = vec4((sum / 8.0).rgb, 1.0);
}
)GLSL";
        const char* up = R"GLSL(#version 150 core

uniform sampler2D u_source;
uniform vec2  u_halfPixel;
uniform float u_offset;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 o = u_halfPixel * u_offset;
    vec4 sum = textureLod(u_source, v_uv + vec2(-o.x * 2.0, 0.0), 0.0);
    sum += textureLod(u_source, v_uv + vec2(-o.x, o.y), 0.0) * 2.0;
    sum += textureLod(u_source, v_uv + vec2(0.0, o.y * 2.0), 0.0);
    sum += textureLod(u_source, v_uv + vec2(o.x, o.y), 0.0) * 2.0;
    sum += textureLod(u_source, v_uv + vec2(o.x * 2.0, 0.0), 0.0);
    sum += textureLod(u_source, v_uv + vec2(o.x, -o.y), 0.0) * 2.0;
    sum += textureLod(u_source, v_uv + vec2(0.0, -o.y * 2.0), 0.0);
    sum += textureLod(u_source, v_uv + vec2(-o.x, -o.y), 0.0) * 2.0;
    fragColor = vec4((sum / 12.0).rgb, 1.0);
}
)GLSL";
        if (!compileShader(dualDownShader, FULLSCREEN_VERT, down, {"u_source", "u_halfPixel", "u_offset"}) ||
            !compileShader(dualUpShader, FULLSCREEN_VERT, up, {"u_source", "u_halfPixel", "u_offset"})) {
            dualFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureBlurShader() {
        if (blurShader.program != 0) {
            return true;
        }
        if (blurFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform sampler2D u_screen;
uniform vec2  u_screenSize;
uniform vec2  u_rectSize;
uniform float u_radius;
uniform vec4  u_corner;
uniform float u_blurRadius;
uniform float u_gamma;
uniform vec4  u_tint;
uniform float u_direct;

in vec2 v_uv;
out vec4 fragColor;

vec4 sampleBlurred(vec2 uv, float radius, float gamma) {
    float sigma = max(radius, 0.5);
    float lod = clamp(floor(log2(sigma)) - 1.0, 0.0, 6.0);
    float scale = exp2(lod);
    float sigmaLod = sigma / scale;
    vec2 texel = scale / u_screenSize;

    float support = min(ceil(sigmaLod * 2.0), 8.0);
    float twoSigmaSq = 2.0 * sigmaLod * sigmaLod;

    vec4 sum = vec4(0.0);
    float norm = 0.0;
    for (float oy = -support; oy <= support; oy += 1.0) {
        for (float ox = -support; ox <= support; ox += 1.0) {
            float d2 = ox * ox + oy * oy;
            if (d2 > support * support) continue;
            float k = exp(-d2 / twoSigmaSq);
            vec2 offset = vec2(ox, oy) * texel;
            sum += k * textureLod(u_screen, clamp(uv + offset, 0.0, 1.0), lod);
            norm += k;
        }
    }
    return sum / max(norm, 1e-5);
}

float sdRoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_uv * u_rectSize - halfSize;

    vec4 cr = vec4(u_corner.w, u_corner.y, u_corner.x, u_corner.z) * u_radius;
    float dist = sdRoundBox(local, halfSize, cr);
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    float maskAlpha = clamp(0.5 - dist / aa, 0.0, 1.0);
    if (maskAlpha < 0.001) discard;

    vec2 screenUV = gl_FragCoord.xy / u_screenSize;
    vec4 blurred = u_direct > 0.5 ? texture(u_screen, screenUV) : sampleBlurred(screenUV, u_blurRadius, u_gamma);

    fragColor = vec4(blurred.rgb * u_tint.rgb, maskAlpha * u_tint.a);
}
)GLSL";
        if (!compileShader(blurShader, RECT_VERT, frag,
                           {"u_proj", "u_rect", "u_screenSize", "u_rectSize", "u_radius", "u_corner", "u_blurRadius",
                            "u_gamma", "u_tint", "u_direct"})) {
            blurFailed = true;
            return false;
        }
        GLint locScreen = GetUniformLocation(blurShader.program, "u_screen");
        UseProgram(blurShader.program);
        if (locScreen >= 0) {
            Uniform1i(locScreen, 0);
        }
        UseProgram(0);
        return true;
    }

    static bool ensureRoundRectShader() {
        if (roundRectShader.program != 0) {
            return true;
        }
        if (roundRectFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform vec2  u_rectSize;
uniform vec4  u_color;
uniform vec4  u_colorB;
uniform float u_gradient;
uniform float u_radius;
uniform vec4  u_corner;

in vec2 v_uv;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_uv * u_rectSize - halfSize;

    vec4 cr = vec4(u_corner.w, u_corner.y, u_corner.x, u_corner.z) * u_radius;
    float dist = sdRoundBox(local, halfSize, cr);
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    vec4 tint = mix(u_color, u_colorB, u_gradient < 0.0 ? v_uv.y : u_gradient * v_uv.x);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0) * tint.a;

    fragColor = vec4(tint.rgb, alpha);
}
)GLSL";
        if (!compileShader(
                    roundRectShader, RECT_VERT, frag,
                    {"u_proj", "u_rect", "u_rectSize", "u_color", "u_colorB", "u_gradient", "u_radius", "u_corner"})) {
            roundRectFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureRoundTextureShader() {
        if (roundTextureShader.program != 0) {
            return true;
        }
        if (roundTextureFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform sampler2D u_texture;
uniform vec2  u_rectSize;
uniform float u_radius;
uniform float u_alpha;
uniform float u_fade;
uniform vec4  u_fadeColor;
uniform vec4  u_corner;
uniform vec2  u_uvScale;

in vec2 v_uv;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_uv * u_rectSize - halfSize;

    vec4 cr = vec4(u_corner.w, u_corner.y, u_corner.x, u_corner.z) * u_radius;
    float dist = sdRoundBox(local, halfSize, cr);
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    float mask = clamp(0.5 - dist / aa, 0.0, 1.0);

    vec2 uv = (v_uv - 0.5) * u_uvScale + 0.5;
    vec4 tex = texture(u_texture, uv);
    if (u_fade > 0.0) {
        float t = clamp(1.0 - v_uv.x / u_fade, 0.0, 1.0);
        tex.rgb = mix(tex.rgb, u_fadeColor.rgb, t * u_fadeColor.a);
        tex.a = mix(tex.a, u_fadeColor.a, t);
    }

    fragColor = vec4(tex.rgb, tex.a * mask * u_alpha);
}
)GLSL";
        if (!compileShader(roundTextureShader, RECT_VERT, frag,
                           {"u_proj", "u_rect", "u_rectSize", "u_radius", "u_alpha", "u_fade", "u_fadeColor",
                            "u_corner", "u_uvScale", "u_texture"})) {
            roundTextureFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureRoundRingShader() {
        if (roundRingShader.program != 0) {
            return true;
        }
        if (roundRingFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform vec2  u_rectSize;
uniform vec4  u_color;
uniform vec4  u_color2;
uniform float u_radius;
uniform float u_thickness;
uniform vec4  u_corner;
uniform float u_fadeEnd;
uniform float u_fadeAxis;
uniform vec2  u_dash;

in vec2 v_uv;
out vec4 fragColor;

float perimeter(vec2 p, vec2 size, float r) {
    float w = size.x - 2.0 * r;
    float h = size.y - 2.0 * r;
    float a = 1.5707963 * r;
    if (p.x > size.x - r && p.y < r) {
        return w + r * (atan(p.y - r, p.x - size.x + r) + 1.5707963);
    }
    if (p.x > size.x - r && p.y > size.y - r) {
        return w + a + h + r * atan(p.y - size.y + r, p.x - size.x + r);
    }
    if (p.x < r && p.y > size.y - r) {
        return 2.0 * w + h + 2.0 * a + r * (atan(p.y - size.y + r, p.x - r) - 1.5707963);
    }
    if (p.x < r && p.y < r) {
        float t = atan(p.y - r, p.x - r);
        return 2.0 * w + 2.0 * h + 3.0 * a + r * (t + 6.2831853 * step(t, 0.0) - 3.1415926);
    }
    float top = p.y;
    float right = size.x - p.x;
    float bottom = size.y - p.y;
    float left = p.x;
    float edge = min(min(top, right), min(bottom, left));
    if (edge == top) {
        return p.x - r;
    }
    if (edge == right) {
        return w + a + p.y - r;
    }
    if (edge == bottom) {
        return w + a + h + a + size.x - r - p.x;
    }
    return 2.0 * w + h + 3.0 * a + size.y - r - p.y;
}

float sdRoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_uv * u_rectSize - halfSize;

    vec4 cr = vec4(u_corner.w, u_corner.y, u_corner.x, u_corner.z) * u_radius;
    float dist = sdRoundBox(local, halfSize, cr);
    float ring = abs(dist + u_thickness * 0.5) - u_thickness * 0.5;
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    vec4 tint = mix(u_color, u_color2, clamp(mix(v_uv.y, v_uv.x, u_fadeAxis) / max(u_fadeEnd, 1e-4), 0.0, 1.0));
    float alpha = clamp(0.5 - ring / aa, 0.0, 1.0) * tint.a;
    if (u_dash.x > 0.0) {
        float r = clamp(u_radius - u_thickness * 0.5, 0.0, min(u_rectSize.x, u_rectSize.y) * 0.5);
        vec2 inset = vec2(u_thickness * 0.5);
        vec2 path = u_rectSize - inset * 2.0;
        float total = 2.0 * (path.x - 2.0 * r) + 2.0 * (path.y - 2.0 * r) + 6.2831853 * r;
        float count = max(1.0, floor(total / (u_dash.x + u_dash.y) + 0.5));
        float phase = perimeter(v_uv * u_rectSize - inset, path, r) / (total / count);
        float on = u_dash.x / (u_dash.x + u_dash.y);
        float f = fract(phase);
        float soft = max(fwidth(phase), 1e-4);
        alpha *= clamp((on - f) / soft + 0.5, 0.0, 1.0) * clamp(f / soft + 0.5, 0.0, 1.0);
    }

    fragColor = vec4(tint.rgb, alpha);
}
)GLSL";
        if (!compileShader(roundRingShader, ID_VERT, frag,
                           {"u_proj", "u_rect", "u_rectSize", "u_color", "u_color2", "u_radius", "u_thickness",
                            "u_corner", "u_fadeEnd", "u_fadeAxis", "u_dash"})) {
            roundRingFailed = true;
            return false;
        }
        return true;
    }

    static bool ensurePickerShader() {
        if (pickerShader.program != 0) {
            return true;
        }
        if (pickerFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform vec2  u_rectSize;
uniform float u_radius;
uniform float u_hue;
uniform float u_sat;
uniform float u_val;
uniform float u_mode;
uniform float u_alpha;

in vec2 v_uv;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    float side = min(b.x, b.y);
    float pill = clamp((r / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r * 1.5, side), r, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_uv * u_rectSize - halfSize;
    float dist = sdRoundBox(local, halfSize, u_radius);
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    float mask = clamp(0.5 - dist / aa, 0.0, 1.0);

    vec3 rgb;
    if (u_mode < 0.5) {
        rgb = hsv2rgb(vec3(u_hue, v_uv.x, 1.0 - v_uv.y));
    } else if (u_mode < 1.5) {
        rgb = hsv2rgb(vec3(v_uv.x, 1.0, 1.0));
    } else {
        vec2 cell = floor(v_uv * u_rectSize / 5.0);
        float checker = mod(cell.x + cell.y, 2.0) < 0.5 ? 0.78 : 0.58;
        vec3 base = hsv2rgb(vec3(u_hue, u_sat, u_val));
        rgb = mix(vec3(checker), base, v_uv.x);
    }
    fragColor = vec4(rgb, mask * u_alpha);
}
)GLSL";
        if (!compileShader(
                    pickerShader, ID_VERT, frag,
                    {"u_proj", "u_rect", "u_rectSize", "u_radius", "u_hue", "u_sat", "u_val", "u_mode", "u_alpha"})) {
            pickerFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureShadowShader() {
        if (shadowShader.program != 0) {
            return true;
        }
        if (shadowFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform vec2  u_rectSize;
uniform vec2  u_shapeSize;
uniform vec4  u_color;
uniform float u_radius;
uniform float u_spread;

in vec2 v_uv;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    float side = min(b.x, b.y);
    float pill = clamp((r / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r * 1.5, side), r, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_uv * u_rectSize - halfSize;
    float dist = sdRoundBox(local, u_shapeSize * 0.5, u_radius);
    float falloff = 1.0 - smoothstep(-u_spread * 0.35, u_spread, dist);
    falloff = falloff * falloff;
    fragColor = vec4(u_color.rgb, u_color.a * falloff);
}
)GLSL";
        if (!compileShader(shadowShader, ID_VERT, frag,
                           {"u_proj", "u_rect", "u_rectSize", "u_shapeSize", "u_color", "u_radius", "u_spread"})) {
            shadowFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureBloomShader() {
        if (bloomShader.program != 0) {
            return true;
        }
        if (bloomFailed) {
            return false;
        }
        const char* frag = R"GLSL(#version 150 core

uniform vec4  u_rect;
uniform sampler2D u_data;
uniform int   u_count;
uniform float u_fill;
uniform float u_colored;
uniform vec4  u_color;
uniform float u_radius;
uniform float u_spread;
uniform float u_intensity;

in vec2 v_uv;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    float side = min(b.x, b.y);
    float pill = clamp((r / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r * 1.5, side), r, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

float sdCorners(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x = (p.y > 0.0) ? r.x : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

vec4 shapeData(int index, int field) {
    return texelFetch(u_data, ivec2(field, index), 0);
}

float sdShapeIndex(vec2 p, out int index) {
    float dist = 1e9;
    index = 0;
    for (int i = 0; i < u_count; i++) {
        vec4 rect = shapeData(i, 0);
        vec2 extent = rect.zw * 0.5;
        vec2 local = p - rect.xy - extent;
        vec4 r = shapeData(i, 1);
        float d;
        if (r.x < -0.5) {
            vec2 q = abs(local) - extent;
            float box = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
            d = max(box, r.w - length(p - r.yz));
        } else {
            d = sdCorners(local, extent, min(r, vec4(min(extent.x, extent.y))));
        }
        if (d < dist) {
            dist = d;
            index = i;
        }
    }
    return dist;
}

float sdShape(vec2 p) {
    int index;
    return sdShapeIndex(p, index);
}

vec4 shapeColor(vec2 p, int index) {
    vec4 s = shapeData(index, 0);
    float t = clamp((p.y - s.y) / max(s.w, 1e-4), 0.0, 1.0);
    return mix(shapeData(index, 2), shapeData(index, 3), t);
}

float energy(vec2 p, float spread) {
    float falloff = 1.0 - smoothstep(-spread * 0.25, spread, sdShape(p));
    return falloff * falloff;
}

void main() {
    vec2 local = u_rect.xy + v_uv * u_rect.zw;
    int nearest;
    float inside = sdShapeIndex(local, nearest);
    vec4 base = u_colored > 0.5 ? shapeColor(local, nearest) : vec4(u_color.rgb, 1.0);
    if (u_fill > 0.5) {
        float edge = max(fwidth(inside), 1e-5);
        float coverage = clamp(0.5 - inside / edge, 0.0, 1.0);
        if (coverage <= 0.0) discard;
        float alpha = u_colored > 0.5 ? base.a : u_color.a;
        fragColor = vec4(base.rgb, alpha * coverage);
        return;
    }
    vec2 tap = vec2(u_spread * 0.28);

    float sum = energy(local + vec2(-tap.x * 2.0, 0.0), u_spread);
    sum += energy(local + vec2(-tap.x, tap.y), u_spread) * 2.0;
    sum += energy(local + vec2(0.0, tap.y * 2.0), u_spread);
    sum += energy(local + vec2(tap.x, tap.y), u_spread) * 2.0;
    sum += energy(local + vec2(tap.x * 2.0, 0.0), u_spread);
    sum += energy(local + vec2(tap.x, -tap.y), u_spread) * 2.0;
    sum += energy(local + vec2(0.0, -tap.y * 2.0), u_spread);
    sum += energy(local + vec2(-tap.x, -tap.y), u_spread) * 2.0;

    float bloom = sum / 12.0;
    bloom = clamp(bloom * u_intensity, 0.0, 1.0);
    float aa = max(fwidth(inside), 1e-5);
    bloom *= clamp(0.5 + inside / aa, 0.0, 1.0);
    fragColor = vec4(base.rgb, u_color.a * base.a * bloom);
}
)GLSL";
        if (!compileShader(bloomShader, ID_VERT, frag,
                           {"u_proj", "u_rect", "u_data", "u_count", "u_fill", "u_colored", "u_color", "u_radius",
                            "u_spread", "u_intensity"})) {
            bloomFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureRoundTexShader() {
        if (roundTexShader.program != 0) {
            return true;
        }
        if (roundTexFailed) {
            return false;
        }
        const char* vert = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;
uniform vec4 u_uv;

out vec2 v_local;
out vec2 v_uv;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );
    vec2 corner = corners[gl_VertexID];
    vec2 pos = u_rect.xy + corner * u_rect.zw;
    gl_Position = u_proj * vec4(pos, 0.0, 1.0);
    v_local = corner;
    v_uv = mix(u_uv.xy, u_uv.zw, corner);
}
)GLSL";
        const char* frag = R"GLSL(#version 150 core

uniform sampler2D u_tex;
uniform vec2  u_rectSize;
uniform float u_radius;
uniform vec4  u_corner;
uniform vec4  u_tint;

in vec2 v_local;
in vec2 v_uv;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

void main() {
    vec2 halfSize = u_rectSize * 0.5;
    vec2 local = v_local * u_rectSize - halfSize;

    vec4 cr = vec4(u_corner.w, u_corner.y, u_corner.x, u_corner.z) * u_radius;
    float dist = sdRoundBox(local, halfSize, cr);
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    float mask = clamp(0.5 - dist / aa, 0.0, 1.0);
    if (mask < 0.001) discard;

    vec4 texel = texture(u_tex, v_uv);
    fragColor = vec4(texel.rgb * u_tint.rgb, texel.a * mask * u_tint.a);
}
)GLSL";
        if (!compileShader(roundTexShader, vert, frag,
                           {"u_proj", "u_rect", "u_uv", "u_rectSize", "u_radius", "u_corner", "u_tint"})) {
            roundTexFailed = true;
            return false;
        }
        GLint locTex = GetUniformLocation(roundTexShader.program, "u_tex");
        UseProgram(roundTexShader.program);
        if (locTex >= 0) {
            Uniform1i(locTex, 0);
        }
        UseProgram(0);
        return true;
    }

    static bool ensureCheckShader() {
        if (checkShader.program != 0) {
            return true;
        }
        if (checkFailed) {
            return false;
        }
        const char* vert = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;

out vec2 v_uv;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );
    vec2 corner = corners[gl_VertexID];
    gl_Position = u_proj * vec4(u_rect.xy + corner * u_rect.zw, 0.0, 1.0);
    v_uv = corner;
}
)GLSL";
        const char* frag = R"GLSL(#version 150 core

uniform vec4  u_rect;
uniform vec4  u_color;
uniform float u_size;
uniform float u_thickness;

in vec2 v_uv;
out vec4 fragColor;

float segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    vec2 p = (v_uv - 0.5) * u_rect.zw;
    vec2 a = vec2(-0.40, 0.00) * u_size;
    vec2 b = vec2(-0.12, 0.28) * u_size;
    vec2 c = vec2(0.42, -0.30) * u_size;
    float dist = min(segment(p, a, b), segment(p, b, c)) - u_thickness * 0.5;
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0) * u_color.a;
    if (alpha < 0.001) discard;
    fragColor = vec4(u_color.rgb, alpha);
}
)GLSL";
        if (!compileShader(checkShader, vert, frag, {"u_proj", "u_rect", "u_color", "u_size", "u_thickness"})) {
            checkFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureArcShader() {
        if (arcShader.program != 0) {
            return true;
        }
        if (arcFailed) {
            return false;
        }
        const char* vert = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;

out vec2 v_uv;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );
    vec2 corner = corners[gl_VertexID];
    gl_Position = u_proj * vec4(u_rect.xy + corner * u_rect.zw, 0.0, 1.0);
    v_uv = corner;
}
)GLSL";
        const char* frag = R"GLSL(#version 150 core

uniform vec4  u_rect;
uniform vec4  u_color;
uniform float u_radius;
uniform float u_thickness;
uniform vec2  u_angles;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 p = (v_uv - 0.5) * u_rect.zw;
    vec2 dir = vec2(cos(u_angles.x), sin(u_angles.x));
    vec2 q = vec2(abs(p.y * dir.x - p.x * dir.y), dot(p, dir));
    vec2 sc = vec2(sin(u_angles.y), cos(u_angles.y));
    float dist = (sc.y * q.x > sc.x * q.y ? length(q - sc * u_radius) : abs(length(q) - u_radius)) - u_thickness * 0.5;
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0) * u_color.a;
    if (alpha < 0.001) discard;
    fragColor = vec4(u_color.rgb, alpha);
}
)GLSL";
        if (!compileShader(arcShader, vert, frag,
                           {"u_proj", "u_rect", "u_color", "u_radius", "u_thickness", "u_angles"})) {
            arcFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureIconShader() {
        if (iconShader.program != 0) {
            return true;
        }
        if (iconFailed) {
            return false;
        }
        const char* vert = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;

out vec2 v_uv;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );
    vec2 corner = corners[gl_VertexID];
    gl_Position = u_proj * vec4(u_rect.xy + corner * u_rect.zw, 0.0, 1.0);
    v_uv = corner;
}
)GLSL";
        int parts = (int) (sizeof(ICON_FRAG_PARTS) / sizeof(ICON_FRAG_PARTS[0]));
        if (!compileShader(iconShader, &vert, 1, ICON_FRAG_PARTS, parts, {"u_proj", "u_rect", "u_tint", "u_icon"})) {
            iconFailed = true;
            return false;
        }
        return true;
    }

    static bool ensureShellShader() {
        if (shellShader.program != 0) {
            return true;
        }
        if (shellFailed) {
            return false;
        }
        const char* vert = R"GLSL(#version 150 core

uniform mat4 u_proj;
uniform vec4 u_rect;

out vec2 v_pos;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );
    vec2 pos = u_rect.xy + corners[gl_VertexID] * u_rect.zw;
    gl_Position = u_proj * vec4(pos, 0.0, 1.0);
    v_pos = pos;
}
)GLSL";
        const char* frag = R"GLSL(#version 150 core

uniform vec4 u_plate;
uniform vec4 u_plateRadii;
uniform vec4 u_plateColor;
uniform vec4 u_main;
uniform vec4 u_mainRadii;
uniform vec4 u_mainColor;

in vec2 v_pos;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    float side = min(b.x, b.y);
    float pill = clamp((r.x / max(side, 1e-4) - 0.7) / 0.3, 0.0, 1.0);
    float rr = mix(min(r.x * 1.5, side), r.x, pill);
    float n = mix(4.0, 2.0, pill);
    vec2 q = abs(p) - b + rr;
    vec2 m = max(q, 0.0);
    return min(max(q.x, q.y), 0.0) + pow(pow(m.x, n) + pow(m.y, n), 1.0 / n) - rr;
}

float coverage(vec4 rect, vec4 radii) {
    vec2 extent = rect.zw * 0.5;
    float dist = sdRoundBox(v_pos - rect.xy - extent, extent, radii);
    float aa = max(length(vec2(dFdx(dist), dFdy(dist))), 1e-5);
    return clamp(0.5 - dist / aa, 0.0, 1.0);
}

void main() {
    float mainCoverage = coverage(u_main, u_mainRadii);
    float mainAlpha = mainCoverage * u_mainColor.a;
    float plateAlpha = coverage(u_plate, u_plateRadii) * u_plateColor.a * (1.0 - mainCoverage);
    float alpha = mainAlpha + plateAlpha;
    if (alpha < 0.001) discard;
    fragColor = vec4((u_mainColor.rgb * mainAlpha + u_plateColor.rgb * plateAlpha) / alpha, alpha);
}
)GLSL";
        if (!compileShader(shellShader, vert, frag,
                           {"u_proj", "u_rect", "u_plate", "u_plateRadii", "u_plateColor", "u_main", "u_mainRadii",
                            "u_mainColor"})) {
            shellFailed = true;
            return false;
        }
        return true;
    }

    static inline float cr(uint32_t c) {
        return ((c >> 16) & 0xFF) / 255.0f;
    }
    static inline float cg(uint32_t c) {
        return ((c >> 8) & 0xFF) / 255.0f;
    }
    static inline float cb(uint32_t c) {
        return (c & 0xFF) / 255.0f;
    }
    static inline float ca(uint32_t c) {
        return ((c >> 24) & 0xFF) / 255.0f;
    }

    static void uniformColor(GLint location, uint32_t c) {
        Uniform4f(location, cr(c), cg(c), cb(c), ca(c));
    }

    static void uniformCorners(GLint location, int cornerMask) {
        Uniform4f(location, (cornerMask & 4) != 0 ? 1.0f : 0.0f, (cornerMask & 2) != 0 ? 1.0f : 0.0f,
                  (cornerMask & 1) != 0 ? 1.0f : 0.0f, (cornerMask & 8) != 0 ? 1.0f : 0.0f);
    }

    static void defaultBlend() {
        Enable(UGL_BLEND);
        BlendEquation(UGL_FUNC_ADD);
        BlendFuncSeparate(UGL_SRC_ALPHA, UGL_ONE_MINUS_SRC_ALPHA, UGL_ONE, UGL_ONE_MINUS_SRC_ALPHA);
    }

    static GLuint contextVertexArray() {
        static std::unordered_map<void*, GLuint> arrays;
        void* context = (void*) wglGetCurrentContext();
        GLuint& array = arrays[context];
        if (array == 0) {
            GenVertexArrays(1, &array);
        }
        return array;
    }

    static void use(const Shader& shader) {
        UseProgram(shader.program);
        UniformMatrix4fv(shader["u_proj"], 1, UGL_FALSE, mvpFloats);
        BindVertexArray(contextVertexArray());
    }

    static void ensureCaptureTexture(int w, int h) {
        if (captureTextureId != 0 && captureWidth == w && captureHeight == h) {
            return;
        }
        if (captureTextureId != 0) {
            DeleteTextures(1, &captureTextureId);
        }
        GenTextures(1, &captureTextureId);
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, captureTextureId);
        int mipLevels = 1;
        int mw = w, mh = h;
        while (mw > 1 || mh > 1) {
            mw = std::max(1, mw / 2);
            mh = std::max(1, mh / 2);
            mipLevels++;
        }
        int lw = w, lh = h;
        for (int i = 0; i < mipLevels; i++) {
            TexImage2D(UGL_TEXTURE_2D, i, UGL_RGBA8, lw, lh, 0, UGL_RGBA, UGL_UNSIGNED_BYTE, nullptr);
            lw = std::max(1, lw / 2);
            lh = std::max(1, lh / 2);
        }
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MIN_FILTER, UGL_LINEAR_MIPMAP_LINEAR);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MAG_FILTER, UGL_LINEAR);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_S, UGL_CLAMP_TO_EDGE);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_T, UGL_CLAMP_TO_EDGE);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_BASE_LEVEL, 0);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MAX_LEVEL, mipLevels - 1);
        captureWidth = w;
        captureHeight = h;
    }

    static void captureScreen(int frame) {
        if (captureValid && captureFrame == frame) {
            return;
        }
        GLint drawFbo = 0;
        GLint readFbo = 0;
        GetIntegerv(UGL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        GetIntegerv(UGL_READ_FRAMEBUFFER_BINDING, &readFbo);
        ensureCaptureTexture(fbWidth, fbHeight);
        BindFramebuffer(UGL_READ_FRAMEBUFFER, (GLuint) drawFbo);
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, captureTextureId);
        bool scissor = IsEnabled(UGL_SCISSOR_TEST) != 0;
        Disable(UGL_SCISSOR_TEST);
        CopyTexSubImage2D(UGL_TEXTURE_2D, 0, 0, 0, 0, 0, fbWidth, fbHeight);
        if (scissor) {
            Enable(UGL_SCISSOR_TEST);
        }
        GenerateMipmap(UGL_TEXTURE_2D);
        BindFramebuffer(UGL_READ_FRAMEBUFFER, (GLuint) readFbo);
        captureGeneration++;
        captureValid = true;
        captureFrame = frame;
    }

    static GLuint createGlowTexture(int w, int h) {
        GLuint id = 0;
        GenTextures(1, &id);
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, id);
        TexImage2D(UGL_TEXTURE_2D, 0, UGL_RGBA8, w, h, 0, UGL_RGBA, UGL_UNSIGNED_BYTE, nullptr);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MIN_FILTER, UGL_LINEAR);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MAG_FILTER, UGL_LINEAR);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_S, UGL_CLAMP_TO_EDGE);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_T, UGL_CLAMP_TO_EDGE);
        return id;
    }

    static void ensureDualTargets(int width, int height, int levels) {
        GLint previous = 0;
        GetIntegerv(UGL_DRAW_FRAMEBUFFER_BINDING, &previous);
        for (int i = 0; i < levels; i++) {
            int w = std::max(1, width >> (i + 1));
            int h = std::max(1, height >> (i + 1));
            if (dualTextures[i] != 0 && dualWidths[i] == w && dualHeights[i] == h) {
                continue;
            }
            if (dualTextures[i] != 0) {
                DeleteTextures(1, &dualTextures[i]);
                DeleteFramebuffers(1, &dualFbos[i]);
            }
            dualTextures[i] = createGlowTexture(w, h);
            GenFramebuffers(1, &dualFbos[i]);
            BindFramebuffer(UGL_FRAMEBUFFER, dualFbos[i]);
            FramebufferTexture2D(UGL_FRAMEBUFFER, UGL_COLOR_ATTACHMENT0, UGL_TEXTURE_2D, dualTextures[i], 0);
            dualWidths[i] = w;
            dualHeights[i] = h;
        }
        BindFramebuffer(UGL_DRAW_FRAMEBUFFER, (GLuint) previous);
    }

    static void dualPass(const Shader& shader, GLuint source, int sourceWidth, int sourceHeight, int target,
                         float offset) {
        BindFramebuffer(UGL_FRAMEBUFFER, dualFbos[target]);
        Viewport(0, 0, dualWidths[target], dualHeights[target]);
        UseProgram(shader.program);
        Uniform1i(shader["u_source"], 0);
        Uniform2f(shader["u_halfPixel"], 0.5f / sourceWidth, 0.5f / sourceHeight);
        Uniform1f(shader["u_offset"], offset);
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, source);
        BindVertexArray(contextVertexArray());
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void updateDualKawase(int iterations, float offset) {
        if (dualGeneration == captureGeneration && dualIterations == iterations && dualOffset == offset) {
            return;
        }
        GLint previousFbo = 0;
        GLint previousRead = 0;
        GLint viewport[4];
        GetIntegerv(UGL_DRAW_FRAMEBUFFER_BINDING, &previousFbo);
        GetIntegerv(UGL_READ_FRAMEBUFFER_BINDING, &previousRead);
        GetIntegerv(UGL_VIEWPORT, viewport);
        bool scissor = IsEnabled(UGL_SCISSOR_TEST) != 0;
        GLint scissorBox[4];
        GetIntegerv(UGL_SCISSOR_BOX, scissorBox);
        Disable(UGL_SCISSOR_TEST);
        Disable(UGL_BLEND);
        Disable(UGL_DEPTH_TEST);
        ensureDualTargets(captureWidth, captureHeight, iterations);
        dualPass(dualDownShader, captureTextureId, captureWidth, captureHeight, 0, offset);
        for (int i = 1; i < iterations; i++) {
            dualPass(dualDownShader, dualTextures[i - 1], dualWidths[i - 1], dualHeights[i - 1], i, offset);
        }
        for (int i = iterations - 2; i >= 0; i--) {
            dualPass(dualUpShader, dualTextures[i + 1], dualWidths[i + 1], dualHeights[i + 1], i, offset);
        }
        UseProgram(0);
        BindVertexArray(0);
        BindFramebuffer(UGL_DRAW_FRAMEBUFFER, (GLuint) previousFbo);
        BindFramebuffer(UGL_READ_FRAMEBUFFER, (GLuint) previousRead);
        Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (scissor) {
            Enable(UGL_SCISSOR_TEST);
            Scissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        }
        defaultBlend();
        dualGeneration = captureGeneration;
        dualIterations = iterations;
        dualOffset = offset;
    }

    static void uploadBloomShape(float x, float y, float w, float h, float radius) {
        float data[16] = {x, y, w, h, radius, radius, radius, radius, 0, 0, 0, 0, 0, 0, 0, 0};
        if (shapeTexture == 0) {
            GenTextures(1, &shapeTexture);
        }
        ActiveTexture(UGL_TEXTURE3);
        BindTexture(UGL_TEXTURE_2D, shapeTexture);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MIN_FILTER, UGL_NEAREST);
        TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MAG_FILTER, UGL_NEAREST);
        BindBuffer(UGL_PIXEL_UNPACK_BUFFER, 0);
        PixelStorei(UGL_UNPACK_ROW_LENGTH, 0);
        PixelStorei(UGL_UNPACK_SKIP_PIXELS, 0);
        PixelStorei(UGL_UNPACK_SKIP_ROWS, 0);
        PixelStorei(UGL_UNPACK_ALIGNMENT, 4);
        TexImage2D(UGL_TEXTURE_2D, 0, UGL_RGBA32F, 4, 1, 0, UGL_RGBA, UGL_FLOAT, data);
        ActiveTexture(UGL_TEXTURE0);
        Uniform1i(bloomShader["u_data"], 3);
        Uniform1i(bloomShader["u_count"], 1);
        Uniform1f(bloomShader["u_colored"], 0.0f);
    }

    static void drawRoundRect(const Prim& p) {
        if (!ensureRoundRectShader()) {
            return;
        }
        defaultBlend();
        use(roundRectShader);
        const Shader& s = roundRectShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        uniformColor(s["u_color"], p.c[0]);
        uniformColor(s["u_colorB"], p.c[1]);
        Uniform1f(s["u_gradient"], p.f[5]);
        Uniform1f(s["u_radius"], p.f[4]);
        Uniform4f(s["u_corner"], p.f[8], p.f[7], p.f[6], p.f[9]);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawRing(const Prim& p) {
        if (!ensureRoundRingShader()) {
            return;
        }
        defaultBlend();
        use(roundRingShader);
        const Shader& s = roundRingShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        uniformColor(s["u_color"], p.c[0]);
        uniformColor(s["u_color2"], p.c[1]);
        Uniform1f(s["u_radius"], p.f[4]);
        Uniform1f(s["u_thickness"], std::max(0.5f, p.f[5]));
        uniformCorners(s["u_corner"], p.i[0]);
        Uniform1f(s["u_fadeEnd"], p.f[6]);
        Uniform1f(s["u_fadeAxis"], p.f[7]);
        Uniform2f(s["u_dash"], p.f[8], p.f[9]);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawShadow(const Prim& p) {
        if (!ensureShadowShader()) {
            return;
        }
        float spread = p.f[5];
        float pad = spread * 2.0f;
        float qx = p.f[0] - pad;
        float qy = p.f[1] - pad + p.f[6];
        float qw = p.f[2] + pad * 2.0f;
        float qh = p.f[3] + pad * 2.0f;
        defaultBlend();
        use(shadowShader);
        const Shader& s = shadowShader;
        Uniform4f(s["u_rect"], qx, qy, qw, qh);
        Uniform2f(s["u_rectSize"], qw, qh);
        Uniform2f(s["u_shapeSize"], p.f[2], p.f[3]);
        uniformColor(s["u_color"], p.c[0]);
        Uniform1f(s["u_radius"], p.f[4]);
        Uniform1f(s["u_spread"], std::max(1.0f, spread));
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawBloom(const Prim& p) {
        if (!ensureBloomShader()) {
            return;
        }
        float spread = p.f[5];
        float pad = spread * 2.5f;
        float qx = p.f[0] - pad;
        float qy = p.f[1] - pad;
        float qw = p.f[2] + pad * 2.0f;
        float qh = p.f[3] + pad * 2.0f;
        uint32_t rgba = p.c[0];
        Enable(UGL_BLEND);
        BlendEquation(UGL_FUNC_ADD);
        if (cr(rgba) + cg(rgba) + cb(rgba) > 0.12f) {
            BlendFunc(UGL_SRC_ALPHA, UGL_ONE);
        } else {
            BlendFuncSeparate(UGL_SRC_ALPHA, UGL_ONE_MINUS_SRC_ALPHA, UGL_ONE, UGL_ONE_MINUS_SRC_ALPHA);
        }
        use(bloomShader);
        const Shader& s = bloomShader;
        Uniform4f(s["u_rect"], qx, qy, qw, qh);
        uploadBloomShape(p.f[0], p.f[1], p.f[2], p.f[3], p.f[4]);
        Uniform1f(s["u_fill"], 0.0f);
        uniformColor(s["u_color"], rgba);
        Uniform1f(s["u_radius"], p.f[4]);
        Uniform1f(s["u_spread"], std::max(1.0f, spread));
        Uniform1f(s["u_intensity"], std::max(0.0f, p.f[6]));
        DrawArrays(UGL_TRIANGLES, 0, 6);
        defaultBlend();
    }

    static void drawGaussian(const Prim& p, int frame) {
        if (!ensureBlurShader()) {
            return;
        }
        if (p.i[1] != 0) {
            captureValid = false;
        }
        captureScreen(frame);
        uint32_t tint = p.c[0];
        defaultBlend();
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, captureTextureId);
        use(blurShader);
        const Shader& s = blurShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform2f(s["u_screenSize"], (float) captureWidth, (float) captureHeight);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        Uniform1f(s["u_radius"], p.f[4]);
        if (p.i[2] != 0) {
            Uniform4f(s["u_corner"], p.f[10], p.f[9], p.f[8], p.f[11]);
        } else {
            uniformCorners(s["u_corner"], p.i[0]);
        }
        Uniform1f(s["u_blurRadius"], std::max(1.0f, p.f[5] / 2.0f));
        Uniform1f(s["u_gamma"], p.f[6]);
        uniformColor(s["u_tint"], tint);
        Uniform1f(s["u_direct"], 0.0f);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawKawase(const Prim& p, int frame) {
        if (!ensureDualKawase() || !ensureBlurShader()) {
            Prim fallback = p;
            fallback.kind = BLUR;
            fallback.f[6] = 6.0f;
            fallback.i[2] = 0;
            drawGaussian(fallback, frame);
            return;
        }
        if (p.i[1] != 0) {
            captureValid = false;
        }
        captureScreen(frame);
        float strength = p.f[5];
        int iterations = std::max(
                1, std::min(5, (int) std::lround((float) (std::log(std::max(2.0f, strength)) / std::log(2.0)) - 1.0f)));
        float offset = std::max(0.5f, std::min(3.0f, strength / (float) (1 << (iterations + 1)) * 1.5f));
        updateDualKawase(iterations, offset);
        defaultBlend();
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, dualTextures[0]);
        use(blurShader);
        const Shader& s = blurShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform2f(s["u_screenSize"], (float) captureWidth, (float) captureHeight);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        Uniform1f(s["u_radius"], p.f[4]);
        uniformCorners(s["u_corner"], p.i[0]);
        uniformColor(s["u_tint"], p.c[0]);
        Uniform1f(s["u_direct"], 1.0f);
        DrawArrays(UGL_TRIANGLES, 0, 6);
        Uniform1f(s["u_direct"], 0.0f);
    }

    static void drawPicker(const Prim& p) {
        if (!ensurePickerShader()) {
            return;
        }
        defaultBlend();
        use(pickerShader);
        const Shader& s = pickerShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        Uniform1f(s["u_radius"], p.f[4]);
        Uniform1f(s["u_hue"], p.f[5]);
        Uniform1f(s["u_sat"], p.f[6]);
        Uniform1f(s["u_val"], p.f[7]);
        Uniform1f(s["u_mode"], p.f[8]);
        Uniform1f(s["u_alpha"], p.f[9]);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawRoundTexture(const Prim& p) {
        if (!ensureRoundTextureShader()) {
            return;
        }
        defaultBlend();
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, p.tex);
        use(roundTextureShader);
        const Shader& s = roundTextureShader;
        float width = p.f[2];
        float fadeWidth = p.f[6];
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        Uniform1f(s["u_radius"], p.f[4]);
        Uniform1f(s["u_alpha"], p.f[5]);
        Uniform1f(s["u_fade"], fadeWidth <= 0 ? 0.0f : fadeWidth / std::max(1.0f, width));
        uniformColor(s["u_fadeColor"], p.c[0]);
        uniformCorners(s["u_corner"], p.i[0]);
        Uniform2f(s["u_uvScale"], p.f[7], p.f[8]);
        Uniform1i(s["u_texture"], 0);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawRoundTex(const Prim& p) {
        if (!ensureRoundTexShader()) {
            return;
        }
        defaultBlend();
        ActiveTexture(UGL_TEXTURE0);
        BindTexture(UGL_TEXTURE_2D, p.tex);
        use(roundTexShader);
        const Shader& s = roundTexShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[3]);
        Uniform4f(s["u_uv"], p.f[5], p.f[6], p.f[7], p.f[8]);
        Uniform2f(s["u_rectSize"], p.f[2], p.f[3]);
        Uniform1f(s["u_radius"], p.f[4]);
        uniformCorners(s["u_corner"], p.i[0]);
        uniformColor(s["u_tint"], p.c[0]);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawCheck(const Prim& p) {
        if (!ensureCheckShader()) {
            return;
        }
        float size = p.f[2];
        float thickness = p.f[3];
        float extent = size + thickness * 2.0f + 2.0f;
        defaultBlend();
        use(checkShader);
        const Shader& s = checkShader;
        Uniform4f(s["u_rect"], p.f[0] - extent * 0.5f, p.f[1] - extent * 0.5f, extent, extent);
        uniformColor(s["u_color"], p.c[0]);
        Uniform1f(s["u_size"], size);
        Uniform1f(s["u_thickness"], thickness);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawArc(const Prim& p) {
        if (!ensureArcShader()) {
            return;
        }
        float radius = p.f[2];
        float thickness = p.f[3];
        float extent = (radius + thickness + 2.0f) * 2.0f;
        defaultBlend();
        use(arcShader);
        const Shader& s = arcShader;
        Uniform4f(s["u_rect"], p.f[0] - extent * 0.5f, p.f[1] - extent * 0.5f, extent, extent);
        uniformColor(s["u_color"], p.c[0]);
        Uniform1f(s["u_radius"], radius);
        Uniform1f(s["u_thickness"], thickness);
        Uniform2f(s["u_angles"], (p.f[4] + p.f[5]) * 0.5f, std::fabs(p.f[5] - p.f[4]) * 0.5f);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawIcon(const Prim& p) {
        if (!ensureIconShader()) {
            return;
        }
        defaultBlend();
        use(iconShader);
        const Shader& s = iconShader;
        Uniform4f(s["u_rect"], p.f[0], p.f[1], p.f[2], p.f[2]);
        uniformColor(s["u_tint"], p.c[0]);
        Uniform1i(s["u_icon"], p.i[0]);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void drawShell(const Prim& p) {
        float x = p.f[0];
        float y = p.f[1];
        float width = p.f[2];
        float height = p.f[3];
        float split = p.f[4];
        float radius = p.f[5];
        float outer = p.f[6];
        uint32_t plateRgba = p.c[0];
        uint32_t mainRgba = p.c[1];
        if (!ensureShellShader()) {
            Prim plate = {};
            plate.kind = ROUND_RECT;
            plate.f[0] = x;
            plate.f[1] = y;
            plate.f[2] = split;
            plate.f[3] = height;
            plate.f[4] = radius;
            plate.f[5] = 0.0f;
            plate.f[6] = 1.0f;
            plate.f[7] = 0.0f;
            plate.f[8] = 1.0f;
            plate.f[9] = 0.0f;
            plate.c[0] = plateRgba;
            plate.c[1] = plateRgba;
            drawRoundRect(plate);
            Prim main = {};
            main.kind = ROUND_RECT;
            main.f[0] = x + split;
            main.f[1] = y;
            main.f[2] = width - split;
            main.f[3] = height;
            main.f[4] = radius;
            main.f[5] = 0.0f;
            main.f[6] = 1.0f;
            main.f[7] = 1.0f;
            main.f[8] = 1.0f;
            main.f[9] = 1.0f;
            main.c[0] = mainRgba;
            main.c[1] = mainRgba;
            drawRoundRect(main);
            return;
        }
        defaultBlend();
        use(shellShader);
        const Shader& s = shellShader;
        Uniform4f(s["u_rect"], x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f);
        Uniform4f(s["u_plate"], x, y, split + radius * 2.0f, height);
        Uniform4f(s["u_plateRadii"], 0.0f, 0.0f, outer, outer);
        uniformColor(s["u_plateColor"], plateRgba);
        Uniform4f(s["u_main"], x + split, y, width - split, height);
        Uniform4f(s["u_mainRadii"], outer, outer, radius, radius);
        uniformColor(s["u_mainColor"], mainRgba);
        DrawArrays(UGL_TRIANGLES, 0, 6);
    }

    static void execute(const ImDrawList*, const ImDrawCmd* cmd) {
        if (!UiGL::loaded() && !UiGL::load()) {
            return;
        }
        const Header* header = (const Header*) cmd->UserCallbackData;
        const Prim* prims = (const Prim*) (header + 1);
        float unit = header->unit > 0.0f ? header->unit : 1.0f;
        float L = header->displayPosX / unit;
        float R = (header->displayPosX + header->displaySizeX) / unit;
        float T = header->displayPosY / unit;
        float B = (header->displayPosY + header->displaySizeY) / unit;
        float ortho[16] = {
                2.0f / (R - L), 0.0f, 0.0f, 0.0f,  0.0f, 2.0f / (T - B),    0.0f,
                0.0f,           0.0f, 0.0f, -1.0f, 0.0f, (R + L) / (L - R), (T + B) / (B - T),
                0.0f,           1.0f,
        };
        std::memcpy(mvpFloats, ortho, sizeof(ortho));
        fbWidth = (int) (header->displaySizeX * header->fbScaleX);
        fbHeight = (int) (header->displaySizeY * header->fbScaleY);
        if (fbWidth <= 0 || fbHeight <= 0) {
            return;
        }
        float clipMinX = (cmd->ClipRect.x - header->displayPosX) * header->fbScaleX;
        float clipMinY = (cmd->ClipRect.y - header->displayPosY) * header->fbScaleY;
        float clipMaxX = (cmd->ClipRect.z - header->displayPosX) * header->fbScaleX;
        float clipMaxY = (cmd->ClipRect.w - header->displayPosY) * header->fbScaleY;
        if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) {
            return;
        }
        Enable(UGL_SCISSOR_TEST);
        Scissor((int) clipMinX, (int) ((float) fbHeight - clipMaxY), (int) (clipMaxX - clipMinX),
                (int) (clipMaxY - clipMinY));
        Disable(UGL_CULL_FACE);
        Disable(UGL_DEPTH_TEST);
        Disable(UGL_STENCIL_TEST);
        for (int i = 0; i < header->count; i++) {
            const Prim& p = prims[i];
            switch (p.kind) {
                case ROUND_RECT: drawRoundRect(p); break;
                case RING: drawRing(p); break;
                case SHADOW: drawShadow(p); break;
                case BLOOM: drawBloom(p); break;
                case KAWASE: drawKawase(p, header->frame); break;
                case BLUR: drawGaussian(p, header->frame); break;
                case PICKER: drawPicker(p); break;
                case ROUND_TEXTURE: drawRoundTexture(p); break;
                case ROUND_TEX: drawRoundTex(p); break;
                case CHECK: drawCheck(p); break;
                case ARC: drawArc(p); break;
                case ICON: drawIcon(p); break;
                case SHELL: drawShell(p); break;
                default: break;
            }
        }
        UseProgram(0);
        BindVertexArray(0);
        ActiveTexture(UGL_TEXTURE0);
        defaultBlend();
    }

    void submit(const Prim& prim) {
        pendingPrims.push_back(prim);
    }

    bool pending() {
        return !pendingPrims.empty();
    }

    void flush(ImDrawList* drawList) {
        if (pendingPrims.empty() || drawList == nullptr) {
            pendingPrims.clear();
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        Header header;
        header.displayPosX = viewport->Pos.x;
        header.displayPosY = viewport->Pos.y;
        header.displaySizeX = io.DisplaySize.x;
        header.displaySizeY = io.DisplaySize.y;
        header.fbScaleX = io.DisplayFramebufferScale.x;
        header.fbScaleY = io.DisplayFramebufferScale.y;
        header.unit = UiRender::unit();
        header.frame = ImGui::GetFrameCount();
        header.count = (int) pendingPrims.size();
        std::vector<unsigned char> payload(sizeof(Header) + sizeof(Prim) * pendingPrims.size());
        std::memcpy(payload.data(), &header, sizeof(Header));
        std::memcpy(payload.data() + sizeof(Header), pendingPrims.data(), sizeof(Prim) * pendingPrims.size());
        drawList->AddCallback(execute, payload.data(), payload.size());
        drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        pendingPrims.clear();
    }

    static Prim make(int kind) {
        Prim p;
        std::memset(&p, 0, sizeof(p));
        p.kind = kind;
        return p;
    }

    void rect(float x, float y, float width, float height, uint32_t rgba) {
        roundRect(x, y, width, height, 0, rgba, 0);
    }

    void roundRect(float x, float y, float width, float height, float radius, uint32_t rgba) {
        roundRect(x, y, width, height, radius, rgba, 0xF);
    }

    void roundRect(float x, float y, float width, float height, float radius, uint32_t rgba, int cornerMask) {
        roundRect(x, y, width, height, radius, rgba, rgba, 0.0f, cornerMask);
    }

    void roundRect(float x, float y, float width, float height, float radius, uint32_t rgba, uint32_t rgbaB,
                   float gradient, int cornerMask) {
        roundRectRadii(x, y, width, height, radius, rgba, rgbaB, gradient, (cornerMask & 1) != 0 ? 1.0f : 0.0f,
                       (cornerMask & 2) != 0 ? 1.0f : 0.0f, (cornerMask & 4) != 0 ? 1.0f : 0.0f,
                       (cornerMask & 8) != 0 ? 1.0f : 0.0f);
    }

    void roundRectRadii(float x, float y, float width, float height, float radius, uint32_t rgba, uint32_t rgbaB,
                        float gradient, float topLeft, float topRight, float bottomLeft, float bottomRight) {
        Prim p = make(ROUND_RECT);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = gradient;
        p.f[6] = topLeft;
        p.f[7] = topRight;
        p.f[8] = bottomLeft;
        p.f[9] = bottomRight;
        p.c[0] = rgba;
        p.c[1] = rgbaB;
        submit(p);
    }

    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgba,
                       int cornerMask) {
        roundRectRing(x, y, width, height, radius, thickness, rgba, rgba, cornerMask);
    }

    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgbaTop,
                       uint32_t rgbaBottom, int cornerMask) {
        roundRectRing(x, y, width, height, radius, thickness, rgbaTop, rgbaBottom, cornerMask, 1.0f, false);
    }

    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgbaTop,
                       uint32_t rgbaBottom, int cornerMask, float fadeEnd, bool horizontal) {
        roundRectRing(x, y, width, height, radius, thickness, rgbaTop, rgbaBottom, cornerMask, fadeEnd, horizontal,
                      0.0f, 0.0f);
    }

    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgbaTop,
                       uint32_t rgbaBottom, int cornerMask, float fadeEnd, bool horizontal, float dash, float gap) {
        Prim p = make(RING);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = thickness;
        p.f[6] = fadeEnd;
        p.f[7] = horizontal ? 1.0f : 0.0f;
        p.f[8] = dash;
        p.f[9] = gap;
        p.c[0] = rgbaTop;
        p.c[1] = rgbaBottom;
        p.i[0] = cornerMask;
        submit(p);
    }

    void roundRectShadow(float x, float y, float width, float height, float radius, float spread, float offsetY,
                         uint32_t rgba) {
        Prim p = make(SHADOW);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = spread;
        p.f[6] = offsetY;
        p.c[0] = rgba;
        submit(p);
    }

    void roundRectBloom(float x, float y, float width, float height, float radius, float spread, float intensity,
                        uint32_t rgba) {
        Prim p = make(BLOOM);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = spread;
        p.f[6] = intensity;
        p.c[0] = rgba;
        submit(p);
    }

    void kawaseBlur(float x, float y, float w, float h, float cornerRadius, float strength, uint32_t tintRgba,
                    int cornerMask, bool recapture) {
        Prim p = make(KAWASE);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = w;
        p.f[3] = h;
        p.f[4] = cornerRadius;
        p.f[5] = strength;
        p.c[0] = tintRgba;
        p.i[0] = cornerMask;
        p.i[1] = recapture ? 1 : 0;
        submit(p);
    }

    void blur(float x, float y, float w, float h, float cornerRadius, float blurRadius) {
        blur(x, y, w, h, cornerRadius, blurRadius, 0xFFFFFFFFu, 0xF, 6.0f);
    }

    void blur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba) {
        blur(x, y, w, h, cornerRadius, blurRadius, tintRgba, 0xF, 6.0f);
    }

    void blur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba,
              int cornerMask, float gamma) {
        Prim p = make(BLUR);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = w;
        p.f[3] = h;
        p.f[4] = cornerRadius;
        p.f[5] = blurRadius;
        p.f[6] = gamma;
        p.c[0] = tintRgba;
        p.i[0] = cornerMask;
        submit(p);
    }

    void pickerSurface(float x, float y, float width, float height, float radius, float hue, int mode, float alpha) {
        pickerSurface(x, y, width, height, radius, hue, 1.0f, 1.0f, mode, alpha);
    }

    void pickerSurface(float x, float y, float width, float height, float radius, float hue, float sat, float val,
                       int mode, float alpha) {
        Prim p = make(PICKER);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = hue;
        p.f[6] = sat;
        p.f[7] = val;
        p.f[8] = (float) mode;
        p.f[9] = alpha;
        submit(p);
    }

    void roundTexture(uint32_t texture, float x, float y, float width, float height, float radius, int cornerMask,
                      float alpha, float fadeWidth, uint32_t fadeRgba, float uvScaleX, float uvScaleY) {
        Prim p = make(ROUND_TEXTURE);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = alpha;
        p.f[6] = fadeWidth;
        p.f[7] = uvScaleX;
        p.f[8] = uvScaleY;
        p.c[0] = fadeRgba;
        p.i[0] = cornerMask;
        p.tex = texture;
        submit(p);
    }

    void roundTex(float x, float y, float width, float height, float radius, uint32_t texture, float u0, float v0,
                  float u1, float v1, uint32_t tintRgba) {
        roundTex(x, y, width, height, radius, texture, u0, v0, u1, v1, tintRgba, 0xF);
    }

    void roundTex(float x, float y, float width, float height, float radius, uint32_t texture, float u0, float v0,
                  float u1, float v1, uint32_t tintRgba, int cornerMask) {
        Prim p = make(ROUND_TEX);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = u0;
        p.f[6] = v0;
        p.f[7] = u1;
        p.f[8] = v1;
        p.c[0] = tintRgba;
        p.i[0] = cornerMask;
        p.tex = texture;
        submit(p);
    }

    void checkMark(float cx, float cy, float size, float thickness, uint32_t rgba) {
        Prim p = make(CHECK);
        p.f[0] = cx;
        p.f[1] = cy;
        p.f[2] = size;
        p.f[3] = thickness;
        p.c[0] = rgba;
        submit(p);
    }

    void arc(float cx, float cy, float radius, float thickness, float from, float to, uint32_t rgba) {
        Prim p = make(ARC);
        p.f[0] = cx;
        p.f[1] = cy;
        p.f[2] = radius;
        p.f[3] = thickness;
        p.f[4] = from;
        p.f[5] = to;
        p.c[0] = rgba;
        submit(p);
    }

    void icon(float x, float y, float size, int kind, uint32_t rgba) {
        Prim p = make(ICON);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = size;
        p.i[0] = kind;
        p.c[0] = rgba;
        submit(p);
    }

    void shell(float x, float y, float width, float height, float split, float radius, uint32_t plateRgba,
               uint32_t mainRgba) {
        shell(x, y, width, height, split, radius, plateRgba, mainRgba, radius);
    }

    void shell(float x, float y, float width, float height, float split, float radius, uint32_t plateRgba,
               uint32_t mainRgba, float outerRadius) {
        Prim p = make(SHELL);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = split;
        p.f[5] = radius;
        p.f[6] = outerRadius;
        p.c[0] = plateRgba;
        p.c[1] = mainRgba;
        submit(p);
    }

    void queueBlur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba,
                   int cornerMask, float gamma) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        Prim p = make(BLUR);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = w;
        p.f[3] = h;
        p.f[4] = cornerRadius;
        p.f[5] = blurRadius;
        p.f[6] = gamma;
        p.c[0] = tintRgba;
        p.i[0] = cornerMask;
        blurQueue.push_back(p);
    }

    void queueBlur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba,
                   int cornerMask, float gamma, bool recapture) {
        size_t before = blurQueue.size();
        queueBlur(x, y, w, h, cornerRadius, blurRadius, tintRgba, cornerMask, gamma);
        if (recapture && blurQueue.size() > before) {
            blurQueue.back().i[1] = 1;
        }
    }

    void queueBlurCorners(float x, float y, float w, float h, float topLeft, float topRight, float bottomLeft,
                          float bottomRight, float blurRadius, uint32_t tintRgba, float gamma) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        Prim p = make(BLUR);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = w;
        p.f[3] = h;
        p.f[4] = 1.0f;
        p.f[5] = blurRadius;
        p.f[6] = gamma;
        p.f[8] = topLeft;
        p.f[9] = topRight;
        p.f[10] = bottomLeft;
        p.f[11] = bottomRight;
        p.c[0] = tintRgba;
        p.i[0] = 0xF;
        p.i[2] = 1;
        blurQueue.push_back(p);
    }

    void flushBlur() {
        for (size_t i = 0; i < blurQueue.size(); i++) {
            submit(blurQueue[i]);
        }
        blurQueue.clear();
    }

    void queueBloom(float x, float y, float width, float height, float radius, float spread, float intensity,
                    uint32_t rgba) {
        if (width <= 0.0f || height <= 0.0f || ((rgba >> 24) & 0xFF) == 0) {
            return;
        }
        Prim p = make(BLOOM);
        p.f[0] = x;
        p.f[1] = y;
        p.f[2] = width;
        p.f[3] = height;
        p.f[4] = radius;
        p.f[5] = spread;
        p.f[6] = intensity;
        p.c[0] = rgba;
        bloomQueue.push_back(p);
    }

    void flushBloom() {
        for (size_t i = 0; i < bloomQueue.size(); i++) {
            submit(bloomQueue[i]);
        }
        bloomQueue.clear();
    }

    void invalidateCapture() {
        captureValid = false;
    }

    void outlineRect(float x, float y, float w, float h, float thickness, uint32_t color) {
        rect(x, y, w, thickness, color);
        rect(x, y + h - thickness, w, thickness, color);
        rect(x, y, thickness, h, color);
        rect(x + w - thickness, y, thickness, h, color);
    }
}
