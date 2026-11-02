#pragma once

#include <string>

namespace ScreenshotViewer {

    void open(const std::string& key, int index);
    void close();
    bool isOpen();
}
