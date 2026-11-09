#pragma once

#include <cstdint>

namespace SystemColors {

    struct State {
        bool darkKnown = false;
        bool dark = false;
        bool accentKnown = false;
        uint32_t accent = 0;

        bool operator==(const State& other) const = default;
    };

    State read();
}
