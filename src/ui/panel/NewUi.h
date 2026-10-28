#pragma once

#include "../value/Setting.h"
#include "../value/Values.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class ContextMenu;
class MenuItem;

namespace NewUi {

    extern const std::string INPUT_COLOR;
    extern const std::string INPUT_TEXT;
    extern const std::string INPUT_SLIDER;

    extern float mouseX;
    extern float mouseY;
    extern bool clickConsumed;
    extern bool pendingClick;
    extern bool mouseDown;
    extern long long pressSerial;
    extern long long pickerOpenedPress;
    extern long long popupOpenedPress;
    extern bool rightClick;
    extern bool middleClick;
    extern std::string draggingSlider;
    extern std::string sliderEditing;
    extern bool focusFieldLive;
    extern float focusFieldX;
    extern float focusFieldY;
    extern float focusFieldW;
    extern float focusFieldH;
    extern std::set<std::string> subInlinePicker;
    extern long long subOpenedFrame;
    extern bool drawingOverlay;
    extern bool drawingDialog;
    extern bool overlayActive;
    extern float overlayX;
    extern float overlayY;
    extern float overlayW;
    extern float overlayH;
    extern bool nextOverlayActive;
    extern float nextOverlayX;
    extern float nextOverlayY;
    extern float nextOverlayW;
    extern float nextOverlayH;
    extern std::string tooltipText;
    extern float tooltipX;
    extern float tooltipY;
    extern std::string tooltipWaiting;
    extern float tooltipWaited;

    extern bool popupOpen;
    extern bool popupMulti;
    extern std::string popupOwner;
    extern std::string popupSource;
    extern std::shared_ptr<Choice> popupChoice;
    extern std::shared_ptr<NumberValue> sliderEditValue;
    extern std::vector<int> windowOrder;
    extern int drawingWindow;
    extern float popupAnchorX;
    extern float popupAnchorY;
    extern float popupWidth;
    extern float popupScroll;
    extern float popupScrollSmooth;
    extern int popupDragFrom;
    extern float popupDragGrab;
    extern std::string subOwner;
    extern std::string subParent;
    extern std::unordered_map<std::string, SettingsPagePtr> subPages;
    extern float subParentX;
    extern float subParentY;
    extern std::shared_ptr<NumberValue> hoveredSlider;
    extern std::shared_ptr<ColorValue> hoveredColor;
    extern std::string hoveredColorChannel;
    extern std::shared_ptr<KeyValue> bindingKey;
    extern std::shared_ptr<ShortcutValue> recordingShortcut;
    extern std::string textEditing;
    extern std::shared_ptr<TextValue> textEditValue;
    extern std::string scrollDragOwner;
    extern float scrollDragOffset;
    extern std::string colorEditing;
    extern std::shared_ptr<ColorValue> colorEditValue;
    extern int colorEditingCell;
    extern std::string colorCopiedOwner;
    extern long long colorCopiedAt;
    extern uint32_t colorClipboard;
    extern bool colorClipboardSet;
    extern std::string swatchMenuOwner;
    extern std::shared_ptr<ColorValue> swatchMenuColor;
    extern float swatchMenuX;
    extern float swatchMenuY;
    extern float pickerRectX;
    extern float pickerRectY;
    extern float pickerRectW;
    extern float pickerRectH;
    extern float swatchRectX;
    extern float swatchRectY;
    extern float swatchRectW;
    extern float swatchRectH;
    extern float popupRectX;
    extern float popupRectY;
    extern float popupRectW;
    extern float popupRectH;
    extern float subPanelRight;
    extern float subPanelTop;
    extern float subPanelX;
    extern float subPanelY;
    extern float subPanelW;
    extern float subPanelH;
    extern bool subPanelDismiss;
    extern bool aboveActive;
    extern float aboveX;
    extern float aboveY;
    extern float aboveW;
    extern float aboveH;
    extern bool nextAboveActive;
    extern float nextAboveX;
    extern float nextAboveY;
    extern float nextAboveW;
    extern float nextAboveH;
    extern float subAnchorX;
    extern float subAnchorY;
    extern std::string subChildOwner;
    extern bool subPanelDrawing;
    extern std::string pickerOwner;
    extern std::shared_ptr<ColorValue> pickerColor;
    extern float pickerX;
    extern float pickerY;
    extern int pickerMode;
    extern std::string pickerModeOwner;
    extern ContextMenu& swatchContextMenu;
    extern ContextMenu& pickerModeMenu;
    extern std::string menuOwner;
    extern float menuX;
    extern float menuY;
    extern float menuMinWidth;
    extern std::function<std::vector<std::shared_ptr<MenuItem>>()> menuItems;
    extern ContextMenu& contextMenu;
    extern long long popupOpenedFrame;
    extern long long pickerOpenedFrame;
    extern long long frameCounter;
    extern float caretTimer;
    extern std::unordered_map<std::string, float> fieldOffset;
    extern std::string fieldMenuTarget;
    extern float fieldMenuX;
    extern float fieldMenuY;
    extern std::string fieldDragId;
    extern std::string fieldClickId;
    extern long long fieldClickAt;
    extern int fieldClicks;
    extern std::string docHoverTarget;
    extern std::string docHoverText;
    extern std::string docHoverNext;
    extern std::string docHoverNextText;
    extern float docHoverX;
    extern float docHoverY;
    extern float docHoverTime;

