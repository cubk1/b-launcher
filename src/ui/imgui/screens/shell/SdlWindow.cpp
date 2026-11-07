#include "ui/imgui/screens/shell/SdlWindow.h"

#include <core/base/strings.hpp>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace {
    std::vector<SdlWindow*> windows;
    bool quitting = false;
    void (*backendIme)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*) = nullptr;

    SdlWindow* windowOf(SDL_WindowID id) {
        for (SdlWindow* window : windows) {
            if (window->window != nullptr && SDL_GetWindowID(window->window) == id) {
                return window;
            }
        }
        return nullptr;
    }

    void scaledIme(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
        SdlWindow* window = windowOf((SDL_WindowID) (intptr_t) viewport->PlatformHandle);
        float density = window != nullptr ? window->density() : 1.0f;
        ImGuiPlatformImeData scaled = *data;
        scaled.InputPos = ImVec2(data->InputPos.x / density, data->InputPos.y / density);
        scaled.InputLineHeight = data->InputLineHeight / density;
        backendIme(context, viewport, &scaled);
    }

    SDL_HitTestResult hitTest(SDL_Window*, const SDL_Point* area, void* data) {
        SdlWindow* self = (SdlWindow*) data;
        float density = self->density();
        int x = (int) (area->x * density);
        int y = (int) (area->y * density);
        if (!self->framed && self->resizable && !self->maximized()) {
            const int edge = 6;
            bool left = x < edge;
            bool right = x >= self->width - edge;
            bool top = y < edge;
            bool bottom = y >= self->height - edge;
            if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
            if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
            if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            if (left) return SDL_HITTEST_RESIZE_LEFT;
            if (right) return SDL_HITTEST_RESIZE_RIGHT;
            if (top) return SDL_HITTEST_RESIZE_TOP;
            if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        }
        return self->isDragArea && self->isDragArea(x, y) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
    }

    void scale(SDL_Event& event, float density) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION:
                event.motion.x *= density;
                event.motion.y *= density;
                event.motion.xrel *= density;
                event.motion.yrel *= density;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event.button.x *= density;
                event.button.y *= density;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event.wheel.mouse_x *= density;
                event.wheel.mouse_y *= density;
                break;
            default: break;
        }
    }
}

SdlWindow::~SdlWindow() {
    destroy();
}

bool SdlWindow::create(const std::string& title, int clientWidth, int clientHeight, SdlWindow* share) {
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    if (share != nullptr) {
        share->makeCurrent();
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, share != nullptr ? 1 : 0);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (!framed) {
        flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT;
    }
    window = SDL_CreateWindow(title.c_str(), clientWidth, clientHeight, flags);
    if (window == nullptr) {
        return false;
    }
    float ratio = density();
    if (ratio > 1.0f) {
        SDL_SetWindowSize(window, std::max(1, (int) (clientWidth / ratio + 0.5f)),
                          std::max(1, (int) (clientHeight / ratio + 0.5f)));
    }
    glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
        return false;
    }
    SDL_SetWindowHitTest(window, hitTest, this);
    windows.push_back(this);
    refreshSize();
    return true;
}

void SdlWindow::refreshSize() {
    if (window != nullptr) {
        SDL_GetWindowSizeInPixels(window, &width, &height);
    }
}

void SdlWindow::resize(int clientWidth, int clientHeight) {
    if (window == nullptr) {
        return;
    }
    float ratio = density();
    SDL_SetWindowSize(window, std::max(1, (int) (clientWidth / ratio + 0.5f)),
                      std::max(1, (int) (clientHeight / ratio + 0.5f)));
    refreshSize();
}

void SdlWindow::setMinimumSize(int clientWidth, int clientHeight) {
    if (window == nullptr) {
        return;
    }
    float ratio = density();
    SDL_SetWindowMinimumSize(window, (int) (clientWidth / ratio + 0.5f), (int) (clientHeight / ratio + 0.5f));
}

void SdlWindow::destroy() {
    if (window == nullptr) {
        return;
    }
    std::erase(windows, this);
    if (glContext != nullptr) {
        SDL_GL_DestroyContext(glContext);
        glContext = nullptr;
    }
    SDL_DestroyWindow(window);
    window = nullptr;
    if (quitOnDestroy) {
        quitting = true;
    }
}

void SdlWindow::show(bool maximized) {
    if (window == nullptr) {
        return;
    }
    SDL_ShowWindow(window);
    if (maximized) {
        SDL_MaximizeWindow(window);
    }
    SDL_RaiseWindow(window);
}

void SdlWindow::hide() {
    if (window != nullptr) {
        SDL_HideWindow(window);
    }
}

void SdlWindow::raise() {
    if (window == nullptr) {
        return;
    }
    if (minimized()) {
        SDL_RestoreWindow(window);
    }
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
}

void SdlWindow::minimize() {
    if (window != nullptr) {
        SDL_MinimizeWindow(window);
    }
}

void SdlWindow::toggleMaximize() {
    if (window == nullptr) {
        return;
    }
    if (maximized()) {
        SDL_RestoreWindow(window);
    } else {
        SDL_MaximizeWindow(window);
    }
}

