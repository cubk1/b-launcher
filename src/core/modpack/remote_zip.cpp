#include <core/base/format.hpp>
#include <core/base/strings.hpp>
#include <core/modpack/remote_zip.hpp>
#include <core/network/http_client.hpp>

#include <zlib.h>

#include <algorithm>
#include <charconv>
#include <climits>
#include <mutex>
#include <set>

namespace b_launcher_base::modpack
{

namespace
{

constexpr std::uint64_t tail_size = 256 * 1024;
constexpr std::uint64_t max_part = 64ull << 20;
constexpr std::uint64_t local_slack = 1024;

std::mutex s_refused_mutex;
std::set<std::string> s_refused;

std::string
host_of (std::string_view url)
{
  auto scheme = url.find ("://");
  if (scheme != std::string_view::npos)
    url.remove_prefix (scheme + 3);
  auto end = url.find_first_of ("/?#");
  return to_lower (url.substr (0, end));
}

bool
refused (std::string_view url)
{
  std::lock_guard lock (s_refused_mutex);
  return s_refused.contains (host_of (url));
}

void
refuse (std::string_view url)
{
  std::lock_guard lock (s_refused_mutex);
  s_refused.insert (host_of (url));
}

std::optional<std::uint64_t>
content_total (std::string_view value)
{
  value = trim (value);
  auto slash = value.rfind ('/');
  if (slash == std::string_view::npos)
    return std::nullopt;
  auto total = trim (value.substr (slash + 1));
  std::uint64_t size = 0;
  auto [end, error]
      = std::from_chars (total.data (), total.data () + total.size (), size);
  if (error != std::errc () || end != total.data () + total.size ())
    return std::nullopt;
  return size;
}

result<remote_range>
http_range (std::string_view url, std::uint64_t begin, std::uint64_t end,
            cancellation_token token)
{
  net::http_request request;
  request.url = std::string (url);
  request.headers.push_back ({ "Range", sformat ("bytes={}-{}", begin, end) });
  request.headers.push_back ({ "Accept-Encoding", "identity" });
  request.timeout = std::chrono::milliseconds (30000);
  request.max_response_size = max_part;
  BLB_TRY_ASSIGN (auto response, net::http_send (request, token));
  remote_range out;
  out.status = response.status;
  out.total = content_total (response.header ("Content-Range"));
  out.body = std::move (response.body);
  return out;
}

std::uint16_t
u16 (std::string_view data, std::size_t at)
{
  return static_cast<std::uint16_t> (
      static_cast<unsigned char> (data[at])
      | static_cast<unsigned char> (data[at + 1]) << 8);
}

std::uint32_t
u32 (std::string_view data, std::size_t at)
{
  return static_cast<std::uint32_t> (u16 (data, at))
         | static_cast<std::uint32_t> (u16 (data, at + 2)) << 16;
}

std::uint64_t
u64 (std::string_view data, std::size_t at)
{
  return static_cast<std::uint64_t> (u32 (data, at))
         | static_cast<std::uint64_t> (u32 (data, at + 4)) << 32;
}

result<std::string>
inflate_raw (std::string_view input, std::uint64_t size)
{
  if (size > max_part || input.size () > UINT_MAX)
    return make_error (errc::size_mismatch, "zip entry too large");
  std::string out (static_cast<std::size_t> (size), '\0');
  z_stream stream {};
  if (inflateInit2 (&stream, -MAX_WBITS) != Z_OK)
    return make_error (errc::zip, "inflate init failed");
  stream.next_in
      = reinterpret_cast<Bytef *> (const_cast<char *> (input.data ()));
  stream.avail_in = static_cast<uInt> (input.size ());
  stream.next_out = reinterpret_cast<Bytef *> (out.data ());
  stream.avail_out = static_cast<uInt> (out.size ());
  int code = inflate (&stream, Z_FINISH);
  auto produced = stream.total_out;
  inflateEnd (&stream);
  if (code != Z_STREAM_END || produced != size)
    return make_error (errc::zip, "corrupt zip entry");
  return out;
}

}

result<remote_zip>
remote_zip::open (std::vector<std::string> urls, std::uint64_t size_hint,
                  cancellation_token token, range_fetch fetch)
{
  remote_zip zip;
  zip.m_urls = std::move (urls);
  zip.m_fetch = fetch ? std::move (fetch) : range_fetch (http_range);
  if (zip.m_urls.empty ())
    return make_error (errc::invalid_argument, "no download address");
  auto probe = [&] () -> result<void> {
    zip.m_size = 0;
    zip.m_blocks.clear ();
    zip.m_entries.clear ();
    BLB_TRY_ASSIGN (auto first, zip.fetch (0, 0, token));
    if (first.status == 200)
      {
        zip.m_size = first.body.size ();
        zip.m_blocks.push_back ({ 0, std::move (first.body) });
        return {};
      }
    if (!first.total || *first.total == 0)
      return make_error (errc::network, "server did not report the size");
    zip.m_size = *first.total;
    return {};
  };
  if (size_hint == 0)
    {
      BLB_TRY (probe ());
      BLB_TRY (zip.load_directory (token));
      return zip;
    }
  zip.m_size = size_hint;
  auto loaded = zip.load_directory (token);
  if (!loaded)
    {
      if (loaded.err ().code == errc::cancelled)
        return std::move (loaded).err ();
      BLB_TRY (probe ());
      BLB_TRY (zip.load_directory (token));
    }
  return zip;
}

const std::vector<remote_entry> &
remote_zip::entries () const noexcept
{
  return m_entries;
}

const remote_entry *
remote_zip::find (std::string_view name) const noexcept
{
  for (const auto &entry : m_entries)
    if (entry.name == name)
      return &entry;
  return nullptr;
}

std::uint64_t
remote_zip::size () const noexcept
{
  return m_size;
}

std::uint64_t
remote_zip::downloaded () const noexcept
{
  return m_downloaded;
}

int
remote_zip::requests () const noexcept
{
  return m_requests;
}

result<remote_range>
remote_zip::fetch (std::uint64_t begin, std::uint64_t end,
                   cancellation_token token)
{
  std::vector<std::string> order;
  for (const auto &url : m_urls)
    if (!refused (url))
      order.push_back (url);
  for (const auto &url : m_urls)
    if (refused (url))
      order.push_back (url);
  error last = make_error (errc::network, "no download address");
  for (const auto &url : order)
    {
      auto got = m_fetch (url, begin, end, token);
      m_requests++;
      if (!got)
        {
          last = got.err ();
          if (last.code == errc::cancelled)
            return last;
          continue;
        }
      if (got->status == 206 || got->status == 200)
        {
          m_downloaded += got->body.size ();
          auto at = std::find (m_urls.begin (), m_urls.end (), url);
          if (at != m_urls.end ())
            std::rotate (m_urls.begin (), at, at + 1);
          return got;
        }
      if (got->status == 400 || got->status == 501)
        refuse (url);
      last = make_error (errc::http_status, sformat ("HTTP {}", got->status));
    }
  return last;
}

result<std::string>
remote_zip::bytes (std::uint64_t begin, std::uint64_t end,
                   cancellation_token token)
{
  if (m_size != 0 && end > m_size)
    end = m_size;
  if (begin >= end)
    return std::string ();
  for (const auto &b : m_blocks)
    if (b.start <= begin && end <= b.start + b.data.size ())
      return b.data.substr (static_cast<std::size_t> (begin - b.start),
                            static_cast<std::size_t> (end - begin));
  if (end - begin > max_part)
    return make_error (errc::size_mismatch, "zip entry too large");
  BLB_TRY_ASSIGN (auto got, fetch (begin, end - 1, token));
  if (got.status == 200)
    {
      m_size = got.body.size ();
      m_blocks.clear ();
      m_blocks.push_back ({ 0, std::move (got.body) });
      if (end > m_size)
        return make_error (errc::zip, "truncated zip archive");
      return m_blocks.back ().data.substr (
          static_cast<std::size_t> (begin),
          static_cast<std::size_t> (end - begin));
    }
  if (got.total && *got.total != m_size)
    return make_error (errc::size_mismatch, "archive size changed");
  if (got.body.size () != end - begin)
    return make_error (errc::size_mismatch, "unexpected range length");
  std::string out = got.body;
  m_blocks.push_back ({ begin, std::move (got.body) });
  return out;
}

result<void>
remote_zip::load_directory (cancellation_token token)
{
  std::uint64_t tail_start = m_size > tail_size ? m_size - tail_size : 0;
  BLB_TRY_ASSIGN (auto tail, bytes (tail_start, m_size, token));
  if (tail.size () < 22)
    return make_error (errc::zip, "not a zip archive");
  std::size_t at = std::string::npos;
  for (std::size_t i = tail.size () - 22 + 1; i-- > 0;)
    if (tail.compare (i, 4, "PK\x05\x06") == 0)
      {
        at = i;
        break;
      }
  if (at == std::string::npos)
    return make_error (errc::zip, "zip directory not found");
  std::uint64_t count = u16 (tail, at + 10);
  std::uint64_t directory_size = u32 (tail, at + 12);
  std::uint64_t directory_offset = u32 (tail, at + 16);
  if (count == 0xFFFF || directory_size == 0xFFFFFFFF
      || directory_offset == 0xFFFFFFFF)
    {
      if (at < 20 || tail.compare (at - 20, 4, "PK\x06\x07") != 0)
        return make_error (errc::zip, "zip64 locator not found");
      std::uint64_t record = u64 (tail, at - 20 + 8);
      BLB_TRY_ASSIGN (auto zip64, bytes (record, record + 56, token));
      if (zip64.size () < 56 || zip64.compare (0, 4, "PK\x06\x06") != 0)
        return make_error (errc::zip, "invalid zip64 directory");
      count = u64 (zip64, 32);
      directory_size = u64 (zip64, 40);
      directory_offset = u64 (zip64, 48);
    }
  if (directory_size > max_part || directory_offset > m_size
      || directory_size > m_size - directory_offset)
    return make_error (errc::zip, "invalid zip directory");
  BLB_TRY_ASSIGN (
      auto directory,
      bytes (directory_offset, directory_offset + directory_size, token));
  m_entries.clear ();
  std::size_t p = 0;
  while (p + 46 <= directory.size ()
         && directory.compare (p, 4, "PK\x01\x02") == 0)
    {
      remote_entry entry;
      entry.method = u16 (directory, p + 10);
      entry.crc = u32 (directory, p + 16);
      entry.compressed = u32 (directory, p + 20);
      entry.size = u32 (directory, p + 24);
      std::size_t name_length = u16 (directory, p + 28);
      std::size_t extra_length = u16 (directory, p + 30);
      std::size_t comment_length = u16 (directory, p + 32);
      entry.offset = u32 (directory, p + 42);
      std::size_t next = p + 46 + name_length + extra_length + comment_length;
      if (next > directory.size ())
        break;
      entry.name = replace_all (
          std::string_view (directory).substr (p + 46, name_length), "\\",
          "/");
      std::string_view extra = std::string_view (directory).substr (
          p + 46 + name_length, extra_length);
      std::size_t e = 0;
      while (e + 4 <= extra.size ())
        {
          std::size_t id = u16 (extra, e);
          std::size_t length = u16 (extra, e + 2);
          if (e + 4 + length > extra.size ())
            break;
          if (id == 0x0001)
            {
              std::size_t q = e + 4;
              auto take = [&] (std::uint64_t &field) {
                if (field == 0xFFFFFFFF && q + 8 <= e + 4 + length)
                  {
                    field = u64 (extra, q);
                    q += 8;
                  }
              };
              take (entry.size);
              take (entry.compressed);
              take (entry.offset);
            }
          e += 4 + length;
        }
      m_entries.push_back (std::move (entry));
      p = next;
    }
  if (m_entries.empty () && count != 0)
    return make_error (errc::zip, "invalid zip directory");
  return {};
}

result<std::string>
remote_zip::read (std::string_view name, cancellation_token token)
{
  const remote_entry *found = find (name);
  if (found == nullptr)
    return make_error (errc::not_found, sformat ("{} not found", name));
  remote_entry entry = *found;
  if (entry.method != 0 && entry.method != 8)
    return make_error (errc::unsupported, "unsupported zip compression");
  if (entry.compressed > max_part || entry.size > max_part)
    return make_error (errc::size_mismatch, "zip entry too large");
  std::uint64_t guess = entry.offset + 30 + entry.name.size () + local_slack
                        + entry.compressed;
  BLB_TRY_ASSIGN (auto head, bytes (entry.offset, guess, token));
  if (head.size () < 30 || head.compare (0, 4, "PK\x03\x04") != 0)
    return make_error (errc::zip, "invalid zip entry header");
  std::uint64_t start
      = 30 + static_cast<std::uint64_t> (u16 (head, 26)) + u16 (head, 28);
  std::string data;
  if (start + entry.compressed <= head.size ())
    data = head.substr (static_cast<std::size_t> (start),
                        static_cast<std::size_t> (entry.compressed));
  else
    {
      BLB_TRY_ASSIGN (data,
                      bytes (entry.offset + start,
                             entry.offset + start + entry.compressed, token));
    }
  if (data.size () != entry.compressed)
    return make_error (errc::zip, "truncated zip entry");
  std::string out;
  if (entry.method == 0)
    out = std::move (data);
  else
    {
      BLB_TRY_ASSIGN (out, inflate_raw (data, entry.size));
    }
  auto crc = crc32 (0L, Z_NULL, 0);
  std::size_t done = 0;
  while (done < out.size ())
    {
      auto chunk = static_cast<uInt> (
          std::min<std::size_t> (out.size () - done, 1u << 30));
      crc = crc32 (crc, reinterpret_cast<const Bytef *> (out.data () + done),
                   chunk);
      done += chunk;
    }
  if (static_cast<std::uint32_t> (crc) != entry.crc)
    return make_error (errc::checksum_mismatch, "zip entry checksum mismatch");
  return out;
}

}
