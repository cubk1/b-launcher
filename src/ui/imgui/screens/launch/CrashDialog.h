#pragma once

#include <memory>

namespace Crashes {
    struct Report;
}

namespace CrashDialog {

    void open(std::shared_ptr<const Crashes::Report> report);
}
