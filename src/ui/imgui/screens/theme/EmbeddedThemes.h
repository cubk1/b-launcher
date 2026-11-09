#pragma once

namespace EmbeddedThemes {

    struct File {
        const char* path;
        const unsigned char* data;
        unsigned int size;
    };

    extern const File files[];
    extern const unsigned int count;
}
