#include <core/base/strings.hpp>

#include "core/base/win_util.hpp"

#include <algorithm>
#include <charconv>
#include <random>

namespace b_launcher_base
{

namespace
{

constexpr char hex_digits[] = "0123456789abcdef";
constexpr char upper_hex_digits[] = "0123456789ABCDEF";

int
hex_value (char c) noexcept
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

bool
is_digit (char c) noexcept
{
  return c >= '0' && c <= '9';
}

char
lower_ascii (char c) noexcept
{
  return c >= 'A' && c <= 'Z' ? static_cast<char> (c - 'A' + 'a') : c;
}

}

std::string
to_lower (std::string_view text)
{
  std::string out (text);
  for (auto &c : out)
    c = lower_ascii (c);
  return out;
}

bool
iequals (std::string_view a, std::string_view b) noexcept
{
  if (a.size () != b.size ())
    return false;
  for (std::size_t i = 0; i < a.size (); ++i)
    if (lower_ascii (a[i]) != lower_ascii (b[i]))
      return false;
  return true;
}

std::string_view
trim (std::string_view text) noexcept
{
  auto space = [] (char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!text.empty () && space (text.front ()))
    text.remove_prefix (1);
  while (!text.empty () && space (text.back ()))
    text.remove_suffix (1);
  return text;
}

std::vector<std::string_view>
split (std::string_view text, char separator)
{
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  for (;;)
    {
      auto pos = text.find (separator, start);
      if (pos == std::string_view::npos)
        {
          parts.push_back (text.substr (start));
          return parts;
        }
      parts.push_back (text.substr (start, pos - start));
      start = pos + 1;
    }
}

std::string
join (std::span<const std::string> parts, std::string_view separator)
{
  std::string out;
  for (std::size_t i = 0; i < parts.size (); ++i)
    {
      if (i)
        out += separator;
      out += parts[i];
    }
  return out;
}

std::string
replace_all (std::string_view text, std::string_view from, std::string_view to)
{
  std::string out;
  if (from.empty ())
    return std::string (text);
  std::size_t start = 0;
  for (;;)
    {
      auto pos = text.find (from, start);
      if (pos == std::string_view::npos)
        {
          out += text.substr (start);
          return out;
        }
      out += text.substr (start, pos - start);
      out += to;
      start = pos + from.size ();
    }
}

std::optional<std::int64_t>
parse_int (std::string_view text) noexcept
{
  text = trim (text);
  std::int64_t value = 0;
  auto [ptr, ec]
      = std::from_chars (text.data (), text.data () + text.size (), value);
  if (ec != std::errc () || ptr != text.data () + text.size ())
    return std::nullopt;
  return value;
}

std::string
to_hex (std::span<const std::uint8_t> bytes)
{
  std::string out (bytes.size () * 2, '0');
  for (std::size_t i = 0; i < bytes.size (); ++i)
    {
      out[i * 2] = hex_digits[bytes[i] >> 4];
      out[i * 2 + 1] = hex_digits[bytes[i] & 15];
    }
  return out;
}

bool
from_hex (std::string_view text, std::span<std::uint8_t> out)
{
  if (text.size () != out.size () * 2)
    return false;
  for (std::size_t i = 0; i < out.size (); ++i)
    {
      int hi = hex_value (text[i * 2]);
      int lo = hex_value (text[i * 2 + 1]);
      if (hi < 0 || lo < 0)
        return false;
      out[i] = static_cast<std::uint8_t> (hi << 4 | lo);
    }
  return true;
}

std::string
base64_encode (std::span<const std::uint8_t> bytes, bool url_safe, bool pad)
{
  const char *alphabet = url_safe ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmno"
                                    "pqrstuvwxyz0123456789-_"
                                  : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmno"
                                    "pqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve ((bytes.size () + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes.size (); i += 3)
    {
      std::uint32_t v = bytes[i] << 16 | bytes[i + 1] << 8 | bytes[i + 2];
      out += alphabet[v >> 18 & 63];
      out += alphabet[v >> 12 & 63];
      out += alphabet[v >> 6 & 63];
      out += alphabet[v & 63];
    }
  auto rest = bytes.size () - i;
  if (rest == 1)
    {
      std::uint32_t v = bytes[i] << 16;
      out += alphabet[v >> 18 & 63];
      out += alphabet[v >> 12 & 63];
      if (pad)
        out += "==";
    }
  else if (rest == 2)
    {
      std::uint32_t v = bytes[i] << 16 | bytes[i + 1] << 8;
      out += alphabet[v >> 18 & 63];
      out += alphabet[v >> 12 & 63];
      out += alphabet[v >> 6 & 63];
      if (pad)
        out += '=';
    }
  return out;
}

std::optional<std::string>
base64_decode (std::string_view text)
{
  auto decode_char = [] (char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '+' || c == '-')
      return 62;
    if (c == '/' || c == '_')
      return 63;
    return -1;
  };
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : text)
    {
      if (c == '=' || c == '\n' || c == '\r')
        continue;
      int v = decode_char (c);
      if (v < 0)
        return std::nullopt;
      buffer = buffer << 6 | static_cast<std::uint32_t> (v);
      bits += 6;
      if (bits >= 8)
        {
          bits -= 8;
          out += static_cast<char> (buffer >> bits & 0xff);
        }
    }
  return out;
}

