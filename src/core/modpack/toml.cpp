#include "core/modpack/toml.hpp"

#include <core/base/format.hpp>
#include <core/base/strings.hpp>

#include <charconv>
#include <optional>
#include <vector>

namespace b_launcher_base::modpack::detail
{

namespace
{

using json = nlohmann::json;

class toml_parser
{
public:
  explicit toml_parser (std::string_view text)
      : m_text (text)
  {
  }

  result<json>
  parse ()
  {
    json root = json::object ();
    json *current = &root;
    while (true)
      {
        skip_blank_lines ();
        if (at_end ())
          break;
        if (peek () == '[')
          {
            bool array = m_text.substr (m_pos, 2) == "[[";
            m_pos += array ? 2 : 1;
            auto keys = parse_key_path ();
            if (!keys)
              return std::move (keys).err ();
            skip_spaces ();
            if (array)
              {
                if (m_text.substr (m_pos, 2) != "]]")
                  return fail ("expected ]]");
                m_pos += 2;
              }
            else
              {
                if (peek () != ']')
                  return fail ("expected ]");
                ++m_pos;
              }
            auto *table = &root;
            for (std::size_t i = 0; i < keys->size (); ++i)
              {
                auto &next = (*table)[(*keys)[i]];
                bool last = i + 1 == keys->size ();
                if (last && array)
                  {
                    if (!next.is_array ())
                      next = json::array ();
                    next.push_back (json::object ());
                    table = &next.back ();
                  }
                else
                  {
                    if (next.is_array () && !next.empty ())
                      table = &next.back ();
                    else
                      {
                        if (!next.is_object ())
                          next = json::object ();
                        table = &next;
                      }
                  }
              }
            current = table;
            if (!finish_line ())
              return fail ("unexpected content after table header");
            continue;
          }
        auto keys = parse_key_path ();
        if (!keys)
          return std::move (keys).err ();
        skip_spaces ();
        if (peek () != '=')
          return fail ("expected =");
        ++m_pos;
        skip_spaces ();
        auto value = parse_value ();
        if (!value)
          return std::move (value).err ();
        auto *target = current;
        for (std::size_t i = 0; i + 1 < keys->size (); ++i)
          {
            auto &next = (*target)[(*keys)[i]];
            if (!next.is_object ())
              next = json::object ();
            target = &next;
          }
        (*target)[keys->back ()] = std::move (*value);
        if (!finish_line ())
          return fail ("unexpected content after value");
      }
    return root;
  }

private:
  bool
  at_end () const
  {
    return m_pos >= m_text.size ();
  }

  char
  peek () const
  {
    return at_end () ? '\0' : m_text[m_pos];
  }

  error
  fail (std::string_view what) const
  {
    std::size_t line = 1;
    for (std::size_t i = 0; i < m_pos && i < m_text.size (); ++i)
      if (m_text[i] == '\n')
        ++line;
    return make_error (errc::parse, sformat ("toml line {}: {}", line, what));
  }

  void
  skip_spaces ()
  {
    while (!at_end () && (peek () == ' ' || peek () == '\t'))
      ++m_pos;
  }

  void
  skip_comment ()
  {
    if (peek () == '#')
      while (!at_end () && peek () != '\n')
        ++m_pos;
  }

  void
  skip_blank_lines ()
  {
    while (!at_end ())
      {
        skip_spaces ();
        skip_comment ();
        if (peek () == '\r' || peek () == '\n')
          ++m_pos;
        else
          break;
      }
  }

  void
  skip_whitespace_and_comments ()
  {
    while (!at_end ())
      {
        char c = peek ();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
          ++m_pos;
        else if (c == '#')
          skip_comment ();
        else
          break;
      }
  }

  bool
  finish_line ()
  {
    skip_spaces ();
    skip_comment ();
    if (peek () == '\r')
      ++m_pos;
    if (at_end ())
      return true;
    if (peek () == '\n')
      {
        ++m_pos;
        return true;
      }
    return false;
  }

  result<std::string>
  parse_key ()
  {
    skip_spaces ();
    char c = peek ();
    if (c == '"' || c == '\'')
      return parse_string ();
    std::size_t start = m_pos;
    while (!at_end ())
      {
        c = peek ();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-')
          ++m_pos;
        else
          break;
      }
    if (start == m_pos)
      return fail ("expected key");
    return std::string (m_text.substr (start, m_pos - start));
  }

  result<std::vector<std::string>>
  parse_key_path ()
  {
    std::vector<std::string> keys;
    for (;;)
      {
        auto key = parse_key ();
        if (!key)
          return std::move (key).err ();
        keys.push_back (std::move (*key));
        skip_spaces ();
        if (peek () != '.')
          return keys;
        ++m_pos;
      }
  }

