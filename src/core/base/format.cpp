#include <core/base/format.hpp>

#include <charconv>
#include <cstdio>

namespace b_launcher_base::detail
{

namespace
{

struct format_spec
{
  char fill = ' ';
  char align = 0;
  std::size_t width = 0;
  int precision = -1;
};

format_spec
parse_spec (std::string_view text)
{
  format_spec spec;
  if (text.size () >= 2
      && (text[1] == '<' || text[1] == '>' || text[1] == '^'))
    {
      spec.fill = text[0];
      spec.align = text[1];
      text.remove_prefix (2);
    }
  else if (!text.empty ()
           && (text[0] == '<' || text[0] == '>' || text[0] == '^'))
    {
      spec.align = text[0];
      text.remove_prefix (1);
    }
  if (!text.empty () && text[0] == '0')
    {
      spec.fill = '0';
      spec.align = '=';
      text.remove_prefix (1);
    }
  while (!text.empty () && text[0] >= '0' && text[0] <= '9')
    {
      spec.width = spec.width * 10 + static_cast<std::size_t> (text[0] - '0');
      text.remove_prefix (1);
    }
  if (!text.empty () && text[0] == '.')
    {
      text.remove_prefix (1);
      spec.precision = 0;
      while (!text.empty () && text[0] >= '0' && text[0] <= '9')
        {
          spec.precision = spec.precision * 10 + (text[0] - '0');
          text.remove_prefix (1);
        }
    }
  return spec;
}

void
pad (std::string &out, std::string_view body, const format_spec &spec,
     bool numeric)
{
  if (body.size () >= spec.width)
    {
      out += body;
      return;
    }
  auto missing = spec.width - body.size ();
  char align = spec.align ? spec.align : (numeric ? '>' : '<');
  if (align == '=')
    {
      if (!body.empty () && body[0] == '-')
        {
          out += '-';
          body.remove_prefix (1);
        }
      out.append (missing, spec.fill);
      out += body;
    }
  else if (align == '>')
    {
      out.append (missing, spec.fill);
      out += body;
    }
  else if (align == '^')
    {
      out.append (missing / 2, spec.fill);
      out += body;
      out.append (missing - missing / 2, spec.fill);
    }
  else
    {
      out += body;
      out.append (missing, spec.fill);
    }
}

void
append (std::string &out, const format_arg &arg, std::string_view spec_text)
{
  auto spec = parse_spec (spec_text);
  char buffer[64];
  switch (arg.type)
    {
    case format_arg::kind::text:
      pad (out, arg.text, spec, false);
      return;
    case format_arg::kind::boolean:
      pad (out, arg.unsigned_value ? "true" : "false", spec, false);
      return;
    case format_arg::kind::character:
      buffer[0] = static_cast<char> (arg.unsigned_value);
      pad (out, std::string_view (buffer, 1), spec, false);
      return;
    case format_arg::kind::signed_integer:
      {
        auto r = std::to_chars (buffer, buffer + sizeof (buffer),
                                arg.signed_value);
        pad (out, std::string_view (buffer, r.ptr), spec, true);
        return;
      }
    case format_arg::kind::unsigned_integer:
      {
        auto r = std::to_chars (buffer, buffer + sizeof (buffer),
                                arg.unsigned_value);
        pad (out, std::string_view (buffer, r.ptr), spec, true);
        return;
      }
    case format_arg::kind::floating:
      {
        int n = spec.precision >= 0
                    ? std::snprintf (buffer, sizeof (buffer), "%.*f",
                                     spec.precision, arg.floating_value)
                    : std::snprintf (buffer, sizeof (buffer), "%g",
                                     arg.floating_value);
        if (n < 0)
          n = 0;
        pad (out, std::string_view (buffer, static_cast<std::size_t> (n)),
             spec, true);
        return;
      }
    }
}

}

std::string
vformat (std::string_view pattern, std::span<const format_arg> args)
{
  std::string out;
  out.reserve (pattern.size () + args.size () * 16);
  std::size_t next = 0;
  std::size_t i = 0;
  while (i < pattern.size ())
    {
      char c = pattern[i];
      if (c == '}' && i + 1 < pattern.size () && pattern[i + 1] == '}')
        {
          out += '}';
          i += 2;
          continue;
        }
      if (c != '{')
        {
          out += c;
          ++i;
          continue;
        }
      if (i + 1 < pattern.size () && pattern[i + 1] == '{')
        {
          out += '{';
          i += 2;
          continue;
        }
      auto close = pattern.find ('}', i);
      if (close == std::string_view::npos)
        {
          out += pattern.substr (i);
          break;
        }
      auto field = pattern.substr (i + 1, close - i - 1);
      auto colon = field.find (':');
      auto index_text = field.substr (0, colon);
      auto spec = colon == std::string_view::npos ? std::string_view ()
                                                  : field.substr (colon + 1);
      std::size_t index = next;
      if (!index_text.empty ())
        std::from_chars (index_text.data (),
                         index_text.data () + index_text.size (), index);
      else
        ++next;
      if (index < args.size ())
        append (out, args[index], spec);
      i = close + 1;
    }
  return out;
}

}
