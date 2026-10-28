#include <core/base/error.hpp>

namespace b_launcher_base
{

std::string_view
errc_name (errc code) noexcept
{
  switch (code)
    {
    case errc::ok:
      return "ok";
    case errc::unknown:
      return "unknown";
    case errc::invalid_argument:
      return "invalid_argument";
    case errc::io:
      return "io";
    case errc::not_found:
      return "not_found";
    case errc::already_exists:
      return "already_exists";
    case errc::parse:
      return "parse";
    case errc::network:
      return "network";
    case errc::http_status:
      return "http_status";
    case errc::checksum_mismatch:
      return "checksum_mismatch";
    case errc::size_mismatch:
      return "size_mismatch";
    case errc::cancelled:
      return "cancelled";
    case errc::timeout:
      return "timeout";
    case errc::unsupported:
      return "unsupported";
    case errc::process:
      return "process";
    case errc::zip:
      return "zip";
    case errc::auth:
      return "auth";
    case errc::auth_pending:
      return "auth_pending";
    case errc::auth_declined:
      return "auth_declined";
    case errc::auth_expired:
      return "auth_expired";
    case errc::no_xbox_account:
      return "no_xbox_account";
    case errc::child_account:
      return "child_account";
    case errc::no_minecraft_profile:
      return "no_minecraft_profile";
    case errc::java_not_found:
      return "java_not_found";
    case errc::version_not_found:
      return "version_not_found";
    case errc::loader_not_found:
      return "loader_not_found";
    case errc::incompatible:
      return "incompatible";
    }
  return "unknown";
}

std::string
error::to_string () const
{
  std::string text (errc_name (code));
  if (!message.empty ())
    {
      text += ": ";
      text += message;
    }
  return text;
}

}
