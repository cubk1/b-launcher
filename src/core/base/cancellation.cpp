#include <core/base/cancellation.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace b_launcher_base
{

namespace detail
{

struct cancel_state
{
  std::atomic<bool> cancelled { false };
  std::mutex mutex;
  std::condition_variable cv;
  std::condition_variable finished;
  std::vector<std::pair<std::uint64_t, std::function<void ()>>> callbacks;
  std::uint64_t next_id = 1;
  std::uint64_t running = 0;
  std::thread::id runner;
};

}

cancellation_token::cancellation_token (
    std::shared_ptr<detail::cancel_state> state)
    : m_state (std::move (state))
{
}

bool
cancellation_token::is_cancelled () const noexcept
{
  return m_state && m_state->cancelled.load (std::memory_order_acquire);
}

bool
cancellation_token::can_be_cancelled () const noexcept
{
  return static_cast<bool> (m_state);
}

result<void>
cancellation_token::check () const
{
  if (is_cancelled ())
    return make_error (errc::cancelled, "operation cancelled");
  return ok ();
}

bool
cancellation_token::wait_for (std::chrono::milliseconds duration) const
{
  if (!m_state)
    {
      std::mutex mutex;
      std::condition_variable cv;
      std::unique_lock lock (mutex);
      cv.wait_for (lock, duration);
      return false;
    }
  std::unique_lock lock (m_state->mutex);
  return m_state->cv.wait_for (lock, duration, [this] {
    return m_state->cancelled.load (std::memory_order_acquire);
  });
}

cancellation_registration
cancellation_token::on_cancel (std::function<void ()> callback) const
{
  if (!m_state)
    return {};
  std::unique_lock lock (m_state->mutex);
  if (m_state->cancelled.load (std::memory_order_acquire))
    {
      lock.unlock ();
      callback ();
      return {};
    }
  auto id = m_state->next_id++;
  m_state->callbacks.emplace_back (id, std::move (callback));
  return cancellation_registration (m_state, id);
}

cancellation_source::cancellation_source ()
    : m_state (std::make_shared<detail::cancel_state> ())
{
}

void
cancellation_source::cancel ()
{
  std::unique_lock lock (m_state->mutex);
  if (m_state->cancelled.exchange (true, std::memory_order_acq_rel))
    return;
  m_state->cv.notify_all ();
  m_state->runner = std::this_thread::get_id ();
  while (!m_state->callbacks.empty ())
    {
      auto entry = std::move (m_state->callbacks.front ());
      m_state->callbacks.erase (m_state->callbacks.begin ());
      m_state->running = entry.first;
      lock.unlock ();
      try
        {
          entry.second ();
        }
      catch (...)
        {
        }
      entry.second = nullptr;
      lock.lock ();
      m_state->running = 0;
      m_state->finished.notify_all ();
    }
}

bool
cancellation_source::is_cancelled () const noexcept
{
  return m_state->cancelled.load (std::memory_order_acquire);
}

cancellation_token
cancellation_source::token () const
{
  return cancellation_token (m_state);
}

cancellation_registration::cancellation_registration (
    std::shared_ptr<detail::cancel_state> state, std::uint64_t id)
    : m_state (std::move (state))
    , m_id (id)
{
}

cancellation_registration::cancellation_registration (
    cancellation_registration &&other) noexcept
    : m_state (std::move (other.m_state))
    , m_id (other.m_id)
{
  other.m_id = 0;
}

cancellation_registration &
cancellation_registration::operator= (
    cancellation_registration &&other) noexcept
{
  if (this != &other)
    {
      reset ();
      m_state = std::move (other.m_state);
      m_id = other.m_id;
      other.m_id = 0;
    }
  return *this;
}

cancellation_registration::~cancellation_registration ()
{
  reset ();
}

void
cancellation_registration::reset ()
{
  if (!m_state)
    return;
  {
    std::unique_lock lock (m_state->mutex);
    std::erase_if (m_state->callbacks,
                   [this] (const auto &entry) { return entry.first == m_id; });
    if (m_state->running == m_id
        && m_state->runner != std::this_thread::get_id ())
      m_state->finished.wait (lock,
                              [this] { return m_state->running != m_id; });
  }
  m_state.reset ();
  m_id = 0;
}

}
