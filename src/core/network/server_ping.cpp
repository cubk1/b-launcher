#include <core/base/format.hpp>
#include <core/base/strings.hpp>
#include <core/network/server_ping.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#if defined(_WIN32)
#include <windns.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace b_launcher_base::net
{

namespace
{

using clock = std::chrono::steady_clock;

constexpr std::uint32_t max_packet = 4u << 20;
constexpr std::size_t max_favicon = 256u << 10;
constexpr auto attempt_limit = std::chrono::milliseconds (2500);

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t no_socket = INVALID_SOCKET;
constexpr int send_flags = 0;

int
last_socket_error ()
{
  return WSAGetLastError ();
}

bool
would_block (int code)
{
  return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
}

bool
interrupted (int code)
{
  return code == WSAEINTR;
}

void
close_socket (socket_t s)
{
  closesocket (s);
}

bool
set_nonblocking (socket_t s)
{
  u_long on = 1;
  return ioctlsocket (s, FIONBIO, &on) == 0;
}

void
start_sockets ()
{
  static std::once_flag once;
  std::call_once (once, [] {
    WSADATA data;
    (void)WSAStartup (MAKEWORD (2, 2), &data);
  });
}
#else
using socket_t = int;
constexpr socket_t no_socket = -1;
#if defined(MSG_NOSIGNAL)
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif

int
last_socket_error ()
{
  return errno;
}

bool
would_block (int code)
{
  return code == EINPROGRESS || code == EWOULDBLOCK || code == EAGAIN;
}

bool
interrupted (int code)
{
  return code == EINTR;
}

void
close_socket (socket_t s)
{
  ::close (s);
}

bool
set_nonblocking (socket_t s)
{
  int flags = fcntl (s, F_GETFL, 0);
  return flags >= 0 && fcntl (s, F_SETFL, flags | O_NONBLOCK) == 0;
}

void
start_sockets ()
{
}
#endif

error
socket_error (std::string_view what, int code)
{
  return make_error (errc::network, sformat ("{} ({})", what, code));
}

class connection
{
public:
  connection () = default;

  explicit connection (socket_t s)
      : m_socket (s)
  {
  }

  connection (connection &&other) noexcept
      : m_socket (std::exchange (other.m_socket, no_socket))
  {
  }

  connection &
  operator= (connection &&other) noexcept
  {
    if (this != &other)
      {
        reset ();
        m_socket = std::exchange (other.m_socket, no_socket);
      }
    return *this;
  }

  connection (const connection &) = delete;
  connection &operator= (const connection &) = delete;

  ~connection ()
  {
    reset ();
  }

  socket_t
  get () const noexcept
  {
    return m_socket;
  }

  explicit
  operator bool () const noexcept
  {
    return m_socket != no_socket;
  }

  void
  reset () noexcept
  {
    if (m_socket != no_socket)
      close_socket (m_socket);
    m_socket = no_socket;
  }

private:
  socket_t m_socket = no_socket;
};

result<void>
wait_socket (socket_t s, bool writing, clock::time_point deadline,
             const cancellation_token &token)
{
  for (;;)
    {
      if (token.is_cancelled ())
        return make_error (errc::cancelled, "operation cancelled");
      auto now = clock::now ();
      if (now >= deadline)
        return make_error (errc::timeout, "server did not respond in time");
      auto slice = std::min<clock::duration> (deadline - now,
                                              std::chrono::milliseconds (100));
      auto micros
          = std::chrono::duration_cast<std::chrono::microseconds> (slice)
                .count ();
      fd_set ready;
      fd_set failed;
      FD_ZERO (&ready);
      FD_ZERO (&failed);
      FD_SET (s, &ready);
      FD_SET (s, &failed);
      timeval wait {};
      wait.tv_sec = static_cast<long> (micros / 1000000);
      wait.tv_usec = static_cast<long> (micros % 1000000);
#if defined(_WIN32)
      int range = 0;
#else
      int range = s + 1;
#endif
      int count = select (range, writing ? nullptr : &ready,
                          writing ? &ready : nullptr, &failed, &wait);
      if (count > 0)
        return ok ();
      if (count < 0 && !interrupted (last_socket_error ()))
        return socket_error ("cannot wait for server", last_socket_error ());
    }
}

result<connection>
connect_to (const sockaddr *address, socklen_t length, int family,
            clock::time_point deadline, const cancellation_token &token)
{
  connection s (::socket (family, SOCK_STREAM, IPPROTO_TCP));
  if (!s)
    return socket_error ("cannot create socket", last_socket_error ());
  if (!set_nonblocking (s.get ()))
    return socket_error ("cannot configure socket", last_socket_error ());
  if (::connect (s.get (), address, length) != 0)
    {
      int code = last_socket_error ();
      if (!would_block (code))
        return socket_error ("cannot connect", code);
      BLB_TRY (wait_socket (s.get (), true, deadline, token));
      int failure = 0;
      socklen_t size = sizeof (failure);
      if (getsockopt (s.get (), SOL_SOCKET, SO_ERROR,
                      reinterpret_cast<char *> (&failure), &size)
          != 0)
        return socket_error ("cannot connect", last_socket_error ());
      if (failure != 0)
        return socket_error ("cannot connect", failure);
    }
  int on = 1;
  (void)setsockopt (s.get (), IPPROTO_TCP, TCP_NODELAY,
                    reinterpret_cast<const char *> (&on), sizeof (on));
  return s;
}

struct endpoint
{
  sockaddr_storage address {};
  socklen_t length = 0;
  int family = 0;
};

result<std::vector<endpoint>>
resolve (const std::string &host, std::uint16_t port)
{
#if defined(_WIN32)
  std::wstring wide_host = path_from_utf8 (host).wstring ();
  std::wstring wide_port = std::to_wstring (port);
  ADDRINFOW hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  ADDRINFOW *found = nullptr;
  int rc
      = GetAddrInfoW (wide_host.c_str (), wide_port.c_str (), &hints, &found);
  if (rc != 0 || found == nullptr)
    return make_error (errc::not_found,
                       sformat ("cannot resolve {} ({})", host, rc));
  std::unique_ptr<ADDRINFOW, decltype (&FreeAddrInfoW)> list (found,
                                                              &FreeAddrInfoW);
#else
  addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo *found = nullptr;
  int rc = getaddrinfo (host.c_str (), std::to_string (port).c_str (), &hints,
                        &found);
  if (rc != 0 || found == nullptr)
    return make_error (errc::not_found, sformat ("cannot resolve {} ({})",
                                                 host, gai_strerror (rc)));
  std::unique_ptr<addrinfo, decltype (&freeaddrinfo)> list (found,
                                                            &freeaddrinfo);
#endif
  std::vector<endpoint> out;
  for (auto *at = list.get (); at != nullptr; at = at->ai_next)
    {
      if (at->ai_addr == nullptr || at->ai_addrlen > sizeof (sockaddr_storage))
        continue;
      endpoint next;
      std::memcpy (&next.address, at->ai_addr, at->ai_addrlen);
      next.length = static_cast<socklen_t> (at->ai_addrlen);
      next.family = at->ai_family;
      out.push_back (next);
    }
  if (out.empty ())
    return make_error (errc::not_found, sformat ("cannot resolve {}", host));
  return out;
}

result<connection>
open_connection (const std::vector<endpoint> &endpoints,
                 clock::time_point deadline, const cancellation_token &token)
{
  error last = make_error (errc::timeout, "server did not respond in time");
  for (std::size_t i = 0; i < endpoints.size (); ++i)
    {
      auto now = clock::now ();
      if (now >= deadline)
        break;
      auto until = i + 1 < endpoints.size ()
                       ? std::min (deadline, now + attempt_limit)
                       : deadline;
      const endpoint &target = endpoints[i];
      auto made
          = connect_to (reinterpret_cast<const sockaddr *> (&target.address),
                        target.length, target.family, until, token);
      if (made)
        return std::move (*made);
      last = std::move (made).err ();
      if (last.code == errc::cancelled)
        break;
    }
  return last;
}

bool
ip_literal (const std::string &host)
{
  in_addr v4 {};
  in6_addr v6 {};
  return inet_pton (AF_INET, host.c_str (), &v4) == 1
         || inet_pton (AF_INET6, host.c_str (), &v6) == 1;
}

std::optional<server_address>
lookup_srv (const std::string &host)
{
#if defined(_WIN32)
  std::wstring name = L"_minecraft._tcp." + path_from_utf8 (host).wstring ();
  PDNS_RECORD records = nullptr;
  if (DnsQuery_W (name.c_str (), DNS_TYPE_SRV, DNS_QUERY_STANDARD, nullptr,
                  &records, nullptr)
          != 0
      || records == nullptr)
    return std::nullopt;
  std::optional<server_address> best;
  WORD priority = 0;
  WORD weight = 0;
  for (PDNS_RECORD at = records; at != nullptr; at = at->pNext)
    {
      if (at->wType != DNS_TYPE_SRV || at->Data.SRV.pNameTarget == nullptr)
        continue;
      if (best
          && (at->Data.SRV.wPriority > priority
              || (at->Data.SRV.wPriority == priority
                  && at->Data.SRV.wWeight <= weight)))
        continue;
      server_address target;
      target.host = path_to_utf8 (
          std::filesystem::path (std::wstring (at->Data.SRV.pNameTarget)));
      target.port = at->Data.SRV.wPort;
      target.explicit_port = true;
      priority = at->Data.SRV.wPriority;
      weight = at->Data.SRV.wWeight;
      best = std::move (target);
    }
  DnsFree (records, DnsFreeRecordList);
  if (best)
    {
      while (!best->host.empty () && best->host.back () == '.')
        best->host.pop_back ();
      if (best->host.empty () || best->port == 0)
        best.reset ();
    }
  return best;
#else
  (void)host;
  return std::nullopt;
#endif
}

struct lookup_state
{
  std::mutex mutex;
  std::condition_variable finished;
  bool done = false;
  result<std::vector<endpoint>> endpoints;
};

result<std::vector<endpoint>>
resolve_address (const server_address &address)
{
  if (address.explicit_port || ip_literal (address.host))
    return resolve (address.host, address.port);
  auto direct = std::make_shared<result<std::vector<endpoint>>> ();
  std::thread plain (
      [direct, address] { *direct = resolve (address.host, address.port); });
  auto srv = lookup_srv (address.host);
  if (srv && (srv->host != address.host || srv->port != address.port))
    {
      plain.detach ();
      return resolve (srv->host, srv->port);
    }
  plain.join ();
  return std::move (*direct);
}

result<std::vector<endpoint>>
lookup (const server_address &address, clock::time_point deadline,
        const cancellation_token &token)
{
  auto state = std::make_shared<lookup_state> ();
  std::thread ([state, address] {
    auto found = resolve_address (address);
    std::lock_guard lock (state->mutex);
    state->endpoints = std::move (found);
    state->done = true;
    state->finished.notify_all ();
  }).detach ();
  std::unique_lock lock (state->mutex);
  while (!state->done)
    {
      if (token.is_cancelled ())
        return make_error (errc::cancelled, "operation cancelled");
      auto now = clock::now ();
      if (now >= deadline)
        return make_error (errc::timeout, "server did not respond in time");
      state->finished.wait_for (
          lock, std::min<clock::duration> (deadline - now,
                                           std::chrono::milliseconds (100)));
    }
  return std::move (state->endpoints);
}

void
put_varint (std::string &out, std::uint32_t value)
{
  do
    {
      auto byte = static_cast<std::uint8_t> (value & 0x7Fu);
      value >>= 7;
      if (value != 0)
        byte = static_cast<std::uint8_t> (byte | 0x80u);
      out.push_back (static_cast<char> (byte));
    }
  while (value != 0);
}

void
put_string (std::string &out, std::string_view text)
{
  put_varint (out, static_cast<std::uint32_t> (text.size ()));
  out.append (text);
}

std::string
packet (const std::string &body)
{
  std::string out;
  put_varint (out, static_cast<std::uint32_t> (body.size ()));
  out += body;
  return out;
}

std::optional<std::uint32_t>
take_varint (std::string_view &data)
{
  std::uint32_t value = 0;
  for (int i = 0; i < 5 && !data.empty (); ++i)
    {
      auto byte = static_cast<std::uint8_t> (data.front ());
      data.remove_prefix (1);
      value |= static_cast<std::uint32_t> (byte & 0x7Fu) << (7 * i);
      if ((byte & 0x80u) == 0)
        return value;
    }
  return std::nullopt;
}

struct stream
{
  socket_t socket;
  clock::time_point deadline;
  cancellation_token token;
  std::string pending;

  result<void>
  send_all (std::string_view data)
  {
    while (!data.empty ())
      {
        auto sent = ::send (socket, data.data (),
                            static_cast<int> (data.size ()), send_flags);
        if (sent > 0)
          {
            data.remove_prefix (static_cast<std::size_t> (sent));
            continue;
          }
        int code = last_socket_error ();
        if (sent < 0 && (would_block (code) || interrupted (code)))
          {
            BLB_TRY (wait_socket (socket, true, deadline, token));
            continue;
          }
        return socket_error ("cannot send to server", code);
      }
    return ok ();
  }

  result<void>
  fill ()
  {
    char chunk[16384];
    for (;;)
      {
        auto got = ::recv (socket, chunk, static_cast<int> (sizeof chunk), 0);
        if (got > 0)
          {
            pending.append (chunk, static_cast<std::size_t> (got));
            return ok ();
          }
        if (got == 0)
          return make_error (errc::network, "server closed the connection");
        int code = last_socket_error ();
        if (!would_block (code) && !interrupted (code))
          return socket_error ("cannot read from server", code);
        BLB_TRY (wait_socket (socket, false, deadline, token));
      }
  }

  result<std::string>
  take (std::size_t count)
  {
    while (pending.size () < count)
      BLB_TRY (fill ());
    std::string out = pending.substr (0, count);
    pending.erase (0, count);
    return out;
  }

  result<std::uint32_t>
  varint ()
  {
    std::uint32_t value = 0;
    for (int i = 0; i < 5; ++i)
      {
        BLB_TRY_ASSIGN (auto next, take (1));
        auto byte = static_cast<std::uint8_t> (next.front ());
        value |= static_cast<std::uint32_t> (byte & 0x7Fu) << (7 * i);
        if ((byte & 0x80u) == 0)
          return value;
      }
    return make_error (errc::parse, "invalid server response");
  }
};

std::size_t
utf8_length (unsigned char lead)
{
  if (lead < 0x80)
    return 1;
  if ((lead & 0xE0) == 0xC0)
    return 2;
  if ((lead & 0xF0) == 0xE0)
    return 3;
  if ((lead & 0xF8) == 0xF0)
    return 4;
  return 1;
}

std::string
strip_codes (std::string_view text)
{
  std::string out;
  out.reserve (text.size ());
  std::size_t i = 0;
  while (i < text.size ())
    {
      if (text.compare (i, 2, "\xC2\xA7") == 0)
        {
          i += 2;
          if (i < text.size ())
            i += utf8_length (static_cast<unsigned char> (text[i]));
          continue;
        }
      if (text[i] != '\r')
        out += text[i];
      ++i;
    }
  return out;
}

void
flatten (const nlohmann::json &node, std::string &out, int depth)
{
  if (depth > 32)
    return;
  if (node.is_string ())
    out += node.get<std::string> ();
  else if (node.is_number ())
    out += node.dump ();
  else if (node.is_array ())
    for (const auto &item : node)
      flatten (item, out, depth + 1);
  else if (node.is_object ())
    {
      if (auto text = node.find ("text"); text != node.end ())
        flatten (*text, out, depth + 1);
      if (auto extra = node.find ("extra");
          extra != node.end () && extra->is_array ())
        for (const auto &item : *extra)
          flatten (item, out, depth + 1);
    }
}

std::int32_t
int_field (const nlohmann::json &node, const char *key)
{
  auto it = node.find (key);
  if (it == node.end () || !it->is_number ())
    return -1;
  return static_cast<std::int32_t> (it->get<double> ());
}

server_status
read_status (const nlohmann::json &doc)
{
  server_status out;
  if (auto version = doc.find ("version");
      version != doc.end () && version->is_object ())
    {
      if (auto name = version->find ("name");
          name != version->end () && name->is_string ())
        out.version = strip_codes (name->get<std::string> ());
      out.protocol = int_field (*version, "protocol");
    }
  if (auto players = doc.find ("players");
      players != doc.end () && players->is_object ())
    {
      out.online = int_field (*players, "online");
      out.max = int_field (*players, "max");
    }
  if (auto description = doc.find ("description"); description != doc.end ())
    {
      std::string text;
      flatten (*description, text, 0);
      out.motd = strip_codes (text);
    }
  if (auto favicon = doc.find ("favicon");
      favicon != doc.end () && favicon->is_string ())
    {
      std::string_view value = favicon->get_ref<const std::string &> ();
      auto comma = value.find (',');
      if (value.starts_with ("data:image/png;base64")
          && comma != std::string_view::npos && value.size () <= max_favicon)
        {
          std::string encoded;
          for (char c : value.substr (comma + 1))
            if (c != '\n' && c != '\r' && c != ' ')
              encoded += c;
          if (auto bytes = base64_decode (encoded))
            out.favicon = std::move (*bytes);
        }
    }
  return out;
}

}

std::optional<server_address>
parse_server_address (std::string_view text)
{
  text = trim (text);
  server_address out;
  std::string_view port;
  if (text.starts_with ('['))
    {
      auto close = text.find (']');
      if (close == std::string_view::npos)
        return std::nullopt;
      out.host = std::string (text.substr (1, close - 1));
      auto rest = text.substr (close + 1);
      if (!rest.empty ())
        {
          if (rest.front () != ':')
            return std::nullopt;
          port = rest.substr (1);
          out.explicit_port = true;
        }
    }
  else if (std::count (text.begin (), text.end (), ':') == 1)
    {
      auto colon = text.find (':');
      out.host = std::string (text.substr (0, colon));
      port = text.substr (colon + 1);
      out.explicit_port = true;
    }
  else
    out.host = std::string (text);
  if (out.host.empty () || out.host.size () > 255
      || out.host.find_first_of (" \t/\\") != std::string::npos)
    return std::nullopt;
  if (out.explicit_port)
    {
      auto number = parse_int (port);
      if (!number || *number < 1 || *number > 65535)
        return std::nullopt;
      out.port = static_cast<std::uint16_t> (*number);
    }
  return out;
}

result<server_status>
ping_server (std::string_view text, cancellation_token token,
             std::chrono::milliseconds timeout)
{
  BLB_TRY (token.check ());
  auto address = parse_server_address (text);
  if (!address)
    return make_error (errc::invalid_argument, "invalid server address");
  start_sockets ();
  auto deadline = clock::now () + timeout;
  BLB_TRY_ASSIGN (auto endpoints, lookup (*address, deadline, token));
  BLB_TRY_ASSIGN (auto connected,
                  open_connection (endpoints, deadline, token));
  stream io { connected.get (), deadline, token, {} };

  std::string handshake;
  put_varint (handshake, 0x00);
  put_varint (handshake, static_cast<std::uint32_t> (-1));
  put_string (handshake, address->host);
  handshake.push_back (static_cast<char> (address->port >> 8));
  handshake.push_back (static_cast<char> (address->port & 0xFF));
  put_varint (handshake, 1);
  std::string request;
  put_varint (request, 0x00);
  auto started = clock::now ();
  BLB_TRY (io.send_all (packet (handshake) + packet (request)));
  BLB_TRY_ASSIGN (auto length, io.varint ());
  if (length == 0 || length > max_packet)
    return make_error (errc::parse, "invalid server response");
  BLB_TRY_ASSIGN (auto body, io.take (length));
  auto latency = clock::now () - started;
  std::string_view view = body;
  auto id = take_varint (view);
  auto size = id && *id == 0 ? take_varint (view) : std::nullopt;
  if (!size || *size > view.size ())
    return make_error (errc::parse, "invalid server response");
  auto doc = nlohmann::json::parse (view.substr (0, *size), nullptr, false);
  if (!doc.is_object ())
    return make_error (errc::parse, "invalid server response");
  server_status status = read_status (doc);

  std::string ping;
  put_varint (ping, 0x01);
  auto stamp = static_cast<std::uint64_t> (
      std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ())
          .count ());
  for (int shift = 56; shift >= 0; shift -= 8)
    ping.push_back (static_cast<char> ((stamp >> shift) & 0xFF));
  io.deadline = std::min (deadline, clock::now () + std::chrono::seconds (2));
  auto sent = clock::now ();
  if (io.send_all (packet (ping)))
    if (auto pong_length = io.varint (); pong_length && *pong_length == 9)
      if (auto pong = io.take (9); pong && pong->front () == 0x01)
        latency = clock::now () - sent;
  status.latency
      = std::chrono::duration_cast<std::chrono::milliseconds> (latency);
  return status;
}

}
