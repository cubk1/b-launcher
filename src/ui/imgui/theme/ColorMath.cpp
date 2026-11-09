#include "ColorMath.h"

#include "../panel/UiCommon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ColorMath {

    namespace {
        int hexDigit(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        int channel(int value) {
            return std::max(0, std::min(255, value));
        }

        uint32_t pack(int a, int r, int g, int b) {
            return ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
        }
    }

    bool parse(std::string_view text, uint32_t& argb) {
        if (text.empty() || text[0] != '#') {
            return false;
        }
        std::string_view digits = text.substr(1);
        if (digits.size() != 3 && digits.size() != 6 && digits.size() != 8) {
            return false;
        }
        int values[8] = {};
        for (size_t i = 0; i < digits.size(); i++) {
            values[i] = hexDigit(digits[i]);
            if (values[i] < 0) {
                return false;
            }
        }
        if (digits.size() == 3) {
            argb = pack(255, values[0] * 17, values[1] * 17, values[2] * 17);
            return true;
        }
        int r = values[0] * 16 + values[1];
        int g = values[2] * 16 + values[3];
        int b = values[4] * 16 + values[5];
        int a = digits.size() == 8 ? values[6] * 16 + values[7] : 255;
        argb = pack(a, r, g, b);
        return true;
    }

    std::string format(uint32_t argb) {
        char out[12];
        uint32_t alpha = argb >> 24;
        if (alpha == 0xFF) {
            std::snprintf(out, sizeof(out), "#%06X", argb & 0x00FFFFFFu);
        } else {
            std::snprintf(out, sizeof(out), "#%06X%02X", argb & 0x00FFFFFFu, alpha);
        }
        return out;
    }

    uint32_t add(uint32_t argb, int amount) {
        return pack((int) (argb >> 24), channel((int) ((argb >> 16) & 0xFF) + amount),
                    channel((int) ((argb >> 8) & 0xFF) + amount), channel((int) (argb & 0xFF) + amount));
    }

    uint32_t scale(uint32_t argb, float factor) {
        return pack((int) (argb >> 24), channel(Ui::round((float) ((argb >> 16) & 0xFF) * factor)),
                    channel(Ui::round((float) ((argb >> 8) & 0xFF) * factor)),
                    channel(Ui::round((float) (argb & 0xFF) * factor)));
    }

    uint32_t withAlpha(uint32_t argb, int alpha) {
        return (argb & 0x00FFFFFFu) | ((uint32_t) channel(alpha) << 24);
    }

    namespace {
        constexpr double PI = 3.14159265358979323846;

        struct Linear {
            double r = 0.0;
            double g = 0.0;
            double b = 0.0;
        };

        double decode(uint32_t value) {
            double c = (double) value / 255.0;
            return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }

        double encode(double value) {
            return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        }

        Linear linearOf(uint32_t argb) {
            return {decode((argb >> 16) & 0xFF), decode((argb >> 8) & 0xFF), decode(argb & 0xFF)};
        }

        int byteOf(double value) {
            return channel((int) std::floor(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5));
        }

        uint32_t pack(const Linear& rgb, int alpha) {
            return ((uint32_t) channel(alpha) << 24) | ((uint32_t) byteOf(encode(rgb.r)) << 16) |
                    ((uint32_t) byteOf(encode(rgb.g)) << 8) | (uint32_t) byteOf(encode(rgb.b));
        }

        Linear fromLab(double l, double a, double b) {
            double lp = l + 0.3963377774 * a + 0.2158037573 * b;
            double mp = l - 0.1055613458 * a - 0.0638541728 * b;
            double sp = l - 0.0894841775 * a - 1.2914855480 * b;
            double lc = lp * lp * lp;
            double mc = mp * mp * mp;
            double sc = sp * sp * sp;
            return {4.0767416621 * lc - 3.3077115913 * mc + 0.2309699292 * sc,
                    -1.2684380046 * lc + 2.6097574011 * mc - 0.3413193965 * sc,
                    -0.0041960863 * lc - 0.7034186147 * mc + 1.7076147010 * sc};
        }

        bool inGamut(const Linear& rgb) {
            const double e = 1e-4;
            return rgb.r >= -e && rgb.r <= 1.0 + e && rgb.g >= -e && rgb.g <= 1.0 + e && rgb.b >= -e &&
                    rgb.b <= 1.0 + e;
        }

        Linear fromLch(double l, double c, double h) {
            double radians = h * PI / 180.0;
            return fromLab(l, c * std::cos(radians), c * std::sin(radians));
        }

        double hue(double v1, double v2, double vh) {
            if (vh < 0.0) {
                vh += 1.0;
            }
            if (vh > 1.0) {
                vh -= 1.0;
            }
            if (vh < 0.16667) {
                return v1 + (v2 - v1) * 6.0 * vh;
            }
            if (vh < 0.5) {
                return v2;
            }
            if (vh < 0.66667) {
                return v1 + (v2 - v1) * (4.0 - vh * 6.0);
            }
            return v1;
        }

        int dotnetByte(double value) {
            return (int) std::nearbyint(std::clamp(value, 0.0, 255.0));
        }
    }

    Oklch toOklch(uint32_t argb) {
        Linear rgb = linearOf(argb);
        double l = std::cbrt(0.4122214708 * rgb.r + 0.5363325363 * rgb.g + 0.0514459929 * rgb.b);
        double m = std::cbrt(0.2119034982 * rgb.r + 0.6806995451 * rgb.g + 0.1073969566 * rgb.b);
        double s = std::cbrt(0.0883024619 * rgb.r + 0.2817188376 * rgb.g + 0.6299787005 * rgb.b);
        double lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
        double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
        double b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
        double h = std::atan2(b, a) * 180.0 / PI;
        return {(float) lightness, (float) std::sqrt(a * a + b * b), (float) (h < 0.0 ? h + 360.0 : h)};
    }

    uint32_t fromOklch(float l, float c, float h, int alpha) {
        double lightness = std::clamp((double) l, 0.0, 1.0);
        double chroma = std::max(0.0, (double) c);
        Linear rgb = fromLch(lightness, chroma, h);
        if (!inGamut(rgb)) {
            double low = 0.0;
            double high = chroma;
            for (int i = 0; i < 24; i++) {
                double mid = (low + high) * 0.5;
                if (inGamut(fromLch(lightness, mid, h))) {
                    low = mid;
                } else {
                    high = mid;
                }
            }
            rgb = fromLch(lightness, low, h);
        }
        return pack(rgb, alpha);
    }

    Hsl toHsl(uint32_t argb) {
        double r = ((argb >> 16) & 0xFF) / 255.0;
        double g = ((argb >> 8) & 0xFF) / 255.0;
        double b = (argb & 0xFF) / 255.0;
        double high = std::max({r, g, b});
        double low = std::min({r, g, b});
        double l = (high + low) * 0.5;
        double d = high - low;
        if (d <= 0.0) {
            return {0.0f, 0.0f, (float) (l * 100.0)};
        }
        double s = l > 0.5 ? d / (2.0 - high - low) : d / (high + low);
        double h = high == r ? (g - b) / d + (g < b ? 6.0 : 0.0) : high == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
        return {(float) (h * 60.0), (float) (s * 100.0), (float) (l * 100.0)};
    }

    uint32_t fromHsl(double h, double s, double l, int alpha) {
        if (s == 0.0) {
            int gray = dotnetByte(l * 2.55);
            return ((uint32_t) channel(alpha) << 24) | ((uint32_t) gray << 16) | ((uint32_t) gray << 8) |
                    (uint32_t) gray;
        }
        double hh = h / 360.0;
        double ss = s / 100.0;
        double ll = l / 100.0;
        ss = ll < 0.5 ? ss * ll + ll : ss * (1.0 - ll) + ll;
        ll = 2.0 * ll - ss;
        int r = dotnetByte(255.0 * hue(ll, ss, hh + 1.0 / 3.0));
        int g = dotnetByte(255.0 * hue(ll, ss, hh));
        int b = dotnetByte(255.0 * hue(ll, ss, hh - 1.0 / 3.0));
        return ((uint32_t) channel(alpha) << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
    }

    uint32_t fromHsl2(double h, double s, double l, int alpha) {
        if (s == 0.0) {
            return fromHsl(h, s, l, alpha);
        }
        static const double CENT[] = {+0.1,  -0.06, -0.3,  -0.19, -0.15, -0.24, -0.32,
                                      -0.09, +0.18, +0.05, -0.12, -0.02, +0.1,  -0.06};
        h = std::fmod(h + 3600000.0, 360.0);
        double center = h / 30.0;
        int index = (int) std::floor(center);
        center = 50.0 - ((1.0 - center + index) * CENT[index] + (center - index) * CENT[index + 1]) * s;
        l = (l < center ? l / center : 1.0 + (l - center) / (100.0 - center)) * 50.0;
        return fromHsl(h, s, l, alpha);
    }

    float luminance(uint32_t argb) {
        Linear rgb = linearOf(argb);
        return (float) (0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b);
    }

    float contrast(uint32_t a, uint32_t b) {
        float la = luminance(a);
        float lb = luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    }

    uint32_t over(uint32_t top, uint32_t bottom) {
        double ta = ((top >> 24) & 0xFF) / 255.0;
        double ba = ((bottom >> 24) & 0xFF) / 255.0;
        double a = ta + ba * (1.0 - ta);
        if (a <= 0.0) {
            return 0;
        }
        auto mixChannel = [&](int shift) {
            double t = (top >> shift) & 0xFF;
            double b = (bottom >> shift) & 0xFF;
            return channel((int) std::floor((t * ta + b * ba * (1.0 - ta)) / a + 0.5));
        };
        return ((uint32_t) channel((int) std::floor(a * 255.0 + 0.5)) << 24) | ((uint32_t) mixChannel(16) << 16) |
                ((uint32_t) mixChannel(8) << 8) | (uint32_t) mixChannel(0);
    }

    uint32_t ensureContrast(uint32_t foreground, uint32_t background, float ratio) {
        if (contrast(foreground, background) >= ratio) {
            return foreground;
        }
        Oklch base = toOklch(foreground);
        int alpha = (int) (foreground >> 24);
        uint32_t dark = fromOklch(0.0f, base.c, base.h, alpha);
        uint32_t light = fromOklch(1.0f, base.c, base.h, alpha);
        bool toLight = contrast(light, background) >= contrast(dark, background);
        float low = base.l;
        float high = toLight ? 1.0f : 0.0f;
        uint32_t best = toLight ? light : dark;
        for (int i = 0; i < 20; i++) {
            float mid = (low + high) * 0.5f;
            uint32_t candidate = fromOklch(mid, base.c, base.h, alpha);
            if (contrast(candidate, background) >= ratio) {
                best = candidate;
                high = mid;
            } else {
                low = mid;
            }
        }
        return best;
    }

    uint32_t rotate(uint32_t argb, float degrees) {
        Oklch value = toOklch(argb);
        return fromOklch(value.l, value.c, std::fmod(value.h + degrees + 360.0f, 360.0f), (int) (argb >> 24));
    }

    uint32_t lighten(uint32_t argb, float amount) {
        Oklch value = toOklch(argb);
        return fromOklch(value.l + amount, value.c, value.h, (int) (argb >> 24));
    }
}
