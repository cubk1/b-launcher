#pragma once

#include "ui/imgui/screens/shell/Markdown.h"

#include <functional>
#include <memory>
#include <string>

class MarkdownView {
public:
    MarkdownView();
    ~MarkdownView();
    MarkdownView(const MarkdownView&) = delete;
    MarkdownView& operator=(const MarkdownView&) = delete;

    void load(const std::string& text, Markdown::Format format, const std::string& id);
    bool empty() const;
    float measure(float width);
    void draw(float x, float y, float clipTop, float clipBottom, bool blocked);

    std::function<void(const std::string& url)> onLink;
    std::function<void(float y)> onAnchor;
    float textSize = 13.5f;

private:
    struct State;
    std::unique_ptr<State> state;
};
