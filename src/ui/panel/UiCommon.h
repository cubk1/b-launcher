#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Ui {

    inline int round(float value) {
        return (int) std::floor(value + 0.5f);
    }

    inline long long roundl(double value) {
        return (long long) std::floor(value + 0.5);
    }

    std::u32string decode(const std::string& text);
    std::string encode(const std::u32string& text);
    std::string encode(char32_t c);
    inline std::string glyph(int codepoint) {
        return encode((char32_t) codepoint);
    }
    int length(const std::string& text);
    std::string substring(const std::string& text, int from, int to);
    std::string substring(const std::string& text, int from);
    char32_t charAt(const std::string& text, int index);
    int indexOf(const std::string& text, const std::string& needle, int from = 0);
    bool startsWith(const std::string& text, const std::string& prefix);
    bool endsWith(const std::string& text, const std::string& suffix);
    std::string trim(const std::string& text);
    std::string toLower(const std::string& text);
    std::string toUpper(const std::string& text);
    std::string replaceAll(const std::string& text, const std::string& from, const std::string& to);
    std::vector<std::string> split(const std::string& text, const std::string& separator);
    std::string join(const std::vector<std::string>& parts, const std::string& separator);
    bool isLetterOrDigit(char32_t c);
    bool isLetter(char32_t c);
    bool isDigit(char32_t c);
    bool contains(const std::vector<std::string>& list, const std::string& value);
    int indexOf(const std::vector<std::string>& list, const std::string& value);
    bool remove(std::vector<std::string>& list, const std::string& value);
    std::string format(const char* fmt, ...);
    std::string hex8(uint32_t value);
    long long millis();
    void rgbToHsb(int r, int g, int b, float* hsb);
    uint32_t hsbToRgb(float hue, float saturation, float brightness);
    bool parseDouble(const std::string& text, double& out);
    bool parseInt(const std::string& text, int& out);
    bool parseHex(const std::string& text, long long& out);
    std::string valueOf(long long value);
    std::string sha256Hex(const std::string& data);
    std::string shortestDouble(double value);
    int decimalScale(double value);
    std::string javaFixed(double value, int decimals);
}
