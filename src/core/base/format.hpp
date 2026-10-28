#pragma once

#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace b_launcher_base
{

namespace detail
{

struct format_arg
{
  enum class kind : std::uint8_t
  {
    text,
    signed_integer,
    unsigned_integer,
    floating,
    boolean,
    character,
  };

  kind type = kind::text;
  std::string_view text;
  std::int64_t signed_value = 0;
  std::uint64_t unsigned_value = 0;
  double floating_value = 0;
};

template <typename T>
format_arg
make_format_arg (const T &value)
{
  format_arg arg;
  using type = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<type, bool>)
    {
      arg.type = format_arg::kind::boolean;
      arg.unsigned_value = value ? 1 : 0;
    }
  else if constexpr (std::is_same_v<type, char>)
    {
      arg.type = format_arg::kind::character;
      arg.unsigned_value = static_cast<unsigned char> (value);
    }
  else if constexpr (std::is_enum_v<type>)
    {
      arg.type = format_arg::kind::signed_integer;
      arg.signed_value = static_cast<std::int64_t> (value);
    }
  else if constexpr (std::is_integral_v<type> && std::is_signed_v<type>)
    {
      arg.type = format_arg::kind::signed_integer;
      arg.signed_value = value;
    }
  else if constexpr (std::is_integral_v<type>)
    {
      arg.type = format_arg::kind::unsigned_integer;
      arg.unsigned_value = value;
    }
  else if constexpr (std::is_floating_point_v<type>)
    {
      arg.type = format_arg::kind::floating;
      arg.floating_value = static_cast<double> (value);
    }
  else
    {
      arg.type = format_arg::kind::text;
      arg.text = std::string_view (value);
    }
  return arg;
}

std::string vformat (std::string_view pattern,
                     std::span<const format_arg> args);

}

template <typename... Args>
std::string
sformat (std::string_view pattern, const Args &...args)
{
  if constexpr (sizeof...(Args) == 0)
    return detail::vformat (pattern, {});
  else
    {
      const detail::format_arg list[] = { detail::make_format_arg (args)... };
      return detail::vformat (pattern, list);
    }
}

}
