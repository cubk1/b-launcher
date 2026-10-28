#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/platform.hpp>
#include <core/base/process.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/installer/task_graph.hpp>

#include "core/installer/loader_impl.hpp"
#include "core/meta/json_util.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

namespace b_launcher_base::install::detail
{

namespace
{

constexpr log::logger s_log ("forge");

struct processor
{
  std::string jar;
  std::vector<std::string> classpath;
  std::vector<std::string> args;
  std::vector<std::pair<std::string, std::string>> outputs;
  std::vector<std::string> sides;
};

struct installer_source
{
  std::string full;
  std::string url;
  meta::maven_artifact artifact;
};

std::string
manifest_main_class (const std::string &manifest)
{
  for (auto line : split (manifest, '\n'))
    {
      line = trim (line);
      if (line.starts_with ("Main-Class:"))
        return std::string (trim (line.substr (11)));
    }
  return {};
}

std::string
tail_lines (const std::string &text, std::size_t count)
{
  auto lines = split (text, '\n');
  std::size_t start = lines.size () > count ? lines.size () - count : 0;
  std::string out;
  for (auto i = start; i < lines.size (); ++i)
    {
      out += lines[i];
      out += '\n';
    }
  return out;
}

class forge_job final : public loader_job
{
public:
  using loader_job::loader_job;

  result<void>
  prepare (loader_context &ctx) override
  {
    BLB_TRY (resolve_version (ctx));
    BLB_TRY_ASSIGN (auto source, locate_installer (ctx));
    m_installer_path = ctx.layout ().library (source.artifact.path ());
    BLB_TRY (download_file (
        ctx, source.url, m_installer_path,
        sformat ("{}-installer", loader_name (m_request.type))));
    BLB_TRY_ASSIGN (auto zip, zip_reader::open (m_installer_path));
    BLB_TRY_ASSIGN (auto profile_text, zip.read ("install_profile.json"));
    BLB_TRY_ASSIGN (m_profile, meta::detail::parse_json (profile_text));
    if (m_profile.contains ("install") && m_profile.contains ("versionInfo"))
      return prepare_legacy (ctx, zip);
    return prepare_modern (ctx, zip);
  }

  result<void>
  finalize (loader_context &ctx) override
  {
    if (m_legacy)
      return ok ();
    return run_processors (ctx);
  }

  std::optional<loader_patch>
  patch () const override
  {
    loader_patch p;
    p.type = m_request.type;
    p.version = m_request.version;
    p.json = m_version;
    p.primary = true;
    return p;
  }

private:
  result<installer_source>
  locate_installer (loader_context &ctx)
  {
    installer_source source;
    const auto &mc = ctx.minecraft;
    const auto &v = m_request.version;
    if (m_request.type == loader_type::cleanroom)
      {
        if (m_resolved && m_resolved->version == v)
          source.url = m_resolved->url;
        else
          {
            BLB_TRY_ASSIGN (auto list,
                            list_loader_versions (m_request.type, mc,
                                                  ctx.mirror (), ctx.token));
            for (const auto &entry : list)
              if (entry.version == v)
                source.url = entry.url;
          }
        if (source.url.empty ())
          return make_error (errc::loader_not_found,
                             sformat ("cleanroom {} not found", v));
        source.full = v;
        source.artifact
            = { "com.cleanroommc", "cleanroom", v, "installer", "jar" };
        return source;
      }
    if (m_request.type == loader_type::neoforge)
      {
        if (mc == "1.20.1")
          {
            source.full = v.starts_with ("1.20.1-") ? v : "1.20.1-" + v;
            source.artifact = { "net.neoforged", "forge", source.full,
                                "installer", "jar" };
          }
        else
          {
            source.full = v;
            source.artifact
                = { "net.neoforged", "neoforge", v, "installer", "jar" };
          }
        source.url = join_url ("https://maven.neoforged.net/releases",
                               source.artifact.path ());
        return source;
      }
    source.full = v.starts_with (mc + "-") ? v : mc + "-" + v;
    if (m_resolved && m_resolved->version == v && !m_resolved->extra.empty ())
      source.full = m_resolved->extra;
    else if (auto list = list_loader_versions (m_request.type, mc,
                                               ctx.mirror (), ctx.token))
      for (const auto &entry : *list)
        if (entry.version == v || entry.extra == v)
          source.full = entry.extra;
    source.artifact
        = { "net.minecraftforge", "forge", source.full, "installer", "jar" };
    source.url = join_url ("https://maven.minecraftforge.net",
                           source.artifact.path ());
    return source;
  }

