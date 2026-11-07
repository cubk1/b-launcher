#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_GLContextState;
union SDL_Event;

class SdlWindow {
public:
    SdlWindow() = default;
    ~SdlWindow();
    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    SDL_Window* window = nullptr;
    SDL_GLContextState* glContext = nullptr;
    int width = 0;
    int height = 0;
    int textAreaX = -1;
    int textAreaY = -1;
    int textAreaH = -1;
    std::function<bool(int x, int y)> isDragArea;
    bool resizable = false;
    bool framed = false;
    bool quitOnDestroy = true;
    std::function<void()> onClose;
    std::function<void()> onModalFrame;
    std::function<void(std::vector<std::filesystem::path> files, int x, int y)> onDrop;
    std::function<void(bool inside, int x, int y)> onDragOver;
    std::function<void(const std::string& text)> onDropText;
    std::function<void()> onFocusLost;
    void* imguiContext = nullptr;

    bool create(const std::string& title, int clientWidth, int clientHeight, SdlWindow* share = nullptr);
    void resize(int clientWidth, int clientHeight);
    void setMinimumSize(int clientWidth, int clientHeight);
    void destroy();
    void show(bool maximized = false);
    void hide();
    void raise();
    void minimize();
    void toggleMaximize();
    bool maximized() const;
    bool minimized() const;
    bool visible() const;
    bool focused() const;
    bool sizing() const;
    bool preparingResize() const;
    void close();
    void setDarkMode(bool dark);
    void setIcon(const std::vector<unsigned char>& rgba, int size);
    void setTitle(const std::string& title);
    bool pumpMessages();
    void swap();
    void setVsync(bool on);
    void setTyping(bool typing);
    void setTextArea(float x, float y, float h);
    void clearTextArea();
    void makeCurrent();
    float density() const;
    float displayScale() const;
    double refreshMs() const;
    void initImGui();
    void newFrame();
    void shutdownImGui();

private:
    void refreshSize();
    void handle(SDL_Event& event);

    bool closing = false;
    std::vector<std::filesystem::path> dropping;
    std::string droppingText;
};
