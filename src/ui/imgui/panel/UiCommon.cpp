#include "UiCommon.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Ui {

    std::u32string decode(const std::string& text) {
        std::u32string out;
        out.reserve(text.size());
        size_t i = 0;
        while (i < text.size()) {
            unsigned char c = (unsigned char) text[i];
            char32_t cp;
            int extra;
            if (c < 0x80) {
                cp = c;
                extra = 0;
            } else if ((c & 0xE0) == 0xC0) {
                cp = c & 0x1F;
                extra = 1;
            } else if ((c & 0xF0) == 0xE0) {
                cp = c & 0x0F;
                extra = 2;
            } else if ((c & 0xF8) == 0xF0) {
                cp = c & 0x07;
                extra = 3;
            } else {
                out.push_back(0xFFFD);
                i++;
                continue;
            }
            if (extra > 0 && i + extra >= text.size()) {
                out.push_back(0xFFFD);
                break;
            }
            bool bad = false;
            for (int k = 1; k <= extra; k++) {
                unsigned char next = (unsigned char) text[i + k];
                if ((next & 0xC0) != 0x80) {
                    bad = true;
                    break;
                }
                cp = (cp << 6) | (next & 0x3F);
            }
            if (bad) {
                out.push_back(0xFFFD);
                i++;
                continue;
            }
            out.push_back(cp);
            i += extra + 1;
        }
        return out;
    }

    std::string encode(char32_t c) {
        std::string out;
        if (c < 0x80) {
            out.push_back((char) c);
        } else if (c < 0x800) {
            out.push_back((char) (0xC0 | (c >> 6)));
            out.push_back((char) (0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char) (0xE0 | (c >> 12)));
            out.push_back((char) (0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char) (0x80 | (c & 0x3F)));
        } else {
            out.push_back((char) (0xF0 | (c >> 18)));
            out.push_back((char) (0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char) (0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char) (0x80 | (c & 0x3F)));
        }
        return out;
    }

    std::string encode(const std::u32string& text) {
        std::string out;
        out.reserve(text.size());
        for (char32_t c : text) {
            out += encode(c);
        }
        return out;
    }

    static size_t byteOffset(const std::string& text, int index) {
        size_t i = 0;
        int count = 0;
        while (i < text.size() && count < index) {
            unsigned char c = (unsigned char) text[i];
            if (c < 0x80) {
                i += 1;
            } else if ((c & 0xE0) == 0xC0) {
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                i += 4;
            } else {
                i += 1;
            }
            count++;
        }
        return std::min(i, text.size());
    }

    int length(const std::string& text) {
        int count = 0;
        for (size_t i = 0; i < text.size(); i++) {
            if (((unsigned char) text[i] & 0xC0) != 0x80) {
                count++;
            }
        }
        return count;
    }

    std::string substring(const std::string& text, int from, int to) {
        if (to <= from) {
            return std::string();
        }
        size_t a = byteOffset(text, from);
        size_t b = byteOffset(text, to);
        return text.substr(a, b - a);
    }

    std::string substring(const std::string& text, int from) {
        return text.substr(byteOffset(text, from));
    }

    int indexOf(const std::string& text, const std::string& needle, int from) {
        size_t start = byteOffset(text, std::max(0, from));
        size_t at = text.find(needle, start);
        if (at == std::string::npos) {
            return -1;
        }
        return length(text.substr(0, at));
    }


    std::string trim(const std::string& text) {
        size_t a = 0;
        size_t b = text.size();
        while (a < b && (unsigned char) text[a] <= ' ') {
            a++;
        }
        while (b > a && (unsigned char) text[b - 1] <= ' ') {
            b--;
        }
        return text.substr(a, b - a);
    }

    static bool isDigit(char32_t c) {
        if (c < 0x80) {
            return c >= '0' && c <= '9';
        }
#if defined(_WIN32)
        if (c < 0x10000) {
            WORD type = 0;
            wchar_t w = (wchar_t) c;
            if (GetStringTypeW(CT_CTYPE1, &w, 1, &type)) {
                return (type & C1_DIGIT) != 0;
            }
        }
#endif
        return false;
    }

    static bool isLetter(char32_t c) {
        if (c < 0x80) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }
#if defined(_WIN32)
        if (c < 0x10000) {
            return IsCharAlphaW((wchar_t) c) != 0;
        }