  result<void>
  extract_bundled (loader_context &ctx, zip_reader &zip,
                   const std::vector<meta::library> &libraries)
  {
    for (const auto &lib : libraries)
      {
        bool remote = (lib.download && !lib.download->url.empty ())
                      || (!lib.download && !lib.url.empty ());
        if (remote)
          continue;
        auto relative = lib.relative_path ();
        auto entry = "maven/" + relative;
        if (!zip.contains (entry))
          continue;
        auto target = ctx.layout ().library (relative);
        if (lib.download && !lib.download->sha1.empty ())
          if (auto digest = sha1_file (target);
              digest && to_hex (*digest) == lib.download->sha1)
            continue;
        BLB_TRY (zip.extract (entry, target));
      }
    return ok ();
  }

  result<void>
  prepare_legacy (loader_context &ctx, zip_reader &zip)
  {
    m_legacy = true;
    const auto &install = m_profile["install"];
    m_version = meta::detail::version_from_json (m_profile["versionInfo"]);
    auto file_path = meta::detail::string_of (install, "filePath");
    auto coords = meta::detail::string_of (install, "path");
    auto artifact = meta::maven_artifact::parse (coords);
    if (!artifact)
      return make_error (errc::parse, "invalid forge install path");
    BLB_TRY (
        zip.extract (file_path, ctx.layout ().library (artifact->path ())));
    std::vector<meta::library> remote;
    for (auto &lib : m_version.libraries)
      {
        if (lib.artifact.group == artifact->group
            && lib.artifact.name == artifact->name)
          continue;
        if (lib.url.empty () && lib.artifact.group == "net.minecraftforge")
          lib.url = "https://maven.minecraftforge.net/";
        remote.push_back (lib);
      }
    if (m_version.inherits_from.empty ())
      m_version.inherits_from = ctx.minecraft;
    if (m_version.jar.empty ())
      m_version.jar = ctx.minecraft;
    return download_libraries (ctx, remote, "forge-libraries");
  }

  result<void>
  prepare_modern (loader_context &ctx, zip_reader &zip)
  {
    auto json_path
        = meta::detail::string_of (m_profile, "json", "/version.json");
    BLB_TRY_ASSIGN (auto version_text, zip.read (json_path));
    BLB_TRY_ASSIGN (m_version, meta::parse_version (version_text));
    if (auto *libs = meta::detail::member (m_profile, "libraries");
        libs && libs->is_array ())
      for (const auto &entry : *libs)
        m_profile_libraries.push_back (
            meta::detail::library_from_json (entry));
    BLB_TRY (extract_bundled (ctx, zip, m_profile_libraries));
    BLB_TRY (extract_bundled (ctx, zip, m_version.libraries));
    auto all = m_profile_libraries;
    all.insert (all.end (), m_version.libraries.begin (),
                m_version.libraries.end ());
    if (auto *processors = meta::detail::member (m_profile, "processors");
        processors && processors->is_array ())
      for (const auto &entry : *processors)
        {
          processor p;
          p.jar = meta::detail::string_of (entry, "jar");
          if (auto *cp = meta::detail::member (entry, "classpath"))
            for (const auto &c : *cp)
              p.classpath.push_back (c.get<std::string> ());
          if (auto *args = meta::detail::member (entry, "args"))
            for (const auto &a : *args)
              p.args.push_back (a.get<std::string> ());
          if (auto *outputs = meta::detail::member (entry, "outputs");
              outputs && outputs->is_object ())
            for (auto it = outputs->begin (); it != outputs->end (); ++it)
              p.outputs.emplace_back (it.key (), it->get<std::string> ());
          if (auto *sides = meta::detail::member (entry, "sides"))
            for (const auto &s : *sides)
              p.sides.push_back (s.get<std::string> ());
          if (!p.sides.empty ()
              && std::find (p.sides.begin (), p.sides.end (), "client")
                     == p.sides.end ())
            continue;
          m_processors.push_back (std::move (p));
        }
    m_zip_data.clear ();
    if (auto *data = meta::detail::member (m_profile, "data");
        data && data->is_object ())
      for (auto it = data->begin (); it != data->end (); ++it)
        m_zip_data.emplace (it.key (),
                            meta::detail::string_of (*it, "client"));
    s_log.info ("{} {}: {} libraries, {} processors",
                loader_name (m_request.type), m_request.version, all.size (),
                m_processors.size ());
    return download_libraries (
        ctx, all, sformat ("{}-libraries", loader_name (m_request.type)));
  }