  void
  append_utf8 (std::string &out, std::uint32_t code)
  {
    if (code < 0x80)
      out += static_cast<char> (code);
    else if (code < 0x800)
      {
        out += static_cast<char> (0xC0 | (code >> 6));
        out += static_cast<char> (0x80 | (code & 0x3F));
      }
    else if (code < 0x10000)
      {
        out += static_cast<char> (0xE0 | (code >> 12));
        out += static_cast<char> (0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char> (0x80 | (code & 0x3F));
      }
    else
      {
        out += static_cast<char> (0xF0 | (code >> 18));
        out += static_cast<char> (0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char> (0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char> (0x80 | (code & 0x3F));
      }
  }

  result<std::string>
  parse_string ()
  {
    char quote = peek ();
    bool literal = quote == '\'';
    bool multi = m_text.substr (m_pos, 3) == std::string (3, quote);
    m_pos += multi ? 3 : 1;
    if (multi && peek () == '\r')
      ++m_pos;
    if (multi && peek () == '\n')
      ++m_pos;
    std::string out;
    while (!at_end ())
      {
        char c = peek ();
        if (multi && m_text.substr (m_pos, 3) == std::string (3, quote))
          {
            m_pos += 3;
            while (peek () == quote)
              {
                out += quote;
                ++m_pos;
              }
            return out;
          }
        if (!multi && c == quote)
          {
            ++m_pos;
            return out;
          }
        if (!multi && c == '\n')
          return fail ("unterminated string");
        if (!literal && c == '\\')
          {
            ++m_pos;
            char e = peek ();
            ++m_pos;
            switch (e)
              {
              case 'n':
                out += '\n';
                break;
              case 't':
                out += '\t';
                break;
              case 'r':
                out += '\r';
                break;
              case 'b':
                out += '\b';
                break;
              case 'f':
                out += '\f';
                break;
              case '"':
                out += '"';
                break;
              case '\\':
                out += '\\';
                break;
              case 'u':
              case 'U':
                {
                  std::size_t digits = e == 'u' ? 4 : 8;
                  std::uint32_t code = 0;
                  auto hex = m_text.substr (m_pos, digits);
                  std::from_chars (hex.data (), hex.data () + hex.size (),
                                   code, 16);
                  m_pos += digits;
                  append_utf8 (out, code);
                  break;
                }
              case '\r':
              case '\n':
              case ' ':
              case '\t':
                while (!at_end ()
                       && (peek () == ' ' || peek () == '\t' || peek () == '\r'
                           || peek () == '\n'))
                  ++m_pos;
                break;
              default:
                out += e;
                break;
              }
            continue;
          }
        out += c;
        ++m_pos;
      }
    return fail ("unterminated string");
  }

  result<json>
  parse_array ()
  {
    ++m_pos;
    json out = json::array ();
    for (;;)
      {
        skip_whitespace_and_comments ();
        if (peek () == ']')
          {
            ++m_pos;
            return out;
          }
        auto value = parse_value ();
        if (!value)
          return std::move (value).err ();
        out.push_back (std::move (*value));
        skip_whitespace_and_comments ();
        if (peek () == ',')
          {
            ++m_pos;
            continue;
          }
        if (peek () == ']')
          {
            ++m_pos;
            return out;
          }
        return fail ("expected , or ] in array");
      }
  }

  result<json>
  parse_inline_table ()
  {
    ++m_pos;
    json out = json::object ();
    for (;;)
      {
        skip_spaces ();
        if (peek () == '}')
          {
            ++m_pos;
            return out;
          }
        auto keys = parse_key_path ();
        if (!keys)
          return std::move (keys).err ();
        skip_spaces ();
        if (peek () != '=')
          return fail ("expected = in inline table");
        ++m_pos;
        skip_spaces ();
        auto value = parse_value ();
        if (!value)
          return std::move (value).err ();
        auto *target = &out;
        for (std::size_t i = 0; i + 1 < keys->size (); ++i)
          target = &(*target)[(*keys)[i]];
        (*target)[keys->back ()] = std::move (*value);
        skip_spaces ();
        if (peek () == ',')
          {
            ++m_pos;
            continue;
          }
        if (peek () == '}')
          {
            ++m_pos;
            return out;
          }
        return fail ("expected , or } in inline table");
      }
  }

  result<json>
  parse_value ()
  {
    char c = peek ();
    if (c == '"' || c == '\'')
      {
        auto text = parse_string ();
        if (!text)
          return std::move (text).err ();
        return json (std::move (*text));
      }
    if (c == '[')
      return parse_array ();
    if (c == '{')
      return parse_inline_table ();
    std::size_t start = m_pos;
    while (!at_end ())
      {
        c = peek ();
        if (c == ',' || c == ']' || c == '}' || c == '\n' || c == '\r'
            || c == '#')
          break;
        ++m_pos;
      }
    auto token = std::string (trim (m_text.substr (start, m_pos - start)));
    if (token == "true")
      return json (true);
    if (token == "false")
      return json (false);
    std::string digits;
    for (char d : token)
      if (d != '_')
        digits += d;
    std::int64_t integer = 0;
    auto [ptr, ec] = std::from_chars (
        digits.data (), digits.data () + digits.size (), integer);
    if (ec == std::errc () && ptr == digits.data () + digits.size ())
      return json (integer);
    if (!digits.empty ()
        && (digits.find ('.') != std::string::npos
            || digits.find ('e') != std::string::npos)
        && digits.find_first_not_of ("0123456789+-.eE") == std::string::npos)
      return json (std::strtod (digits.c_str (), nullptr));
    if (token.empty ())
      return fail ("missing value");
    return json (token);
  }

  std::string_view m_text;
  std::size_t m_pos = 0;
};

}

result<json>
parse_toml (std::string_view text)
{
  if (text.starts_with ("\xEF\xBB\xBF"))
    text.remove_prefix (3);
  return toml_parser (text).parse ();
}

}