std::string
url_encode (std::string_view text)
{
  std::string out;
  out.reserve (text.size () * 3);
  for (unsigned char c : text)
    {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
          || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
          || c == '~')
        out += static_cast<char> (c);
      else
        {
          out += '%';
          out += upper_hex_digits[c >> 4];
          out += upper_hex_digits[c & 15];
        }
    }
  return out;
}

std::string
form_encode (std::span<const std::pair<std::string, std::string>> fields)
{
  std::string out;
  for (const auto &[key, value] : fields)
    {
      if (!out.empty ())
        out += '&';
      out += url_encode (key);
      out += '=';
      out += url_encode (value);
    }
  return out;
}

void
random_bytes (std::span<std::uint8_t> out)
{
  thread_local std::random_device device;
  std::uniform_int_distribution<unsigned> dist (0, 255);
  for (auto &b : out)
    b = static_cast<std::uint8_t> (dist (device));
}

std::string
random_bytes_hex (std::size_t count)
{
  std::vector<std::uint8_t> bytes (count);
  random_bytes (bytes);
  return to_hex (bytes);
}

std::filesystem::path
path_from_utf8 (std::string_view text)
{
#if defined(_WIN32)
  return std::filesystem::path (detail::utf8_to_wide (text));
#else
  return std::filesystem::path (std::string (text));
#endif
}

std::string
path_to_utf8 (const std::filesystem::path &path)
{
#if defined(_WIN32)
  auto text = detail::wide_to_utf8 (path.native ());
  std::replace (text.begin (), text.end (), '\\', '/');
  return text;
#else
  return path.native ();
#endif
}

std::string
path_to_native (const std::filesystem::path &path)
{
#if defined(_WIN32)
  auto text = detail::wide_to_utf8 (path.native ());
  std::replace (text.begin (), text.end (), '/', '\\');
  return text;
#else
  return path.native ();
#endif
}

int
compare_versions (std::string_view a, std::string_view b)
{
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a.size () && j < b.size ())
    {
      if (is_digit (a[i]) && is_digit (b[j]))
        {
          auto si = i;
          auto sj = j;
          while (i < a.size () && is_digit (a[i]))
            ++i;
          while (j < b.size () && is_digit (b[j]))
            ++j;
          auto na = a.substr (si, i - si);
          auto nb = b.substr (sj, j - sj);
          while (na.size () > 1 && na.front () == '0')
            na.remove_prefix (1);
          while (nb.size () > 1 && nb.front () == '0')
            nb.remove_prefix (1);
          if (na.size () != nb.size ())
            return na.size () < nb.size () ? -1 : 1;
          if (auto c = na.compare (nb); c != 0)
            return c < 0 ? -1 : 1;
          continue;
        }
      if (a[i] != b[j])
        {
          if (a[i] == '.' || a[i] == '-')
            return 1;
          if (b[j] == '.' || b[j] == '-')
            return -1;
          return a[i] < b[j] ? -1 : 1;
        }
      ++i;
      ++j;
    }
  if (i < a.size ())
    return a[i] == '-' ? -1 : 1;
  if (j < b.size ())
    return b[j] == '-' ? 1 : -1;
  return 0;
}

}
