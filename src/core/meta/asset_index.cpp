#include <core/base/format.hpp>
#include <core/base/strings.hpp>
#include <core/meta/asset_index.hpp>

#include <simdjson.h>

namespace b_launcher_base::meta
{

result<asset_index>
parse_asset_index (std::string_view json)
{
  try
    {
      simdjson::ondemand::parser parser;
      simdjson::padded_string padded (json);
      auto doc = parser.iterate (padded);
      asset_index index;
      for (auto field : doc.get_object ())
        {
          std::string_view key = field.unescaped_key ();
          if (key == "virtual")
            index.is_virtual = field.value ().get_bool ();
          else if (key == "map_to_resources")
            index.map_to_resources = field.value ().get_bool ();
          else if (key == "objects")
            {
              simdjson::ondemand::object objects
                  = field.value ().get_object ();
              for (auto entry : objects)
                {
                  asset_object object;
                  object.name = std::string (
                      std::string_view (entry.unescaped_key ()));
                  simdjson::ondemand::object body
                      = entry.value ().get_object ();
                  bool has_hash = false;
                  for (auto item : body)
                    {
                      std::string_view item_key = item.unescaped_key ();
                      if (item_key == "hash")
                        {
                          std::string_view hash = item.value ().get_string ();
                          has_hash = from_hex (hash, object.hash);
                        }
                      else if (item_key == "size")
                        object.size = item.value ().get_uint64 ();
                    }
                  if (!has_hash)
                    return make_error (
                        errc::parse,
                        sformat ("asset '{}' has no hash", object.name));
                  index.objects.push_back (std::move (object));
                }
            }
        }
      return index;
    }
  catch (const simdjson::simdjson_error &e)
    {
      return make_error (errc::parse,
                         sformat ("invalid asset index: {}", e.what ()));
    }
}

std::string
asset_object_path (const sha1_digest &hash)
{
  auto hex = to_hex (hash);
  return hex.substr (0, 2) + "/" + hex;
}

std::string
asset_object_url (const sha1_digest &hash)
{
  return std::string (asset_object_base_url) + asset_object_path (hash);
}

}
