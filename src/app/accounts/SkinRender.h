#pragma once

#include "app/system/Pixels.h"

#include <memory>

namespace SkinRender {

    enum class Framing { Head, Bust, Body };
    enum class Layer { None, Flat };

    struct Options {
        int size = 128;
        Framing framing = Framing::Bust;
        Layer layer = Layer::Flat;
        bool slim = false;
        float yaw = 30.0f;
        float pitch = -3.0f;
        float capeTilt = 6.0f;
        int samples = 4;
        bool shade = true;
    };

    bool looksSlim(const Pixels& skin);
    std::shared_ptr<const Pixels> render(const Pixels& skin, const Pixels* cape, const Options& options);
}
