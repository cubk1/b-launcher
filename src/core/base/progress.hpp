#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace b_launcher_base
{

struct progress_event
{
  std::string_view task;
  std::uint64_t done_bytes = 0;
  std::uint64_t total_bytes = 0;
  std::uint64_t done_items = 0;
  std::uint64_t total_items = 0;
  double bytes_per_second = 0;
  bool finished = false;
};

class progress_sink
{
public:
  virtual ~progress_sink ();
  virtual void on_progress (const progress_event &event) = 0;
  virtual void on_stage (std::string_view task, std::string_view stage);
};

class function_progress_sink final : public progress_sink
{
public:
  using progress_fn = std::function<void (const progress_event &)>;
  using stage_fn = std::function<void (std::string_view, std::string_view)>;

  explicit function_progress_sink (progress_fn on_progress,
                                   stage_fn on_stage = {});
  void on_progress (const progress_event &event) override;
  void on_stage (std::string_view task, std::string_view stage) override;

private:
  progress_fn m_progress;
  stage_fn m_stage;
};

class progress_tracker
{
public:
  progress_tracker (progress_sink *sink, std::string task,
                    std::chrono::milliseconds interval
                    = std::chrono::milliseconds (100));
  progress_tracker (const progress_tracker &) = delete;
  progress_tracker &operator= (const progress_tracker &) = delete;

  void add_total (std::uint64_t bytes, std::uint64_t items = 0) noexcept;
  void add_done (std::uint64_t bytes) noexcept;
  void sub_done (std::uint64_t bytes) noexcept;
  void item_done (std::uint64_t count = 1) noexcept;
  void stage (std::string_view text);
  void report (bool force = false);
  void finish ();

  std::uint64_t done_bytes () const noexcept;
  std::uint64_t total_bytes () const noexcept;
  std::uint64_t done_items () const noexcept;
  std::uint64_t total_items () const noexcept;
  progress_sink *sink () const noexcept;
  const std::string &task () const noexcept;

private:
  void emit (bool finished);

  progress_sink *m_sink;
  std::string m_task;
  std::chrono::steady_clock::duration m_interval;
  std::atomic<std::uint64_t> m_done_bytes { 0 };
  std::atomic<std::uint64_t> m_total_bytes { 0 };
  std::atomic<std::uint64_t> m_done_items { 0 };
  std::atomic<std::uint64_t> m_total_items { 0 };
  std::atomic<std::int64_t> m_next_report { 0 };
  std::mutex m_speed_mutex;
  std::chrono::steady_clock::time_point m_speed_time;
  std::uint64_t m_speed_bytes = 0;
  double m_speed = 0;
};

}
