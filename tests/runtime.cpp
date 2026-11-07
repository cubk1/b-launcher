#include <core/base/platform.hpp>
#include <core/base/process.hpp>
#include <core/base/strings.hpp>
#include <core/java/java_detect.hpp>
#include <core/java/java_requirement.hpp>
#include <core/java/java_runtime.hpp>
#include <core/launcher/launcher.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

extern char **environ;
namespace blb = b_launcher_base;
namespace fs = std::filesystem;

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

struct no_network final : blb::net::mirror_provider
{
    mutable int calls = 0;
    std::string_view name() const override { return "test"; }
    std::vector<std::string> resolve(std::string_view) const override
    {
        ++calls;
        return {};
    }
};

int main(int argc, char **argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--wrapper") {
        setenv("BLB_TEST_WRAPPED", "1", 1);
        execv(argv[2], argv + 2);
        return 127;
    }
    if (fs::path(argv[0]).filename() == "java") {
        if (std::getenv("BLB_TEST_JAVA_FAIL")) return 19;
        if (std::getenv("BLB_TEST_NEEDS_WRAPPER") && !std::getenv("BLB_TEST_WRAPPED")) return 17;
        std::cerr << "    java.version = 21.0.1\n    os.arch = "
                  << blb::arch_name(blb::current_platform().arch) << '\n';
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--environment") {
        int count = 0;
        for (char **p = environ; p && *p; ++p)
            if (std::string_view(*p).starts_with("BLB_TEST_VALUE=")) ++count;
        std::cout << count << ':' << std::getenv("BLB_TEST_VALUE") << ':'
                  << std::getenv("BLB_TEST_PARENT");
        return 0;
    }
    auto self = fs::canonical(argv[0]);
    auto root = fs::temp_directory_path() / ("b-launcher-tests-" + std::to_string(getpid()));
    fs::create_directories(root / "jdk" / "bin");
    try {
        setenv("BLB_TEST_PARENT", "inherited", 1);
        setenv("BLB_TEST_VALUE", "parent", 1);
        blb::process_options process;
        process.args = {self.string(), "--environment"};
        process.environment = {{"BLB_TEST_VALUE", "first"}, {"BLB_TEST_VALUE", "last"}};
        auto output = blb::run_process(process);
        require(output && output->exit_code == 0 && output->out == "1:last:inherited\n",
                "duplicate child variables must use the last value and preserve inherited variables");

        fs::create_symlink(self, root / "jdk" / "bin" / "java");
        std::ofstream(root / "jdk" / "release") << "JAVA_VERSION=\"99\"\nOS_ARCH=\""
                << blb::arch_name(blb::current_platform().arch) << "\"\n";
        auto java = blb::java::probe_java(root / "jdk", true, {}, {}, true);
        require(java && java->major == 21, "manual Java validation must read the running process version");
        setenv("BLB_TEST_JAVA_FAIL", "1", 1);
        require(!blb::java::probe_java(root / "jdk", true, {}, {}, true),
                "a release file must not hide Java execution failure");
        unsetenv("BLB_TEST_JAVA_FAIL");

        setenv("BLB_TEST_NEEDS_WRAPPER", "1", 1);
        blb::java::scan_options scan;
        scan.system_locations = false;
        scan.extra_homes = {root / "jdk"};
        scan.wrapper = {self.string(), "--wrapper"};
        auto found = blb::java::scan_java(scan);
        require(found.size() == 1 && found.front().major == 21,
                "Java scans must pass the instance wrapper to the probe");
        unsetenv("BLB_TEST_NEEDS_WRAPPER");

        auto local = *java;
        local.major = 21;
        local.source = blb::java::java_source::system;
        auto path = local;
        path.major = 25;
        path.source = blb::java::java_source::path;
        blb::java::java_requirement requirement;
        requirement.min_major = requirement.preferred_major = 21;
        std::vector<blb::java::java_installation> choices{local, path};
        auto chosen = blb::java::select_java(choices, requirement);
        require(chosen && chosen->major == (blb::is_nixos() ? 25 : 21),
                "NixOS must prefer suitable Java from the environment");
        choices[1].major = 8;
        chosen = blb::java::select_java(choices, requirement);
        require(chosen && chosen->major == 21, "environment priority must respect the game requirement");

        auto home = blb::home_directory();
        auto saved = blb::environment("XDG_DATA_HOME");
        unsetenv("XDG_DATA_HOME");
        require(blb::launcher_data_directory() == home / ".local/share/b-launcher", "default data directory");
        setenv("XDG_DATA_HOME", "relative", 1);
        require(blb::launcher_data_directory() == home / ".local/share/b-launcher", "relative XDG value");
        setenv("XDG_DATA_HOME", root.c_str(), 1);
        require(blb::launcher_data_directory() == root / "b-launcher", "absolute XDG value");
        if (saved) setenv("XDG_DATA_HOME", saved->c_str(), 1); else unsetenv("XDG_DATA_HOME");

        require(blb::java::runtime_blocked(21).has_value() == blb::is_nixos(),
                "runtime download policy must depend on the distribution");
        if (blb::is_nixos()) {
            auto mirror = std::make_shared<no_network>();
            blb::install::install_options options;
            options.mirror = mirror;
            options.java_scan.system_locations = false;
            options.java_candidates = std::vector<blb::java::java_installation>{};
            auto installer = std::make_shared<blb::install::game_installer>(
                    blb::meta::game_layout(root / "game"), options);
            require(!blb::java::install_runtime(*installer, "java-runtime-delta"),
                    "NixOS must reject direct runtime downloads");
            blb::launch::game_launcher launcher(installer);
            blb::meta::version version;
            version.release_time = "2025-01-01";
            blb::launch::launch_settings settings;
            settings.wrapper = {self.string(), "--wrapper"};
            auto missing = launcher.resolve_java(version, settings);
            require(!missing && missing.err().message == "在系统配置声明以安装 Java",
                    "missing local Java must show the NixOS installation hint");
            require(mirror->calls == 0, "NixOS must reject Java downloads before network access");
        }
        fs::remove_all(root);
        std::cout << "runtime regression tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