    struct Window {
        int id;
        std::function<bool()> open;
        std::function<bool(float x, float y)> contains;
        std::function<void()> draw;
    };

    struct Hooks {
        std::function<void(const std::string& id)> commitInput;
        std::function<bool(const std::string& id)> releaseInput;
        std::function<bool(int key)> keyPress;
        std::function<bool(char32_t typed)> charTyped;
        std::function<void()> onCloseAllOverlays;
    };

    extern Hooks hooks;
    extern std::vector<Window> windows;

    extern bool overlaysEnabled;

    struct FrameState {
        bool overlayActive = false;
        float overlayX = 0, overlayY = 0, overlayW = 0, overlayH = 0;
        bool nextOverlayActive = false;
        float nextOverlayX = 0, nextOverlayY = 0, nextOverlayW = 0, nextOverlayH = 0;
        bool aboveActive = false;
        float aboveX = 0, aboveY = 0, aboveW = 0, aboveH = 0;
        bool nextAboveActive = false;
        float nextAboveX = 0, nextAboveY = 0, nextAboveW = 0, nextAboveH = 0;
        bool focusFieldLive = false;
        float focusFieldX = 0, focusFieldY = 0, focusFieldW = 0, focusFieldH = 0;
        bool mouseDown = false;
        std::string draggingSlider;
        std::string scrollDragOwner;
        std::string tooltipText;
        float tooltipX = 0, tooltipY = 0;
        std::string tooltipWaiting;
        float tooltipWaited = 0;
    };
    FrameState saveFrameState();
    void restoreFrameState(const FrameState& state);

    void beginFrame();
    void endFrame();
    void render(const std::function<void()>& content);

    std::vector<int>& windowList();
    bool windowOpen(int id);
    bool windowContains(int id, float x, float y);
    int windowAt(float x, float y);
    void bringWindowToFront(int id);
    void sendWindowBack(int id);
    bool hit(float x, float y, float w, float h);
    bool windowBlocks();
    bool rawHit(float x, float y, float w, float h);
    bool overlayCoversMouse();
    void markAbove(float x, float y, float w, float h);
    bool overlayBlocks(float x, float y);
    void markOverlayUnion(float x, float y, float w, float h);
    void markOverlay(float x, float y, float w, float h);
    bool click(float x, float y, float w, float h);
    void closeAllOverlays();

