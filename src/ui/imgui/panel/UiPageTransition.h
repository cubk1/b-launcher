#pragma once

class PageTransition {
public:
    int page;
    float mix;

    explicit PageTransition(int page = 0);
    bool go(int next);
    void begin();
    void end();

private:
    float savedAlpha;
    float savedOffsetX;
};
