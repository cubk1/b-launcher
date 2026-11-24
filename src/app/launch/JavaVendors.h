#pragma once

#include <core/java/java_detect.hpp>

#include <string>

namespace JavaVendors {

    struct Vendor {
        std::string icon;
        std::string name;
    };

    Vendor of(const b_launcher_base::java::java_installation& java);
    std::string title(const b_launcher_base::java::java_installation& java);
}
