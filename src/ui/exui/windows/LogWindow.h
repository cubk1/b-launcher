#pragma once

namespace ForgottenMelody {

    // The game log, in a window of its own beside the main window that stays usable under the dialogs; brought
    // forward when it is open already.
    void showLog();
    // Closes it, for when the launcher quits.
    void closeLog();
}
