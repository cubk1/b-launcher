#pragma once

namespace ThemeManager {

    void draw(float left, float width, bool covered);
    int settingCount();
    void settingRows(float left, float width, float y, bool blocked);
    int displayCount();
    void displayRows(float left, float width, float y, bool blocked);
}
