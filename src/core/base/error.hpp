#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace b_launcher_base
{

enum class errc : std::uint16_t
{
  ok = 0,
  unknown,
  invalid_argument,
  io,
  not_found,
  already_exists,
  parse,
  network,
  http_status,
  checksum_mismatch,
  size_mismatch,
  cancelled,
  timeout,
  unsupported,
  process,
  zip,
  auth,
  auth_pending,
  auth_declined,
  auth_expired,
  no_xbox_account,
  child_account,
  no_minecraft_profile,
  java_not_found,
  version_not_found,
  loader_not_found,
  incompatible,
};

std::string_view errc_name (errc code) noexcept;

struct error
{
  errc code = errc::unknown;
  std::string message;

  std::string to_string () const;
};

inline error
make_error (errc code, std::string message = {})
{
  return error { code, std::move (message) };
}

template <typename T>
class [[nodiscard]] result
{
public:
  using value_type = T;

  result ()
    requires std::is_default_constructible_v<T>
      : m_data (std::in_place_index<0>)
  {
  }

  result (const T &value)
      : m_data (std::in_place_index<0>, value)
  {
  }

  result (T &&value)
      : m_data (std::in_place_index<0>, std::move (value))
  {
  }

  template <typename U>
    requires (std::is_constructible_v<T, U &&>
              && !std::is_same_v<std::remove_cvref_t<U>, T>
              && !std::is_same_v<std::remove_cvref_t<U>, error>
              && !std::is_same_v<std::remove_cvref_t<U>, result>)
  result (U &&value)
      : m_data (std::in_place_index<0>, std::forward<U> (value))
  {
  }

  result (error err)
      : m_data (std::in_place_index<1>, std::move (err))
  {
  }

  bool
  has_value () const noexcept
  {
    return m_data.index () == 0;
  }

  explicit
  operator bool () const noexcept
  {
    return has_value ();
  }

  T &
  value () &
  {
    return std::get<0> (m_data);
  }

  const T &
  value () const &
  {
    return std::get<0> (m_data);
  }

  T
  value () &&
  {
    return std::get<0> (std::move (m_data));
  }

  T &
  operator* () &
  {
    return value ();
  }

  const T &
  operator* () const &
  {
    return value ();
  }

  T
  operator* () &&
  {
    return std::move (*this).value ();
  }

  T *
  operator->()
  {
    return &value ();
  }

  const T *
  operator->() const
  {
    return &value ();
  }

  template <typename U>
  T
  value_or (U &&fallback) const &
  {
    return has_value () ? value ()
                        : static_cast<T> (std::forward<U> (fallback));
  }

  const error &
  err () const &
  {
    return std::get<1> (m_data);
  }

  error
  err () &&
  {
    return std::get<1> (std::move (m_data));
  }

private:
  std::variant<T, error> m_data;
};

template <>
class [[nodiscard]] result<void>
{
public:
  using value_type = void;

  result () = default;

  result (error err)
      : m_error (std::move (err))
  {
  }

  bool
  has_value () const noexcept
  {
    return !m_error.has_value ();
  }

  explicit
  operator bool () const noexcept
  {
    return has_value ();
  }

  const error &
  err () const &
  {
    return *m_error;
  }

  error
  err () &&
  {
    return std::move (*m_error);
  }

private:
  std::optional<error> m_error;
};

inline result<void>
ok ()
{
  return {};
}

}

#define BLB_CONCAT_IMPL(a, b) a##b
#define BLB_CONCAT(a, b) BLB_CONCAT_IMPL (a, b)

#define BLB_TRY(expr)                                                         \
  do                                                                          \
    {                                                                         \
      auto blb_try_result_ = (expr);                                          \
      if (!blb_try_result_)                                                   \
        return std::move (blb_try_result_).err ();                            \
    }                                                                         \
  while (0)

#define BLB_TRY_ASSIGN_IMPL(tmp, decl, expr)                                  \
  auto tmp = (expr);                                                          \
  if (!tmp)                                                                   \
    return std::move (tmp).err ();                                            \
  decl = std::move (*tmp)

#define BLB_TRY_ASSIGN(decl, expr)                                            \
  BLB_TRY_ASSIGN_IMPL (BLB_CONCAT (blb_try_value_, __LINE__), decl, expr)
