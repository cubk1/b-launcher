#pragma once

#include <core/base/error.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace b_launcher_base
{

namespace detail
{
struct cancel_state;
}

class cancellation_registration;

class cancellation_token
{
public:
  cancellation_token () = default;

  bool is_cancelled () const noexcept;
  bool can_be_cancelled () const noexcept;
  result<void> check () const;
  bool wait_for (std::chrono::milliseconds duration) const;
  cancellation_registration on_cancel (std::function<void ()> callback) const;

private:
  friend class cancellation_source;
  explicit cancellation_token (std::shared_ptr<detail::cancel_state> state);

  std::shared_ptr<detail::cancel_state> m_state;
};

class cancellation_source
{
public:
  cancellation_source ();

  void cancel ();
  bool is_cancelled () const noexcept;
  cancellation_token token () const;

private:
  std::shared_ptr<detail::cancel_state> m_state;
};

class cancellation_registration
{
public:
  cancellation_registration () = default;
  cancellation_registration (cancellation_registration &&other) noexcept;
  cancellation_registration &
  operator= (cancellation_registration &&other) noexcept;
  cancellation_registration (const cancellation_registration &) = delete;
  cancellation_registration &operator= (const cancellation_registration &)
      = delete;
  ~cancellation_registration ();

  void reset ();

private:
  friend class cancellation_token;
  cancellation_registration (std::shared_ptr<detail::cancel_state> state,
                             std::uint64_t id);

  std::shared_ptr<detail::cancel_state> m_state;
  std::uint64_t m_id = 0;
};

}
