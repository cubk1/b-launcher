#include "NewUi.h"
#include "UiSound.h"
#include "UiCommon.h"
#include "UiCursor.h"
#include "UiFont.h"
#include "UiInput.h"
#include "UiRender.h"
#include "UiState.h"
#include "UiTheme.h"
#include "UiWidgets.h"
#include "UiKeys.h"
#include "../menu/ContextMenu.h"
#include "../menu/MenuItems.h"
#include "../overlay/UiOverlays.h"
#include "../widget/BindWidget.h"
#include "../widget/ColorPickerWidget.h"
#include "../widget/DotsWidget.h"
#include "../widget/SelectWidget.h"
#include "../widget/SliderWidget.h"
#include "../widget/SwatchWidget.h"
#include "../widget/TextFieldWidget.h"
#include "../widget/ToggleWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace NewUi {

    const std::string INPUT_COLOR = "colorField";
    const std::string INPUT_TEXT = "textProp";
    const std::string INPUT_SLIDER = "sliderEdit";

    float mouseX;
    float mouseY;
    bool clickConsumed;
    static bool dismissClick = false;
    bool pendingClick;
    bool mouseDown;
    long long pressSerial;
    long long pickerOpenedPress;
    long long popupOpenedPress;
    bool rightClick;
    bool middleClick;
    std::string draggingSlider;
    std::string sliderEditing;
    bool focusFieldLive = false;
    float focusFieldX;
    float focusFieldY;
    float focusFieldW;
    float focusFieldH;
    std::set<std::string> subInlinePicker;
    long long subOpenedFrame;
    bool drawingOverlay;
    bool drawingDialog;
    bool overlayActive;
    float overlayX;
    float overlayY;
    float overlayW;
    float overlayH;
    bool nextOverlayActive;
    float nextOverlayX;
    float nextOverlayY;
    float nextOverlayW;
    float nextOverlayH;
    std::string tooltipText;
    float tooltipX;
    float tooltipY;

    bool popupOpen;
    bool popupMulti;
    std::string popupOwner;
    std::string popupSource;
    std::shared_ptr<Choice> popupChoice;
    std::shared_ptr<NumberValue> sliderEditValue;
    std::vector<int> windowOrder;
    int drawingWindow = -1;
    float popupAnchorX;
    float popupAnchorY;
    float popupWidth;
    float popupScroll;
    float popupScrollSmooth;
    int popupDragFrom = -1;
    float popupDragGrab;
    std::string subOwner;
    std::string subParent;
    std::unordered_map<std::string, SettingsPagePtr> subPages;
    float subParentX;
    float subParentY;
    std::shared_ptr<NumberValue> hoveredSlider;
    std::shared_ptr<ColorValue> hoveredColor;
    std::string hoveredColorChannel;
    std::shared_ptr<KeyValue> bindingKey;
    std::shared_ptr<ShortcutValue> recordingShortcut;
    std::string textEditing;
    std::shared_ptr<TextValue> textEditValue;
    std::string scrollDragOwner;
    float scrollDragOffset;
    std::string colorEditing;
    std::shared_ptr<ColorValue> colorEditValue;
    int colorEditingCell = -1;
    std::string colorCopiedOwner;
    long long colorCopiedAt;
    uint32_t colorClipboard;
    bool colorClipboardSet;
    std::string swatchMenuOwner;
    std::shared_ptr<ColorValue> swatchMenuColor;
    float swatchMenuX;
    float swatchMenuY;
    float pickerRectX;
    float pickerRectY;
    float pickerRectW;
    float pickerRectH;
    float swatchRectX;
    float swatchRectY;
    float swatchRectW;
    float swatchRectH;
    float popupRectX;
    float popupRectY;
    float popupRectW;
    float popupRectH;
    float subPanelRight;
    float subPanelTop;
    float subPanelX;
    float subPanelY;
    float subPanelW;
    float subPanelH;
    bool subPanelDismiss;
    bool aboveActive;
    float aboveX;
    float aboveY;
    float aboveW;
    float aboveH;
    bool nextAboveActive;
    float nextAboveX;
    float nextAboveY;
    float nextAboveW;
    float nextAboveH;
    float subAnchorX;
    float subAnchorY;
    std::string subChildOwner;
    bool subPanelDrawing;
    std::string pickerOwner;
    std::shared_ptr<ColorValue> pickerColor;
    float pickerX;
    float pickerY;
    int pickerMode;
    std::string pickerModeOwner;
    static ContextMenu swatchContextMenuInstance("swatchMenu");
    static ContextMenu pickerModeMenuInstance("pickerMode");
    ContextMenu& swatchContextMenu = swatchContextMenuInstance;
    ContextMenu& pickerModeMenu = pickerModeMenuInstance;
    static ContextMenu contextMenuInstance("contextMenu");
    ContextMenu& contextMenu = contextMenuInstance;
    std::string menuOwner;
    float menuX;
    float menuY;
    float menuMinWidth = 160.0f;
    std::function<MenuItems()> menuItems;
    long long popupOpenedFrame;
    long long pickerOpenedFrame;
    long long frameCounter;
    float caretTimer;
    std::unordered_map<std::string, float> fieldOffset;
    std::string fieldMenuTarget;
    float fieldMenuX;
    float fieldMenuY;
    std::string fieldDragId;
    std::string fieldClickId;
    long long fieldClickAt;
    int fieldClicks;
    std::string docHoverTarget;
    std::string docHoverText;
    std::string docHoverNext;
    std::string docHoverNextText;
    float docHoverX;
    float docHoverY;
    float docHoverTime;

    Hooks hooks;
    static float popupGripLit = 0.0f;
    std::vector<Window> windows;

    float limitX() {
        return UiRender::screenWidth() / UiRender::uiScale;
    }

    float limitY() {
        return UiRender::screenHeight() / UiRender::uiScale;
    }

    void pollInput() {
        ImGuiIO& io = ImGui::GetIO();
        for (int button = 0; button < ImGuiMouseButton_COUNT; button++) {
            if (ImGui::IsMouseClicked(button)) {
                onMouseClick(button, 1);
            }
            if (ImGui::IsMouseReleased(button)) {
                onMouseClick(button, 0);
            }
        }
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
            ImGuiKey imKey = (ImGuiKey) key;
            if ((key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) ||
                (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadR3) || key == ImGuiKey_ReservedForModCtrl ||
                key == ImGuiKey_ReservedForModShift || key == ImGuiKey_ReservedForModAlt ||
                key == ImGuiKey_ReservedForModSuper) {
                continue;
            }
            if (ImGui::IsKeyPressed(imKey, false)) {
                onKeyPress(key, 1);
            } else if (ImGui::IsKeyPressed(imKey, true)) {
                onKeyRepeat(key);
            }
        }
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c != 0) {
                onCharTyped((char32_t) c);
            }
        }
        if (io.MouseWheel != 0.0f) {
            onMouseScroll(io.MouseWheel);
        }
    }

    bool onMouseScroll(double amount) {
        if (UiOverlays::scroll(amount)) {
            return true;
        }
        if (hoveredSlider != nullptr) {
            hoveredSlider->nudge(amount);
            return true;
        }
        return false;
    }

    void beginFrame() {
        ImGuiIO& io = ImGui::GetIO();
        UiFont::ensureBaseFonts();
        mouseX = io.MousePos.x / UiRender::unit() / UiRender::uiScale;
        mouseY = io.MousePos.y / UiRender::unit() / UiRender::uiScale;
        pollInput();
        frameCounter++;
        caretTimer += UiRender::deltaTime();
        docHoverNext.clear();
        docHoverNextText.clear();
        hoveredSlider = nullptr;
        hoveredColor = nullptr;
        hoveredColorChannel.clear();
        mouseX = io.MousePos.x / UiRender::unit() / UiRender::uiScale;
        mouseY = io.MousePos.y / UiRender::unit() / UiRender::uiScale;
        clickConsumed = false;
        bool focusLive = focusFieldLive;
        focusFieldLive = false;
        if ((pendingClick || rightClick) && !fieldMenuTarget.empty() &&
            !UiOverlays::fieldMenu->contains(mouseX, mouseY)) {
            UiOverlays::close(UiOverlays::fieldMenu);
        }
        if ((pendingClick || rightClick) && !UiInput::focused.empty() && fieldMenuTarget.empty() &&
            !(focusLive && rawHit(focusFieldX, focusFieldY, focusFieldW, focusFieldH))) {
            releaseInput(UiInput::focused);
        }
        overlayActive = nextOverlayActive;
        overlayX = nextOverlayX;
        overlayY = nextOverlayY;
        overlayW = nextOverlayW;
        overlayH = nextOverlayH;
        nextOverlayActive = false;
        aboveActive = nextAboveActive;
        aboveX = nextAboveX;
        aboveY = nextAboveY;
        aboveW = nextAboveW;
        aboveH = nextAboveH;
        nextAboveActive = false;
        UiOverlays::beginFrame();
        bool outside = overlaysEnabled && !UiOverlays::isEmpty() && !UiOverlays::blocks(mouseX, mouseY);
        dismissClick = pendingClick && outside;
        if (dismissClick) {
            clickConsumed = true;
        } else if (rightClick && outside) {
            UiOverlays::closeAll();
        }
        if (pendingClick && !overlayBlocks(mouseX, mouseY) && !UiOverlays::blocks(mouseX, mouseY)) {
            int clicked = windowAt(mouseX, mouseY);
            if (clicked >= 0) {
                bringWindowToFront(clicked);
            }
        }
    }

    bool overlaysEnabled = true;

    FrameState saveFrameState() {
        FrameState s;
        s.overlayActive = overlayActive;
        s.overlayX = overlayX;
        s.overlayY = overlayY;
        s.overlayW = overlayW;
        s.overlayH = overlayH;
        s.nextOverlayActive = nextOverlayActive;
        s.nextOverlayX = nextOverlayX;
        s.nextOverlayY = nextOverlayY;
        s.nextOverlayW = nextOverlayW;
        s.nextOverlayH = nextOverlayH;
        s.aboveActive = aboveActive;
        s.aboveX = aboveX;
        s.aboveY = aboveY;
        s.aboveW = aboveW;
        s.aboveH = aboveH;
        s.nextAboveActive = nextAboveActive;
        s.nextAboveX = nextAboveX;
        s.nextAboveY = nextAboveY;
        s.nextAboveW = nextAboveW;
        s.nextAboveH = nextAboveH;
        s.focusFieldLive = focusFieldLive;
        s.focusFieldX = focusFieldX;
        s.focusFieldY = focusFieldY;
        s.focusFieldW = focusFieldW;
        s.focusFieldH = focusFieldH;
        s.mouseDown = mouseDown;
        s.draggingSlider = draggingSlider;
        s.scrollDragOwner = scrollDragOwner;
        s.tooltipText = tooltipText;
        s.tooltipX = tooltipX;
        s.tooltipY = tooltipY;
        s.tooltipWaiting = tooltipWaiting;
        s.tooltipWaited = tooltipWaited;
        return s;
    }

    void restoreFrameState(const FrameState& s) {
        overlayActive = s.overlayActive;
        overlayX = s.overlayX;
        overlayY = s.overlayY;
        overlayW = s.overlayW;
        overlayH = s.overlayH;
        nextOverlayActive = s.nextOverlayActive;
        nextOverlayX = s.nextOverlayX;
        nextOverlayY = s.nextOverlayY;
        nextOverlayW = s.nextOverlayW;
        nextOverlayH = s.nextOverlayH;
        aboveActive = s.aboveActive;
        aboveX = s.aboveX;
        aboveY = s.aboveY;
        aboveW = s.aboveW;
        aboveH = s.aboveH;
        nextAboveActive = s.nextAboveActive;
        nextAboveX = s.nextAboveX;
        nextAboveY = s.nextAboveY;
        nextAboveW = s.nextAboveW;
        nextAboveH = s.nextAboveH;
        focusFieldLive = s.focusFieldLive;
        focusFieldX = s.focusFieldX;
        focusFieldY = s.focusFieldY;
        focusFieldW = s.focusFieldW;
        focusFieldH = s.focusFieldH;
        mouseDown = s.mouseDown;
        draggingSlider = s.draggingSlider;
        scrollDragOwner = s.scrollDragOwner;
        tooltipText = s.tooltipText;
        tooltipX = s.tooltipX;
        tooltipY = s.tooltipY;
        tooltipWaiting = s.tooltipWaiting;
        tooltipWaited = s.tooltipWaited;
    }

    void endFrame() {
        drawingWindow = -1;
        drawingOverlay = true;
        if (overlaysEnabled) {
            UiOverlays::drawAll();
            docLayer();
        }
        tooltipLayer();
        drawingOverlay = false;
        if (overlaysEnabled && pendingClick && (dismissClick || !clickConsumed) &&
            !UiOverlays::dismiss(mouseX, mouseY)) {
            closeAllOverlays();
        }
        dismissClick = false;
        pendingClick = false;
        rightClick = false;
        middleClick = false;
        UiRender::flush();
        UiCursor::apply();
        UiSound::flush();
    }

    void render(const std::function<void()>& content) {
        beginFrame();
        if (content) {
            drawingWindow = -1;
            content();
        }
        std::vector<int> order = windowList();
        for (size_t i = 0; i < order.size(); i++) {
            int id = order[i];
            drawingWindow = id;
            for (size_t k = 0; k < windows.size(); k++) {
                if (windows[k].id == id && windowOpen(id) && windows[k].draw) {
                    windows[k].draw();
                }
            }
        }
        drawingWindow = -1;
        endFrame();
    }

    static const Window* findWindow(int id) {
        for (size_t i = 0; i < windows.size(); i++) {
            if (windows[i].id == id) {
                return &windows[i];
            }
        }
        return nullptr;
    }

    std::vector<int>& windowList() {
        for (size_t i = 0; i < windows.size(); i++) {
            if (std::find(windowOrder.begin(), windowOrder.end(), windows[i].id) == windowOrder.end()) {
                windowOrder.push_back(windows[i].id);
            }
        }
        return windowOrder;
    }

    bool windowOpen(int id) {
        const Window* window = findWindow(id);
        if (window == nullptr) {
            return false;
        }
        return window->open ? window->open() : true;
    }

    bool windowContains(int id, float x, float y) {
        const Window* window = findWindow(id);
        return window != nullptr && window->contains && window->contains(x, y);
    }

    int windowAt(float x, float y) {
        std::vector<int>& order = windowList();
        for (int i = (int) order.size() - 1; i >= 0; i--) {
            int id = order[i];
            if (windowOpen(id) && windowContains(id, x, y)) {
                return id;
            }
        }
        return -1;
    }

    void bringWindowToFront(int id) {
        std::vector<int>& order = windowList();
        order.erase(std::remove(order.begin(), order.end(), id), order.end());
        order.push_back(id);
    }

    void sendWindowBack(int id) {
        std::vector<int>& order = windowList();
        order.erase(std::remove(order.begin(), order.end(), id), order.end());
        order.insert(order.begin(), id);
    }

    bool hit(float x, float y, float w, float h) {
        if (!rawHit(x, y, w, h)) {
            return false;
        }
        if (!subOwner.empty() && !drawingOverlay && !drawingDialog) {
            return false;
        }
        if (drawingOverlay) {
            return UiOverlays::drawing == nullptr || !UiOverlays::blocks(mouseX, mouseY);
        }
        if (windowBlocks()) {
            return false;
        }
        return !overlayBlocks(mouseX, mouseY) && !UiOverlays::blocks(mouseX, mouseY);
    }

    bool windowBlocks() {
        if (drawingWindow < 0) {
            return false;
        }
        std::vector<int>& order = windowList();
        auto at = std::find(order.begin(), order.end(), drawingWindow);
        size_t start = at == order.end() ? 0 : (size_t) (at - order.begin()) + 1;
        for (size_t i = start; i < order.size(); i++) {
            int id = order[i];
            if (windowOpen(id) && windowContains(id, mouseX, mouseY)) {
                return true;
            }
        }
        return false;
    }

    bool rawHit(float x, float y, float w, float h) {
        return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    }

    bool overlayCoversMouse() {
        return aboveActive && mouseX >= aboveX && mouseX <= aboveX + aboveW && mouseY >= aboveY &&
                mouseY <= aboveY + aboveH;
    }

    void markAbove(float x, float y, float w, float h) {
        if (!nextAboveActive) {
            nextAboveActive = true;
            nextAboveX = x;
            nextAboveY = y;
            nextAboveW = w;
            nextAboveH = h;
            return;
        }
        float x0 = std::min(nextAboveX, x);
        float y0 = std::min(nextAboveY, y);
        float x1 = std::max(nextAboveX + nextAboveW, x + w);
        float y1 = std::max(nextAboveY + nextAboveH, y + h);
        nextAboveX = x0;
        nextAboveY = y0;
        nextAboveW = x1 - x0;
        nextAboveH = y1 - y0;
    }

    bool overlayBlocks(float x, float y) {
        return overlayActive && x >= overlayX && x <= overlayX + overlayW && y >= overlayY && y <= overlayY + overlayH;
    }

    void markOverlayUnion(float x, float y, float w, float h) {
        if (!nextOverlayActive) {
            markOverlay(x, y, w, h);
            return;
        }
        float x0 = std::min(nextOverlayX, x);
        float y0 = std::min(nextOverlayY, y);
        float x1 = std::max(nextOverlayX + nextOverlayW, x + w);
        float y1 = std::max(nextOverlayY + nextOverlayH, y + h);
        UiOverlays::mark(x, y, w, h);
        nextOverlayX = x0;
        nextOverlayY = y0;
        nextOverlayW = x1 - x0;
        nextOverlayH = y1 - y0;
    }

    void markOverlay(float x, float y, float w, float h) {
        UiOverlays::mark(x, y, w, h);
        nextOverlayActive = true;
        nextOverlayX = x;
        nextOverlayY = y;
        nextOverlayW = w;
        nextOverlayH = h;
    }

    bool click(float x, float y, float w, float h) {
        if (!subOwner.empty() && !drawingOverlay && !drawingDialog) {
            return false;
        }
        if (!pendingClick || clickConsumed) {
            return false;
        }
        if (hit(x, y, w, h)) {
            clickConsumed = true;
            UiSound::requestDefault();
            return true;
        }
        return false;
    }

    void closeAllOverlays() {
        if (hooks.onCloseAllOverlays) {
            hooks.onCloseAllOverlays();
        }
        UiOverlays::closeAll();
        bindingKey = nullptr;
        recordingShortcut = nullptr;
        cancelColorEdit();
        commitTextEdit();
        sliderEditing.clear();
        docHoverTarget.clear();
        docHoverNext.clear();
        docHoverTime = 0;
        UiInput::blur();
    }

    float fieldScroll(const std::string& id, float room, int family, float size, bool focused) {
        std::string value = UiInput::get(id);
        float total = UiFont::width(value, family, size);
        if (!focused || total <= room) {
            fieldOffset[id] = 0.0f;
            return 0.0f;
        }
        auto stored = fieldOffset.find(id);
        float offset = stored == fieldOffset.end() ? 0.0f : stored->second;
        float caretX = UiFont::width(Ui::substring(value, 0, UiInput::caret(id)), family, size);
        if (caretX - offset > room - 6) {
            offset = caretX - room + 6;
        }
        if (caretX - offset < 6) {
            offset = caretX - 6;
        }
        offset = UiRender::clamp(offset, 0, std::max(0.0f, total - room));
        fieldOffset[id] = offset;
        return offset;
    }

    std::string fitControl(const std::string& label, float available) {
        return UiFont::fitEnd(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, available);
    }

    std::string fit(const std::string& label, float available) {
        return UiFont::fitEnd(label, UiFont::MEDIUM, UiWidgets::TEXT_BODY, available);
    }

    void commitSliderEdit(const std::string& id) {
        (void) id;
        double parsed = 0;
        if (Ui::parseDouble(UiInput::get(INPUT_SLIDER), parsed)) {
            if (sliderEditValue != nullptr) {
                sliderEditValue->set(std::max(sliderEditValue->min(), std::min(sliderEditValue->max(), parsed)));
            }
        }
        sliderEditValue = nullptr;
        sliderEditing.clear();
        UiInput::set(INPUT_SLIDER, "");
        if (UiInput::isFocused(INPUT_SLIDER)) {
            UiInput::blur();
        }
    }

    std::string formatNumber(double value) {
        return Ui::javaFixed(value, 1);
    }

    std::string formatStep(double value, double step) {
        int decimals = step <= 0 ? 2 : std::max(0, std::min(4, Ui::decimalScale(step)));
        if (decimals == 0) {
            return std::to_string(Ui::roundl(value));
        }
        return Ui::javaFixed(value, decimals);
    }

    std::string formatValue(NumberValue& value) {
        return formatRaw(value) + value.suffix();
    }

    std::string formatRaw(NumberValue& value) {
        double step = value.step();
        return formatStep(value.get(), step <= 0 ? 1.0 : step);
    }

    std::string formatCompact(double value) {
        if (std::fabs(value - (double) Ui::roundl(value)) < 0.001) {
            return std::to_string(Ui::roundl(value));
        }
        return Ui::javaFixed(value, 1);
    }

    void openChoice(const std::shared_ptr<Choice>& choice, float anchorX, float anchorY, float width,
                    const std::string& source) {
        bool same = popupOpen && choice->owner == popupOwner && source == popupSource;
        cancelColorEdit();
        if (same) {
            UiOverlays::close(UiOverlays::popup);
            return;
        }
        UiOverlays::open(UiOverlays::popup);
        std::vector<std::string> nextOptions = choice->options();
        std::vector<std::string> stale = popupChoice == nullptr ? std::vector<std::string>() : popupChoice->options();
        stale.insert(stale.end(), nextOptions.begin(), nextOptions.end());
        for (const std::string& option : stale) {
            UiState::remove("popup#row" + option);
            UiState::remove("popup#lift" + option);
        }
        bool anySelected = false;
        for (size_t i = 0; i < nextOptions.size() && !anySelected; i++) {
            anySelected = choice->selected(nextOptions[i]);
        }
        UiState::put("popup#checks", choice->multi() && anySelected ? 1.0f : 0.0f);
        UiState::put("popup#open", 0.0f);
        popupOpen = true;
        popupChoice = choice;
        popupOwner = choice->owner;
        popupSource = source;
        popupMulti = choice->multi();
        popupAnchorX = anchorX;
        popupAnchorY = anchorY;
        popupWidth = width;
        popupOpenedFrame = frameCounter;
        popupOpenedPress = pressSerial;
        popupScroll = centerScrollFor(choice);
        popupScrollSmooth = popupScroll;
    }

    float centerScrollFor(const std::shared_ptr<Choice>& choice) {
        std::vector<std::string> options = choice->options();
        if (options.size() <= 6) {
            return 0;
        }
        int index = -1;
        for (size_t i = 0; i < options.size() && index < 0; i++) {
            if (choice->selected(options[i])) {
                index = (int) i;
            }
        }
        if (index < 0) {
            return 0;
        }
        float viewH = 6 * 32.0f;
        float contentH = options.size() * 32.0f;
        return UiRender::clamp(index * 32.0f + 16.0f - viewH * 0.5f, 0, contentH - viewH);
    }

    float listScrollbar(const std::string& owner, float trackX, float trackY, float trackH, float viewH,
                        float maxScroll, float shown, float scroll) {
        float thumbH = std::max(24.0f, trackH * (viewH / (viewH + maxScroll)));
        float thumbY = trackY + (trackH - thumbH) * (shown / maxScroll);
        bool dragging = owner == scrollDragOwner;
        bool trackHover = hit(trackX - 3, trackY, 10, trackH);
        if (!dragging && pendingClick && !clickConsumed && trackHover) {
            clickConsumed = true;
            scrollDragOwner = owner;
            scrollDragOffset = rawHit(trackX - 3, thumbY, 10, thumbH) ? mouseY - thumbY : thumbH * 0.5f;
            dragging = true;
        }
        if (dragging) {
            if (!mouseDown) {
                scrollDragOwner.clear();
            } else {
                float ratio =
                        UiRender::saturate((mouseY - scrollDragOffset - trackY) / std::max(1.0f, trackH - thumbH));
                scroll = ratio * maxScroll;
                thumbY = trackY + (trackH - thumbH) * ratio;
            }
        }
        float lit = UiState::motion(owner + "#thumb", dragging ? 1.0f : (trackHover ? 0.6f : 0.0f), 18.0f);
        UiRender::roundRect(trackX, trackY, 4, trackH, 2.0f, UiTheme::scrollTrack());
        UiRender::roundRect(trackX, thumbY, 4, thumbH, 2.0f,
                            UiRender::mix(UiTheme::scrollThumb(), UiTheme::textFaint(), lit));
        return scroll;
    }

    std::string tooltipWaiting;
    float tooltipWaited = 0.0f;
    constexpr float TOOLTIP_DELAY = 0.5f;

    void tooltipLayer() {
        if (tooltipText.empty()) {
            tooltipWaiting.clear();
            tooltipWaited = 0.0f;
            return;
        }
        if (tooltipText != tooltipWaiting) {
            tooltipWaiting = tooltipText;
            tooltipWaited = 0.0f;
        }
        tooltipWaited += UiRender::deltaTime();
        if (tooltipWaited < TOOLTIP_DELAY) {
            tooltipText.clear();
            return;
        }
        float fadeIn = UiRender::saturate((tooltipWaited - TOOLTIP_DELAY) / 0.12f);
        float savedTipAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedTipAlpha * fadeIn;
        drawTooltip();
        UiRender::alphaMul = savedTipAlpha;
    }

    void drawTooltip() {
        float w = UiFont::width(tooltipText, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
        UiRender::shadow(tooltipX, tooltipY, w, 28, 9.0f, 8.0f, 3.0f, UiTheme::shadow());
        UiWidgets::overlayPanel(tooltipX, tooltipY, w, 28, 9.0f, UiTheme::surfaceRaised(), 0xF);
        UiRender::textY(tooltipText, tooltipX + 11, tooltipY, 28, UiTheme::textPrimary(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
        tooltipText.clear();
    }

    void popupLayer() {
        float open = UiState::motionFrom("popup#open", popupOpen ? 1.0f : 0.0f, 12.0f, 0.0f);
        if (open < 0.002f) {
            return;
        }
        if (popupChoice == nullptr) {
            return;
        }
        std::shared_ptr<Choice> choice = popupChoice;
        std::vector<std::string> options = choice->options();
        int count = (int) options.size();
        int maxVisible = 6;
        bool scrollable = count > maxVisible;
        static std::vector<std::string> measuredOptions;
        static float measuredScale = 0.0f;
        static float measuredWidest = 0.0f;
        static bool measuredIcons = false;
        float scaleNow = UiRender::stableScale();
        if (options != measuredOptions || scaleNow != measuredScale) {
            measuredOptions = options;
            measuredScale = scaleNow;
            measuredWidest = 0.0f;
            measuredIcons = false;
            for (int i = 0; i < count; i++) {
                measuredWidest =
                        std::max(measuredWidest, UiFont::width(options[i], UiFont::MEDIUM, UiWidgets::TEXT_CONTROL));
                measuredIcons = measuredIcons || choice->anchorGlyphs() || !choice->optionIcon(options[i]).empty();
            }
        }
        float widest = measuredWidest;
        bool anyOptionIcon = measuredIcons;
        if (anyOptionIcon) {
            widest += 18;
        }
        bool anySelected = false;
        for (int i = 0; i < count && popupMulti && !anySelected; i++) {
            anySelected = choice->selected(options[i]);
        }
        float checkColumn = UiState::motion("popup#checks", popupMulti && anySelected ? 1.0f : 0.0f, 16.0f);
        float textIndent = 14.0f + 20.0f * checkColumn;
        float sizeX = std::max(popupWidth,
                               widest + textIndent + 20 + (scrollable ? 12 : 0) + (choice->reorderable() ? 24 : 0));
        float sizeY = std::min(count, maxVisible) * 32.0f + 8.0f;
        float contentH = count * 32.0f;
        float viewH = sizeY - 8.0f;
        if (!scrollable) {
            popupScroll = 0;
        }
        popupScroll = UiRender::clamp(popupScroll, 0, std::max(0.0f, contentH - viewH));
        popupScrollSmooth = UiRender::clamp(UiRender::approach(popupScrollSmooth, popupScroll, 18.0f), 0,
                                            std::max(0.0f, contentH - viewH));
        float px = popupAnchorX;
        float py = popupAnchorY + (23.0f - sizeY) * 0.5f;
        float lx = limitX();
        float ly = limitY();
        px = UiRender::clamp(px, 6.0f, std::max(6.0f, lx - sizeX - 6.0f));
        py = UiRender::clamp(py, 6.0f, std::max(6.0f, ly - sizeY - 6.0f));

        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        float savedOffsetY = UiRender::offsetY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = (px + sizeX * 0.5f) * UiRender::uiScale;
        UiRender::pivotY = py * UiRender::uiScale;
        UiRender::offsetY = savedOffsetY + (1.0f - eased) * 8.0f * UiRender::uiScale;

        UiRender::shadow(px, py, sizeX, sizeY, 12, 9.0f, 4.0f, UiTheme::shadow());
        UiWidgets::overlayPanel(px, py, sizeX, sizeY, 12, UiTheme::surface(), 0xF);

        if (popupOpen) {
            markOverlayUnion(px, py, sizeX, sizeY);
            markAbove(px, py, sizeX, sizeY);
        }
        popupRectX = px;
        popupRectY = py;
        popupRectW = sizeX;
        popupRectH = sizeY;
        bool accepts = popupOpen && frameCounter > popupOpenedFrame && pressSerial != popupOpenedPress;
        bool reorderable = choice->reorderable();
        float rowW = sizeX - 8 - (scrollable ? 14 : 0);
        float listTop = py + 4 - popupScrollSmooth;
        int dropIndex = -1;
        if (popupDragFrom >= count || (popupDragFrom >= 0 && !reorderable)) {
            popupDragFrom = -1;
        }
        if (popupDragFrom >= 0) {
            dropIndex = std::max(0, std::min(count - 1, Ui::round((mouseY - popupDragGrab - listTop) / 32.0f)));
        }
        popupGripLit = UiState::motion("popup#grips", popupDragFrom >= 0 ? 1.0f : 0.0f, 14.0f);
        UiRender::enableScissor((int) (UiRender::transformX(px)), (int) (UiRender::transformY(py + 4)),
                                (int) std::ceil(UiRender::transformX(px + sizeX)),
                                (int) std::ceil(UiRender::transformY(py + 4 + viewH)));
        for (int i = 0; i < count; i++) {
            const std::string& option = options[i];
            float shiftTarget = 0.0f;
            if (popupDragFrom >= 0 && popupDragFrom < dropIndex && i > popupDragFrom && i <= dropIndex) {
                shiftTarget = -32.0f;
            } else if (popupDragFrom >= 0 && popupDragFrom > dropIndex && i >= dropIndex && i < popupDragFrom) {
                shiftTarget = 32.0f;
            }
            float shift = reorderable ? UiState::motion("popup#shift" + option, shiftTarget, 20.0f) : 0.0f;
            if (i == popupDragFrom) {
                continue;
            }
            float rx = px + 4;
            float ry = listTop + i * 32 + shift;
            if (ry + 32 < py + 4 || ry > py + 4 + viewH) {
                continue;
            }
            bool inView = mouseY >= py + 4 && mouseY <= py + 4 + viewH;
            bool rowHover = popupOpen && popupDragFrom < 0 && hit(rx, ry, rowW, 32) && inView;
            float hover = UiState::motion("popup#row" + option, rowHover ? 1.0f : 0.0f, 22.0f);
            float lift = UiState::motion("popup#lift" + option, 0.0f, 16.0f);
            if (lift > 0.001f) {
                float savedLiftAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedLiftAlpha * lift;
                UiRender::shadow(rx, ry, rowW, 32, 8.0f, 6.0f, 2.0f, UiTheme::shadow());
                UiRender::roundRect(rx + 2, ry + 2, rowW - 4, 28, 8, UiTheme::surfaceRaised());
                UiRender::alphaMul = savedLiftAlpha;
            }
            popupRow(option, rx, ry, rowW, textIndent, checkColumn, hover, reorderable);
            if (reorderable && accepts && popupDragFrom < 0 && inView && pendingClick && !clickConsumed &&
                hit(rx + rowW - 26, ry, 26, 32)) {
                clickConsumed = true;
                popupDragFrom = i;
                popupDragGrab = mouseY - ry;
            } else if (accepts && popupDragFrom < 0 && click(rx, ry, rowW, 32)) {
                UiSound::request(UiSound::TICK);
                choice->choose(option);
                if (!popupMulti) {
                    popupOpen = false;
                }
            }
        }
        if (popupDragFrom >= 0) {
            std::string dragged = options[popupDragFrom];
            float dragY = UiRender::clamp(mouseY - popupDragGrab, py + 4 - 8, py + 4 + viewH - 24);
            UiRender::shadow(px + 4, dragY, rowW, 32, 8.0f, 6.0f, 2.0f, UiTheme::shadow());
            UiRender::roundRect(px + 6, dragY + 2, rowW - 4, 28, 8, UiTheme::surfaceRaised());
            popupRow(dragged, px + 4, dragY, rowW, textIndent, checkColumn, 1.0f, true);
            if (!mouseDown) {
                UiState::put("popup#shift" + dragged, dragY - (listTop + dropIndex * 32));
                UiState::put("popup#row" + dragged, 1.0f);
                UiState::put("popup#lift" + dragged, 1.0f);
                for (int i = 0; i < count; i++) {
                    if (i == popupDragFrom) {
                        continue;
                    }
                    int moved = popupDragFrom < dropIndex && i > popupDragFrom && i <= dropIndex
                            ? -1
                            : (popupDragFrom > dropIndex && i >= dropIndex && i < popupDragFrom ? 1 : 0);
                    float current = UiState::getOrDefault("popup#shift" + options[i], 0.0f);
                    UiState::put("popup#shift" + options[i], current - moved * 32.0f);
                }
                choice->reorder(popupDragFrom, dropIndex);
                popupDragFrom = -1;
            }
        }

        UiRender::disableScissor();
        if (scrollable) {
            float trackX = px + sizeX - 10;
            float trackY = py + 6;
            float trackH = viewH - 4;
            float thumbH = std::max(24.0f, trackH * (viewH / contentH));
            float maxScrollLocal = std::max(1.0f, contentH - viewH);
            float thumbY = trackY + (trackH - thumbH) * (popupScrollSmooth / maxScrollLocal);
            bool dragging = scrollDragOwner == "popup";
            if (!dragging && popupOpen && pendingClick && !clickConsumed && hit(trackX - 4, trackY, 14, trackH)) {
                clickConsumed = true;
                scrollDragOwner = "popup";
                scrollDragOffset = rawHit(trackX - 4, thumbY, 14, thumbH) ? mouseY - thumbY : thumbH * 0.5f;
                dragging = true;
            }
            if (dragging) {
                if (!mouseDown) {
                    scrollDragOwner.clear();
                } else {
                    float ratio =
                            UiRender::saturate((mouseY - scrollDragOffset - trackY) / std::max(1.0f, trackH - thumbH));
                    popupScroll = ratio * maxScrollLocal;
                    popupScrollSmooth = popupScroll;
                    thumbY = trackY + (trackH - thumbH) * ratio;
                }
            }
            UiRender::roundRect(trackX, trackY, 6, trackH, 3.0f, UiTheme::surfaceRaised());
            UiRender::roundRect(trackX, thumbY, 6, thumbH, 3.0f,
                                dragging ? UiTheme::textFaint() : UiTheme::outlineSoft());
        }

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
        UiRender::offsetY = savedOffsetY;
    }

    void popupRow(const std::string& option, float rx, float ry, float rowW, float textIndent, float checkColumn,
                  float hover, bool grip) {
        if (hover > 0.001f) {
            UiRender::roundRect(rx + 2, ry + 2, rowW - 4, 32 - 4, 8, UiTheme::overlay(Ui::round(14 * hover)));
        }
        bool selected = popupChoice != nullptr && popupChoice->selected(option);
        if (selected && checkColumn > 0.01f) {
            float savedCheckAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedCheckAlpha * checkColumn;
            UiWidgets::check(rx + 16, ry + 16, UiTheme::textPrimary());
            UiRender::alphaMul = savedCheckAlpha;
        }
        std::string optionGlyph = popupChoice != nullptr ? popupChoice->optionIcon(option) : std::string();
        float optionTextX = rx + textIndent;
        uint32_t optionColor = popupChoice != nullptr ? popupChoice->optionColor(option, selected)
                                                      : (selected ? UiTheme::textPrimary() : UiTheme::textMuted());
        if (popupChoice != nullptr && popupChoice->anchorGlyphs()) {
            UiWidgets::anchorGlyph(option, optionTextX + 6, ry + 16, optionColor);
            optionTextX += 18;
        } else if (!optionGlyph.empty()) {
            UiWidgets::iconBeside(optionGlyph, optionTextX + 6, option, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, ry, 32,
                                  optionColor, 10.5f);
            optionTextX += 18;
        }
        UiRender::textY(option, optionTextX, ry, 32, optionColor, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        if (grip) {
            bool gripHover = hit(rx + rowW - 26, ry, 26, 32);
            UiWidgets::icon(Ui::glyph(0xf58e), rx + rowW - 14, ry + 16,
                            gripHover ? UiTheme::textSecondary()
                                      : UiRender::mix(UiTheme::textFaint(), UiTheme::textSecondary(), popupGripLit),
                            10.0f);
        }
    }

    SettingsPagePtr settingsPage(const std::string& id) {
        auto found = subPages.find(id);
        return found == subPages.end() ? nullptr : found->second;
    }

    SettingPtr firstColorChild(const std::string& owner) {
        SettingList children = subChildren(owner);
        for (size_t i = 0; i < children.size(); i++) {
            if (children[i]->kind == Setting::Kind::Color && children[i]->color != nullptr &&
                children[i]->color->available()) {
                return children[i];
            }
        }
        return nullptr;
    }

    void openSettingsPicker(const SettingsPagePtr& page, float anchorX, float anchorY) {
        closeAllOverlays();
        subPages[page->id] = page;
        UiOverlays::openOn(nullptr, UiOverlays::sub);
        subOwner = page->id;
        subAnchorX = anchorX;
        subAnchorY = anchorY;
        subOpenedFrame = frameCounter;
        subInlinePicker.insert(page->id);
        UiState::remove("sub#open");
    }

    SettingList subChildren(const std::string& owner) {
        SettingsPagePtr page = owner.empty() ? nullptr : settingsPage(owner);
        return page == nullptr ? SettingList() : page->visibleItems();
    }

    std::string subTitle(const std::string& owner) {
        SettingsPagePtr page = owner.empty() ? nullptr : settingsPage(owner);
        return page == nullptr ? std::string() : page->title;
    }

    void openSettings(const SettingsPagePtr& page, float anchorX, float anchorY) {
        subPages[page->id] = page;
        toggleSubPanel(page->id, anchorX, anchorY);
    }

    void toggleSubPanel(const std::string& owner, float anchorX, float anchorY) {
        if (owner == subChildOwner) {
            UiOverlays::open(UiOverlays::sub);
            subInlinePicker.erase(owner);
            subChildOwner.clear();
            return;
        }
        if (owner == subOwner || (!subPanelDrawing && owner == subParent)) {
            UiOverlays::close(UiOverlays::sub);
            return;
        }
        if (subPanelDrawing) {
            SettingPtr childColor = firstColorChild(owner);
            UiOverlays::open(UiOverlays::sub);
            subParent = subOwner;
            subParentX = subAnchorX;
            subParentY = subAnchorY;
            subOwner = owner;
            subAnchorX = anchorX;
            subAnchorY = anchorY;
            subOpenedFrame = frameCounter;
            if (childColor != nullptr) {
                subInlinePicker.insert(owner);
            }
            UiState::remove("sub#open");
            return;
        }
        UiOverlays::open(UiOverlays::sub);
        subOwner = owner;
        subParent.clear();
        subAnchorX = anchorX;
        subAnchorY = anchorY;
    }

    void subLayer() {
        subPanelDrawing = true;
        subLayerPanels();
        subPanelDrawing = false;
    }

    void subLayerPanels() {
        if (!subParent.empty() && !subOwner.empty()) {
            std::string child = subOwner;
            std::string parent = subParent;
            float childX = subAnchorX;
            float childY = subAnchorY;
            float parentX = subParentX;
            float parentY = subParentY;
            subOwner = parent;
            subParent.clear();
            subAnchorX = parentX;
            subAnchorY = parentY;
            subChildOwner = child;
            subPanelBody(1.0f);
            bool childClosed = subChildOwner.empty();
            subChildOwner.clear();
            if (!childClosed && parent == subOwner && subParent.empty()) {
                subOwner = child;
                subParent = parent;
                subParentX = parentX;
                subParentY = parentY;
                subAnchorX = childX;
                subAnchorY = childY;
            }
        }
        subPanelBody(UiState::motionFrom("sub#open", !subOwner.empty() ? 1.0f : 0.0f, 12.0f, 0.0f));
    }

    void subPanelBody(float open) {
        if (open < 0.002f || subOwner.empty()) {
            return;
        }
        std::string owner = subOwner;
        SettingPtr inlineColor = subInlinePicker.count(owner) != 0 ? firstColorChild(owner) : nullptr;
        SettingList children = subChildren(owner);
        if (children.empty()) {
            if (!subParent.empty()) {
                subOwner = subParent;
                subAnchorX = subParentX;
                subAnchorY = subParentY;
                subParent.clear();
                return;
            }
            subOwner.clear();
            return;
        }

        SettingList colors;
        SettingList rows;
        for (size_t i = 0; i < children.size(); i++) {
            if (inlineColor != nullptr && children[i] == inlineColor) {
                continue;
            }
            if (children[i]->kind == Setting::Kind::Color) {
                colors.push_back(children[i]);
            } else {
                rows.push_back(children[i]);
            }
        }

        float padding = 12.0f;
        float rowH = 30.0f;
        float titleH = 34.0f;
        float groupGap = padding * 0.5f;
        float colorGroupH = colors.empty() ? 0 : colors.size() * rowH + 10.0f;
        float rowGroupH = rows.empty() ? 0 : rows.size() * rowH + 10.0f;
        float sizeY = titleH + (colors.empty() ? 0 : colorGroupH + groupGap) + rowGroupH + padding * 0.5f;

        float widest = UiFont::width(subTitle(owner), UiFont::BOLD, UiWidgets::TEXT_TITLE);
        for (size_t i = 0; i < children.size(); i++) {
            const SettingPtr& child = children[i];
            float need = UiFont::width(child->label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 28 + 26;
            if (child->kind == Setting::Kind::Select) {
                need += std::min(150.0f, UiWidgets::subModeWidth(child->choice->label())) + 14;
            } else if (child->kind == Setting::Kind::Slider) {
                need += 110;
            } else {
                need += 58;
            }
            widest = std::max(widest, need);
        }
        float sizeX = std::max(228.0f, std::min(360.0f, widest + padding * 2));
        if (inlineColor != nullptr) {
            sizeX = std::max(sizeX, 248.0f);
            sizeY = sizeX - 26.0f + 94.0f + (colors.empty() ? 0 : colorGroupH) + (rows.empty() ? 0 : rowGroupH) +
                    padding * 0.5f;
        }

        float lx = limitX();
        float ly = limitY();
        if (sizeY > ly - 12.0f) {
            rowH = std::max(20.0f, rowH - (sizeY - (ly - 12.0f)) / std::max(1, (int) (rows.size() + colors.size())));
            colorGroupH = colors.empty() ? 0 : colors.size() * rowH + 10.0f;
            rowGroupH = rows.empty() ? 0 : rows.size() * rowH + 10.0f;
            sizeY = titleH + (colors.empty() ? 0 : colorGroupH + groupGap) + rowGroupH + padding * 0.5f;
            if (inlineColor != nullptr) {
                sizeY = sizeX - 26.0f + 94.0f + (colors.empty() ? 0 : colorGroupH) + (rows.empty() ? 0 : rowGroupH) +
                        padding * 0.5f;
            }
        }
        float px = UiRender::clamp(subAnchorX, 6.0f, std::max(6.0f, lx - sizeX - 6.0f));
        float py = UiRender::clamp(subAnchorY, 6.0f, std::max(6.0f, ly - sizeY - 6.0f));

        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = px * UiRender::uiScale;
        UiRender::pivotY = (py + sizeY * 0.5f) * UiRender::uiScale;

        UiWidgets::subPanel(px, py, sizeX, sizeY);
        markOverlayUnion(px, py, sizeX, sizeY);
        subPanelX = px;
        subPanelY = py;
        subPanelW = sizeX;
        subPanelH = sizeY;
        subPanelRight = px + sizeX;
        subPanelTop = py;

        subPanelDismiss = pendingClick && !overlayCoversMouse() && rawHit(px, py, sizeX, sizeY);

        if (inlineColor == nullptr) {
            UiRender::textY(subTitle(owner), px + padding + 4, py + 4, titleH - 8, UiTheme::textPrimary(), UiFont::BOLD,
                            UiWidgets::TEXT_TITLE);
        }

        float cursor = py + titleH;
        if (inlineColor != nullptr) {
            float pickerH = sizeX - 26.0f + 94.0f;
            ColorPickerWidget(inlineColor->color, true,
                              frameCounter > subOpenedFrame && !overlayCoversMouse() && swatchMenuOwner.empty())
                    .bounds(px, py, sizeX, pickerH)
                    .paint();
            cursor = py + pickerH;
        }
        if (!colors.empty()) {
            if (inlineColor == nullptr) {
                UiWidgets::subGroup(px + padding * 0.5f, cursor, sizeX - padding, colorGroupH);
            }
            for (size_t i = 0; i < colors.size(); i++) {
                subRow(colors[i], px + padding * 0.5f, cursor + 5.0f + i * rowH, sizeX - padding, rowH);
            }
            cursor += colorGroupH + (inlineColor == nullptr ? groupGap : 0);
        }
        if (!rows.empty()) {
            if (inlineColor == nullptr) {
                UiWidgets::subGroup(px + padding * 0.5f, cursor, sizeX - padding, rowGroupH);
            }
            for (size_t i = 0; i < rows.size(); i++) {
                subRow(rows[i], px + padding * 0.5f, cursor + 5.0f + i * rowH, sizeX - padding, rowH);
            }
            cursor += rowGroupH + (inlineColor == nullptr ? groupGap : 0);
        }

        if (subPanelDismiss) {
            if (!pickerOwner.empty() && pressSerial != pickerOpenedPress) {
                pickerOwner.clear();
            }
            if (popupOpen && pressSerial != popupOpenedPress) {
                popupOpen = false;
            }
        }
        subPanelDismiss = false;

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    void subRow(const SettingPtr& setting, float x, float y, float w, float h) {
        Setting::Kind kind = setting->kind;
        bool pending = setting->context && !overlayCoversMouse() && rightClick && hit(x, y, w, h) &&
                kind != Setting::Kind::Color && kind != Setting::Kind::Group;
        subRowBody(setting, x, y, w, h);
        if (pending && rightClick) {
            rightClick = false;
            setting->context(mouseX, mouseY);
        }
    }

    void subRowBody(const SettingPtr& setting, float x, float y, float w, float h) {
        Setting::Kind kind = setting->kind;
        const std::string& id = setting->id;
        const std::string& label = setting->label;
        bool blocked = overlayCoversMouse();
        bool rowHover = !blocked && kind != Setting::Kind::Text && hit(x, y, w, h);
        UiWidgets::subRowHover("subrow#" + id, x, y, w, h, rowHover);
        if (!blocked && hit(x, y, w, h)) {
            markDoc(id, setting->doc, subPanelRight, y);
        }
        float labelX = x + 14;
        float rightX = x + w - 14;
        if (setting->hasChildren()) {
            float controlLeft = kind == Setting::Kind::Toggle ? rightX - 34
                    : kind == Setting::Kind::Color            ? rightX - 20
                    : kind == Setting::Kind::Slider           ? rightX - 96
                    : kind == Setting::Kind::Text             ? rightX - 130
                                                              : rightX -
                            UiWidgets::subModeWidth(setting->choice != nullptr ? setting->choice->label()
                                                                               : std::string());
            bool group = kind == Setting::Kind::Group;
            float dotsX = group ? rightX - 24 : controlLeft - 34;
            float dotsY = y + (h - 22) * 0.5f;
            DotsWidget dots("subsub#" + id, setting->page());
            dots.dotsAt(dotsX, dotsY).anchor(subPanelRight + 8, y);
            if (group) {
                dots.hitArea(x, y, w, h);
            }
            dots.block(blocked).paint();
        }
        if (!setting->glyph.empty()) {
            UiWidgets::iconBeside(setting->glyph, labelX + 5, label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, y, h,
                                  UiTheme::textFaint(), 10.5f);
            labelX += 17;
        }
        UiRender::textY(label, labelX, y, h, UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        float swatchY = y + (h - 15) * 0.5f;
        if (kind == Setting::Kind::Text) {
            TextFieldWidget(setting->text)
                    .box(rightX - 130, y + (h - 23) * 0.5f, 130, 23)
                    .bounds(x, y, w, h)
                    .block(blocked)
                    .paint();
        } else if (kind == Setting::Kind::Key) {
            BindWidget(setting->key).row(x, y, w, h, rightX).block(blocked).paint();
        } else if (kind == Setting::Kind::Color) {
            SwatchWidget(setting->color)
                    .swatchAt(rightX - 17, swatchY)
                    .picker(subPanelRight + 8, subPanelTop)
                    .hitArea(x, y, w, h)
                    .block(blocked)
                    .paint();
        } else if (kind == Setting::Kind::Toggle) {
            float toggleX = rightX - 30;
            if (setting->swatchColor != nullptr) {
                SwatchWidget(setting->swatchColor)
                        .swatchAt(toggleX - 25, swatchY)
                        .picker(subPanelRight + 8, subPanelTop)
                        .block(blocked)
                        .paint();
            }
            ToggleWidget("subprop#" + id, setting->flag, true)
                    .bounds(toggleX, y + (h - 18) * 0.5f, 30, 18)
                    .hitArea(x, y, w, h)
                    .block(blocked)
                    .paint();
        } else if (kind == Setting::Kind::Slider) {
            SliderWidget(id, setting->number)
                    .compactTrack(rightX - 96.0f, y + (h - 4) * 0.5f, 96.0f)
                    .block(blocked)
                    .paint();
        } else if (kind == Setting::Kind::Select) {
            std::string value = setting->choice->label();
            float labelRoom = UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float swatchRoom = setting->swatchColor == nullptr ? 0 : 25.0f;
            float valueRoom = std::max(30.0f, rightX - (labelX + labelRoom + 12) - swatchRoom);
            if (setting->swatchColor != nullptr) {
                float valueWidth = std::min(valueRoom, UiWidgets::subModeWidth(value));
                SwatchWidget(setting->swatchColor)
                        .swatchAt(rightX - valueWidth - 25.0f, swatchY)
                        .picker(subPanelRight + 8, subPanelTop)
                        .block(blocked)
                        .paint();
            }
            SelectWidget("subprop#" + id, setting->choice)
                    .compactRow(x, y, w, h, rightX, valueRoom)
                    .block(blocked)
                    .paint();
        }
    }

    void beginColorEdit(const std::shared_ptr<ColorValue>& color, int cell, const std::string& value) {
        commitColorEdit();
        colorEditing = color->owner;
        colorEditValue = color;
        colorEditingCell = cell;
        UiInput::set(INPUT_COLOR, value);
        UiInput::focus(INPUT_COLOR, cell == 0 ? 8 : 3);
        UiInput::select(INPUT_COLOR, value);
    }

    void cancelColorEdit() {
        colorEditing.clear();
        colorEditValue = nullptr;
        colorEditingCell = -1;
        UiInput::set(INPUT_COLOR, "");
        if (UiInput::isFocused(INPUT_COLOR)) {
            UiInput::blur();
        }
    }

    void commitColorEdit() {
        std::shared_ptr<ColorValue> color = colorEditValue;
        int cell = colorEditingCell;
        std::string text = Ui::trim(UiInput::get(INPUT_COLOR));
        cancelColorEdit();
        if (color == nullptr || text.empty() || !color->available()) {
            return;
        }
        uint32_t argb = color->get();
        if (cell == 0) {
            std::string hex = Ui::startsWith(text, "#") ? text.substr(1) : text;
            long long parsed = 0;
            if (Ui::parseHex(hex, parsed)) {
                color->set(hex.size() <= 6 ? (uint32_t) (0xFF000000LL | parsed) : (uint32_t) parsed);
            }
            return;
        }
        int value = 0;
        if (!Ui::parseInt(text, value)) {
            return;
        }
        int alpha = (argb >> 24) & 0xFF;
        if (pickerMode == 1) {
            int r = (argb >> 16) & 0xFF;
            int g = (argb >> 8) & 0xFF;
            int b = argb & 0xFF;
            int clamped = std::max(0, std::min(255, value));
            if (cell == 1) r = clamped;
            if (cell == 2) g = clamped;
            if (cell == 3) b = clamped;
            if (cell == 4) alpha = clamped;
            color->set(((uint32_t) alpha << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b);
            return;
        }
        float hsb[3];
        Ui::rgbToHsb((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, hsb);
        if (cell == 1) hsb[0] = std::max(0, std::min(360, value)) / 360.0f;
        if (cell == 2) hsb[1] = std::max(0, std::min(100, value)) / 100.0f;
        if (cell == 3) hsb[2] = std::max(0, std::min(100, value)) / 100.0f;
        if (cell == 4) alpha = std::max(0, std::min(255, value));
        color->set(((uint32_t) alpha << 24) | (Ui::hsbToRgb(hsb[0], hsb[1], hsb[2]) & 0xFFFFFFu));
    }

    bool scrollColor(double amount) {
        std::shared_ptr<ColorValue> color = hoveredColor;
        if (color == nullptr || !color->available()) {
            return false;
        }
        int direction = amount > 0 ? 1 : -1;
        uint32_t argb = color->get();
        int alpha = (argb >> 24) & 0xFF;
        int r = (argb >> 16) & 0xFF;
        int g = (argb >> 8) & 0xFF;
        int b = argb & 0xFF;
        const std::string& channel = hoveredColorChannel;
        if (channel == "alpha" || channel == "red" || channel == "green" || channel == "blue") {
            int step = direction * 5;
            alpha = channel == "alpha" ? std::max(0, std::min(255, alpha + step)) : alpha;
            r = channel == "red" ? std::max(0, std::min(255, r + step)) : r;
            g = channel == "green" ? std::max(0, std::min(255, g + step)) : g;
            b = channel == "blue" ? std::max(0, std::min(255, b + step)) : b;
            color->set(((uint32_t) alpha << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b);
            return true;
        }
        float hsb[3];
        Ui::rgbToHsb(r, g, b, hsb);
        if (channel == "hue") {
            hsb[0] = std::fmod(hsb[0] + direction * 2.0f / 360.0f + 1.0f, 1.0f);
            hsb[1] = std::max(0.0001f, hsb[1]);
            hsb[2] = std::max(0.0001f, hsb[2]);
        } else if (channel == "sat") {
            hsb[1] = UiRender::saturate(hsb[1] + direction * 0.02f);
        } else {
            hsb[2] = UiRender::saturate(hsb[2] + direction * 0.02f);
        }
        color->set(((uint32_t) alpha << 24) | (Ui::hsbToRgb(hsb[0], hsb[1], hsb[2]) & 0xFFFFFFu));
        return true;
    }

    void beginSliderEdit(const std::string& id, const std::string& value) {
        sliderEditValue = nullptr;
        sliderEditing = id;
        UiInput::set(INPUT_SLIDER, value);
        UiInput::focus(INPUT_SLIDER, 10);
        UiInput::select(INPUT_SLIDER, value);
    }

    void beginTextEdit(const std::shared_ptr<TextValue>& target) {
        commitTextEdit();
        textEditing = target->owner;
        textEditValue = target;
        std::string value = target->get();
        UiInput::set(INPUT_TEXT, value);
        UiInput::focus(INPUT_TEXT, target->maxLength());
        UiInput::select(INPUT_TEXT, value);
    }

    void cancelTextEdit() {
        textEditing.clear();
        textEditValue = nullptr;
        UiInput::set(INPUT_TEXT, "");
        if (UiInput::isFocused(INPUT_TEXT)) {
            UiInput::blur();
        }
    }

    void commitTextEdit() {
        std::shared_ptr<TextValue> target = textEditValue;
        std::string value = UiInput::get(INPUT_TEXT);
        cancelTextEdit();
        if (target != nullptr) {
            target->set(Ui::trim(value));
        }
    }

    void openPicker(const std::shared_ptr<ColorValue>& color, float x, float y) {
        bool same = color->owner == pickerOwner;
        cancelColorEdit();
        if (same) {
            UiOverlays::close(UiOverlays::picker);
            return;
        }
        UiOverlays::open(UiOverlays::picker);
        pickerOwner = color->owner;
        pickerColor = color;
        pickerOpenedFrame = frameCounter;
        pickerOpenedPress = pressSerial;
        pickerX = x;
        pickerY = y;
    }

    void openSwatchMenu(const std::shared_ptr<ColorValue>& color, float x, float y) {
        UiOverlays::open(UiOverlays::swatch);
        UiState::remove("swatchMenu#open" + color->owner);
        cancelColorEdit();
        swatchMenuOwner = color->owner;
        swatchMenuColor = color;
        swatchMenuX = x;
        swatchMenuY = y;
    }

    void swatchMenuLayer() {
        float open = UiState::motionFrom("swatchMenu#open" + swatchMenuOwner, !swatchMenuOwner.empty() ? 1.0f : 0.0f,
                                         12.0f, 0.0f);
        if (open < 0.002f || swatchMenuOwner.empty()) {
            return;
        }
        if (swatchMenuColor == nullptr || !swatchMenuColor->available()) {
            swatchMenuOwner.clear();
            return;
        }
        MenuItems items;
        items.push_back(std::make_shared<SwatchMenuItem>(swatchMenuColor, 0, "Copy", 0xf0c5));
        items.push_back(std::make_shared<SwatchMenuItem>(swatchMenuColor, 1, "Paste", 0xf0ea));
        items.push_back(std::make_shared<SwatchMenuItem>(swatchMenuColor, 2, "Reset", 0xf0e2));
        swatchContextMenu.minWidth = 132.0f;
        swatchContextMenu.fill(items);
        float sizeX = swatchContextMenu.menuWidth();
        float sizeY = swatchContextMenu.menuHeight();
        float lx = limitX();
        float ly = limitY();
        float px = UiRender::clamp(swatchMenuX, 6.0f, std::max(6.0f, lx - sizeX - 6.0f));
        float py = UiRender::clamp(swatchMenuY, 6.0f, std::max(6.0f, ly - sizeY - 6.0f));
        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = px * UiRender::uiScale;
        UiRender::pivotY = py * UiRender::uiScale;
        swatchRectX = px;
        swatchRectY = py;
        swatchRectW = sizeX;
        swatchRectH = sizeY;
        swatchContextMenu.bounds(px, py, 0, 0).paint();
        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    void colorPickerLayer() {
        if (!pickerOwner.empty() && (pickerColor == nullptr || !pickerColor->available())) {
            pickerOwner.clear();
        }
        float open = UiState::motionFrom("picker#open", !pickerOwner.empty() ? 1.0f : 0.0f, 12.0f, 0.0f);
        if (open < 0.002f || pickerOwner.empty()) {
            if (!colorEditing.empty()) {
                commitColorEdit();
            }
            return;
        }
        float sizeX = 214;
        float sizeY = 282;
        float lx = limitX();
        float ly = limitY();
        float px = UiRender::clamp(pickerX, 6.0f, std::max(6.0f, lx - sizeX - 6.0f));
        float py = UiRender::clamp(pickerY, 6.0f, std::max(6.0f, ly - sizeY - 6.0f));
        markOverlayUnion(px, py, sizeX, sizeY);
        markAbove(px, py, sizeX, sizeY);
        pickerRectX = px;
        pickerRectY = py;
        pickerRectW = sizeX;
        pickerRectH = sizeY;

        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = (px + sizeX * 0.5f) * UiRender::uiScale;
        UiRender::pivotY = py * UiRender::uiScale;

        UiRender::shadow(px, py, sizeX, sizeY, 14.0f, 10.0f, 4.0f, UiTheme::shadow());
        UiWidgets::overlayPanel(px, py, sizeX, sizeY, 14.0f, UiTheme::surface(), 0xF);

        ColorPickerWidget(pickerColor, false,
                          frameCounter > pickerOpenedFrame && pressSerial != pickerOpenedPress &&
                                  swatchMenuOwner.empty())
                .bounds(px, py, sizeX, sizeY)
                .paint();

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    bool insideRect(bool active, float x, float y, float w, float h) {
        return active && mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    }

    std::string pickerModeName() {
        return pickerMode == 0 ? "HEX" : (pickerMode == 1 ? "RGB" : "HSV");
    }

    void pickerCursor(float cx, float cy, uint32_t argb) {
        UiRender::circle(cx, cy, 7.0f, UiTheme::pickerRing());
        UiRender::circle(cx, cy, 5.2f, argb | 0xFF000000u);
    }

    void pickerHandle(float cx, float cy, uint32_t argb) {
        UiRender::roundRect(cx - 5, cy - 8, 10, 16, 5.0f, UiTheme::pickerRing());
        UiRender::roundRect(cx - 3.4f, cy - 6.4f, 6.8f, 12.8f, 3.4f, argb);
    }

    std::vector<std::shared_ptr<MenuItem>> pickerModes() {
        MenuItems modes;
        modes.push_back(std::make_shared<PickerModeItem>(0, "HEX", 0xf1fc));
        modes.push_back(std::make_shared<PickerModeItem>(1, "RGB", 0xf53f));
        modes.push_back(std::make_shared<PickerModeItem>(2, "HSV", 0xf5c3));
        return modes;
    }

    void pickerModeLayer(const std::string& owner, float x, float baseYPos) {
        float open =
                UiState::motionFrom("pickerMode#open" + owner, owner == pickerModeOwner ? 1.0f : 0.0f, 12.0f, 0.0f);
        if (open < 0.002f) {
            return;
        }
        pickerModeMenu.fill(pickerModes());
        float sizeY = pickerModeMenu.menuHeight();
        float sy = baseYPos - sizeY - 6;
        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = x * UiRender::uiScale;
        UiRender::pivotY = (sy + sizeY) * UiRender::uiScale;
        pickerModeMenu.block(owner != pickerModeOwner);
        pickerModeMenu.bounds(x, sy, 0, 0).paint();
        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    void openMenu(const std::string& owner, std::function<MenuItems()> items, float x, float y, float minWidth) {
        if (owner == menuOwner) {
            UiOverlays::close(UiOverlays::menu);
            return;
        }
        UiOverlays::open(UiOverlays::menu);
        contextMenu.closeChild();
        menuOwner = owner;
        menuItems = std::move(items);
        menuX = x;
        menuY = y;
        menuMinWidth = minWidth;
        UiState::remove("contextMenu#open");
    }

    void openMenu(const std::string& owner, const MenuItems& items, float x, float y, float minWidth) {
        openMenu(owner, std::function<MenuItems()>([items] { return items; }), x, y, minWidth);
    }

    void closeMenu() {
        if (!menuOwner.empty()) {
            UiOverlays::close(UiOverlays::menu);
        }
    }

    void menuLayer() {
        float open = UiState::motionFrom("contextMenu#open", !menuOwner.empty() ? 1.0f : 0.0f, 12.0f, 0.0f);
        if (open < 0.002f || menuOwner.empty() || !menuItems) {
            return;
        }
        contextMenu.minWidth = menuMinWidth;
        contextMenu.fill(menuItems());
        float sizeX = contextMenu.menuWidth();
        float sizeY = contextMenu.menuHeight();
        float px = UiRender::clamp(menuX, 6.0f, std::max(6.0f, limitX() - sizeX - 6.0f));
        float py = UiRender::clamp(menuY, 6.0f, std::max(6.0f, limitY() - sizeY - 6.0f));
        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = px * UiRender::uiScale;
        UiRender::pivotY = py * UiRender::uiScale;
        contextMenu.bounds(px, py, 0, 0).paint();
        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    void openLink(const std::string& url) {
        if (url.empty() || !(Ui::startsWith(url, "http://") || Ui::startsWith(url, "https://"))) {
            return;
        }
#if defined(_WIN32)
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    }

    void markDoc(const std::string& target, const std::string& text, float anchorX, float rowY) {
        if (target.empty() || text.empty()) {
            return;
        }
        docHoverNext = target;
        docHoverNextText = text;
        docHoverX = anchorX;
        docHoverY = rowY;
    }

    void advanceDocHover() {
        if (docHoverNext.empty()) {
            docHoverTarget.clear();
            docHoverTime = 0;
            return;
        }
        docHoverText = docHoverNextText;
        if (docHoverNext != docHoverTarget) {
            docHoverTarget = docHoverNext;
            docHoverTime = 0;
            UiState::remove("doc#reveal");
        } else {
            docHoverTime += UiRender::deltaTime();
        }
    }

    void docLayer() {
        advanceDocHover();
        bool ready = !docHoverTarget.empty() && docHoverTime >= 1.0f;
        float open = UiState::motionFrom("doc#reveal", ready ? 1.0f : 0.0f, 16.0f, 0.0f);
        if (open < 0.002f || docHoverTarget.empty()) {
            return;
        }

        float lx = limitX();
        float ly = limitY();
        float contentPad = 14.0f;
        float lineH = 19.0f;
        float room = std::max(180.0f, lx - docHoverX - 26.0f);
        float maxW = std::min(380.0f, room);
        std::vector<std::string> lines =
                UiFont::wrap(docHoverText, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, maxW - contentPad * 2);
        float widest = 0.0f;
        for (const std::string& line : lines) {
            widest = std::max(widest, UiFont::width(line, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL));
        }
        float sizeX = std::min(maxW, widest + contentPad * 2);
        float sizeY = std::min(lines.size() * lineH + contentPad * 2 - 4.0f, ly - 24.0f);

        float px = docHoverX + 10.0f;
        if (px + sizeX > lx - 6.0f) {
            px = std::max(6.0f, docHoverX - 10.0f - sizeX);
        }
        float py = UiRender::clamp(docHoverY - 8.0f, 6.0f, std::max(6.0f, ly - sizeY - 6.0f));

        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = px * UiRender::uiScale;
        UiRender::pivotY = (py + 20.0f) * UiRender::uiScale;

        UiRender::shadow(px, py, sizeX, sizeY, 12.0f, 12.0f, 4.0f, UiTheme::shadow());
        UiRender::roundRect(px, py, sizeX, sizeY, 12.0f, UiTheme::docSurface());
        UiWidgets::overlayPanel(px, py, sizeX, sizeY, 12.0f, UiTheme::docSurface(), 0xF);

        UiRender::enableScissor((int) UiRender::transformX(px), (int) UiRender::transformY(py),
                                (int) std::ceil(UiRender::transformX(px + sizeX)),
                                (int) std::ceil(UiRender::transformY(py + sizeY)));
        for (size_t i = 0; i < lines.size(); i++) {
            UiRender::textY(lines[i], px + contentPad, py + contentPad - 2.0f + i * lineH, lineH,
                            UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }
        UiRender::disableScissor();

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    bool ctrlDown() {
        return ImGui::GetIO().KeyCtrl;
    }

    bool onKeyPress(int key, int action) {
        if (action == 0) {
            return false;
        }
        if (bindingKey != nullptr) {
            std::shared_ptr<KeyValue> target = bindingKey;
            bindingKey = nullptr;
            target->set(UiKeys::isBindKey(key) ? key : -1);
            return true;
        }
        if (recordingShortcut != nullptr) {
            if (UiKeys::modifierKey(key)) {
                return true;
            }
            std::shared_ptr<ShortcutValue> target = recordingShortcut;
            recordingShortcut = nullptr;
            ImGuiIO& io = ImGui::GetIO();
            bool plain = !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
            if (key == ImGuiKey_Escape && plain) {
                return true;
            }
            if ((key == ImGuiKey_Backspace || key == ImGuiKey_Delete) && plain) {
                target->set(std::vector<int>());
                return true;
            }
            target->record(UiKeys::combo(key, io.KeyCtrl, io.KeyShift, io.KeyAlt));
            return true;
        }
        if (hooks.keyPress && hooks.keyPress(key)) {
            return true;
        }
        std::string target = UiInput::focused;
        if (target.empty()) {
            return false;
        }
        if (key == ImGuiKey_Enter || key == ImGuiKey_KeypadEnter) {
            commitInput(target);
            return true;
        }
        if (key == ImGuiKey_Escape && target == INPUT_COLOR) {
            cancelColorEdit();
            return true;
        }
        if (key == ImGuiKey_Escape && target == INPUT_TEXT) {
            cancelTextEdit();
            return true;
        }
        if (!fieldMenuTarget.empty()) {
            fieldMenuTarget.clear();
        }
        UiInput::keyPressed(target, key);
        return true;
    }

    bool onKeyRepeat(int key) {
        std::string target = UiInput::focused;
        if (target.empty()) {
            return false;
        }
        if (key == ImGuiKey_Backspace || key == ImGuiKey_Delete || key == ImGuiKey_LeftArrow ||
            key == ImGuiKey_RightArrow) {
            UiInput::keyPressed(target, key);
        }
        return true;
    }

    bool onCharTyped(char32_t typed) {
        if (hooks.charTyped && hooks.charTyped(typed)) {
            return true;
        }
        std::string target = UiInput::focused;
        if (target.empty()) {
            return false;
        }
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
            return false;
        }
        return UiInput::charTyped(target, typed);
    }

    void commitInput(const std::string& target) {
        if (target == INPUT_TEXT) {
            commitTextEdit();
            return;
        }
        if (target == INPUT_COLOR) {
            commitColorEdit();
            return;
        }
        if (target == INPUT_SLIDER) {
            if (!sliderEditing.empty()) {
                commitSliderEdit(sliderEditing);
            }
            return;
        }
        if (hooks.commitInput) {
            hooks.commitInput(target);
            return;
        }
        UiInput::blur();
    }

    void releaseInput(const std::string& target) {
        if (target == INPUT_TEXT) {
            commitTextEdit();
        } else if (target == INPUT_COLOR) {
            commitColorEdit();
        } else if (target == INPUT_SLIDER && !sliderEditing.empty()) {
            commitSliderEdit(sliderEditing);
        } else if (hooks.releaseInput && hooks.releaseInput(target)) {
            return;
        } else {
            UiInput::blur();
        }
    }

    void drawSelection(const std::string& id, float x, float y, float h, int family, float size) {
        if (!UiInput::hasSelection(id)) {
            return;
        }
        std::string value = UiInput::get(id);
        float from = UiFont::width(Ui::substring(value, 0, UiInput::selectionStart(id)), family, size);
        float to = UiFont::width(Ui::substring(value, 0, UiInput::selectionEnd(id)), family, size);
        UiRender::roundRect(x + from - 1, y, to - from + 2, h, 2.0f, UiTheme::selection());
    }

    void drawCaret(const std::string& id, float x, float y, float h, int family, float size) {
        if (((long long) (caretTimer * 1.6f)) % 2 != 0) {
            return;
        }
        float at = UiFont::width(Ui::substring(UiInput::get(id), 0, UiInput::caret(id)), family, size);
        UiRender::roundRect(x + at, y, 1.2f, h, 0, UiTheme::textPrimary());
    }

    int caretIndexAt(const std::string& id, float textX, float pointerX, int family, float size) {
        std::string value = UiInput::get(id);
        float best = std::fabs(pointerX - textX);
        int index = 0;
        int length = Ui::length(value);
        for (int i = 1; i <= length; i++) {
            float distance = std::fabs(pointerX - (textX + UiFont::width(Ui::substring(value, 0, i), family, size)));
            if (distance < best) {
                best = distance;
                index = i;
            }
        }
        return index;
    }

    void fieldMouse(const std::string& id, float x, float y, float w, float h, float textX, int family, float size,
                    int maxLength) {
        bool inside = hit(x, y, w, h);
        if (inside) {
            UiCursor::text();
        }
        if (UiInput::isFocused(id)) {
            focusFieldLive = true;
            focusFieldX = x;
            focusFieldY = y;
            focusFieldW = w;
            focusFieldH = h;
        }
        if (rightClick && inside) {
            rightClick = false;
            if (!UiInput::isFocused(id)) {
                int keepCaret = UiInput::caret(id);
                int keepAnchor = UiInput::anchor(id);
                UiInput::focus(id, maxLength);
                UiInput::selectRange(id, keepAnchor, keepCaret);
            }
            int index = caretIndexAt(id, textX, mouseX, family, size);
            if (index < UiInput::selectionStart(id) || index > UiInput::selectionEnd(id)) {
                UiInput::moveCaret(id, index, false);
            }
            UiOverlays::open(UiOverlays::fieldMenu);
            fieldMenuTarget = id;
            fieldMenuX = mouseX;
            fieldMenuY = mouseY;
            UiState::remove("fieldMenu#open");
            return;
        }
        if (pendingClick && !clickConsumed && !rightClick && inside) {
            clickConsumed = true;
            pendingClick = false;
            UiInput::focus(id, maxLength);
            int index = caretIndexAt(id, textX, mouseX, family, size);
            long long now = Ui::millis();
            bool sameSpot = id == fieldClickId && now - fieldClickAt < 400;
            fieldClickAt = now;
            fieldClickId = id;
            fieldClicks = sameSpot ? fieldClicks + 1 : 1;
            if (fieldClicks >= 3) {
                UiInput::selectAll(id);
            } else if (fieldClicks == 2) {
                UiInput::selectWordAt(id, index);
            } else {
                UiInput::moveCaret(id, index, UiInput::shiftDown());
                fieldDragId = id;
            }
            return;
        }
        if (!fieldDragId.empty() && fieldDragId == id) {
            if (!mouseDown) {
                fieldDragId.clear();
            } else {
                UiInput::moveCaret(id, caretIndexAt(id, textX, mouseX, family, size), true);
            }
        }
    }

    void fieldMenuLayer() {
        float open = UiState::motionFrom("fieldMenu#open", !fieldMenuTarget.empty() ? 1.0f : 0.0f, 12.0f, 0.0f);
        if (open < 0.002f || fieldMenuTarget.empty()) {
            return;
        }
        std::string id = fieldMenuTarget;
        const char* options[] = {"Undo", "Redo", "Cut", "Copy", "Paste", "Delete", "Select All"};
        const char* shortcuts[] = {"Ctrl+Z", "Ctrl+Y", "Ctrl+X", "Ctrl+C", "Ctrl+V", "", "Ctrl+A"};
        bool selected = UiInput::hasSelection(id);
        bool enabled[] = {UiInput::canUndo(id),
                          UiInput::canRedo(id),
                          selected,
                          selected,
                          !UiInput::filterAllowed(UiInput::clipboard()).empty(),
                          selected,
                          !UiInput::get(id).empty()};

        float rowH = 30.0f;
        float padding = 5.0f;
        float labelW = 0;
        float shortcutW = 0;
        for (int i = 0; i < 7; i++) {
            labelW = std::max(labelW, UiFont::width(options[i], UiFont::MEDIUM, UiWidgets::TEXT_CONTROL));
            shortcutW = std::max(shortcutW, UiFont::width(shortcuts[i], UiFont::MEDIUM, UiWidgets::TEXT_CAPTION));
        }
        float sizeX = 22 + labelW + 26 + shortcutW + 18;
        float sizeY = 7 * rowH + padding * 2 + 10;
        float lx = limitX();
        float ly = limitY();
        float px = UiRender::clamp(fieldMenuX, 6.0f, std::max(6.0f, lx - sizeX - 6.0f));
        float py = fieldMenuY + sizeY > ly - 6.0f ? fieldMenuY - sizeY : fieldMenuY;
        py = UiRender::clamp(py, 6.0f, std::max(6.0f, ly - sizeY - 6.0f));

        float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * open;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
        UiRender::pivotX = px * UiRender::uiScale;
        UiRender::pivotY = py * UiRender::uiScale;

        UiWidgets::menuPanel(px, py, sizeX, sizeY);
        markOverlayUnion(px, py, sizeX, sizeY);
        markAbove(px, py, sizeX, sizeY);

        float cursor = py + padding;
        for (int i = 0; i < 7; i++) {
            if (i == 2 || i == 6) {
                UiRender::hLine(px + 10, px + sizeX - 10, cursor + 2.5f, UiTheme::divider());
                cursor += 5;
            }
            bool rowHover = enabled[i] && hit(px, cursor, sizeX, rowH);
            UiWidgets::menuRowHover("fieldMenu#row" + std::to_string(i), px, cursor, sizeX, rowH, rowHover);
            UiRender::textY(options[i], px + 22, cursor, rowH,
                            enabled[i] ? UiTheme::textPrimary() : UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (shortcuts[i][0] != '\0') {
                float sw = UiFont::width(shortcuts[i], UiFont::MEDIUM, UiWidgets::TEXT_CAPTION);
                UiRender::textY(shortcuts[i], px + sizeX - 18 - sw, cursor, rowH, UiTheme::textFaint(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CAPTION);
            }
            if (enabled[i] && click(px, cursor, sizeX, rowH)) {
                fieldAction(id, i);
                fieldMenuTarget.clear();
            }
            cursor += rowH;
        }

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    void fieldAction(const std::string& id, int index) {
        if (index == 0) {
            UiInput::undo(id);
        } else if (index == 1) {
            UiInput::redo(id);
        } else if (index == 2) {
            UiInput::cut(id);
        } else if (index == 3) {
            UiInput::copy(id);
        } else if (index == 4) {
            UiInput::paste(id);
        } else if (index == 5) {
            UiInput::snapshot(id, false);
            UiInput::deleteSelection(id);
        } else {
            UiInput::selectAll(id);
        }
    }

    bool onMouseClick(int button, int action) {
        if (action == 1 && bindingKey != nullptr && UiKeys::isBindableMouse(button)) {
            std::shared_ptr<KeyValue> target = bindingKey;
            bindingKey = nullptr;
            target->set(UiKeys::mouseKey(button));
            return true;
        }
        if (button == 0) {
            if (action == 1) {
                pendingClick = true;
                mouseDown = true;
                pressSerial++;
            } else if (action == 0) {
                mouseDown = false;
                draggingSlider.clear();
                scrollDragOwner.clear();
            }
        } else if (button == 1 && action == 1) {
            rightClick = true;
        } else if (button == 2 && action == 1) {
            middleClick = true;
        }
        return true;
    }
}
