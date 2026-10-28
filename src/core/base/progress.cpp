#include <core/base/progress.hpp>

namespace b_launcher_base
{

namespace
{

std::mutex &
sink_mutex ()
{
  static std::mutex mutex;
  return mutex;
}

std::int64_t
now_ticks ()
{
  return std::chrono::steady_clock::now ().time_since_epoch ().count ();
}

}

progress_sink::~progress_sink () = default;

void
progress_sink::on_stage (std::string_view, std::string_view)
{
}

function_progress_sink::function_progress_sink (progress_fn on_progress,
                                                stage_fn on_stage)
    : m_progress (std::move (on_progress))
    , m_stage (std::move (on_stage))
{
}

void
function_progress_sink::on_progress (const progress_event &event)
{
  if (m_progress)
    m_progress (event);
}

void
function_progress_sink::on_stage (std::string_view task,
                                  std::string_view stage)
{
  if (m_stage)
    m_stage (task, stage);
}

progress_tracker::progress_tracker (progress_sink *sink, std::string task,
                                    std::chrono::milliseconds interval)
    : m_sink (sink)
    , m_task (std::move (task))
    , m_interval (interval)
    , m_speed_time (std::chrono::steady_clock::now ())
{
}

void
progress_tracker::add_total (std::uint64_t bytes, std::uint64_t items) noexcept
{
  m_total_bytes.fetch_add (bytes, std::memory_order_relaxed);
  m_total_items.fetch_add (items, std::memory_order_relaxed);
}

void
progress_tracker::add_done (std::uint64_t bytes) noexcept
{
  m_done_bytes.fetch_add (bytes, std::memory_order_relaxed);
}

void
progress_tracker::sub_done (std::uint64_t bytes) noexcept
{
  m_done_bytes.fetch_sub (bytes, std::memory_order_relaxed);
}

void
progress_tracker::item_done (std::uint64_t count) noexcept
{
  m_done_items.fetch_add (count, std::memory_order_relaxed);
}

void
progress_tracker::stage (std::string_view text)
{
  if (!m_sink)
    return;
  std::lock_guard lock (sink_mutex ());
  m_sink->on_stage (m_task, text);
}

void
progress_tracker::report (bool force)
{
  if (!m_sink)
    return;
  auto now = now_ticks ();
  auto next = m_next_report.load (std::memory_order_relaxed);
  if (!force)
    {
      if (now < next)
        return;
      if (!m_next_report.compare_exchange_strong (next,
                                                  now + m_interval.count ()))
        return;
    }
  else
    m_next_report.store (now + m_interval.count (), std::memory_order_relaxed);
  emit (false);
}

void
progress_tracker::finish ()
{
  if (m_sink)
    emit (true);
}

void
progress_tracker::emit (bool finished)
{
  progress_event event;
  event.task = m_task;
  event.done_bytes = m_done_bytes.load (std::memory_order_relaxed);
  event.total_bytes = m_total_bytes.load (std::memory_order_relaxed);
  event.done_items = m_done_items.load (std::memory_order_relaxed);
  event.total_items = m_total_items.load (std::memory_order_relaxed);
  event.finished = finished;
  {
    std::lock_guard lock (m_speed_mutex);
    auto now = std::chrono::steady_clock::now ();
    std::chrono::duration<double> elapsed = now - m_speed_time;
    if (elapsed.count () >= 0.25)
      {
        double delta
            = event.done_bytes >= m_speed_bytes
                  ? static_cast<double> (event.done_bytes - m_speed_bytes)
                  : 0.0;
        double instant = delta / elapsed.count ();
        m_speed = m_speed == 0 ? instant : m_speed * 0.6 + instant * 0.4;
        m_speed_time = now;
        m_speed_bytes = event.done_bytes;
      }
    event.bytes_per_second = m_speed;
  }
  std::lock_guard lock (sink_mutex ());
  m_sink->on_progress (event);
}

std::uint64_t
progress_tracker::done_bytes () const noexcept
{
  return m_done_bytes.load (std::memory_order_relaxed);
}

std::uint64_t
progress_tracker::total_bytes () const noexcept
{
  return m_total_bytes.load (std::memory_order_relaxed);
}

std::uint64_t
progress_tracker::done_items () const noexcept
{
  return m_done_items.load (std::memory_order_relaxed);
}

std::uint64_t
progress_tracker::total_items () const noexcept
{
  return m_total_items.load (std::memory_order_relaxed);
}

progress_sink *
progress_tracker::sink () const noexcept
{
  return m_sink;
}

const std::string &
progress_tracker::task () const noexcept
{
  return m_task;
}

}
