#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include "core/base/win_util.hpp"

#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace b_launcher_base
{

#if defined(_WIN32)

namespace detail
{

std::wstring
utf8_to_wide (std::string_view text)
{
  if (text.empty ())
    return {};
  int size = MultiByteToWideChar (CP_UTF8, 0, text.data (),
                                  static_cast<int> (text.size ()), nullptr, 0);
  std::wstring out (static_cast<std::size_t> (size), L'\0');
  MultiByteToWideChar (CP_UTF8, 0, text.data (),
                       static_cast<int> (text.size ()), out.data (), size);
  return out;
}

std::string
wide_to_utf8 (std::wstring_view text)
{
  if (text.empty ())
    return {};
  int size = WideCharToMultiByte (CP_UTF8, 0, text.data (),
                                  static_cast<int> (text.size ()), nullptr, 0,
                                  nullptr, nullptr);
  std::string out (static_cast<std::size_t> (size), '\0');
  WideCharToMultiByte (CP_UTF8, 0, text.data (),
                       static_cast<int> (text.size ()), out.data (), size,
                       nullptr, nullptr);
  return out;
}

std::wstring
native_path (const std::filesystem::path &path)
{
  std::wstring text = path.native ();
  for (auto &c : text)
    if (c == L'/')
      c = L'\\';
  if (text.size () < 240 || text.starts_with (L"\\\\?\\"))
    return text;
  if (text.starts_with (L"\\\\"))
    return L"\\\\?\\UNC\\" + text.substr (2);
  if (text.size () > 2 && text[1] == L':')
    return L"\\\\?\\" + text;
  return text;
}

std::string
last_error_message (unsigned long code)
{
  wchar_t *buffer = nullptr;
  auto length = FormatMessageW (
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
          | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t *> (&buffer), 0, nullptr);
  std::string text;
  if (length && buffer)
    {
      text = wide_to_utf8 (std::wstring_view (buffer, length));
      LocalFree (buffer);
    }
  while (!text.empty () && (text.back () == '\n' || text.back () == '\r'))
    text.pop_back ();
  return sformat ("{} (code {})", text, code);
}

}

namespace
{

error
os_error (std::string_view what, const fs::path &target,
          unsigned long code = GetLastError ())
{
  return make_error (errc::io,
                     sformat ("{} '{}': {}", what, path_to_utf8 (target),
                              detail::last_error_message (code)));
}

std::int64_t
filetime_value (const FILETIME &ft) noexcept
{
  return static_cast<std::int64_t> (
      (static_cast<std::uint64_t> (ft.dwHighDateTime) << 32)
      | ft.dwLowDateTime);
}

HANDLE
as_handle (std::intptr_t value) noexcept
{
  return reinterpret_cast<HANDLE> (value);
}

}

#else

namespace
{

error
os_error (std::string_view what, const fs::path &target, int code = errno)
{
  return make_error (errc::io,
                     sformat ("{} '{}': {}", what, path_to_utf8 (target),
                              std::strerror (code)));
}

std::int64_t
stat_mtime (const struct stat &st) noexcept
{
#if defined(__APPLE__)
  return static_cast<std::int64_t> (st.st_mtimespec.tv_sec) * 1000000000
         + st.st_mtimespec.tv_nsec;
#else
  return static_cast<std::int64_t> (st.st_mtim.tv_sec) * 1000000000
         + st.st_mtim.tv_nsec;
#endif
}

}

#endif

namespace fs
{

file_stat
stat (const path &target) noexcept
{
  file_stat out;
#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA data;
  auto wide = detail::native_path (target);
  if (!GetFileAttributesExW (wide.c_str (), GetFileExInfoStandard, &data))
    return out;
  out.exists = true;
  out.is_directory = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
  out.is_link = data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
  out.size = (static_cast<std::uint64_t> (data.nFileSizeHigh) << 32)
             | data.nFileSizeLow;
  out.mtime = filetime_value (data.ftLastWriteTime);
#else
  struct stat st;
  if (::lstat (target.c_str (), &st) != 0)
    return out;
  out.is_link = S_ISLNK (st.st_mode);
  if (out.is_link && ::stat (target.c_str (), &st) != 0)
    {
      out.exists = true;
      return out;
    }
  out.exists = true;
  out.is_directory = S_ISDIR (st.st_mode);
  out.size = static_cast<std::uint64_t> (st.st_size);
  out.mtime = stat_mtime (st);
#endif
  return out;
}

std::int64_t
mtime_now () noexcept
{
#if defined(_WIN32)
  FILETIME ft;
  GetSystemTimeAsFileTime (&ft);
  return filetime_value (ft);
#else
  struct timespec ts;
  clock_gettime (CLOCK_REALTIME, &ts);
  return static_cast<std::int64_t> (ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
}

std::int64_t
mtime_ticks_per_second () noexcept
{
#if defined(_WIN32)
  return 10000000;
#else
  return 1000000000;
#endif
}

bool
exists (const path &target) noexcept
{
  return fs::stat (target).exists;
}

std::unordered_map<std::string, file_stat>
list_directory (const path &directory)
{
  std::unordered_map<std::string, file_stat> entries;
#if defined(_WIN32)
  auto pattern = detail::native_path (directory / L"*");
  WIN32_FIND_DATAW data;
  HANDLE handle = FindFirstFileExW (pattern.c_str (), FindExInfoBasic, &data,
                                    FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
  if (handle == INVALID_HANDLE_VALUE)
    return entries;
  do
    {
      std::wstring_view name (data.cFileName);
      if (name == L"." || name == L"..")
        continue;
      file_stat st;
      st.exists = true;
      st.is_directory = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
      st.is_link = data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
      st.size = (static_cast<std::uint64_t> (data.nFileSizeHigh) << 32)
                | data.nFileSizeLow;
      st.mtime = filetime_value (data.ftLastWriteTime);
      entries.emplace (detail::wide_to_utf8 (name), st);
    }
  while (FindNextFileW (handle, &data));
  FindClose (handle);
#else
  DIR *dir = opendir (directory.c_str ());
  if (!dir)
    return entries;
  int fd = dirfd (dir);
  while (auto *entry = readdir (dir))
    {
      std::string_view name (entry->d_name);
      if (name == "." || name == "..")
        continue;
      struct stat st;
      if (fstatat (fd, entry->d_name, &st, 0) != 0)
        continue;
      file_stat out;
      out.exists = true;
      out.is_link = entry->d_type == DT_LNK;
      out.is_directory = S_ISDIR (st.st_mode);
      out.size = static_cast<std::uint64_t> (st.st_size);
      out.mtime = stat_mtime (st);
      entries.emplace (std::string (name), out);
    }
  closedir (dir);
#endif
  return entries;
}

result<std::string>
read_file (const path &target)
{
  BLB_TRY_ASSIGN (auto file,
                  native_file::open (target, native_file::mode::read));
  BLB_TRY_ASSIGN (auto size, file.size ());
  std::string data (static_cast<std::size_t> (size), '\0');
  std::size_t offset = 0;
  while (offset < data.size ())
    {
      BLB_TRY_ASSIGN (
          auto n, file.read (data.data () + offset, data.size () - offset));
      if (n == 0)
        break;
      offset += n;
    }
  data.resize (offset);
  return data;
}

result<void>
write_file (const path &target, std::string_view data)
{
  if (target.has_parent_path ())
    BLB_TRY (fs::create_directories (target.parent_path ()));
  BLB_TRY_ASSIGN (auto file,
                  native_file::open (target, native_file::mode::write));
  BLB_TRY (file.write_all (data.data (), data.size ()));
  return file.close ();
}

result<void>
write_file_atomic (const path &target, std::string_view data)
{
  auto temp = temp_sibling (target);
  if (auto r = write_file (temp, data); !r)
    {
      (void)remove_file (temp);
      return r;
    }
  if (auto r = rename_file (temp, target); !r)
    {
      (void)remove_file (temp);
      return r;
    }
  return ok ();
}

result<void>
create_directories (const path &directory)
{
  if (directory.empty () || fs::stat (directory).is_directory)
    return ok ();
  auto parent = directory.parent_path ();
  if (directory.has_relative_path () && parent != directory)
    BLB_TRY (fs::create_directories (parent));
#if defined(_WIN32)
  auto wide = detail::native_path (directory);
  if (CreateDirectoryW (wide.c_str (), nullptr))
    return ok ();
  auto code = GetLastError ();
  if (code == ERROR_ALREADY_EXISTS && fs::stat (directory).is_directory)
    return ok ();
  return os_error ("cannot create directory", directory, code);
#else
  if (::mkdir (directory.c_str (), 0755) == 0
      || (errno == EEXIST && fs::stat (directory).is_directory))
    return ok ();
  return os_error ("cannot create directory", directory);
#endif
}

result<void>
remove_file (const path &target)
{
#if defined(_WIN32)
  auto wide = detail::native_path (target);
  if (DeleteFileW (wide.c_str ()))
    return ok ();
  auto code = GetLastError ();
  if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
    return ok ();
  if (code == ERROR_ACCESS_DENIED)
    {
      SetFileAttributesW (wide.c_str (), FILE_ATTRIBUTE_NORMAL);
      if (DeleteFileW (wide.c_str ()))
        return ok ();
      code = GetLastError ();
    }
  return os_error ("cannot remove", target, code);
#else
  if (::unlink (target.c_str ()) == 0 || errno == ENOENT)
    return ok ();
  return os_error ("cannot remove", target);
#endif
}

result<void>
remove_all (const path &target)
{
  auto st = fs::stat (target);
  if (!st.exists && !st.is_link)
    return ok ();
  if (!st.is_directory || st.is_link)
    {
#if defined(_WIN32)
      if (st.is_directory)
        {
          auto wide = detail::native_path (target);
          if (RemoveDirectoryW (wide.c_str ()))
            return ok ();
          return os_error ("cannot remove", target);
        }
#endif
      return fs::remove_file (target);
    }
  for (const auto &[name, entry] : fs::list_directory (target))
    BLB_TRY (fs::remove_all (target / path_from_utf8 (name)));
#if defined(_WIN32)
  auto wide = detail::native_path (target);
  if (RemoveDirectoryW (wide.c_str ()))
    return ok ();
  auto code = GetLastError ();
  if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
    return ok ();
  return os_error ("cannot remove", target, code);
#else
  if (::rmdir (target.c_str ()) == 0 || errno == ENOENT)
    return ok ();
  return os_error ("cannot remove", target);
#endif
}

result<void>
rename_file (const path &from, const path &to)
{
#if defined(_WIN32)
  auto wfrom = detail::native_path (from);
  auto wto = detail::native_path (to);
  if (MoveFileExW (wfrom.c_str (), wto.c_str (),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    return ok ();
  return os_error ("cannot rename", from);
#else
  if (::rename (from.c_str (), to.c_str ()) == 0)
    return ok ();
  return os_error ("cannot rename", from);
#endif
}

result<void>
copy_file (const path &from, const path &to)
{
  if (to.has_parent_path ())
    BLB_TRY (fs::create_directories (to.parent_path ()));
  auto temp = temp_sibling (to);
#if defined(_WIN32)
  auto wfrom = detail::native_path (from);
  auto wtemp = detail::native_path (temp);
  if (!CopyFileW (wfrom.c_str (), wtemp.c_str (), FALSE))
    {
      auto code = GetLastError ();
      (void)remove_file (temp);
      return os_error ("cannot copy", from, code);
    }
#else
  {
    BLB_TRY_ASSIGN (auto input,
                    native_file::open (from, native_file::mode::read));
    BLB_TRY_ASSIGN (auto output,
                    native_file::open (temp, native_file::mode::write));
    std::string buffer (1 << 20, '\0');
    for (;;)
      {
        auto n = input.read (buffer.data (), buffer.size ());
        if (!n)
          {
            (void)remove_file (temp);
            return std::move (n).err ();
          }
        if (*n == 0)
          break;
        if (auto w = output.write_all (buffer.data (), *n); !w)
          {
            (void)remove_file (temp);
            return w;
          }
      }
    BLB_TRY (output.close ());
    struct stat st;
    if (::stat (from.c_str (), &st) == 0)
      ::chmod (temp.c_str (), st.st_mode & 07777);
  }
#endif
  if (auto r = rename_file (temp, to); !r)
    {
      (void)remove_file (temp);
      return r;
    }
  return ok ();
}

result<link_result>
link_or_copy (const path &from, const path &to)
{
  if (to.has_parent_path ())
    BLB_TRY (fs::create_directories (to.parent_path ()));
  BLB_TRY (fs::remove_file (to));
#if defined(_WIN32)
  auto wfrom = detail::native_path (from);
  auto wto = detail::native_path (to);
  if (CreateHardLinkW (wto.c_str (), wfrom.c_str (), nullptr))
    return link_result::hard_link;
#else
  if (::link (from.c_str (), to.c_str ()) == 0)
    return link_result::hard_link;
#endif
  BLB_TRY (fs::copy_file (from, to));
  return link_result::copied;
}

result<void>
set_executable (const path &target)
{
#if defined(_WIN32)
  (void)target;
  return ok ();
#else
  struct stat st;
  if (::stat (target.c_str (), &st) != 0)
    return os_error ("cannot stat", target);
  if (::chmod (target.c_str (), st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0)
    return os_error ("cannot chmod", target);
  return ok ();
#endif
}

path
temp_sibling (const path &target)
{
  auto name = target.filename ();
  name += ".";
  name += random_bytes_hex (4);
  name += ".part";
  return target.parent_path () / name;
}

bool
same_volume (const path &a, const path &b) noexcept
{
#if defined(_WIN32)
  auto ra = fs::absolute (a).root_name ().native ();
  auto rb = fs::absolute (b).root_name ().native ();
  return _wcsicmp (ra.c_str (), rb.c_str ()) == 0;
#else
  struct stat sa;
  struct stat sb;
  auto pa = a;
  auto pb = b;
  while (::stat (pa.c_str (), &sa) != 0 && pa.has_parent_path ()
         && pa != pa.parent_path ())
    pa = pa.parent_path ();
  while (::stat (pb.c_str (), &sb) != 0 && pb.has_parent_path ()
         && pb != pb.parent_path ())
    pb = pb.parent_path ();
  return sa.st_dev == sb.st_dev;
#endif
}

bool
same_file (const path &a, const path &b) noexcept
{
#if defined(_WIN32)
  auto identity = [] (const path &p, BY_HANDLE_FILE_INFORMATION &info) {
    auto wide = detail::native_path (p);
    HANDLE handle = CreateFileW (
        wide.c_str (), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      return false;
    bool ok = GetFileInformationByHandle (handle, &info);
    CloseHandle (handle);
    return ok;
  };
  BY_HANDLE_FILE_INFORMATION ia;
  BY_HANDLE_FILE_INFORMATION ib;
  if (!identity (a, ia) || !identity (b, ib))
    return false;
  return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber
         && ia.nFileIndexHigh == ib.nFileIndexHigh
         && ia.nFileIndexLow == ib.nFileIndexLow;
#else
  struct stat sa;
  struct stat sb;
  if (::stat (a.c_str (), &sa) != 0 || ::stat (b.c_str (), &sb) != 0)
    return false;
  return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

bool
safe_relative (std::string_view relative) noexcept
{
  if (relative.empty () || relative.front () == '/'
      || relative.front () == '\\'
      || relative.find (':') != std::string_view::npos)
    return false;
  std::size_t start = 0;
  while (start <= relative.size ())
    {
      auto end = relative.find_first_of ("/\\", start);
      if (end == std::string_view::npos)
        end = relative.size ();
      if (relative.substr (start, end - start) == "..")
        return false;
      start = end + 1;
    }
  return true;
}

bool
is_inside (const path &root, const path &target)
{
  auto relative
      = fs::absolute (target).lexically_relative (fs::absolute (root));
  if (relative.empty () || relative.is_absolute ())
    return false;
  const path first = *relative.begin ();
  return first != path ("..") && first != path (".");
}

path
current_directory ()
{
#if defined(_WIN32)
  auto size = GetCurrentDirectoryW (0, nullptr);
  std::wstring buffer (size, L'\0');
  size = GetCurrentDirectoryW (size, buffer.data ());
  buffer.resize (size);
  return path (buffer);
#else
  std::string buffer (4096, '\0');
  if (!::getcwd (buffer.data (), buffer.size ()))
    return path (".");
  buffer.resize (std::char_traits<char>::length (buffer.c_str ()));
  return path (buffer);
#endif
}

path
absolute (const path &target)
{
  if (target.empty ())
    return current_directory ();
#if defined(_WIN32)
  auto wide = target.native ();
  auto size = GetFullPathNameW (wide.c_str (), 0, nullptr, nullptr);
  if (size == 0)
    return target.lexically_normal ();
  std::wstring buffer (size, L'\0');
  size = GetFullPathNameW (wide.c_str (), size, buffer.data (), nullptr);
  buffer.resize (size);
  return path (buffer).lexically_normal ();
#else
  if (target.is_absolute ())
    return target.lexically_normal ();
  return (current_directory () / target).lexically_normal ();
#endif
}

path
real_path (const path &target)
{
#if defined(_WIN32)
  auto wide = detail::native_path (target);
  HANDLE handle = CreateFileW (
      wide.c_str (), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return fs::absolute (target);
  std::wstring buffer (1024, L'\0');
  auto size = GetFinalPathNameByHandleW (handle, buffer.data (),
                                         static_cast<DWORD> (buffer.size ()),
                                         FILE_NAME_NORMALIZED);
  CloseHandle (handle);
  if (size == 0 || size >= buffer.size ())
    return fs::absolute (target);
  buffer.resize (size);
  if (buffer.starts_with (L"\\\\?\\UNC\\"))
    buffer = L"\\\\" + buffer.substr (8);
  else if (buffer.starts_with (L"\\\\?\\"))
    buffer = buffer.substr (4);
  return path (buffer);
#else
  char *resolved = ::realpath (target.c_str (), nullptr);
  if (!resolved)
    return fs::absolute (target);
  path out (resolved);
  std::free (resolved);
  return out;
#endif
}

result<void>
copy_tree (const path &from, const path &to)
{
  BLB_TRY (fs::create_directories (to));
  for (const auto &[name, entry] : fs::list_directory (from))
    {
      auto source = from / path_from_utf8 (name);
      auto target = to / path_from_utf8 (name);
      if (entry.is_directory && !entry.is_link)
        BLB_TRY (fs::copy_tree (source, target));
      else if (!entry.is_directory)
        BLB_TRY (fs::copy_file (source, target));
    }
  return ok ();
}

result<void>
create_symlink (const path &target, const path &link)
{
  (void)fs::remove_file (link);
#if defined(_WIN32)
  auto wtarget = target.native ();
  auto wlink = detail::native_path (link);
  if (CreateSymbolicLinkW (wlink.c_str (), wtarget.c_str (),
                           SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    return ok ();
  return os_error ("cannot create link", link);
#else
  if (::symlink (target.c_str (), link.c_str ()) == 0)
    return ok ();
  return os_error ("cannot create link", link);
#endif
}

native_file::native_file (native_file &&other) noexcept
    : m_handle (other.m_handle)
{
  other.m_handle = -1;
}

native_file &
native_file::operator= (native_file &&other) noexcept
{
  if (this != &other)
    {
      (void)close ();
      m_handle = other.m_handle;
      other.m_handle = -1;
    }
  return *this;
}

native_file::~native_file ()
{
  (void)close ();
}

result<native_file>
native_file::open (const path &target, mode how)
{
  native_file file;
#if defined(_WIN32)
  auto wide = detail::native_path (target);
  HANDLE handle;
  if (how == mode::read)
    handle = CreateFileW (
        wide.c_str (), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
  else
    {
      DeleteFileW (wide.c_str ());
      handle = CreateFileW (wide.c_str (), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    }
  if (handle == INVALID_HANDLE_VALUE)
    {
      auto code = GetLastError ();
      auto e = os_error ("cannot open", target, code);
      if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        e.code = errc::not_found;
      return e;
    }
  file.m_handle = reinterpret_cast<std::intptr_t> (handle);
#else
  int fd;
  if (how == mode::read)
    fd = ::open (target.c_str (), O_RDONLY | O_CLOEXEC);
  else
    {
      ::unlink (target.c_str ());
      fd = ::open (target.c_str (), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                   0644);
    }
  if (fd < 0)
    {
      auto code = errno;
      auto e = os_error ("cannot open", target, code);
      if (code == ENOENT)
        e.code = errc::not_found;
      return e;
    }
#if defined(POSIX_FADV_SEQUENTIAL)
  if (how == mode::read)
    posix_fadvise (fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
  file.m_handle = fd;
#endif
  return file;
}

result<std::size_t>
native_file::read (void *buffer, std::size_t size)
{
#if defined(_WIN32)
  DWORD got = 0;
  DWORD want = size > 0x40000000 ? 0x40000000 : static_cast<DWORD> (size);
  if (!ReadFile (as_handle (m_handle), buffer, want, &got, nullptr))
    return make_error (errc::io, detail::last_error_message (GetLastError ()));
  return static_cast<std::size_t> (got);
#else
  for (;;)
    {
      auto n = ::read (static_cast<int> (m_handle), buffer, size);
      if (n >= 0)
        return static_cast<std::size_t> (n);
      if (errno != EINTR)
        return make_error (errc::io, std::strerror (errno));
    }
#endif
}

result<void>
native_file::write_all (const void *buffer, std::size_t size)
{
  auto *bytes = static_cast<const char *> (buffer);
  while (size > 0)
    {
#if defined(_WIN32)
      DWORD wrote = 0;
      DWORD want = size > 0x40000000 ? 0x40000000 : static_cast<DWORD> (size);
      if (!WriteFile (as_handle (m_handle), bytes, want, &wrote, nullptr))
        return make_error (errc::io,
                           detail::last_error_message (GetLastError ()));
#else
      auto wrote = ::write (static_cast<int> (m_handle), bytes, size);
      if (wrote < 0)
        {
          if (errno == EINTR)
            continue;
          return make_error (errc::io, std::strerror (errno));
        }
#endif
      bytes += wrote;
      size -= static_cast<std::size_t> (wrote);
    }
  return ok ();
}

result<void>
native_file::write_at (std::uint64_t offset, const void *buffer,
                       std::size_t size)
{
  auto *bytes = static_cast<const char *> (buffer);
  while (size > 0)
    {
#if defined(_WIN32)
      OVERLAPPED at {};
      at.Offset = static_cast<DWORD> (offset & 0xFFFFFFFFu);
      at.OffsetHigh = static_cast<DWORD> (offset >> 32);
      DWORD wrote = 0;
      DWORD want = size > 0x40000000 ? 0x40000000 : static_cast<DWORD> (size);
      if (!WriteFile (as_handle (m_handle), bytes, want, &wrote, &at))
        return make_error (errc::io,
                           detail::last_error_message (GetLastError ()));
#else
      auto wrote = ::pwrite (static_cast<int> (m_handle), bytes, size,
                             static_cast<off_t> (offset));
      if (wrote < 0)
        {
          if (errno == EINTR)
            continue;
          return make_error (errc::io, std::strerror (errno));
        }
#endif
      bytes += wrote;
      size -= static_cast<std::size_t> (wrote);
      offset += static_cast<std::uint64_t> (wrote);
    }
  return ok ();
}

result<void>
native_file::resize (std::uint64_t size)
{
#if defined(_WIN32)
  FILE_END_OF_FILE_INFO info {};
  info.EndOfFile.QuadPart = static_cast<LONGLONG> (size);
  if (!SetFileInformationByHandle (as_handle (m_handle), FileEndOfFileInfo,
                                   &info, sizeof info))
    return make_error (errc::io, detail::last_error_message (GetLastError ()));
#else
  if (::ftruncate (static_cast<int> (m_handle), static_cast<off_t> (size))
      != 0)
    return make_error (errc::io, std::strerror (errno));
#endif
  return ok ();
}

result<std::uint64_t>
native_file::size () const
{
#if defined(_WIN32)
  LARGE_INTEGER value;
  if (!GetFileSizeEx (as_handle (m_handle), &value))
    return make_error (errc::io, detail::last_error_message (GetLastError ()));
  return static_cast<std::uint64_t> (value.QuadPart);
#else
  struct stat st;
  if (::fstat (static_cast<int> (m_handle), &st) != 0)
    return make_error (errc::io, std::strerror (errno));
  return static_cast<std::uint64_t> (st.st_size);
#endif
}

result<void>
native_file::close ()
{
  if (m_handle == -1)
    return ok ();
  auto handle = m_handle;
  m_handle = -1;
#if defined(_WIN32)
  if (!CloseHandle (as_handle (handle)))
    return make_error (errc::io, detail::last_error_message (GetLastError ()));
#else
  if (::close (static_cast<int> (handle)) != 0)
    return make_error (errc::io, std::strerror (errno));
#endif
  return ok ();
}

bool
native_file::is_open () const noexcept
{
  return m_handle != -1;
}

}

}
