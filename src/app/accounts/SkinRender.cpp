#include "app/accounts/SkinRender.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace SkinRender {

    namespace {
        constexpr float PI = 3.14159265358979f;
        constexpr float BUST_CUT = 16.0f;
        constexpr float MARGIN = 0.04f;
        constexpr int CUTOUT = 26;
        constexpr int SOLID = 250;

        using Color = std::array<unsigned char, 4>;

        struct Vec3 {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Vec3 operator+(Vec3 a, Vec3 b) {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }

        Vec3 operator-(Vec3 a, Vec3 b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        struct Transform {
            float m[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
            Vec3 offset;

            Vec3 rotate(Vec3 v) const {
                return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
            }

            Vec3 apply(Vec3 v) const {
                return rotate(v) + offset;
            }
        };

        Transform then(const Transform& outer, const Transform& inner) {
            Transform out;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    out.m[r][c] = outer.m[r][0] * inner.m[0][c] + outer.m[r][1] * inner.m[1][c] +
                            outer.m[r][2] * inner.m[2][c];
                }
            }
            out.offset = outer.apply(inner.offset);
            return out;
        }

        Transform aroundX(float degrees) {
            float angle = degrees * PI / 180.0f;
            Transform out;
            out.m[1][1] = std::cos(angle);
            out.m[1][2] = -std::sin(angle);
            out.m[2][1] = std::sin(angle);
            out.m[2][2] = std::cos(angle);
            return out;
        }

        Transform aroundY(float degrees) {
            float angle = degrees * PI / 180.0f;
            Transform out;
            out.m[0][0] = std::cos(angle);
            out.m[0][2] = std::sin(angle);
            out.m[2][0] = -std::sin(angle);
            out.m[2][2] = std::cos(angle);
            return out;
        }

        Transform moved(Vec3 offset) {
            Transform out;
            out.offset = offset;
            return out;
        }

        struct Texture {
            const Pixels* image = nullptr;
            int scale = 1;
            bool solid = false;
        };

        struct Face {
            Vec3 origin;
            Vec3 along;
            Vec3 down;
            Vec3 normal;
            const Texture* texture = nullptr;
            float u = 0.0f;
            float v = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            Color color{};
        };

        struct Side {
            Vec3 origin;
            Vec3 along;
            Vec3 down;
            Vec3 normal;
        };

        std::array<Side, 6> sides(Vec3 a, Vec3 b) {
            Vec3 s = b - a;
            return {{
                    {{a.x, b.y, b.z}, {s.x, 0.0f, 0.0f}, {0.0f, -s.y, 0.0f}, {0.0f, 0.0f, 1.0f}},
                    {{b.x, b.y, a.z}, {-s.x, 0.0f, 0.0f}, {0.0f, -s.y, 0.0f}, {0.0f, 0.0f, -1.0f}},
                    {{a.x, b.y, a.z}, {0.0f, 0.0f, s.z}, {0.0f, -s.y, 0.0f}, {-1.0f, 0.0f, 0.0f}},
                    {{b.x, b.y, b.z}, {0.0f, 0.0f, -s.z}, {0.0f, -s.y, 0.0f}, {1.0f, 0.0f, 0.0f}},
                    {{a.x, b.y, a.z}, {s.x, 0.0f, 0.0f}, {0.0f, 0.0f, s.z}, {0.0f, 1.0f, 0.0f}},
                    {{a.x, a.y, a.z}, {s.x, 0.0f, 0.0f}, {0.0f, 0.0f, s.z}, {0.0f, -1.0f, 0.0f}},
            }};
        }

        Face placed(const Transform& place, const Side& side) {
            Face face;
            face.origin = place.apply(side.origin);
            face.along = place.rotate(side.along);
            face.down = place.rotate(side.down);
            face.normal = place.rotate(side.normal);
            return face;
        }

        void box(std::vector<Face>& faces, const Transform& place, Vec3 low, Vec3 high, int u, int v, int w, int h,
                 int d, const Texture* texture) {
            const float rects[6][4] = {
                    {(float) (u + d), (float) (v + d), (float) w, (float) h},
                    {(float) (u + 2 * d + w), (float) (v + d), (float) w, (float) h},
                    {(float) u, (float) (v + d), (float) d, (float) h},
                    {(float) (u + d + w), (float) (v + d), (float) d, (float) h},
                    {(float) (u + d), (float) v, (float) w, (float) d},
                    {(float) (u + d + w), (float) v, (float) w, (float) d},
            };
            std::array<Side, 6> list = sides(low, high);
            for (size_t i = 0; i < list.size(); i++) {
                Face face = placed(place, list[i]);
                face.texture = texture;
                face.u = rects[i][0];
                face.v = rects[i][1];
                face.w = rects[i][2];
                face.h = rects[i][3];
                faces.push_back(face);
            }
        }

        struct Part {
            Vec3 low;
            Vec3 size;
            int u = 0;
            int v = 0;
            int overU = 0;
            int overV = 0;
            float grow = 0.0f;
        };

        void copyRect(Pixels& image, int k, int x, int y, int dx, int dy, int w, int h) {
            for (int j = 0; j < h * k; j++) {
                for (int i = 0; i < w * k; i++) {
                    const unsigned char* from = &image.rgba[((size_t) (y * k + j) * image.width + x * k + i) * 4];
                    unsigned char* to =
                            &image.rgba[((size_t) ((y + dy) * k + j) * image.width + (x + dx) * k + w * k - 1 - i) * 4];
                    std::copy(from, from + 4, to);
                }
            }
        }

        void setOpaque(Pixels& image, int k, int x0, int y0, int x1, int y1) {
            for (int y = y0 * k; y < y1 * k; y++) {
                for (int x = x0 * k; x < x1 * k; x++) {
                    image.rgba[((size_t) y * image.width + x) * 4 + 3] = 255;
                }
            }
        }

        void clearSolidHat(Pixels& image, int k) {
            for (int y = 0; y < 32 * k; y++) {
                for (int x = 32 * k; x < 64 * k; x++) {
                    if (image.rgba[((size_t) y * image.width + x) * 4 + 3] < 128) {
                        return;
                    }
                }
            }
            for (int y = 0; y < 32 * k; y++) {
                for (int x = 32 * k; x < 64 * k; x++) {
                    image.rgba[((size_t) y * image.width + x) * 4 + 3] = 0;
                }
            }
        }

        std::optional<Pixels> normalized(const Pixels& skin) {
            if (skin.width < 64 || skin.width % 64 != 0 ||
                (skin.height != skin.width && skin.height * 2 != skin.width) ||
                skin.rgba.size() < (size_t) skin.width * skin.height * 4) {
                return std::nullopt;
            }
            int k = skin.width / 64;
            bool legacy = skin.height != skin.width;
            Pixels image{skin.width, skin.width, std::vector<unsigned char>((size_t) skin.width * skin.width * 4, 0)};
            std::copy(skin.rgba.begin(), skin.rgba.begin() + (long long) skin.width * skin.height * 4,
                      image.rgba.begin());
            if (legacy) {
                const int moves[][6] = {{4, 16, 16, 32, 4, 4},   {8, 16, 16, 32, 4, 4},    {0, 20, 24, 32, 4, 12},
                                        {4, 20, 16, 32, 4, 12},  {8, 20, 8, 32, 4, 12},    {12, 20, 16, 32, 4, 12},
                                        {44, 16, -8, 32, 4, 4},  {48, 16, -8, 32, 4, 4},   {40, 20, 0, 32, 4, 12},
                                        {44, 20, -8, 32, 4, 12}, {48, 20, -16, 32, 4, 12}, {52, 20, -8, 32, 4, 12}};
                for (const auto& move : moves) {
                    copyRect(image, k, move[0], move[1], move[2], move[3], move[4], move[5]);
                }
            }
            setOpaque(image, k, 0, 0, 32, 16);
            if (legacy) {
                clearSolidHat(image, k);
            }
            setOpaque(image, k, 0, 16, 64, 32);
            setOpaque(image, k, 16, 48, 48, 64);
            return image;
        }

        struct Bounds {
            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();

            void include(const Transform& view, const Transform& place, Vec3 a, Vec3 b) {
                for (int corner = 0; corner < 8; corner++) {
                    Vec3 p{corner & 1 ? b.x : a.x, corner & 2 ? b.y : a.y, corner & 4 ? b.z : a.z};
                    Vec3 seen = view.rotate(place.apply(p));
                    minX = std::min(minX, seen.x);
                    maxX = std::max(maxX, seen.x);
                    minY = std::min(minY, seen.y);
                    maxY = std::max(maxY, seen.y);
                }
            }
        };

        struct Camera {
            Transform view;
            float scale = 1.0f;
            float x = 0.0f;
            float y = 0.0f;

            Vec3 project(Vec3 p) const {
                Vec3 seen = view.rotate(p);
                return {seen.x * scale + x, -seen.y * scale + y, seen.z};
            }
        };

        struct Canvas {
            int size = 0;
            std::vector<float> depth;
            std::vector<float> color;
        };

        float brightness(Vec3 normal, bool shade) {
            if (!shade) {
                return 1.0f;
            }
            const Vec3 light{-0.35f, 0.55f, 0.76f};
            float facing = normal.x * light.x + normal.y * light.y + normal.z * light.z;
            return std::min(1.0f, 0.7f + 0.4f * std::max(0.0f, facing));
        }

        void draw(Canvas& canvas, const Camera& camera, const Face& face, bool translucent, bool shade) {
            Vec3 normal = camera.view.rotate(face.normal);
            if (normal.z <= 1e-4f) {
                return;
            }
            Vec3 p0 = camera.project(face.origin);
            Vec3 p1 = camera.project(face.origin + face.along);
            Vec3 p2 = camera.project(face.origin + face.down);
            float ax = p1.x - p0.x;
            float ay = p1.y - p0.y;
            float bx = p2.x - p0.x;
            float by = p2.y - p0.y;
            float det = ax * by - ay * bx;
            if (std::fabs(det) < 1e-6f) {
                return;
            }
            float xs[4] = {p0.x, p1.x, p2.x, p1.x + bx};
            float ys[4] = {p0.y, p1.y, p2.y, p1.y + by};
            int left = std::max(0, (int) std::floor(*std::min_element(xs, xs + 4)));
            int right = std::min(canvas.size - 1, (int) std::ceil(*std::max_element(xs, xs + 4)));
            int top = std::max(0, (int) std::floor(*std::min_element(ys, ys + 4)));
            int bottom = std::min(canvas.size - 1, (int) std::ceil(*std::max_element(ys, ys + 4)));
            if (left > right || top > bottom) {
                return;
            }
            float light = brightness(normal, shade);
            const Texture* texture = face.texture;
            int k = texture != nullptr ? texture->scale : 1;
            int texLeft = (int) std::lround(face.u * k);
            int texTop = (int) std::lround(face.v * k);
            int texRight = (int) std::lround((face.u + face.w) * k) - 1;
            int texBottom = (int) std::lround((face.v + face.h) * k) - 1;
            for (int py = top; py <= bottom; py++) {
                for (int px = left; px <= right; px++) {
                    float dx = (float) px + 0.5f - p0.x;
                    float dy = (float) py + 0.5f - p0.y;
                    float s = (dx * by - dy * bx) / det;
                    float t = (ax * dy - ay * dx) / det;
                    if (s < 0.0f || s >= 1.0f || t < 0.0f || t >= 1.0f) {
                        continue;
                    }
                    float z = p0.z + s * (p1.z - p0.z) + t * (p2.z - p0.z);
                    size_t at = (size_t) py * canvas.size + px;
                    if (z <= canvas.depth[at]) {
                        continue;
                    }
                    Color color = face.color;
                    if (texture != nullptr) {
                        int tx = std::clamp((int) std::floor((face.u + s * face.w) * k), texLeft, texRight);
                        int ty = std::clamp((int) std::floor((face.v + t * face.h) * k), texTop, texBottom);
                        if (tx < 0 || ty < 0 || tx >= texture->image->width || ty >= texture->image->height) {
                            continue;
                        }
                        const unsigned char* p = &texture->image->rgba[((size_t) ty * texture->image->width + tx) * 4];
                        color = Color{p[0], p[1], p[2], texture->solid ? (unsigned char) 255 : p[3]};
                    }
                    if (color[3] < CUTOUT || (color[3] >= SOLID) == translucent) {
                        continue;
                    }
                    float* out = &canvas.color[at * 4];
                    float r = color[0] / 255.0f * light;
                    float g = color[1] / 255.0f * light;
                    float b = color[2] / 255.0f * light;
                    if (!translucent) {
                        out[0] = r;
                        out[1] = g;
                        out[2] = b;
                        out[3] = 1.0f;
                        canvas.depth[at] = z;
                        continue;
                    }
                    float alpha = color[3] / 255.0f;
                    out[0] = r * alpha + out[0] * (1.0f - alpha);
                    out[1] = g * alpha + out[1] * (1.0f - alpha);
                    out[2] = b * alpha + out[2] * (1.0f - alpha);
                    out[3] = alpha + out[3] * (1.0f - alpha);
                }
            }
        }

        std::shared_ptr<const Pixels> resolve(const Canvas& canvas, int size, int samples) {
            auto out = std::make_shared<Pixels>();
            out->width = size;
            out->height = size;
            out->rgba.assign((size_t) size * size * 4, 0);
            float count = (float) (samples * samples);
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    for (int sy = 0; sy < samples; sy++) {
                        for (int sx = 0; sx < samples; sx++) {
                            const float* c =
                                    &canvas.color[((size_t) (y * samples + sy) * canvas.size + x * samples + sx) * 4];
                            for (int i = 0; i < 4; i++) {
                                sum[i] += c[i];
                            }
                        }
                    }
                    if (sum[3] <= 0.0f) {
                        continue;
                    }
                    unsigned char* p = &out->rgba[((size_t) y * size + x) * 4];
                    for (int i = 0; i < 3; i++) {
                        p[i] = (unsigned char) std::lround(std::clamp(sum[i] / sum[3], 0.0f, 1.0f) * 255.0f);
                    }
                    p[3] = (unsigned char) std::lround(std::clamp(sum[3] / count, 0.0f, 1.0f) * 255.0f);
                }
            }
            return out;
        }
    }

    bool looksSlim(const Pixels& skin) {
        if (skin.width < 64 || skin.width % 64 != 0 || skin.height != skin.width ||
            skin.rgba.size() < (size_t) skin.width * skin.height * 4) {
            return false;
        }
        int k = skin.width / 64;
        auto clear = [&](int x0, int y0, int x1, int y1) {
            for (int y = y0 * k; y < y1 * k; y++) {
                for (int x = x0 * k; x < x1 * k; x++) {
                    if (skin.rgba[((size_t) y * skin.width + x) * 4 + 3] != 0) {
                        return false;
                    }
                }
            }
            return true;
        };
        return clear(50, 16, 52, 20) && clear(54, 20, 56, 32);
    }

    std::shared_ptr<const Pixels> render(const Pixels& skin, const Pixels* cape, const Options& options) {
        std::optional<Pixels> image = normalized(skin);
        if (!image) {
            return nullptr;
        }
        int size = std::clamp(options.size, 8, 1024);
        int samples = std::clamp(options.samples, 1, 8);
        bool head = options.framing == Framing::Head;
        bool bust = options.framing == Framing::Bust;
        Texture skinTexture{&*image, image->width / 64, false};
        bool caped = cape != nullptr && !head && cape->width > 0 && cape->height > 0 &&
                cape->rgba.size() >= (size_t) cape->width * cape->height * 4;
        Texture capeTexture{cape, caped ? std::max(1, (cape->width + 63) / 64) : 1, true};

        float arm = options.slim ? 3.0f : 4.0f;
        float armLow = options.slim ? 11.5f : 12.0f;
        std::vector<Part> parts = {{{-4.0f, 24.0f, -4.0f}, {8.0f, 8.0f, 8.0f}, 0, 0, 32, 0, 0.5f}};
        if (!head) {
            parts.push_back({{-4.0f, 12.0f, -2.0f}, {8.0f, 12.0f, 4.0f}, 16, 16, 16, 32, 0.25f});
            parts.push_back({{-4.0f - arm, armLow, -2.0f}, {arm, 12.0f, 4.0f}, 40, 16, 40, 32, 0.25f});
            parts.push_back({{4.0f, armLow, -2.0f}, {arm, 12.0f, 4.0f}, 32, 48, 48, 48, 0.25f});
        }
        if (options.framing == Framing::Body) {
            parts.push_back({{-4.0f, 0.0f, -2.0f}, {4.0f, 12.0f, 4.0f}, 0, 16, 0, 32, 0.25f});
            parts.push_back({{0.0f, 0.0f, -2.0f}, {4.0f, 12.0f, 4.0f}, 16, 48, 0, 48, 0.25f});
        }

        float cut = bust ? BUST_CUT : std::numeric_limits<float>::lowest();
        Transform view = then(aroundX(options.pitch), aroundY(options.yaw));
        Transform none;
        std::vector<Face> faces;
        Bounds bounds;
        for (const Part& part : parts) {
            int w = (int) part.size.x;
            int h = (int) part.size.y;
            int d = (int) part.size.z;
            Vec3 high = part.low + part.size;
            box(faces, none, part.low, high, part.u, part.v, w, h, d, &skinTexture);
            float extent = 0.0f;
            if (options.layer == Layer::Flat) {
                extent = part.grow;
                Vec3 grow{extent, extent, extent};
                box(faces, none, part.low - grow, high + grow, part.overU, part.overV, w, h, d, &skinTexture);
            }
            Vec3 a{part.low.x - extent, std::max(part.low.y - extent, cut), part.low.z - extent};
            Vec3 b{high.x + extent, high.y + extent, high.z + extent};
            bounds.include(view, none, a, b);
        }
        if (caped) {
            Transform place = then(moved({0.0f, 24.0f, -2.0f}), then(aroundX(options.capeTilt), aroundY(180.0f)));
            Vec3 a{-5.0f, -16.0f, 0.0f};
            Vec3 b{5.0f, 0.0f, 1.0f};
            box(faces, place, a, b, 0, 0, 10, 16, 1, &capeTexture);
            if (!bust) {
                bounds.include(view, place, a, b);
            }
        }

        int n = size * samples;
        float room = (float) n;
        float width = std::max(1e-3f, bounds.maxX - bounds.minX);
        float height = std::max(1e-3f, bounds.maxY - bounds.minY);
        Camera camera;
        camera.view = view;
        if (bust) {
            camera.scale = std::min(room * (1.0f - 2.0f * MARGIN) / width, room * (1.0f - MARGIN) / height);
            camera.x = room * 0.5f - (bounds.minX + bounds.maxX) * 0.5f * camera.scale;
            camera.y = room + bounds.minY * camera.scale;
        } else {
            camera.scale = std::min(room * (1.0f - 2.0f * MARGIN) / width, room * (1.0f - 2.0f * MARGIN) / height);
            camera.x = room * 0.5f - (bounds.minX + bounds.maxX) * 0.5f * camera.scale;
            camera.y = room * 0.5f + (bounds.minY + bounds.maxY) * 0.5f * camera.scale;
        }

        Canvas canvas;
        canvas.size = n;
        canvas.depth.assign((size_t) n * n, std::numeric_limits<float>::lowest());
        canvas.color.assign((size_t) n * n * 4, 0.0f);
        for (bool translucent : {false, true}) {
            for (const Face& face : faces) {
                draw(canvas, camera, face, translucent, options.shade);
            }
        }
        return resolve(canvas, size, samples);
    }
}
