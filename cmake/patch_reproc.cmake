# Patches applied to the reproc sources after FetchContent extracted them.
#
# Windows: hide the console window of spawned processes.
#
# POSIX: fix two problems that make every spawn fail on systems whose
# RLIMIT_NOFILE soft limit is huge (systemd raises fs.nr_open to about a billion
# and user sessions inherit that value as their soft limit):
#   1. process_start() refused to work when the limit exceeded MAX_FD_LIMIT and
#      returned -EMFILE. Close the descriptors with close_range instead, which
#      costs one syscall per kept descriptor instead of one per possible one.
#   2. The child reported its failure by writing the leftover libc errno
#      instead of the saved error code, so EMFILE reached the parent as a stale
#      EINVAL ("Invalid argument") and hid the real cause.

set(target "${SOURCE_DIR}/reproc/src/process.windows.c")
file(READ "${target}" content)
set(original "${content}")
string(FIND "${content}" "CREATE_NO_WINDOW" found)
if(found EQUAL -1)
  string(REPLACE "CREATE_NEW_PROCESS_GROUP |"
    "CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW |" content "${content}")
endif()
string(REPLACE ".dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW,"
  ".dwFlags = STARTF_USESTDHANDLES," content "${content}")
if(NOT content STREQUAL original)
  file(WRITE "${target}" "${content}")
endif()

set(target "${SOURCE_DIR}/reproc/src/process.posix.c")
file(READ "${target}" content)

string(FIND "${content}" "close_fds_except" patched)
if(patched EQUAL -1)
  set(includes_old "#include <sys/wait.h>
#include <unistd.h>")

  set(includes_new "#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/syscall.h>
#if defined(SYS_close_range)
// glibc only declares syscall() under _DEFAULT_SOURCE, which this file does
// not define.
extern long syscall(long, ...);
#endif
#endif")

  string(FIND "${content}" "${includes_old}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "cannot patch ${target}: include block changed upstream")
  endif()
  string(REPLACE "${includes_old}" "${includes_new}" content "${content}")

  set(fd_set_old "static bool fd_in_set(int fd, const int *fd_set, size_t size)
{
  for (size_t i = 0; i < size; i++) {
    if (fd == fd_set[i]) {
      return true;
    }
  }

  return false;
}")

  set(fd_set_new "${fd_set_old}

#if defined(__linux__) && defined(SYS_close_range)
static int close_range_upto(int from, int to)
{
  int r = (int) syscall(SYS_close_range, (unsigned) from, (unsigned) to, 0);

  return r < 0 ? -errno : 0;
}
#endif

// Close every descriptor below max_fd except the ones in except and the two
// error pipe descriptors, which the caller destroys itself.
//
// close_range keeps the cost proportional to the number of kept descriptors
// instead of the descriptor limit. That limit is not a small constant: systemd
// raises fs.nr_open to around a billion and user sessions inherit it, so a loop
// over it either refuses to work or takes about a second per spawn.
static int close_fds_except(int max_fd, const int *except, size_t num_except,
                            int pipe_read, int pipe_write)
{
#if defined(__linux__) && defined(SYS_close_range)
  int keep[16];
  size_t kept = 0;

  if (num_except + 2 <= ARRAY_SIZE(keep)) {
    for (size_t i = 0; i < num_except; i++) {
      if (except[i] >= 0 && except[i] < max_fd) {
        keep[kept++] = except[i];
      }
    }

    if (pipe_read >= 0 && pipe_read < max_fd) {
      keep[kept++] = pipe_read;
    }

    if (pipe_write >= 0 && pipe_write < max_fd) {
      keep[kept++] = pipe_write;
    }

    // Insertion sort, kept is at most a handful of descriptors.
    for (size_t i = 1; i < kept; i++) {
      int value = keep[i];
      size_t j = i;

      while (j > 0 && keep[j - 1] > value) {
        keep[j] = keep[j - 1];
        j--;
      }

      keep[j] = value;
    }

    int start = 0;

    for (size_t i = 0; i <= kept; i++) {
      int end = i < kept ? keep[i] : max_fd;

      if (end > start) {
        int r = close_range_upto(start, end - 1);

        if (r == -ENOSYS) {
          goto fallback;
        }

        if (r < 0) {
          return r;
        }
      }

      start = end + 1;
    }

    return 0;
  }

fallback:
#endif
  // Kernels without close_range, and keepsets that do not fit above. Closing up
  // to MAX_FD_LIMIT descriptors instead of failing leaves descriptors above it
  // open, which is still better than refusing to start any process at all.
  for (int i = 0; i < max_fd && i <= MAX_FD_LIMIT; i++) {
    // Make sure we don't close the error pipe file descriptors twice.
    if (i == pipe_read || i == pipe_write) {
      continue;
    }

    if (fd_in_set(i, except, num_except)) {
      continue;
    }

    // Check if i is a valid file descriptor before trying to close it.
    if (fcntl(i, F_GETFD) >= 0) {
      handle_destroy(i);
    }
  }

  return 0;
}")

  string(FIND "${content}" "${fd_set_old}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "cannot patch ${target}: fd_in_set changed upstream")
  endif()
  string(REPLACE "${fd_set_old}" "${fd_set_new}" content "${content}")

  set(start_anchor "  r = get_max_fd();")
  set(end_anchor "\n  r = 0;\n\nfinish:")

  string(FIND "${content}" "${start_anchor}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "cannot patch ${target}: get_max_fd call changed upstream")
  endif()

  string(SUBSTRING "${content}" ${start} -1 tail)
  string(FIND "${tail}" "${end_anchor}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "cannot patch ${target}: descriptor loop changed upstream")
  endif()

  math(EXPR resume "${start} + ${length}")

  string(SUBSTRING "${content}" 0 ${start} head)
  string(SUBSTRING "${content}" ${resume} -1 tail)

  set(replacement "  r = get_max_fd();
  if (r < 0) {
    goto finish;
  }

  r = close_fds_except(r, except, num_except, pipe.read, pipe.write);
  if (r < 0) {
    goto finish;
  }
")

  set(content "${head}${replacement}${tail}")
endif()

string(REPLACE "(void) !write(pipe.write, &errno, sizeof(errno));"
  "(void) !write(pipe.write, &(int){ -r }, sizeof(int));" content "${content}")

file(WRITE "${target}" "${content}")