  result<std::string>
  artifact_path (loader_context &ctx, std::string_view coords) const
  {
    auto artifact = meta::maven_artifact::parse (coords);
    if (!artifact)
      return make_error (errc::parse,
                         sformat ("invalid artifact '{}'", coords));
    return path_to_utf8 (ctx.layout ().library (artifact->path ()));
  }

  result<void>
  resolve_data (loader_context &ctx)
  {
    const auto &layout = ctx.layout ();
    m_data.clear ();
    m_data["SIDE"] = "client";
    m_data["MINECRAFT_JAR"]
        = path_to_utf8 (layout.version_jar (ctx.vanilla->jar_id ()));
    m_data["MINECRAFT_VERSION"] = ctx.minecraft;
    m_data["ROOT"] = path_to_utf8 (layout.root ());
    m_data["INSTALLER"] = path_to_utf8 (m_installer_path);
    m_data["LIBRARY_DIR"] = path_to_utf8 (layout.libraries ());
    BLB_TRY_ASSIGN (auto zip, zip_reader::open (m_installer_path));
    for (const auto &[key, value] : m_zip_data)
      {
        if (value.size () >= 2 && value.front () == '['
            && value.back () == ']')
          {
            BLB_TRY_ASSIGN (m_data[key], artifact_path (ctx, value));
          }
        else if (value.size () >= 2 && value.front () == '\''
                 && value.back () == '\'')
          m_data[key] = value.substr (1, value.size () - 2);
        else if (value.starts_with ('/'))
          {
            auto target = ctx.temp_dir / path_from_utf8 (value.substr (1));
            BLB_TRY (zip.extract (value, target));
            m_data[key] = path_to_utf8 (target);
          }
        else
          m_data[key] = value;
      }
    return ok ();
  }

  result<std::string>
  resolve_arg (loader_context &ctx, std::string_view arg) const
  {
    if (arg.size () >= 2 && arg.front () == '[' && arg.back () == ']')
      return artifact_path (ctx, arg);
    if (arg.size () >= 2 && arg.front () == '\'' && arg.back () == '\'')
      return std::string (arg.substr (1, arg.size () - 2));
    std::string out;
    std::size_t pos = 0;
    while (pos < arg.size ())
      {
        auto open = arg.find ('{', pos);
        if (open == std::string_view::npos)
          {
            out += arg.substr (pos);
            break;
          }
        auto close = arg.find ('}', open);
        if (close == std::string_view::npos)
          {
            out += arg.substr (pos);
            break;
          }
        out += arg.substr (pos, open - pos);
        auto key = std::string (arg.substr (open + 1, close - open - 1));
        auto it = m_data.find (key);
        if (it == m_data.end ())
          return make_error (errc::parse,
                             sformat ("unknown processor variable '{}'", key));
        out += it->second;
        pos = close + 1;
      }
    return out;
  }

  struct prepared_processor
  {
    std::size_t index = 0;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> outputs;
    std::set<std::string> files;
    std::set<std::string> writes;
    bool barrier = false;
    std::string mojmaps_output;
  };

  bool
  outputs_valid (const prepared_processor &p) const
  {
    if (p.outputs.empty ())
      return false;
    for (const auto &[file, sha] : p.outputs)
      {
        auto digest = sha1_file (path_from_utf8 (file));
        if (!digest || to_hex (*digest) != to_lower (sha))
          return false;
      }
    return true;
  }

  result<void>
  run_one (loader_context &ctx, const prepared_processor &p,
           const java::java_installation &java)
  {
    const auto &spec = m_processors[p.index];
    if (outputs_valid (p))
      {
        s_log.debug ("processor {} up to date", spec.jar);
        return ok ();
      }
    if (!p.mojmaps_output.empty ())
      {
        auto it = ctx.vanilla->downloads.find ("client_mappings");
        if (it == ctx.vanilla->downloads.end ())
          return make_error (errc::not_found,
                             "version has no client mappings");
        return download_file (ctx, it->second.url,
                              path_from_utf8 (p.mojmaps_output), "mojmaps",
                              it->second.sha1, it->second.size);
      }
    BLB_TRY_ASSIGN (auto jar, artifact_path (ctx, spec.jar));
    BLB_TRY_ASSIGN (auto jar_zip, zip_reader::open (path_from_utf8 (jar)));
    BLB_TRY_ASSIGN (auto manifest, jar_zip.read ("META-INF/MANIFEST.MF"));
    auto main_class = manifest_main_class (manifest);
    if (main_class.empty ())
      return make_error (errc::parse,
                         sformat ("processor {} has no main class", spec.jar));
    std::vector<std::string> classpath { jar };
    for (const auto &entry : spec.classpath)
      {
        BLB_TRY_ASSIGN (auto path, artifact_path (ctx, entry));
        classpath.push_back (std::move (path));
      }
    process_options options;
    options.args = { path_to_utf8 (java.executable), "-cp",
                     join (classpath, std::string (1, path_list_separator ())),
                     main_class };
    options.args.insert (options.args.end (), p.args.begin (), p.args.end ());
    options.working_directory = ctx.temp_dir;
    s_log.info ("running processor {}", spec.jar);
    s_log.debug ("processor command: {}", join (options.args, " "));
    BLB_TRY_ASSIGN (auto output, run_process (options, ctx.token));
    if (output.exit_code != 0)
      return make_error (errc::process,
                         sformat ("processor {} exited with {}:\n{}", spec.jar,
                                  output.exit_code,
                                  tail_lines (output.out + output.err, 20)));
    for (const auto &[file, sha] : p.outputs)
      {
        auto digest = sha1_file (path_from_utf8 (file));
        if (!digest || to_hex (*digest) != to_lower (sha))
          return make_error (errc::checksum_mismatch,
                             sformat ("processor output {} is invalid", file));
      }
    return ok ();
  }

