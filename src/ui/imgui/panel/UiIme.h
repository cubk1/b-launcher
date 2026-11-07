#pragma once

#include <string>

namespace UiIme {

    void set(const std::string& text, int caret);
    void clear();
    void commit(const std::string& text);
    std::string takeCommitted();
    bool active();
    const std::string& text();
    int caret();
    bool wanted();
}
