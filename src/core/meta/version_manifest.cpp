#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/meta/version_manifest.hpp>
#include <core/network/http_client.hpp>

#include <simdjson.h>

namespace b_launcher_base::meta
{

namespace
{

std::string
take_string (simdjson::ondemand::value value)
{
  std::string_view text = value.get_string ();
  return std::string (text);
}

}

const manifest_version *
version_manifest::find (std::string_view id) const
{
  for (const auto &v : versions)
    if (v.id == id)
      return &v;
  return nullptr;
}

result<version_manifest>
parse_version_manifest (std::string_view json)
{
  try
    {
      simdjson::ondemand::parser parser;
      simdjson::padded_string padded (json);
      auto doc = parser.iterate (padded);
      version_manifest manifest;
      for (auto field : doc.get_object ())
        {
          std::string_view key = field.unescaped_key ();
          if (key == "latest")
            {
              for (auto latest : field.value ().get_object ())
                {
                  std::string_view name = latest.unescaped_key ();
                  if (name == "release")
                    manifest.latest_release = take_string (latest.value ());
                  else if (name == "snapshot")
                    manifest.latest_snapshot = take_string (latest.value ());
                }
            }
          else if (key == "versions")
            {
              for (auto entry : field.value ().get_array ())
                {
                  manifest_version v;
                  for (auto item : entry.get_object ())
                    {
                      std::string_view name = item.unescaped_key ();
                      if (name == "id")
                        v.id = take_string (item.value ());
                      else if (name == "type")
                        v.type = take_string (item.value ());
                      else if (name == "url")
                        v.url = take_string (item.value ());
                      else if (name == "time")
                        v.time = take_string (item.value ());
                      else if (name == "releaseTime")
                        v.release_time = take_string (item.value ());
                      else if (name == "sha1")
                        v.sha1 = take_string (item.value ());
                      else if (name == "complianceLevel")
                        v.compliance_level
                            = static_cast<int> (item.value ().get_int64 ());
                    }
                  manifest.versions.push_back (std::move (v));
                }
            }
        }
      return manifest;
    }
  catch (const simdjson::simdjson_error &e)
    {
      return make_error (errc::parse,
                         sformat ("invalid version manifest: {}", e.what ()));
    }
}

result<version_manifest>
fetch_version_manifest (const net::mirror_provider &mirror,
                        cancellation_token token)
{
  auto urls = mirror.resolve (version_manifest_url);
  BLB_TRY_ASSIGN (auto body, net::http_get (urls, token));
  return parse_version_manifest (body);
}

}
