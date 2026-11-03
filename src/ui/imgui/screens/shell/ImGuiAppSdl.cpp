#include "ui/imgui/screens/shell/ImGuiApp.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "ui/imgui/screens/shell/SdlWindow.h"
#include "app/system/Config.h"
#include "ui/imgui/screens/shell/Drop.h"
#include "app/instances/Drops.h"
#include "app/launch/GameLog.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "ui/imgui/screens/launch/LogWindow.h"
#include "app/launch/Game.h"
#include "app/launch/ShortcutLaunch.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/system/Notices.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <core/base/json.hpp>

#include <SDL3/SDL.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

    constexpr int ICON_SIZE = 64;
    constexpr double BACKGROUND_FRAME_MS = 1000.0 / 30.0;
    constexpr double IDLE_FRAME_MS = 100.0;
    constexpr double ROOM_CLOSE_MS = 10000.0;
    constexpr double INPUT_GRACE_MS = 600.0;

    double sinceMs(std::chrono::steady_clock::time_point started) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void idleUntil(std::chrono::steady_clock::time_point started, double frameMs) {
        double wait = frameMs - sinceMs(started);
        if (wait >= 1.0) {
            SDL_WaitEventTimeout(nullptr, (Sint32) wait);
        }
    }

    void sleepUntil(std::chrono::steady_clock::time_point started, double frameMs) {
        double wait = frameMs - sinceMs(started);
        if (wait >= 0.5) {
            SDL_DelayPrecise((Uint64) (wait * 1000000.0));
        }
    }

    void holdUntil(std::chrono::steady_clock::time_point started, double frameMs) {
        if (sinceMs(started) < frameMs * 0.75) {
            sleepUntil(started, frameMs - 0.3);
        }
    }

    void releaseMemory() {
#if defined(__GLIBC__)
        malloc_trim(0);
#endif
    }

    bool interacted() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
            io.InputQueueCharacters.Size > 0) {
            return true;
        }
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
            if (ImGui::IsKeyDown((ImGuiKey) key)) {
                return true;
            }
        }
        return false;
    }

    int pixels(float layout, float scale) {
        return (int) (layout * scale + 0.5f);
    }

    float storedSize(const char* key, float fallback) {
        const nlohmann::json& doc = Config::data();
        if (auto it = doc.find(key); it != doc.end() && it->is_number()) {
            float size = it->get<float>();
            if (std::isfinite(size)) return std::max(fallback, size);
        }
        return fallback;
    }

    bool storedMaximized() {
        return b_launcher_base::json_bool(Config::data(), "windowMaximized");
    }

    bool storeWindow(const SdlWindow& window, float scale) {
        bool maximized = window.maximized();
        nlohmann::json& doc = Config::data();
        auto matches = [&](const char* key, const auto& value) {
            auto found = doc.find(key);
            return found != doc.end() && *found == value;
        };
        if (maximized || window.minimized()) {
            if (matches("windowMaximized", maximized) || !maximized) {
                return false;
            }
            doc["windowMaximized"] = true;
            return true;
        }
        float w = std::round(std::max(MainWindow::MIN_WIDTH, window.width / scale));
        float h = std::round(std::max(MainWindow::MIN_HEIGHT, window.height / scale));
        if (matches("windowWidth", w) && matches("windowHeight", h) && matches("windowMaximized", false)) {
            return false;
        }
        doc["windowWidth"] = w;
        doc["windowHeight"] = h;
        doc["windowMaximized"] = false;
        return true;
    }

    float displayScale() {
        float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        return scale > 0.0f ? scale : 1.0f;
    }

    SDL_Rect usableArea() {
        SDL_Rect area{0, 0, 1280, 800};
        SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area);
        return area;
    }
}

namespace ImGuiApp {

