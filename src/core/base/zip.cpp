#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>

#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <ctime>

namespace b_launcher_base
{

namespace
{

error
zip_error (std::string_view what, std::int32_t code)
{
  return make_error (errc::zip, sformat ("{} (minizip {})", what, code));
}

zip_entry
to_entry (const mz_zip_file *info)
{
  zip_entry entry;
  entry.name = info->filename ? info->filename : "";
  entry.size = static_cast<std::uint64_t> (info->uncompressed_size);
  entry.compressed_size = static_cast<std::uint64_t> (info->compressed_size);
  entry.crc = info->crc;
  entry.directory = !entry.name.empty () && entry.name.back () == '/';
  return entry;
}

}

zip_reader::zip_reader (zip_reader &&other) noexcept
    : m_handle (other.m_handle)
    , m_memory (std::move (other.m_memory))
{
  other.m_handle = nullptr;
}

zip_reader &
zip_reader::operator= (zip_reader &&other) noexcept
{
  if (this != &other)
    {
      release ();
      m_handle = other.m_handle;
      m_memory = std::move (other.m_memory);
      other.m_handle = nullptr;
    }
  return *this;
}

zip_reader::~zip_reader ()
{
  release ();
}

void
zip_reader::release () noexcept
{
  if (!m_handle)
    return;
  mz_zip_reader_close (m_handle);
  mz_zip_reader_delete (&m_handle);
  m_handle = nullptr;
}

result<zip_reader>
zip_reader::open (const std::filesystem::path &path)
{
  zip_reader reader;
  reader.m_handle = mz_zip_reader_create ();
  if (!reader.m_handle)
    return make_error (errc::zip, "cannot create zip reader");
  auto utf8 = path_to_utf8 (path);
  auto rc = mz_zip_reader_open_file (reader.m_handle, utf8.c_str ());
  if (rc != MZ_OK)
    {
      auto e = zip_error (sformat ("cannot open zip '{}'", utf8), rc);
      if (!fs::exists (path))
        e.code = errc::not_found;
      return e;
    }
  return reader;
}

result<zip_reader>
zip_reader::open_memory (std::string data)
{
  zip_reader reader;
  reader.m_memory = std::make_unique<std::string> (std::move (data));
  reader.m_handle = mz_zip_reader_create ();
  if (!reader.m_handle)
    return make_error (errc::zip, "cannot create zip reader");
  auto rc = mz_zip_reader_open_buffer (
      reader.m_handle,
      reinterpret_cast<const std::uint8_t *> (reader.m_memory->data ()),
      static_cast<std::int32_t> (reader.m_memory->size ()), 0);
  if (rc != MZ_OK)
    return zip_error ("cannot open zip buffer", rc);
  return reader;
}

result<std::vector<zip_entry>>
zip_reader::entries ()
{
  std::vector<zip_entry> list;
  BLB_TRY (for_each ([&] (const zip_entry &entry, zip_reader &) {
    list.push_back (entry);
    return true;
  }));
  return list;
}

result<void>
zip_reader::for_each (
    const std::function<bool (const zip_entry &, zip_reader &)> &visit)
{
  auto rc = mz_zip_reader_goto_first_entry (m_handle);
  while (rc == MZ_OK)
    {
      mz_zip_file *info = nullptr;
      if (mz_zip_reader_entry_get_info (m_handle, &info) != MZ_OK || !info)
        return make_error (errc::zip, "cannot read zip entry info");
      if (!visit (to_entry (info), *this))
        return ok ();
      rc = mz_zip_reader_goto_next_entry (m_handle);
    }
  if (rc != MZ_END_OF_LIST)
    return zip_error ("cannot iterate zip", rc);
  return ok ();
}

result<void>
zip_reader::locate (std::string_view name)
{
  std::string key (name);
  if (!key.empty () && key.front () == '/')
    key.erase (0, 1);
  auto rc = mz_zip_reader_locate_entry (m_handle, key.c_str (), 0);
  if (rc != MZ_OK)
    return make_error (errc::not_found,
                       sformat ("zip entry '{}' not found", key));
  return ok ();
}

bool
zip_reader::contains (std::string_view name)
{
  return locate (name).has_value ();
}

result<std::string>
zip_reader::read_current ()
{
  auto length = mz_zip_reader_entry_save_buffer_length (m_handle);
  if (length < 0)
    return zip_error ("cannot get zip entry length", length);
  std::string data (static_cast<std::size_t> (length), '\0');
  if (length == 0)
    return data;
  auto rc = mz_zip_reader_entry_save_buffer (m_handle, data.data (), length);
  if (rc != MZ_OK)
    return zip_error ("cannot read zip entry", rc);
  return data;
}

result<void>
zip_reader::extract_current (const std::filesystem::path &target)
{
  BLB_TRY_ASSIGN (auto data, read_current ());
  return fs::write_file (target, data);
}

result<std::string>
zip_reader::read (std::string_view name)
{
  BLB_TRY (locate (name));
  return read_current ();
}

zip_writer::zip_writer (zip_writer &&other) noexcept
    : m_handle (other.m_handle)
{
  other.m_handle = nullptr;
}

zip_writer &
zip_writer::operator= (zip_writer &&other) noexcept
{
  if (this != &other)
    {
      (void)close ();
      m_handle = other.m_handle;
      other.m_handle = nullptr;
    }
  return *this;
}

zip_writer::~zip_writer ()
{
  (void)close ();
}

result<zip_writer>
zip_writer::create (const std::filesystem::path &path)
{
  if (path.has_parent_path ())
    BLB_TRY (fs::create_directories (path.parent_path ()));
  (void)fs::remove_file (path);
  zip_writer writer;
  writer.m_handle = mz_zip_writer_create ();
  if (!writer.m_handle)
    return make_error (errc::zip, "cannot create zip writer");
  auto utf8 = path_to_utf8 (path);
  auto rc = mz_zip_writer_open_file (writer.m_handle, utf8.c_str (), 0, 0);
  if (rc != MZ_OK)
    return zip_error (sformat ("cannot create zip '{}'", utf8), rc);
  return writer;
}

bool
zip_writer::should_store (std::string_view name) noexcept
{
  auto dot = name.rfind ('.');
  if (dot == std::string_view::npos)
    return false;
  auto ext = to_lower (name.substr (dot + 1));
  for (auto stored : { "jar", "zip", "png", "jpg", "jpeg", "ogg", "mp3", "gz",
                       "xz", "7z", "mca", "disabled", "mrpack" })
    if (ext == stored)
      return true;
  return false;
}

result<void>
zip_writer::open_entry (std::string_view name, std::int64_t size,
                        std::int64_t modified)
{
  if (!m_handle)
    return make_error (errc::zip, "zip writer is closed");
  std::string entry_name (name);
  mz_zip_file info {};
  info.filename = entry_name.c_str ();
  info.modified_date = static_cast<time_t> (modified);
  info.version_madeby = MZ_VERSION_MADEBY;
  info.flag = MZ_ZIP_FLAG_UTF8;
  info.compression_method = should_store (name) ? MZ_COMPRESS_METHOD_STORE
                                                : MZ_COMPRESS_METHOD_DEFLATE;
  info.uncompressed_size = size;
  info.zip64 = size >= 0xffffffffLL ? MZ_ZIP64_FORCE : MZ_ZIP64_AUTO;
  auto rc = mz_zip_writer_entry_open (m_handle, &info);
  if (rc != MZ_OK)
    return zip_error (sformat ("cannot add '{}'", name), rc);
  return ok ();
}

result<void>
zip_writer::add_buffer (std::string_view name, std::string_view data)
{
  BLB_TRY (open_entry (name, static_cast<std::int64_t> (data.size ()),
                       std::time (nullptr)));
  std::size_t offset = 0;
  while (offset < data.size ())
    {
      auto chunk = std::min<std::size_t> (data.size () - offset, 1 << 20);
      auto rc = mz_zip_writer_entry_write (m_handle, data.data () + offset,
                                           static_cast<std::int32_t> (chunk));
      if (rc < 0)
        return zip_error (sformat ("cannot write '{}'", name), rc);
      offset += chunk;
    }
  auto rc = mz_zip_writer_entry_close (m_handle);
  if (rc != MZ_OK)
    return zip_error (sformat ("cannot finish '{}'", name), rc);
  return ok ();
}

result<void>
zip_writer::add_file (std::string_view name,
                      const std::filesystem::path &source)
{
  BLB_TRY_ASSIGN (auto file,
                  fs::native_file::open (source, fs::native_file::mode::read));
  BLB_TRY_ASSIGN (auto size, file.size ());
  BLB_TRY (open_entry (name, static_cast<std::int64_t> (size),
                       std::time (nullptr)));
  std::string buffer (1 << 20, '\0');
  for (;;)
    {
      BLB_TRY_ASSIGN (auto n, file.read (buffer.data (), buffer.size ()));
      if (n == 0)
        break;
      auto rc = mz_zip_writer_entry_write (m_handle, buffer.data (),
                                           static_cast<std::int32_t> (n));
      if (rc < 0)
        return zip_error (sformat ("cannot write '{}'", name), rc);
    }
  auto rc = mz_zip_writer_entry_close (m_handle);
  if (rc != MZ_OK)
    return zip_error (sformat ("cannot finish '{}'", name), rc);
  return ok ();
}

result<void>
zip_writer::close ()
{
  if (!m_handle)
    return ok ();
  auto rc = mz_zip_writer_close (m_handle);
  mz_zip_writer_delete (&m_handle);
  m_handle = nullptr;
  if (rc != MZ_OK)
    return zip_error ("cannot finalize zip", rc);
  return ok ();
}

result<void>
zip_reader::extract (std::string_view name,
                     const std::filesystem::path &target)
{
  BLB_TRY (locate (name));
  return extract_current (target);
}

}
