#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace XamlView {

    struct Action {
        std::string type;
        std::string data;
    };

    class Document {
    public:
        Document();
        ~Document();
        Document(const Document&) = delete;
        Document& operator=(const Document&) = delete;

        bool load(const std::string& source, const std::string& id);
        const std::string& error() const;
        bool empty() const;
        float measure(float width);
        void draw(float x, float y, float clipTop, float clipBottom, bool blocked);

        std::function<void(const std::vector<Action>& actions)> onActions;

    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