#endif
        return c >= 0x10000;
    }

    bool isLetterOrDigit(char32_t c) {
        return isLetter(c) || isDigit(c);
    }



    long long millis() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    void rgbToHsb(int r, int g, int b, float* hsbvals) {
        float hue;
        float saturation;
        float brightness;
        int cmax = (r > g) ? r : g;
        if (b > cmax) {
            cmax = b;
        }
        int cmin = (r < g) ? r : g;
        if (b < cmin) {
            cmin = b;
        }
        brightness = ((float) cmax) / 255.0f;
        if (cmax != 0) {
            saturation = ((float) (cmax - cmin)) / ((float) cmax);
        } else {
            saturation = 0;
        }
        if (saturation == 0) {
            hue = 0;
        } else {
            float redc = ((float) (cmax - r)) / ((float) (cmax - cmin));
            float greenc = ((float) (cmax - g)) / ((float) (cmax - cmin));
            float bluec = ((float) (cmax - b)) / ((float) (cmax - cmin));
            if (r == cmax) {
                hue = bluec - greenc;
            } else if (g == cmax) {
                hue = 2.0f + redc - bluec;
            } else {
                hue = 4.0f + greenc - redc;
            }
            hue = hue / 6.0f;
            if (hue < 0) {
                hue = hue + 1.0f;
            }
        }
        hsbvals[0] = hue;
        hsbvals[1] = saturation;
        hsbvals[2] = brightness;
    }

    uint32_t hsbToRgb(float hue, float saturation, float brightness) {
        int r = 0;
        int g = 0;
        int b = 0;
        if (saturation == 0) {
            r = g = b = (int) (brightness * 255.0f + 0.5f);
        } else {
            float h = (hue - (float) std::floor(hue)) * 6.0f;
            float f = h - (float) std::floor(h);
            float p = brightness * (1.0f - saturation);
            float q = brightness * (1.0f - saturation * f);
            float t = brightness * (1.0f - (saturation * (1.0f - f)));
            switch ((int) h) {
                case 0:
                    r = (int) (brightness * 255.0f + 0.5f);
                    g = (int) (t * 255.0f + 0.5f);
                    b = (int) (p * 255.0f + 0.5f);
                    break;
                case 1:
                    r = (int) (q * 255.0f + 0.5f);
                    g = (int) (brightness * 255.0f + 0.5f);
                    b = (int) (p * 255.0f + 0.5f);
                    break;
                case 2:
                    r = (int) (p * 255.0f + 0.5f);
                    g = (int) (brightness * 255.0f + 0.5f);
                    b = (int) (t * 255.0f + 0.5f);
                    break;
                case 3:
                    r = (int) (p * 255.0f + 0.5f);
                    g = (int) (q * 255.0f + 0.5f);
                    b = (int) (brightness * 255.0f + 0.5f);
                    break;
                case 4:
                    r = (int) (t * 255.0f + 0.5f);
                    g = (int) (p * 255.0f + 0.5f);
                    b = (int) (brightness * 255.0f + 0.5f);
                    break;
                case 5:
                    r = (int) (brightness * 255.0f + 0.5f);
                    g = (int) (p * 255.0f + 0.5f);
                    b = (int) (q * 255.0f + 0.5f);
                    break;
                default: break;
            }
        }
        return 0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
    }

    bool parseDouble(const std::string& text, double& out) {
        std::string value = trim(text);
        if (value.empty()) {
            return false;
        }
        char* end = nullptr;
        double parsed = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || (end != nullptr && *end != '\0')) {
            return false;
        }
        out = parsed;
        return true;
    }

    template <typename T>
    static bool parseInteger(std::string_view text, T& out, int base) {
        // UI input accepts a leading '+', which from_chars does not consume.
        if (text.starts_with('+')) {
            text.remove_prefix(1);
            if (text.starts_with('-')) return false;
        }
        T parsed = 0;
        auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, base);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return false;
        out = parsed;
        return true;
    }

    bool parseInt(const std::string& text, int& out) {
        return parseInteger(text, out, 10);
    }

    bool parseHex(const std::string& text, long long& out) {
        if (text.empty()) return false;
        size_t sign = text[0] == '-' || text[0] == '+' ? 1 : 0;
        return text.size() - sign <= 15 && parseInteger(text, out, 16);
    }

    static std::string shortestDouble(double value) {
        char buffer[400];
        for (int decimals = 0; decimals <= 17; decimals++) {
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            if (std::strtod(buffer, nullptr) == value) {
                return buffer;
            }
        }
        return buffer;
    }

    int decimalScale(double value) {
        std::string text = shortestDouble(value);
        size_t dot = text.find('.');
        if (dot == std::string::npos) {
            int zeros = 0;
            for (int i = (int) text.size() - 1; i >= 0 && text[i] == '0'; i--) {
                zeros++;
            }
            if (zeros == (int) text.size()) {
                return 0;
            }
            return -zeros;
        }
        size_t end = text.size();
        while (end > dot + 1 && text[end - 1] == '0') {
            end--;
        }
        return (int) (end - dot - 1);
    }

    std::string javaFixed(double value, int decimals) {
        if (std::isnan(value)) {
            return "NaN";
        }
        if (std::isinf(value)) {
            return value > 0 ? "Infinity" : "-Infinity";
        }
        bool negative = std::signbit(value);
        std::string text = shortestDouble(std::fabs(value));
        size_t dot = text.find('.');
        std::string integer = dot == std::string::npos ? text : text.substr(0, dot);
        std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot + 1);
        if ((int) fraction.size() < decimals) {
            fraction.append((size_t) decimals - fraction.size(), '0');
        }
        bool roundUp = (int) fraction.size() > decimals && fraction[(size_t) decimals] >= '5';
        std::string digits = integer + fraction.substr(0, (size_t) decimals);
        if (roundUp) {
            int i = (int) digits.size() - 1;
            while (i >= 0) {
                if (digits[(size_t) i] == '9') {
                    digits[(size_t) i] = '0';
                    i--;
                } else {
                    digits[(size_t) i]++;
                    break;
                }
            }
            if (i < 0) {
                digits.insert(digits.begin(), '1');
            }
        }
        size_t integerLength = digits.size() - (size_t) decimals;
        std::string out = digits.substr(0, integerLength);
        if (out.empty()) {
            out = "0";
        }
        if (decimals > 0) {
            out += "." + digits.substr(integerLength);
        }
        if (negative) {
            out = "-" + out;
        }
        return out;
    }
}
