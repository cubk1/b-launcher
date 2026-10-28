#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>

namespace b_launcher_base
{

class thread_pool
{
public:
  explicit thread_pool (unsigned threads = 0);
  thread_pool (const thread_pool &) = delete;
  thread_pool &operator= (const thread_pool &) = delete;
  ~thread_pool ();

  void post (std::function<void ()> job);
  unsigned size () const noexcept;
  bool in_worker () const noexcept;

  template <typename F>
  auto
  submit (F &&fn) -> std::future<std::invoke_result_t<std::decay_t<F>>>
  {
    using return_type = std::invoke_result_t<std::decay_t<F>>;
    auto task = std::make_shared<std::packaged_task<return_type ()>> (
        std::forward<F> (fn));
    auto future = task->get_future ();
    post ([task] { (*task) (); });
    return future;
  }

  static thread_pool &shared ();
  static thread_pool &io ();

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

template <typename F>
void
parallel_for (thread_pool &pool, std::size_t count, F &&fn,
              std::size_t grain = 1)
{
  if (count == 0)
    return;
  if (grain == 0)
    grain = 1;
  std::size_t chunks = (count + grain - 1) / grain;
  if (chunks == 1 || pool.size () <= 1)
    {
      for (std::size_t i = 0; i < count; ++i)
        fn (i);
      return;
    }
  struct shared_state
  {
    std::atomic<std::size_t> next { 0 };
    std::atomic<std::size_t> finished { 0 };
    std::mutex mutex;
    std::condition_variable cv;
  };
  auto state = std::make_shared<shared_state> ();
  auto body = [state, count, grain, chunks, &fn] {
    for (;;)
      {
        auto chunk = state->next.fetch_add (1, std::memory_order_relaxed);
        if (chunk >= chunks)
          return;
        auto begin = chunk * grain;
        auto end = begin + grain < count ? begin + grain : count;
        for (auto i = begin; i < end; ++i)
          fn (i);
        if (state->finished.fetch_add (1, std::memory_order_acq_rel) + 1
            == chunks)
          {
            std::lock_guard lock (state->mutex);
            state->cv.notify_all ();
          }
      }
  };
  std::size_t helpers = pool.size ();
  if (helpers > chunks - 1)
    helpers = chunks - 1;
  for (std::size_t i = 0; i < helpers; ++i)
    pool.post (body);
  body ();
  std::unique_lock lock (state->mutex);
  state->cv.wait (lock, [&] {
    return state->finished.load (std::memory_order_acquire) == chunks;
  });
}

}
