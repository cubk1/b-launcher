#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace b_launcher_base
{

class instance_channel
{
public:
  instance_channel ();
  ~instance_channel ();
  instance_channel (const instance_channel &) = delete;
  instance_channel &operator= (const instance_channel &) = delete;

  // True when an existing instance accepts the message. Otherwise try to listen.
  bool forward_or_listen (const std::filesystem::path &directory,
                          std::string_view message);
  std::optional<std::string> receive ();
  bool listening () const;
  void reset ();

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

}
