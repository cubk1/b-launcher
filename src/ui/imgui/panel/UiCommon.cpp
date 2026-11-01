#include "UiCommon.h"

#include <core/base/hash.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    char32_t charAt(const std::string& text, int index) {
        size_t a = byteOffset(text, index);
        size_t b = byteOffset(text, index + 1);
        std::u32string one = decode(text.substr(a, b - a));
        return one.empty() ? 0 : one[0];
    }

    int indexOf(const std::string& text, const std::string& needle, int from) {
        size_t start = byteOffset(text, std::max(0, from));
        size_t at = text.find(needle, start);
        if (at == std::string::npos) {
            return -1;
        }
        return length(text.substr(0, at));
    }

    bool startsWith(const std::string& text, const std::string& prefix) {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    bool endsWith(const std::string& text, const std::string& suffix) {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
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

    std::string toLower(const std::string& text) {
        return b_launcher_base::to_lower(text);
    }

    std::string toUpper(const std::string& text) {
        std::string out = text;
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') {
                c = (char) (c - 'a' + 'A');
            }
        }
        return out;
    }

    std::string replaceAll(const std::string& text, const std::string& from, const std::string& to) {
        return b_launcher_base::replace_all(text, from, to);
    }

    std::vector<std::string> split(const std::string& text, const std::string& separator) {
        std::vector<std::string> parts;
        if (separator.empty()) {
            parts.push_back(text);
            return parts;
        }
        size_t at = 0;
        while (true) {
            size_t next = text.find(separator, at);
            if (next == std::string::npos) {
                parts.push_back(text.substr(at));
                break;
            }
            parts.push_back(text.substr(at, next - at));
            at = next + separator.size();
        }
        return parts;
    }

    std::string join(const std::vector<std::string>& parts, const std::string& separator) {
        return b_launcher_base::join(parts, separator);
    }

    bool isDigit(char32_t c) {
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

    bool isLetter(char32_t c) {
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

    bool contains(const std::vector<std::string>& list, const std::string& value) {
        return std::find(list.begin(), list.end(), value) != list.end();
    }

    int indexOf(const std::vector<std::string>& list, const std::string& value) {
        auto it = std::find(list.begin(), list.end(), value);
        return it == list.end() ? -1 : (int) (it - list.begin());
    }

    bool remove(std::vector<std::string>& list, const std::string& value) {
        auto it = std::find(list.begin(), list.end(), value);
        if (it == list.end()) {
            return false;
        }
        list.erase(it);
        return true;
    }

    std::string format(const char* fmt, ...) {
        char buffer[512];
        va_list args;
        va_start(args, fmt);
        int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (written < 0) {
            return std::string();
        }
        if (written < (int) sizeof(buffer)) {
            return std::string(buffer, written);
        }
        std::string out((size_t) written + 1, '\0');
        va_start(args, fmt);
        vsnprintf(&out[0], out.size(), fmt, args);
        va_end(args);
        out.resize((size_t) written);
        return out;
    }

    std::string hex8(uint32_t value) {
        return format("%08X", value);
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

    bool parseInt(const std::string& text, int& out) {
        if (text.empty()) {
            return false;
        }
        size_t i = 0;
        if (text[0] == '-' || text[0] == '+') {
            i = 1;
            if (text.size() == 1) {
                return false;
            }
        }
        for (size_t k = i; k < text.size(); k++) {
            if (text[k] < '0' || text[k] > '9') {
                return false;
            }
        }
        long long parsed = std::strtoll(text.c_str(), nullptr, 10);
        if (parsed > 2147483647LL || parsed < -2147483648LL) {
            return false;
        }
        out = (int) parsed;
        return true;
    }

    bool parseHex(const std::string& text, long long& out) {
        if (text.empty()) {
            return false;
        }
        size_t i = 0;
        if (text[0] == '-' || text[0] == '+') {
            i = 1;
            if (text.size() == 1) {
                return false;
            }
        }
        for (size_t k = i; k < text.size(); k++) {
            char c = text[k];
            bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex) {
                return false;
            }
        }
        if (text.size() - i > 15) {
            return false;
        }
        out = std::strtoll(text.c_str(), nullptr, 16);
        return true;
    }

    std::string valueOf(long long value) {
        return std::to_string(value);
    }

    std::string sha256Hex(const std::string& data) {
        return b_launcher_base::to_hex(b_launcher_base::sha256::digest(data.data(), data.size()));
    }

    std::string shortestDouble(double value) {
        char buffer[400];
        for (int decimals = 0; decimals <= 17; decimals++) {
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            if (std::strtod(buffer, nullptr) == value) {
                return buffer;
            }
        }
        return format("%.17f", value);
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
        bool allZero = true;
        for (char c : out) {
            if (c != '0' && c != '.') {
                allZero = false;
            }
        }
        if (negative && !allZero) {
            out = "-" + out;
        } else if (negative && allZero) {
            out = "-" + out;
        }
        return out;
    }
}
