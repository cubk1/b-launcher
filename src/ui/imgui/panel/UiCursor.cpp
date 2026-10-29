#include "UiCursor.h"

#include "imgui.h"

namespace UiCursor {

    int wanted = ImGuiMouseCursor_Arrow;
    int applied = ImGuiMouseCursor_Arrow;

    void text() {
        wanted = ImGuiMouseCursor_TextInput;
    }

    void hand() {
        wanted = ImGuiMouseCursor_Hand;
    }

    void resize() {
        wanted = ImGuiMouseCursor_ResizeNWSE;
    }

    void apply() {
        int shape = wanted;
        wanted = ImGuiMouseCursor_Arrow;
        applied = shape;
        if (shape != ImGuiMouseCursor_Arrow) {
            ImGui::SetMouseCursor(shape);
        }
    }
}
