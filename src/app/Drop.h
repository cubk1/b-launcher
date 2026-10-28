#pragma once

namespace Drop {

    void attach(void* window);
    void detach();
    void draw(float width, float height);
    void poll();
    void shutdown();
}
