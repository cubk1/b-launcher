#include "app/launch/AskAi.h"
#include "app/system/Config.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <algorithm>

namespace AskAi {

    namespace blb = b_launcher_base;

    namespace {
        const char* const KEY = "crashAi";
        constexpr size_t MARGIN = 200;

        constexpr Service SERVICES[] = {
                {"chatgpt", "ChatGPT", "openai", "https://chatgpt.com/", 30000},
                {"claude", "Claude", "claude", "https://claude.ai/new", 7000},
                {"grok", "Grok", "grok", "https://grok.com/", 7000},
                {"deepseek", "DeepSeek", "deepseek", "https://chat.deepseek.com/", 8000},
                {"kimi", "Kimi", "kimi", "https://www.kimi.com/", 14000},
        };

        const Service* find(const std::string& key) {
            for (const Service& service : SERVICES) {
                if (key == service.key) {
                    return &service;
                }
            }
            return nullptr;
        }

        std::string urlOf(const std::string& key, const std::string& prompt) {
            std::string text = blb::url_encode(prompt);
            if (key == "chatgpt") {
                return "https://chatgpt.com/?q=" + text;
            }
            if (key == "grok") {
                return "https://grok.com/?q=" + text;
            }
            if (key == "claude") {
                return "https://claude.ai/new?q=" + text;
            }
            if (key == "deepseek") {
                return "https://chat.deepseek.com/?q=" + text + "&autosend=1&thinking=1";
            }
            if (key == "kimi") {
                return "https://www.kimi.com/_prefill_chat?prefill_prompt=" + text + "&send_immediately=true";
            }
            return {};
        }

        bool open(const std::string& url) {
            if (Platform::openUrl(url)) {
                return true;
            }
            Notices::add(Notices::Kind::Failed, "无法打开浏览器", 4000);
            return false;
        }
    }

    std::span<const Service> services() {
        return SERVICES;
    }

    std::string nameOf(const std::string& key) {
        const Service* service = find(key);
        return service != nullptr ? service->name : std::string();
    }

    std::string chosen() {
        std::string key = blb::json_text(Config::data(), KEY);
        return find(key) != nullptr ? key : std::string();
    }

    void setChosen(const std::string& key) {
        if (key.empty()) {
            Config::data().erase(KEY);
        } else {
            Config::data()[KEY] = key;
        }
        Config::save();
    }

    size_t budget(const std::string& key) {
        const Service* service = find(key);
        return service != nullptr ? service->limit - MARGIN : 0;
    }

    bool ask(const std::string& key, const std::string& prompt) {
        const Service* service = find(key);
        if (service == nullptr) {
            return false;
        }
        std::string url = urlOf(key, prompt);
        if (url.size() <= service->limit) {
            return open(url);
        }
        if (!Platform::setClipboardText(prompt)) {
            Notices::add(Notices::Kind::Failed, "无法复制到剪贴板", 4000);
            return false;
        }
        if (!open(service->home)) {
            return false;
        }
        Notices::add(Notices::Kind::Info, "崩溃报告太长，已复制到剪贴板，请在打开的网页里粘贴后发送", 8000);
        return true;
    }
}
