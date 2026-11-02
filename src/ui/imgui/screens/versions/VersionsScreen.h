#pragma once

namespace VersionsScreen {

    // Called when the page opens so a stale query does not hide the version the player came for.
    void open();
    void draw();
}
