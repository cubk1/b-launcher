#include <core/base/strings.hpp>
#include <core/meta/maven.hpp>

namespace b_launcher_base::meta
{

std::optional<maven_artifact>
maven_artifact::parse (std::string_view descriptor)
{
  descriptor = trim (descriptor);
  if (descriptor.size () >= 2 && descriptor.front () == '['
      && descriptor.back () == ']')
    descriptor = descriptor.substr (1, descriptor.size () - 2);
  maven_artifact out;
  auto at = descriptor.rfind ('@');
  if (at != std::string_view::npos)
    {
      out.extension = std::string (descriptor.substr (at + 1));
      descriptor = descriptor.substr (0, at);
    }
  auto parts = split (descriptor, ':');
  if (parts.size () < 3 || parts.size () > 4)
    return std::nullopt;
  for (auto part : parts)
    if (part.empty ())
      return std::nullopt;
  out.group = std::string (parts[0]);
  out.name = std::string (parts[1]);
  out.version = std::string (parts[2]);
  if (parts.size () == 4)
    out.classifier = std::string (parts[3]);
  if (out.extension.empty ())
    out.extension = "jar";
  return out;
}

std::string
maven_artifact::file_name () const
{
  std::string file = name + "-" + version;
  if (!classifier.empty ())
    file += "-" + classifier;
  file += "." + extension;
  return file;
}

std::string
maven_artifact::path () const
{
  return replace_all (group, ".", "/") + "/" + name + "/" + version + "/"
         + file_name ();
}

std::string
maven_artifact::descriptor () const
{
  std::string out = group + ":" + name + ":" + version;
  if (!classifier.empty ())
    out += ":" + classifier;
  if (extension != "jar")
    out += "@" + extension;
  return out;
}

std::string
maven_artifact::key () const
{
  std::string out = group + ":" + name;
  if (!classifier.empty ())
    out += ":" + classifier;
  return out;
}

maven_artifact
maven_artifact::with_classifier (std::string_view value) const
{
  maven_artifact copy = *this;
  copy.classifier = std::string (value);
  return copy;
}

}