    float fieldScroll(const std::string& id, float room, int family, float size, bool focused);
    std::string fitControl(const std::string& label, float available);
    std::string fit(const std::string& label, float available);
    void commitSliderEdit(const std::string& id);
    std::string formatNumber(double value);
    std::string formatStep(double value, double step);
    std::string formatValue(NumberValue& value);
    std::string formatRaw(NumberValue& value);
    std::string formatCompact(double value);

    void openChoice(const std::shared_ptr<Choice>& choice, float anchorX, float anchorY, float width,
                    const std::string& source = "");
    float centerScrollFor(const std::shared_ptr<Choice>& choice);
    float listScrollbar(const std::string& owner, float trackX, float trackY, float trackH, float viewH,
                        float maxScroll, float shown, float scroll);
    void tooltipLayer();
    void drawTooltip();
    void popupLayer();
    void popupRow(const std::string& option, float rx, float ry, float rowW, float textIndent, float checkColumn,
                  float hover, bool grip);

    void openSettings(const SettingsPagePtr& page, float anchorX, float anchorY);
    void openSettingsPicker(const SettingsPagePtr& page, float anchorX, float anchorY);
    SettingsPagePtr settingsPage(const std::string& id);
    SettingPtr firstColorChild(const std::string& owner);
    SettingList subChildren(const std::string& owner);
    std::string subTitle(const std::string& owner);
    void toggleSubPanel(const std::string& owner, float anchorX, float anchorY);
    void subLayer();
    void subLayerPanels();
    void subPanelBody(float open);
    void subRow(const SettingPtr& setting, float x, float y, float w, float h);
    void subRowBody(const SettingPtr& setting, float x, float y, float w, float h);

    void beginColorEdit(const std::shared_ptr<ColorValue>& color, int cell, const std::string& value);
    void cancelColorEdit();
    void commitColorEdit();
    bool scrollColor(double amount);
    void beginSliderEdit(const std::string& id, const std::string& value);
    void beginTextEdit(const std::shared_ptr<TextValue>& value);
    void cancelTextEdit();
    void commitTextEdit();
    void openPicker(const std::shared_ptr<ColorValue>& color, float x, float y);
    void openSwatchMenu(const std::shared_ptr<ColorValue>& color, float x, float y);
    void swatchMenuLayer();
    void colorPickerLayer();
    bool insideRect(bool active, float x, float y, float w, float h);
    std::string pickerModeName();
    void pickerCursor(float cx, float cy, uint32_t argb);
    void pickerHandle(float cx, float cy, uint32_t argb);
    std::vector<std::shared_ptr<MenuItem>> pickerModes();
    void pickerModeLayer(const std::string& owner, float x, float baseYPos);

    void openMenu(const std::string& owner, std::function<std::vector<std::shared_ptr<MenuItem>>()> items, float x,
                  float y, float minWidth = 160.0f);
    void openMenu(const std::string& owner, const std::vector<std::shared_ptr<MenuItem>>& items, float x, float y,
                  float minWidth = 160.0f);
    void closeMenu();
    void menuLayer();

    void openLink(const std::string& url);
    void markDoc(const std::string& target, const std::string& text, float anchorX, float rowY);
    void advanceDocHover();
    void docLayer();
    bool ctrlDown();
    bool onKeyPress(int key, int action);
    bool onKeyRepeat(int key);
    bool onCharTyped(char32_t typed);
    void commitInput(const std::string& target);
    void releaseInput(const std::string& target);
    void drawSelection(const std::string& id, float x, float y, float h, int family, float size);
    void drawCaret(const std::string& id, float x, float y, float h, int family, float size);
    int caretIndexAt(const std::string& id, float textX, float pointerX, int family, float size);
    void fieldMouse(const std::string& id, float x, float y, float w, float h, float textX, int family, float size,
                    int maxLength);
    void fieldMenuLayer();
    void fieldAction(const std::string& id, int index);
    bool onMouseClick(int button, int action);
    bool onMouseScroll(double amount);
    void pollInput();
    float limitX();
    float limitY();
}
