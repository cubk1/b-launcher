#include <core/base/instance_channel.hpp>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace b_launcher_base
{

struct instance_channel::impl
{
  int lock = -1;
  int socket = -1;
  std::filesystem::path address;
};

instance_channel::instance_channel () : m_impl (std::make_unique<impl> ()) {}
instance_channel::~instance_channel () { reset (); }

void
instance_channel::reset ()
{
#if !defined(_WIN32)
  if (m_impl->socket >= 0)
    {
      close (m_impl->socket);
      unlink (m_impl->address.c_str ());
    }
  if (m_impl->lock >= 0)
    close (m_impl->lock);
#endif
  m_impl->socket = m_impl->lock = -1;
}

bool
instance_channel::listening () const
{
  return m_impl->socket >= 0;
}

bool
instance_channel::forward_or_listen (const std::filesystem::path &directory,
                                     std::string_view message)
{
#if defined(_WIN32)
  (void)directory;
  (void)message;
  return false;
#else
  if (listening () || message.size () > 32768)
    return false;
  struct stat info {};
  if (mkdir (directory.c_str (), 0700) != 0 && errno != EEXIST)
    return false;
  if (lstat (directory.c_str (), &info) != 0 || !S_ISDIR (info.st_mode)
      || info.st_uid != getuid () || (info.st_mode & 0077) != 0)
    return false;
  auto address = directory / "launch.sock";
  sockaddr_un endpoint {};
  endpoint.sun_family = AF_UNIX;
  if (address.string ().size () >= sizeof (endpoint.sun_path))
    return false;
  std::strcpy (endpoint.sun_path, address.c_str ());
  int lock = open ((directory / "lock").c_str (), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock < 0)
    return false;
  int socket = ::socket (AF_UNIX, SOCK_DGRAM, 0);
  if (socket < 0)
    {
      close (lock);
      return false;
    }
  fcntl (socket, F_SETFD, FD_CLOEXEC);
  fcntl (socket, F_SETFL, O_NONBLOCK);
  for (int attempt = 0; attempt < 100; ++attempt)
    {
      if (flock (lock, LOCK_EX | LOCK_NB) == 0)
        {
          unlink (address.c_str ());
          if (bind (socket, reinterpret_cast<sockaddr *> (&endpoint), sizeof (endpoint)) == 0)
            {
              m_impl->lock = lock;
              m_impl->socket = socket;
              m_impl->address = address;
              return false;
            }
          break;
        }
      if (message.empty ())
        break;
      if (sendto (socket, message.data (), message.size (), 0,
                  reinterpret_cast<sockaddr *> (&endpoint), sizeof (endpoint))
          == static_cast<ssize_t> (message.size ()))
        {
          close (socket);
          close (lock);
          return true;
        }
      std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
  close (socket);
  close (lock);
  return false;
#endif
}

std::optional<std::string>
instance_channel::receive ()
{
#if !defined(_WIN32)
  if (listening ())
    {
      char message[32769];
      auto count = recv (m_impl->socket, message, sizeof (message), 0);
      if (count > 0 && count <= 32768)
        return std::string (message, static_cast<std::size_t> (count));
    }
#endif
  return std::nullopt;
}

}