bool SdlWindow::maximized() const {
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool SdlWindow::minimized() const {
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
}

bool SdlWindow::visible() const {
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0;
}

bool SdlWindow::focused() const {
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool SdlWindow::sizing() const {
    return false;
}

bool SdlWindow::preparingResize() const {
    return false;
}

void SdlWindow::close() {
    closing = true;
}

void SdlWindow::setDarkMode(bool) {}

void SdlWindow::setIcon(const std::vector<unsigned char>& rgba, int size) {
    if (window == nullptr || size <= 0 || rgba.size() < (size_t) size * size * 4) {
        return;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(size, size, SDL_PIXELFORMAT_RGBA32, (void*) rgba.data(), size * 4);
    if (surface != nullptr) {
        SDL_SetWindowIcon(window, surface);
        SDL_DestroySurface(surface);
    }
}

void SdlWindow::setTitle(const std::string& title) {
    if (window != nullptr) {
        SDL_SetWindowTitle(window, title.c_str());
    }
}

void SdlWindow::handle(SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: closing = true; return;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: refreshSize(); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (onFocusLost) {
                onFocusLost();
            }
            break;
        case SDL_EVENT_DROP_BEGIN:
            dropping.clear();
            droppingText.clear();
            if (onDragOver) {
                onDragOver(true, -1, -1);
            }
            return;
        case SDL_EVENT_DROP_POSITION:
            if (onDragOver) {
                float ratio = density();
                onDragOver(true, (int) (event.drop.x * ratio), (int) (event.drop.y * ratio));
            }
            return;
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data != nullptr) {
                dropping.push_back(b_launcher_base::path_from_utf8(event.drop.data));
            }
            return;
        case SDL_EVENT_DROP_TEXT:
            if (event.drop.data != nullptr && droppingText.size() < 4096) {
                droppingText += event.drop.data;
            }
            return;
        case SDL_EVENT_DROP_COMPLETE:
            if (onDragOver) {
                onDragOver(false, -1, -1);
            }
            if (onDrop && !dropping.empty()) {
                float ratio = density();
                onDrop(std::move(dropping), (int) (event.drop.x * ratio), (int) (event.drop.y * ratio));
            } else if (onDropText && !droppingText.empty()) {
                onDropText(droppingText);
            }
            dropping.clear();
            droppingText.clear();
            return;
        default: break;
    }
    if (imguiContext == nullptr) {
        return;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext((ImGuiContext*) imguiContext);
    scale(event, density());
    ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui::SetCurrentContext(previous);
}

bool SdlWindow::pumpMessages() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            for (SdlWindow* window : windows) {
                if (window->quitOnDestroy) {
                    window->closing = true;
                }
            }
            continue;
        }
        SDL_Window* target = SDL_GetWindowFromEvent(&event);
        for (SdlWindow* window : windows) {
            if (window->window == target) {
                window->handle(event);
                break;
            }
        }
    }
    std::vector<SdlWindow*> current = windows;
    for (SdlWindow* window : current) {
        if (window->closing && std::find(windows.begin(), windows.end(), window) != windows.end()) {
            window->closing = false;
            if (window->onClose) {
                window->onClose();
            } else {
                window->destroy();
            }
        }
    }
    return !quitting;
}

void SdlWindow::swap() {
    if (window != nullptr) {
        SDL_GL_SwapWindow(window);
    }
}

void SdlWindow::setVsync(bool on) {
    SDL_GL_SetSwapInterval(on ? 1 : 0);
}

void SdlWindow::setTyping(bool typing) {
    if (window == nullptr || typing == SDL_TextInputActive(window)) {
        return;
    }
    if (typing) {
        SDL_StartTextInput(window);
    } else {
        SDL_StopTextInput(window);
    }
}

void SdlWindow::setTextArea(float x, float y, float h) {
    if (window == nullptr) {
        return;
    }
    float scale = density();
    int areaX = (int) (x / scale);
    int areaY = (int) (y / scale);
    int areaH = std::max(1, (int) (h / scale + 0.5f));
    if (areaX == textAreaX && areaY == textAreaY && areaH == textAreaH) {
        return;
    }
    textAreaX = areaX;
    textAreaY = areaY;
    textAreaH = areaH;
    SDL_Rect area{areaX, areaY, 1, areaH};
    SDL_SetTextInputArea(window, &area, 0);
}

void SdlWindow::clearTextArea() {
    textAreaX = -1;
    textAreaY = -1;
    textAreaH = -1;
}

void SdlWindow::makeCurrent() {
    if (window != nullptr && glContext != nullptr) {
        SDL_GL_MakeCurrent(window, glContext);
    }
}

float SdlWindow::density() const {
    float ratio = window != nullptr ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return ratio > 0.0f ? ratio : 1.0f;
}

float SdlWindow::displayScale() const {
    float scale = window != nullptr ? SDL_GetWindowDisplayScale(window) : 1.0f;
    return scale > 0.0f ? scale : 1.0f;
}

double SdlWindow::refreshMs() const {
    const SDL_DisplayMode* mode =
            window != nullptr ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)) : nullptr;
    return mode != nullptr && mode->refresh_rate > 1.0f ? 1000.0 / mode->refresh_rate : 1000.0 / 60.0;
}

void SdlWindow::initImGui() {
    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(nullptr);
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    if (platform.Platform_SetImeDataFn != scaledIme) {
        backendIme = platform.Platform_SetImeDataFn;
        platform.Platform_SetImeDataFn = scaledIme;
    }
}

void SdlWindow::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float) width, (float) height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

void SdlWindow::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}
