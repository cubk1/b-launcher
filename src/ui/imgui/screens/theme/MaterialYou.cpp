#include "ui/imgui/screens/theme/MaterialYou.h"

#include "cpp/blend/blend.h"
#include "cpp/cam/hct.h"
#include "cpp/palettes/tones.h"
#include "cpp/quantize/celebi.h"
#include "cpp/score/score.h"

#include <algorithm>

namespace MaterialYou {

    namespace mcu = material_color_utilities;

    uint32_t hct(double hue, double chroma, double tone) {
        return mcu::Hct(hue, chroma, tone).ToInt();
    }

    void toHct(uint32_t argb, double& hue, double& chroma, double& tone) {
        mcu::Hct value(argb);
        hue = value.get_hue();
        chroma = value.get_chroma();
        tone = value.get_tone();
    }

    uint32_t tone(double hue, double chroma, double value) {
        return mcu::TonalPalette(hue, chroma).get(value);
    }

    uint32_t harmonize(uint32_t design, uint32_t key) {
        return mcu::BlendHarmonize(design, key);
    }

    std::vector<std::pair<uint32_t, uint32_t>> quantize(const std::vector<uint32_t>& pixels, int count) {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        if (pixels.empty()) {
            return out;
        }
        mcu::QuantizerResult result = mcu::QuantizeCelebi(pixels, count);
        for (const auto& [argb, population] : result.color_to_count) {
            out.push_back({argb, population});
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return out;
    }

    std::vector<uint32_t> ranked(const std::vector<uint32_t>& pixels, int count) {
        if (pixels.empty()) {
            return {};
        }
        mcu::QuantizerResult result = mcu::QuantizeCelebi(pixels, 128);
        mcu::ScoreOptions options;
        options.desired = (size_t) std::max(1, count);
        return mcu::RankedSuggestions(result.color_to_count, options);
    }
}