    int run(const std::string& pendingLaunch, bool headless) {
        bool launchSent = false;
        bool launchFailed = false;
        SDL_SetHint(SDL_HINT_APP_ID, "b-launcher");
        SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            return 1;
        }
        float dpiScale = displayScale();
        float scale = dpiScale * Appearance::zoom() / 100.0f;
        SdlWindow window;
        window.resizable = true;
        window.framed = Appearance::systemFrameRunning();
        window.isDragArea = [&scale](int x, int y) { return MainWindow::isDragArea(x / scale, y / scale); };
        bool remember = Appearance::rememberWindow();
        SDL_Rect area = usableArea();
        float startW = std::min(remember ? storedSize("windowWidth", MainWindow::MIN_WIDTH) : MainWindow::MIN_WIDTH,
                                std::max(MainWindow::MIN_WIDTH, area.w * dpiScale / scale));
        float startH = std::min(remember ? storedSize("windowHeight", MainWindow::MIN_HEIGHT) : MainWindow::MIN_HEIGHT,
                                std::max(MainWindow::MIN_HEIGHT, area.h * dpiScale / scale));
        bool startMaximized = remember && storedMaximized();
        std::string shownTitle = Appearance::windowTitle();
        LogWindow::setTitle(Platform::widen(shownTitle));
        if (!window.create(shownTitle, pixels(startW, scale), pixels(startH, scale))) {
            SDL_Quit();
            return 1;
        }
        if (window.displayScale() != dpiScale) {
            dpiScale = window.displayScale();
            scale = dpiScale * Appearance::zoom() / 100.0f;
            window.resize(pixels(startW, scale), pixels(startH, scale));
        }
        window.makeCurrent();
        window.setVsync(true);
        window.setMinimumSize(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
        Platform::window = window.window;
        Platform::claimLaunches();
        window.onDrop = [](std::vector<std::filesystem::path> files, int x, int y) {
            Drops::dropped(std::move(files), x, y);
        };

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        window.initImGui();
        UiGL::load();
        UiFont::init();
        Appearance::load();
        MainWindow::init();
        Services::start();
        if (!pendingLaunch.empty()) {
            ShortcutLaunch::request(pendingLaunch, headless);
        }
        window.imguiContext = ImGui::GetCurrentContext();
        LogWindow::init(&window);
        Drop::attach(window.window);
        bool shown = false;
        uint32_t iconAccent = 0;
        auto lastInput = std::chrono::steady_clock::now();
        auto lastCompact = std::chrono::steady_clock::now();
        bool released = false;
        double frameMs = window.refreshMs();
        int framesSinceRefresh = 0;

        auto frame = [&]() {
            float next = window.displayScale() * Appearance::zoom() / 100.0f;
            if (next != scale) {
                float layoutW = window.width / scale;
                float layoutH = window.height / scale;
                scale = next;
                window.setMinimumSize(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
                if (!window.maximized()) {
                    window.resize(pixels(std::max(MainWindow::MIN_WIDTH, layoutW), scale),
                                  pixels(std::max(MainWindow::MIN_HEIGHT, layoutH), scale));
                }
            }
            MainWindow::setSize(window.width / scale, window.height / scale, window.maximized());

            window.makeCurrent();
            window.newFrame();
            ImGui::NewFrame();
            UiState::beginFrame();
            if (interacted()) {
                lastInput = std::chrono::steady_clock::now();
            }
            Services::poll();
            Background::poll();
            Logo::poll();
            Homepage::poll();
            if (std::optional<ShortcutLaunch::Outcome> outcome = ShortcutLaunch::take()) {
                launchSent = outcome->launched;
                launchFailed = !outcome->launched;
                if (!outcome->launched && outcome->quiet && !headless) {
                    window.raise();
                }
            }
            UiRender::uiScale = scale / (float) UiRender::guiScale();
            UiRender::begin(ImGui::GetForegroundDrawList());
            NewUi::render(MainWindow::draw);
            UiRender::end();

            ImGui::Render();
            UiGL::Viewport(0, 0, window.width, window.height);
            UiGL::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            UiGL::Clear(UGL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            window.swap();
            if (!shown) {
                shown = true;
                if (!headless) {
                    window.show(startMaximized);
                }
            }
            LogWindow::frame(scale);

            uint32_t accent = UiTheme::accent() | 0xFF000000u;
            if (accent != iconAccent) {
                iconAccent = accent;
                window.setIcon(Logo::pixels(ICON_SIZE), ICON_SIZE);
                LogWindow::setIcons(nullptr, nullptr);
            }
            if (std::string title = Appearance::windowTitle(); title != shownTitle) {
                shownTitle = title;
                window.setTitle(title);
                LogWindow::setTitle(Platform::widen(title));
            }

            if (MainWindow::minimizeRequested) {
                MainWindow::minimizeRequested = false;
                window.minimize();
            }
            if (MainWindow::maximizeRequested) {
                MainWindow::maximizeRequested = false;
                window.toggleMaximize();
            }
            if (MainWindow::closeRequested) {
                MainWindow::closeRequested = false;
                window.close();
            }
            if (Appearance::rememberWindow() && window.visible() && storeWindow(window, scale)) {
                Config::save();
            }
        };
        bool closingRoom = false;
        bool exiting = false;
        auto closingSince = std::chrono::steady_clock::now();
        auto finishClose = [&]() {
            closingRoom = false;
            if (Appearance::rememberWindow() && window.visible()) {
                storeWindow(window, scale);
            }
            window.hide();
            exiting = true;
        };
        window.onClose = [&]() {
            if (!closingRoom && Multiplayer::active()) {
                closingRoom = true;
                closingSince = std::chrono::steady_clock::now();
                Multiplayer::leave();
                Notices::add(Notices::Kind::Info, "正在关闭房间，关闭后启动器会退出", 4000);
                return;
            }
            finishClose();
        };

        while (window.pumpMessages() && !exiting) {
            if (closingRoom &&
                (Multiplayer::state().role == Multiplayer::Role::None || sinceMs(closingSince) > ROOM_CLOSE_MS)) {
                finishClose();
                continue;
            }
            GameLog::poll();
            if (window.minimized() || (shown && !headless && !window.visible())) {
                if (!released) {
                    released = true;
                    releaseMemory();
                }
                Services::poll();
                LogWindow::frame(scale);
                SDL_Delay(LogWindow::isOpen() ? 15 : 10);
                continue;
            }
            auto started = std::chrono::steady_clock::now();
            frame();
            if (headless) {
                if (launchSent && Game::running()) {
                    headless = false;
                    window.close();
                } else if (launchFailed || (launchSent && !Game::active())) {
                    headless = false;
                    Game::setQuiet(false);
                    window.show(startMaximized);
                }
            }
            if (sinceMs(lastInput) > 20000.0 && sinceMs(lastCompact) > 180000.0) {
                lastCompact = std::chrono::steady_clock::now();
                UiFont::compact();
            }
            bool foreground = window.focused();
            if (foreground) {
                released = false;
            } else if (!released && sinceMs(lastInput) > 30000.0) {
                released = true;
                releaseMemory();
            }
            if (++framesSinceRefresh >= 120) {
                framesSinceRefresh = 0;
                frameMs = window.refreshMs();
            }
            bool lively = UiState::animating() || Notifications::active() || sinceMs(lastInput) < INPUT_GRACE_MS ||
                    Game::preparing();
            if (lively && foreground) {
                holdUntil(started, frameMs);
            } else {
                sleepUntil(started, BACKGROUND_FRAME_MS);
                if (!lively) {
                    idleUntil(started, IDLE_FRAME_MS);
                }
            }
        }

        MainWindow::save();
        Config::save();

        window.makeCurrent();
        LogWindow::shutdown();
        UiImages::shutdown();
        Homepage::shutdown();
        Background::shutdown();
        Logo::shutdown();
        Services::stop();
        window.shutdownImGui();
        ImGui::DestroyContext();
        window.destroy();
        SDL_Quit();
        return 0;
    }
}
