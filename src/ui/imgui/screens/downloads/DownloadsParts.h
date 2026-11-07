#pragma once

#include "app/downloads/Addons.h"
#include "app/downloads/Authors.h"
#include "app/downloads/Mcmod.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Choice;

namespace DownloadsParts {

    enum class View { Project, Author };

    struct Route {
        View view = View::Project;
        Addons::Source source = Addons::Source::Modrinth;
        Addons::Kind kind = Addons::Kind::Mod;
        Addons::Hit hit;
        Authors::Kind author = Authors::Kind::ModrinthUser;
        std::string authorId;
        std::string authorName;
        std::string authorAvatar;
        int tab = 0;
    };

    constexpr float GAP = 8.0f;
    constexpr float CARD_RADIUS = 12.0f;
    constexpr float RESULT_H = 56.0f;

    float mainX();
    float mainW();
    float tag(const std::string& text, float x, float centreY, float limit, uint32_t fill, uint32_t ink);
    void message(const std::string& text, uint32_t color, float top, float bottom, bool loading);
    void projectIcon(const std::string& url, float x, float y, float size, Addons::Kind kind);
    uint32_t loaderImage(const std::string& tag);
    void progressRing(const std::string& id, float cx, float cy, float fraction);
    void select(const std::string& id, const std::shared_ptr<Choice>& choice, float x, float y, float w, bool blocked);
    void instancePicker(float x, float y, float w, bool blocked);
    std::shared_ptr<Choice> versionChoice();
    std::shared_ptr<Choice> loaderChoice(Addons::Source source, Addons::Kind kind);
    std::string& versionFilter();
    std::string& loaderFilter();
    const std::string& target();
    std::string displayTitle(const std::string& title, Addons::Kind kind);
    std::string otherTitle(const std::string& title, Addons::Kind kind);
    void versionAction(const Addons::Version& version, const Route& route, float right, float rowY, float rowH,
                       bool primary, bool blocked);
    bool projectRow(const std::string& id, const Addons::Hit& hit, Addons::Kind kind, Addons::Source source, float x,
                    float y, float w, int index, int count, bool blocked);
    void push(const Route& route);
    void back();
    void openProject(Addons::Source source, Addons::Kind kind, const Addons::Hit& hit);
    void openAuthor(Authors::Kind kind, const std::string& id, const std::string& name, const std::string& avatar,
                    Addons::Source source);
    void openMcmod(const Mcmod::Ref& ref);
    void follow(const std::string& url);

    void webImage(const std::string& url, float x, float y, float w, float h, float radius, bool cover);
    std::string relativeTime(const std::string& iso);
    std::string shortDate(const std::string& iso);
    std::string gameRange(const std::vector<std::string>& versions, size_t limit);
    std::string sideLabel(const std::string& side);
    const char* versionTypeLabel(const std::string& type);
    void typeTag(const std::string& type, float x, float centreY, float limit);
    float sectionTitle(const std::string& text, float x, float y, float w);
    constexpr int MARK_MODRINTH = 1;
    constexpr int MARK_CURSEFORGE = 2;
    constexpr int MARK_MCMOD = 3;

    struct Segment {
        int id = 0;
        std::string label;
        int glyph = 0;
        size_t count = 0;
        int mark = 0;
    };

    constexpr float SEGMENT_H = 28.0f;

    float segmentsWidth(const std::vector<Segment>& items);
    int segments(const std::string& owner, int current, const std::vector<Segment>& items, float x, float y,
                 bool covered, float maxWidth = 0.0f);
    std::vector<std::string> gameRanges(const std::vector<std::string>& versions);
}