  result<void>
  run_processors (loader_context &ctx)
  {
    if (m_processors.empty ())
      return ok ();
    BLB_TRY (resolve_data (ctx));
    static const std::set<std::string> output_flags
        = { "--output",  "--out",    "--slim",      "--extra",
            "--out-jar", "--outJar", "--outputFile" };
    std::vector<prepared_processor> prepared;
    for (std::size_t i = 0; i < m_processors.size (); ++i)
      {
        const auto &spec = m_processors[i];
        prepared_processor p;
        p.index = i;
        for (const auto &arg : spec.args)
          {
            BLB_TRY_ASSIGN (auto value, resolve_arg (ctx, arg));
            p.args.push_back (std::move (value));
          }
        for (const auto &[file, sha] : spec.outputs)
          {
            BLB_TRY_ASSIGN (auto f, resolve_arg (ctx, file));
            BLB_TRY_ASSIGN (auto s, resolve_arg (ctx, sha));
            p.writes.insert (to_lower (f));
            p.outputs.emplace_back (std::move (f), std::move (s));
          }
        for (std::size_t a = 0; a < p.args.size (); ++a)
          {
            const auto &value = p.args[a];
            if (value.find ('/') != std::string::npos
                || value.find ('\\') != std::string::npos)
              p.files.insert (to_lower (value));
            if (a > 0 && output_flags.contains (p.args[a - 1]))
              p.writes.insert (to_lower (value));
            if (value == "DOWNLOAD_MOJMAPS" && a > 0
                && p.args[a - 1] == "--task")
              for (std::size_t b = 0; b + 1 < p.args.size (); ++b)
                if (p.args[b] == "--output")
                  p.mojmaps_output = p.args[b + 1];
          }
        p.barrier = p.writes.empty ();
        prepared.push_back (std::move (p));
      }

    BLB_TRY_ASSIGN (auto java, ctx.java (*ctx.vanilla));
    task_graph graph;
    std::vector<std::size_t> ids;
    for (std::size_t i = 0; i < prepared.size (); ++i)
      {
        std::vector<std::size_t> deps;
        for (std::size_t j = 0; j < i; ++j)
          {
            bool conflict = prepared[i].barrier || prepared[j].barrier;
            for (const auto &w : prepared[j].writes)
              if (prepared[i].files.contains (w)
                  || prepared[i].writes.contains (w))
                conflict = true;
            for (const auto &w : prepared[i].writes)
              if (prepared[j].files.contains (w))
                conflict = true;
            if (conflict)
              deps.push_back (ids[j]);
          }
        auto name = sformat ("processor-{}:{}", i,
                             m_processors[prepared[i].index].jar);
        ids.push_back (graph.add (
            name,
            [this, &ctx, &prepared, i, &java] (cancellation_token) {
              return run_one (ctx, prepared[i], java);
            },
            std::move (deps)));
      }
    return graph.run (ctx.token, ctx.sink);
  }

  std::filesystem::path m_installer_path;
  json m_profile;
  bool m_legacy = false;
  meta::version m_version;
  std::vector<meta::library> m_profile_libraries;
  std::vector<processor> m_processors;
  std::map<std::string, std::string> m_zip_data;
  std::map<std::string, std::string> m_data;
};

}

std::unique_ptr<loader_job>
make_forge_job (loader_request request)
{
  return std::make_unique<forge_job> (std::move (request));
}

}
