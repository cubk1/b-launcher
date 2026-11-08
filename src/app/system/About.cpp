#include "app/system/About.h"

#include "GitCommit.h"

namespace About {

    std::string versionLabel() {
        std::string label = std::string("v") + VERSION;
        if (*B_LAUNCHER_COMMIT != '\0') {
            label += std::string(" (") + B_LAUNCHER_COMMIT + ")";
        }
        return label;
    }
}
