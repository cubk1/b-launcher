#pragma once

namespace Network {

    bool systemProxy();
    void setSystemProxy(bool on);
    bool offlineLaunch();
    void setOfflineLaunch(bool on);
    // Starts the next launch offline without touching the saved setting, for one launch only.
    void setOfflineOnce(bool on);
    // Reads that one-shot request and clears it, so the next launch online is online again.
    bool takeOfflineOnce();
    void apply();
}
