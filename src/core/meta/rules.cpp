#include <core/base/strings.hpp>
#include <core/meta/rules.hpp>

namespace b_launcher_base::meta
{

namespace
{

struct regex_atom
{
  enum class kind
  {
    literal,
    any,
    digit,
    word,
    space,
  };
  kind type = kind::literal;
  char value = 0;
  std::size_t length = 1;
};

regex_atom
read_atom (std::string_view pattern, std::size_t pos)
{
  regex_atom atom;
  char c = pattern[pos];
  if (c == '.')
    atom.type = regex_atom::kind::any;
  else if (c == '\\' && pos + 1 < pattern.size ())
    {
      atom.length = 2;
      char e = pattern[pos + 1];
      if (e == 'd')
        atom.type = regex_atom::kind::digit;
      else if (e == 'w')
        atom.type = regex_atom::kind::word;
      else if (e == 's')
        atom.type = regex_atom::kind::space;
      else
        atom.value = e;
    }
  else
    atom.value = c;
  return atom;
}

bool
atom_matches (const regex_atom &atom, char c)
{
  switch (atom.type)
    {
    case regex_atom::kind::any:
      return true;
    case regex_atom::kind::digit:
      return c >= '0' && c <= '9';
    case regex_atom::kind::word:
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
             || (c >= 'A' && c <= 'Z') || c == '_';
    case regex_atom::kind::space:
      return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    case regex_atom::kind::literal:
      return atom.value == c;
    }
  return false;
}

bool
match_here (std::string_view pattern, std::string_view text)
{
  if (pattern.empty ())
    return true;
  if (pattern == "$")
    return text.empty ();
  auto atom = read_atom (pattern, 0);
  auto rest = pattern.substr (atom.length);
  if (!rest.empty () && (rest[0] == '*' || rest[0] == '+' || rest[0] == '?'))
    {
      char op = rest[0];
      rest.remove_prefix (1);
      std::size_t count = 0;
      while (count < text.size () && atom_matches (atom, text[count])
             && !(op == '?' && count == 1))
        ++count;
      std::size_t min = op == '+' ? 1 : 0;
      for (std::size_t n = count + 1; n-- > min;)
        if (match_here (rest, text.substr (n)))
          return true;
      return false;
    }
  if (!text.empty () && atom_matches (atom, text[0]))
    return match_here (rest, text.substr (1));
  return false;
}

}

bool
simple_regex_search (std::string_view pattern, std::string_view text)
{
  if (!pattern.empty () && pattern[0] == '^')
    return match_here (pattern.substr (1), text);
  for (std::size_t i = 0; i <= text.size (); ++i)
    if (match_here (pattern, text.substr (i)))
      return true;
  return false;
}

rule_context
rule_context::current ()
{
  rule_context context;
  const auto &platform = current_platform ();
  context.os = platform.os;
  context.os_version = platform.os_version;
  context.arch = platform.arch;
  return context;
}

bool
rule_context::feature (std::string_view name) const
{
  auto it = features.find (name);
  return it != features.end () && it->second;
}

bool
rule_matches (const rule &value, const rule_context &context)
{
  if (value.os)
    {
      const auto &os = *value.os;
      if (!os.name.empty ())
        {
          auto name = to_lower (os.name);
          if (name == "macos")
            name = "osx";
          if (name != os_name (context.os))
            return false;
        }
      if (!os.version.empty ()
          && !simple_regex_search (os.version, context.os_version))
        return false;
      if (!os.arch.empty ())
        {
          auto arch = parse_arch (os.arch);
          if (!arch || *arch != context.arch)
            return false;
        }
    }
  for (const auto &[name, expected] : value.features)
    if (context.feature (name) != expected)
      return false;
  return true;
}

bool
rules_allow (std::span<const rule> rules, const rule_context &context)
{
  if (rules.empty ())
    return true;
  bool allowed = false;
  for (const auto &r : rules)
    if (rule_matches (r, context))
      allowed = r.allow;
  return allowed;
}

}
