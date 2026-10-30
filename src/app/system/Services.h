#pragma once

namespace Services {

    void start();
    void poll();
    void stop();
    // A game, an install, a download or an export is running, which a restart would cut off.
    bool busy();
}
