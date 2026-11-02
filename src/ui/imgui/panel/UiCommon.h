#pragma once

#include <cmath>
#include <cstdint>
#include <string>

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
    int indexOf(const std::string& text, const std::string& needle, int from = 0);
    std::string trim(const std::string& text);
    bool isLetterOrDigit(char32_t c);
    long long millis();
    void rgbToHsb(int r, int g, int b, float* hsb);
    uint32_t hsbToRgb(float hue, float saturation, float brightness);
    bool parseDouble(const std::string& text, double& out);
    // Optional sign, no surrounding whitespace; failure leaves out unchanged.
    bool parseInt(const std::string& text, int& out);
    // At most 15 hexadecimal digits, excluding the optional sign.
    bool parseHex(const std::string& text, long long& out);
    int decimalScale(double value);
    std::string javaFixed(double value, int decimals);
}
