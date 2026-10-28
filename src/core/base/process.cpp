#include <core/base/format.hpp>
#include <core/base/process.hpp>
#include <core/base/strings.hpp>

#include <reproc/reproc.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace b_launcher_base
{

namespace
{

error
reproc_error (std::string_view what, int code)
{
  return make_error (errc::process,
                     sformat ("{}: {}", what, reproc_strerror (code)));
}

class line_splitter
{
public:
  void
  feed (std::string_view chunk, output_stream stream,
        const output_callback &callback)
  {
    m_pending.append (chunk);
    std::size_t start = 0;
    for (;;)
      {
        auto pos = m_pending.find ('\n', start);
        if (pos == std::string::npos)
          break;
        auto line = std::string_view (m_pending).substr (start, pos - start);
        if (!line.empty () && line.back () == '\r')
          line.remove_suffix (1);
        callback (line, stream);
        start = pos + 1;
      }
    m_pending.erase (0, start);
  }

  void
  flush (output_stream stream, const output_callback &callback)
  {
    if (!m_pending.empty ())
      callback (m_pending, stream);
    m_pending.clear ();
  }

private:
  std::string m_pending;
};

}

struct process::impl
{
  reproc_t *handle = nullptr;
  std::thread reader;
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<int> exit_code;
  int pid = -1;
  std::mutex callback_mutex;
#if defined(_WIN32)
  HANDLE out_read = nullptr;
  HANDLE err_read = nullptr;
#endif

  ~impl ()
  {
    if (handle)
      reproc_destroy (handle);
#if defined(_WIN32)
    if (out_read)
      CloseHandle (out_read);
    if (err_read)
      CloseHandle (err_read);
#endif
  }

  void
  deliver (line_splitter &splitter, std::string_view chunk,
           output_stream stream, const output_callback &callback)
  {
    if (!callback)
      return;
    std::lock_guard lock (callback_mutex);
    splitter.feed (chunk, stream, callback);
  }

  void
  finish (line_splitter &splitter, output_stream stream,
          const output_callback &callback)
  {
    if (!callback)
      return;
    std::lock_guard lock (callback_mutex);
    splitter.flush (stream, callback);
  }

  void
  publish (int code)
  {
    {
      std::lock_guard lock (mutex);
      exit_code = code;
    }
    cv.notify_all ();
  }

#if defined(_WIN32)

  void
  read_pipe (HANDLE pipe, output_stream stream,
             const output_callback &callback)
  {
    line_splitter splitter;
    char buffer[16384];
    for (;;)
      {
        DWORD got = 0;
        if (!ReadFile (pipe, buffer, sizeof (buffer), &got, nullptr)
            || got == 0)
          break;
        deliver (splitter, std::string_view (buffer, got), stream, callback);
      }
    finish (splitter, stream, callback);
  }

  void
  run (output_callback callback, bool capture)
  {
    std::atomic<int> active { 0 };
    std::mutex done_mutex;
    std::condition_variable done_cv;
    std::vector<std::thread> readers;
    auto spawn = [&] (HANDLE pipe, output_stream stream) {
      ++active;
      readers.emplace_back ([&, pipe, stream] {
        read_pipe (pipe, stream, callback);
        std::lock_guard lock (done_mutex);
        --active;
        done_cv.notify_all ();
      });
    };
    if (capture)
      {
        spawn (out_read, output_stream::out);
        spawn (err_read, output_stream::err);
      }
    int code = reproc_wait (handle, REPROC_INFINITE);
    {
      std::unique_lock lock (done_mutex);
      if (!done_cv.wait_for (lock, std::chrono::seconds (2),
                             [&] { return active.load () == 0; }))
        {
          CancelIoEx (out_read, nullptr);
          CancelIoEx (err_read, nullptr);
        }
    }
    for (auto &t : readers)
      t.join ();
    publish (code);
  }

#else

  bool
  consume (REPROC_STREAM stream, line_splitter &splitter,
           const output_callback &callback, std::uint8_t *buffer,
           std::size_t size)
  {
    int n = reproc_read (handle, stream, buffer, size);
    if (n < 0)
      return n == REPROC_EWOULDBLOCK;
    deliver (splitter,
             std::string_view (reinterpret_cast<const char *> (buffer),
                               static_cast<std::size_t> (n)),
             stream == REPROC_STREAM_OUT ? output_stream::out
                                         : output_stream::err,
             callback);
    return true;
  }

  void
  run (output_callback callback, bool capture)
  {
    line_splitter out;
    line_splitter err;
    std::uint8_t buffer[16384];
    int streams = capture ? REPROC_EVENT_OUT | REPROC_EVENT_ERR : 0;
    while (streams)
      {
        reproc_event_source source { handle, streams, 0 };
        if (reproc_poll (&source, 1, REPROC_INFINITE) <= 0)
          break;
        if ((source.events & REPROC_EVENT_OUT)
            && !consume (REPROC_STREAM_OUT, out, callback, buffer,
                         sizeof (buffer)))
          streams &= ~REPROC_EVENT_OUT;
        if ((source.events & REPROC_EVENT_ERR)
            && !consume (REPROC_STREAM_ERR, err, callback, buffer,
                         sizeof (buffer)))
          streams &= ~REPROC_EVENT_ERR;
      }
    finish (out, output_stream::out, callback);
    finish (err, output_stream::err, callback);
    publish (reproc_wait (handle, REPROC_INFINITE));
  }

#endif
};

process::process (process &&other) noexcept
    : m_impl (std::move (other.m_impl))
{
}

process &
process::operator= (process &&other) noexcept
{
  if (this != &other)
    {
      detach ();
      m_impl = std::move (other.m_impl);
    }
  return *this;
}

process::~process ()
{
  detach ();
}

result<process>
process::start (const process_options &options, output_callback on_output)
{
  if (options.args.empty ())
    return make_error (errc::invalid_argument, "empty command line");
  std::vector<const char *> argv;
  argv.reserve (options.args.size () + 1);
  for (const auto &arg : options.args)
    argv.push_back (arg.c_str ());
  argv.push_back (nullptr);

  std::vector<std::string> env_storage;
  std::vector<const char *> env;
  for (const auto &[key, value] : options.environment)
    env_storage.push_back (key + "=" + value);
  for (const auto &entry : env_storage)
    env.push_back (entry.c_str ());
  env.push_back (nullptr);

  auto working = path_to_utf8 (options.working_directory);

  reproc_options opts {};
  opts.working_directory
      = options.working_directory.empty () ? nullptr : working.c_str ();
  opts.env.behavior = REPROC_ENV_EXTEND;
  opts.env.extra = env_storage.empty () ? nullptr : env.data ();
  opts.redirect.in.type = REPROC_REDIRECT_DISCARD;
  opts.redirect.out.type = options.capture_output ? REPROC_REDIRECT_PIPE
                                                  : REPROC_REDIRECT_DISCARD;
  opts.redirect.err.type = options.capture_output ? REPROC_REDIRECT_PIPE
                                                  : REPROC_REDIRECT_DISCARD;
  opts.stop.first = { REPROC_STOP_WAIT, 0 };
  opts.stop.second = { REPROC_STOP_NOOP, 0 };
  opts.stop.third = { REPROC_STOP_NOOP, 0 };

  auto state = std::make_shared<impl> ();
#if defined(_WIN32)
  HANDLE out_write = nullptr;
  HANDLE err_write = nullptr;
  if (options.capture_output)
    {
      SECURITY_ATTRIBUTES attributes { sizeof (attributes), nullptr, FALSE };
      if (!CreatePipe (&state->out_read, &out_write, &attributes, 0)
          || !CreatePipe (&state->err_read, &err_write, &attributes, 0))
        {
          if (out_write)
            CloseHandle (out_write);
          return make_error (errc::process, "cannot create output pipes");
        }
      opts.redirect.out = {};
      opts.redirect.out.type = REPROC_REDIRECT_HANDLE;
      opts.redirect.out.handle = out_write;
      opts.redirect.err = {};
      opts.redirect.err.type = REPROC_REDIRECT_HANDLE;
      opts.redirect.err.handle = err_write;
    }
#endif
  state->handle = reproc_new ();
  if (!state->handle)
    return make_error (errc::process, "cannot allocate process");
  int rc = reproc_start (state->handle, argv.data (), opts);
#if defined(_WIN32)
  if (out_write)
    CloseHandle (out_write);
  if (err_write)
    CloseHandle (err_write);
#endif
  if (rc < 0)
    return reproc_error (sformat ("cannot start '{}'", options.args[0]), rc);
  state->pid = reproc_pid (state->handle);
  state->reader = std::thread ([state, callback = std::move (on_output),
                                capture = options.capture_output] () mutable {
    state->run (std::move (callback), capture);
  });
  process p;
  p.m_impl = std::move (state);
  return p;
}

int
process::pid () const noexcept
{
  return m_impl ? m_impl->pid : -1;
}

bool
process::valid () const noexcept
{
  return static_cast<bool> (m_impl);
}

bool
process::running ()
{
  if (!m_impl)
    return false;
  std::lock_guard lock (m_impl->mutex);
  return !m_impl->exit_code.has_value ();
}

result<int>
process::wait (std::optional<std::chrono::milliseconds> timeout)
{
  if (!m_impl)
    return make_error (errc::invalid_argument, "no process");
  std::unique_lock lock (m_impl->mutex);
  auto done = [this] {
    return m_impl->exit_code.has_value ();
  };
  if (timeout)
    {
      if (!m_impl->cv.wait_for (lock, *timeout, done))
        return make_error (errc::timeout, "process still running");
    }
  else
    m_impl->cv.wait (lock, done);
  int code = *m_impl->exit_code;
  lock.unlock ();
  if (m_impl->reader.joinable ()
      && m_impl->reader.get_id () != std::this_thread::get_id ())
    m_impl->reader.join ();
  if (code < 0)
    return reproc_error ("wait failed", code);
  return code;
}

result<void>
process::terminate ()
{
  if (!m_impl)
    return ok ();
#if defined(_WIN32)
  return kill ();
#else
  int rc = reproc_terminate (m_impl->handle);
  if (rc < 0)
    return reproc_error ("terminate failed", rc);
  if (!wait (std::chrono::milliseconds (5000)))
    return kill ();
  return ok ();
#endif
}

result<void>
process::kill ()
{
  if (!m_impl)
    return ok ();
  int rc = reproc_kill (m_impl->handle);
  if (rc < 0 && running ())
    return reproc_error ("kill failed", rc);
  return ok ();
}

void
process::detach ()
{
  if (!m_impl)
    return;
  if (m_impl->reader.joinable ())
    m_impl->reader.detach ();
  m_impl.reset ();
}

result<process_result>
run_process (const process_options &options, cancellation_token token,
             std::optional<std::chrono::milliseconds> timeout,
             output_callback on_output)
{
  BLB_TRY (token.check ());
  process_result out;
  std::mutex mutex;
  auto collect = [&] (std::string_view line, output_stream stream) {
    {
      std::lock_guard lock (mutex);
      auto &target = stream == output_stream::out ? out.out : out.err;
      target.append (line);
      target.push_back ('\n');
    }
    if (on_output)
      on_output (line, stream);
  };
  BLB_TRY_ASSIGN (auto child, process::start (options, collect));
  std::atomic<bool> cancelled { false };
  auto registration = token.on_cancel ([&] {
    cancelled = true;
    (void)child.kill ();
  });
  auto status = child.wait (timeout);
  if (!status && status.err ().code == errc::timeout)
    {
      (void)child.kill ();
      (void)child.wait ();
      return make_error (errc::timeout,
                         sformat ("'{}' timed out", options.args[0]));
    }
  registration.reset ();
  if (cancelled)
    return make_error (errc::cancelled, "process cancelled");
  if (!status)
    return std::move (status).err ();
  out.exit_code = *status;
  return out;
}

}
