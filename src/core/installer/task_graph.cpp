#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/installer/task_graph.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace b_launcher_base::install
{

namespace
{
constexpr log::logger s_log ("tasks");
}

std::size_t
task_graph::add (std::string name, task_fn fn,
                 std::vector<std::size_t> dependencies)
{
  m_nodes.push_back (
      { std::move (name), std::move (fn), std::move (dependencies) });
  return m_nodes.size () - 1;
}

std::size_t
task_graph::size () const noexcept
{
  return m_nodes.size ();
}

const std::string &
task_graph::name (std::size_t id) const
{
  return m_nodes.at (id).name;
}

result<void>
task_graph::run (cancellation_token token, progress_sink *sink)
{
  const auto count = m_nodes.size ();
  std::vector<std::size_t> pending (count, 0);
  std::vector<std::vector<std::size_t>> dependents (count);
  for (std::size_t i = 0; i < count; ++i)
    for (auto dep : m_nodes[i].dependencies)
      {
        if (dep >= count || dep == i)
          return make_error (
              errc::invalid_argument,
              sformat ("task '{}' has invalid dependency", m_nodes[i].name));
        ++pending[i];
        dependents[dep].push_back (i);
      }

  cancellation_source local;
  auto link = token.on_cancel ([&local] { local.cancel (); });
  auto child = local.token ();

  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::size_t> ready;
  std::size_t finished = 0;
  std::size_t running = 0;
  std::optional<error> failure;
  std::vector<std::thread> threads;

  for (std::size_t i = 0; i < count; ++i)
    if (pending[i] == 0)
      ready.push_back (i);

  auto launch = [&] (std::size_t id) {
    ++running;
    threads.emplace_back ([&, id] {
      auto started = std::chrono::steady_clock::now ();
      if (sink)
        sink->on_stage (m_nodes[id].name, "start");
      auto outcome
          = child.is_cancelled ()
                ? result<void> (make_error (errc::cancelled, "cancelled"))
                : m_nodes[id].fn (child);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                    std::chrono::steady_clock::now () - started)
                    .count ();
      s_log.debug ("task '{}' finished in {} ms: {}", m_nodes[id].name, ms,
                   outcome ? "ok" : outcome.err ().to_string ());
      if (sink)
        sink->on_stage (m_nodes[id].name, outcome ? "done" : "failed");
      std::lock_guard lock (mutex);
      --running;
      ++finished;
      if (!outcome)
        {
          if (!failure || failure->code == errc::cancelled)
            failure = std::move (outcome).err ();
          local.cancel ();
        }
      else
        for (auto next : dependents[id])
          if (--pending[next] == 0)
            ready.push_back (next);
      cv.notify_all ();
    });
  };

  {
    std::unique_lock lock (mutex);
    for (;;)
      {
        if (!failure)
          {
            auto batch = std::move (ready);
            ready.clear ();
            for (auto id : batch)
              launch (id);
          }
        if (running == 0 && (failure || ready.empty ()))
          break;
        cv.wait (lock);
      }
  }
  for (auto &t : threads)
    t.join ();
  if (failure)
    return *failure;
  if (finished != count)
    return make_error (errc::invalid_argument,
                       "task graph contains a dependency cycle");
  return ok ();
}

}
