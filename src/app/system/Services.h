#pragma once

namespace Services {

    void start();
    void poll();
    void stop();
    // A game, an install or a download is running, which a restart would cut off.
    bool busy();
}
