#include "app/system/DiskScan.h"

namespace DiskScan {

    namespace {
        Status current;
        std::vector<Drive> none;
    }

    const std::vector<Drive>& drives() {
        return none;
    }

    bool drivesReady() {
        return true;
    }

    bool driveSelected(wchar_t) {
        return false;
    }

    void selectDrive(wchar_t, bool) {}

    bool anySelected() {
        return false;
    }

    void start(Kind) {}

    const Status& status() {
        return current;
    }

    std::optional<Helper> helperArgument() {
        return std::nullopt;
    }

    int runHelper(const Helper&) {
        return 1;
    }

    void poll() {}

    void shutdown() {}
}
