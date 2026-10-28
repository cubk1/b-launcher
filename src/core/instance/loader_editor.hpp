#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/installer/loader.hpp>
#include <core/instance/instance_manager.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::instance
{

enum class choice_state : std::uint8_t
{
  installed,
  available,
  incompatible,
  requires_loader,
};

struct loader_choice
{
  install::loader_type type = install::loader_type::fabric;
  choice_state state = choice_state::available;
  std::string installed_version;
  std::string related;
};

std::string_view choice_state_name (choice_state state) noexcept;
std::vector<install::loader_type>
supported_loaders (std::string_view minecraft);
std::vector<loader_choice>
loader_choices (std::string_view minecraft,
                std::span<const install::loader_info> current);
std::vector<std::string>
loader_warnings (std::span<const install::loader_info> current);
result<std::vector<install::loader_info>>
installed_loaders (const instance_manager &manager, std::string_view id);
result<std::string>
modify_loaders (install::game_installer &installer,
                const instance_manager &manager, std::string_view id,
                std::span<const install::loader_request> desired,
                progress_sink *sink = nullptr, cancellation_token token = {});

}
