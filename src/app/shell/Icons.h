#pragma once

#include <cstdint>

namespace b_launcher_base::install {
    enum class loader_type : std::uint8_t;
}

namespace Icons {

    struct Encoded {
        const unsigned char* data = nullptr;
        unsigned int size = 0;
    };

    Encoded encoded(b_launcher_base::install::loader_type type);
    Encoded encodedVanilla();

    uint32_t forge();
    uint32_t vanilla();
    uint32_t loader(b_launcher_base::install::loader_type type);
    uint32_t iris();
    uint32_t canvas();
    uint32_t hmcl();
    uint32_t pcl();
    uint32_t backgroundSidebar();
    uint32_t backgroundPanel();
}
