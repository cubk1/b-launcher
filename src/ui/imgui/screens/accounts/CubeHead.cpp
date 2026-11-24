#include "ui/imgui/screens/accounts/CubeHead.h"
#include "app/accounts/Avatars.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/screens/shell/Textures.h"

namespace CubeHead {

    bool draw(const b_launcher_base::auth::account& account, float x, float y, float size) {
        uint32_t head = Textures::of(Avatars::cube(account));
        if (head == 0) {
            return false;
        }
        UiRender::texture(head, x, y, size, size);
        return true;
    }
}
