#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base
{

enum class output_stream : std::uint8_t
{
  out,
  err,
};

using output_callback = std::function<void (std::string_view, output_stream)>;

struct process_options
{
  std::vector<std::string> args;
  std::filesystem::path working_directory;
  std::vector<std::pair<std::string, std::string>> environment;
  bool capture_output = true;
};

struct process_result
{
  int exit_code = -1;
  std::string out;
  std::string err;
};

class process
{
public:
  process () = default;
  process (process &&other) noexcept;
  process &operator= (process &&other) noexcept;
  process (const process &) = delete;
  process &operator= (const process &) = delete;
  ~process ();

  static result<process> start (const process_options &options,
                                output_callback on_output = {});

  int pid () const noexcept;
  bool valid () const noexcept;
  bool running ();
  result<int> wait (std::optional<std::chrono::milliseconds> timeout
                    = std::nullopt);
  result<void> terminate ();
  result<void> kill ();
  void detach ();

private:
  struct impl;
  std::shared_ptr<impl> m_impl;
};

result<process_result>
run_process (const process_options &options, cancellation_token token = {},
             std::optional<std::chrono::milliseconds> timeout = std::nullopt,
             output_callback on_output = {});

}
