#include <core/base/thread_pool.hpp>

#include <algorithm>
#include <deque>
#include <thread>
#include <vector>

namespace b_launcher_base
{

namespace
{
thread_local const void *t_current_pool = nullptr;
}

struct thread_pool::impl
{
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::function<void ()>> jobs;
  std::vector<std::thread> workers;
  bool stopping = false;

  void
  run ()
  {
    t_current_pool = this;
    for (;;)
      {
        std::function<void ()> job;
        {
          std::unique_lock lock (mutex);
          cv.wait (lock, [this] { return stopping || !jobs.empty (); });
          if (jobs.empty ())
            return;
          job = std::move (jobs.front ());
          jobs.pop_front ();
        }
        job ();
      }
  }
};

thread_pool::thread_pool (unsigned threads)
    : m_impl (std::make_unique<impl> ())
{
  if (threads == 0)
    threads = std::max (2u, std::thread::hardware_concurrency ());
  m_impl->workers.reserve (threads);
  for (unsigned i = 0; i < threads; ++i)
    m_impl->workers.emplace_back ([p = m_impl.get ()] { p->run (); });
}

thread_pool::~thread_pool ()
{
  {
    std::lock_guard lock (m_impl->mutex);
    m_impl->stopping = true;
  }
  m_impl->cv.notify_all ();
  for (auto &worker : m_impl->workers)
    if (worker.joinable ())
      worker.join ();
}

void
thread_pool::post (std::function<void ()> job)
{
  {
    std::lock_guard lock (m_impl->mutex);
    m_impl->jobs.push_back (std::move (job));
  }
  m_impl->cv.notify_one ();
}

unsigned
thread_pool::size () const noexcept
{
  return static_cast<unsigned> (m_impl->workers.size ());
}

bool
thread_pool::in_worker () const noexcept
{
  return t_current_pool == m_impl.get ();
}

thread_pool &
thread_pool::shared ()
{
  static thread_pool *pool = new thread_pool ();
  return *pool;
}

thread_pool &
thread_pool::io ()
{
  static thread_pool *pool = new thread_pool (
      std::clamp (std::thread::hardware_concurrency () * 2, 8u, 32u));
  return *pool;
}

}
