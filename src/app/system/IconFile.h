#pragma once

#include <string>
#include <vector>

namespace IconFile {

    std::vector<unsigned char> square(const std::vector<unsigned char>& rgba, int width, int height, int size);
    std::string ico(const std::vector<unsigned char>& rgba, int width, int height);
}
