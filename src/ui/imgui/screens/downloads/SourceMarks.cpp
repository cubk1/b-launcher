#include "ui/imgui/screens/downloads/SourceMarks.h"
#include "ui/imgui/screens/shell/SvgPath.h"

#include "ui/imgui/panel/UiRender.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace SourceMarks {

    namespace {
        const char* const MODRINTH_MARK =
                "F1 M12.252.004a11.78 11.768 0 0 0-8.92 3.73 11 10.999 0 0 0-2.17 3.11 11.37 11.359 0 0 0-1.16 5.169c"
                "0 1.42.17 2.5.6 3.77.24.759.77 1.899 1.17 2.529a12.3 12.298 0 0 0 8.85 5.639c.44.05 2.54.07 2.76.02."
                "2-.04.22.1-.26-1.7l-.36-1.37-1.01-.06a8.5 8.489 0 0 1-5.18-1.8 5.34 5.34 0 0 1-1.3-1.26c0-.05.34-.28"
                ".74-.5a37.572 37.545 0 0 1 2.88-1.629c.03 0 .5.45 1.06.98l1 .97 2.07-.43 2.06-.43 1.47-1.47c.8-.8 1."
                "48-1.5 1.48-1.52 0-.09-.42-1.63-.46-1.7-.04-.06-.2-.03-1.02.18-.53.13-1.2.3-1.45.4l-.48.15-.53.53-.5"
                "3.53-.93.1-.93.07-.52-.5a2.7 2.7 0 0 1-.96-1.7l-.13-.6.43-.57c.68-.9.68-.9 1.46-1.1.4-.1.65-.2.83-.3"
                "3.13-.099.65-.579 1.14-1.069l.9-.9-.7-.7-.7-.7-1.95.54c-1.07.3-1.96.53-1.97.53-.03 0-2.23 2.48-2.63 "
                "2.97l-.29.35.28 1.03c.16.56.3 1.16.31 1.34l.03.3-.34.23c-.37.23-2.22 1.3-2.84 1.63-.36.2-.37.2-.44.1"
                "-.08-.1-.23-.6-.32-1.03-.18-.86-.17-2.75.02-3.73a8.84 8.839 0 0 1 7.9-6.93c.43-.03.77-.08.78-.1.06-."
                "17.5-2.999.47-3.039-.01-.02-.1-.02-.2-.03Zm3.68.67c-.2 0-.3.1-.37.38-.06.23-.46 2.42-.46 2.52 0 .04."
                "1.11.22.16a8.51 8.499 0 0 1 2.99 2 8.38 8.379 0 0 1 2.16 3.449 6.9 6.9 0 0 1 .4 2.8c0 1.07 0 1.27-.1"
                " 1.73a9.37 9.369 0 0 1-1.76 3.769c-.32.4-.98 1.06-1.37 1.38-.38.32-1.54 1.1-1.7 1.14-.1.03-.1.06-.07"
                ".26.03.18.64 2.56.7 2.78l.06.06a12.07 12.058 0 0 0 7.27-9.4c.13-.77.13-2.58 0-3.4a11.96 11.948 0 0 0"
                "-5.73-8.578c-.7-.42-2.05-1.06-2.25-1.06Z";
        const char* const CURSEFORGE_MARK =
                "F1 M18.326 9.2145S23.2261 8.4418 24 6.1882h-7.5066V4.4H0l2.0318 2.3576V9.173s5.1267-.2665 7.1098 1.2"
                "372c2.7146 2.516-3.053 5.917-3.053 5.917L5.0995 19.6c1.5465-1.4726 4.494-3.3775 9.8983-3.2857-2.0565"
                ".65-4.1245 1.6651-5.7344 3.2857h10.9248l-1.0288-3.2726s-7.918-4.6688-.8336-7.1127z";
        const char* const MCMOD_SQUARE = "M4.5 0H19.5A4.5 4.5 0 0 1 24 4.5V19.5A4.5 4.5 0 0 1 19.5 24H4.5A4.5 4.5 0 0 "
                                         "1 0 19.5V4.5A4.5 4.5 0 0 1 4.5 0Z";
        const char* const MCMOD_HALF = "M1.318 22.682A4.5 4.5 0 0 1 0 19.5V4.5A4.5 4.5 0 0 1 4.5 0H19.5A4.5 4.5 0 0 1 "
                                       "22.682 1.318Z";
        const char* const MCMOD_LETTERS =
                "M8.614 16.568c0.818-2.255 1.625-4.487 2.431-6.708 0.035 0 0.069 0.012 0.104 0.012v6.685h1.786v-8.848"
                "h-3.042c-0.749 2.048-1.486 4.073-2.27 6.224-0.081-0.23-0.127-0.345-0.173-0.472l-2.005-5.476c-0.046-0"
                ".115-0.173-0.276-0.265-0.276-0.979-0.023-1.97-0.012-2.938-0.012v8.859h1.878v-6.662c0.023 0 0.058-0.0"
                "11 0.081-0.011 0.807 2.232 1.625 4.453 2.431 6.685h1.982zM21.414 7.778c-0.092-0.058-0.104-0.081-0.12"
                "7-0.081-1.648 0-3.307-0.046-4.954 0.023-0.461 0.011-0.945 0.265-1.348 0.529-0.806 0.529-0.933 1.415-"
                "0.968 2.29-0.046 1.047-0.058 2.106 0.011 3.152 0.035 0.575 0.161 1.196 0.415 1.703 0.472 0.943 1.417"
                " 1.151 2.373 1.185 0.945 0.035 1.89 0.011 2.846 0.011h1.878v-1.783c-0.058-0.012-0.081-0.023-0.104-0."
                "023h-4.563c-0.53 0-0.933-0.299-0.968-0.817-0.058-1.082-0.115-2.175-0.081-3.256 0.035-0.966 0.415-1.2"
                "66 1.383-1.266h2.235c0.115 0 0.277-0.012 0.357-0.081 0.541-0.541 1.06-1.058 1.613-1.588z";
        constexpr float LETTERS_X = 2.243f;
        constexpr float LETTERS_Y = 7.682f;
        constexpr float LETTERS_W = 19.298f;
        constexpr float LETTERS_H = 8.916f;
        constexpr float HALF = 22.682f;

        enum Layer { KNOCKOUT, SQUARE, HALF_FILL, LETTERS };

        const std::string& layer(Layer which) {
            static const std::string layers[] = {std::string("F0 ") + MCMOD_SQUARE + MCMOD_LETTERS,
                                                 std::string("F1 ") + MCMOD_SQUARE, std::string("F1 ") + MCMOD_HALF,
                                                 std::string("F1 ") + MCMOD_LETTERS};
            return layers[which];
        }
    }

    void draw(Addons::Source source, float x, float y, float size, uint32_t color) {
        SvgPath::draw(source == Addons::Source::CurseForge ? CURSEFORGE_MARK : MODRINTH_MARK, x, y, size, size, color);
    }

    void mcmod(float x, float y, float size, uint32_t color) {
        SvgPath::draw(layer(KNOCKOUT), x, y, size, size, color);
    }

    void mcmodColored(float x, float y, float size, float alpha) {
        int a = (int) std::lround(255.0f * std::clamp(alpha, 0.0f, 1.0f));
        if (a <= 0) {
            return;
        }
        float unit = size / 24.0f;
        SvgPath::draw(layer(SQUARE), x, y, size, size, UiRender::color(91, 181, 219, a));
        SvgPath::draw(layer(HALF_FILL), x, y, HALF * unit, HALF * unit, UiRender::color(139, 195, 84, a));
        SvgPath::draw(layer(LETTERS), x + LETTERS_X * unit, y + LETTERS_Y * unit, LETTERS_W * unit, LETTERS_H * unit,
                      UiRender::color(255, 255, 255, a));
    }

    uint32_t brand(Addons::Source source) {
        return source == Addons::Source::CurseForge ? UiRender::color(241, 100, 54) : UiRender::color(27, 217, 106);
    }
}
