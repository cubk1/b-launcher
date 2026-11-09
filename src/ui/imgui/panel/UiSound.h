#pragma once

#include <string>

namespace UiSound {

    enum Kind { PRESS, TICK, RELEASE, PAGE, PULSE, KIND_COUNT };

    extern bool enabled;

    void load(Kind kind, const unsigned char* wav, unsigned int size, float volume);
    bool theme(Kind kind, const unsigned char* wav, unsigned int size, float volume);
    void silence(Kind kind);
    void restore(Kind kind);
    void request(Kind kind);
    void requestDefault();
    void flush();
    Kind forLabel(const std::string& label);
}
