#pragma once

#include <core/base/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace b_launcher_base
{

using sha1_digest = std::array<std::uint8_t, 20>;
using md5_digest = std::array<std::uint8_t, 16>;
using sha256_digest = std::array<std::uint8_t, 32>;
using sha512_digest = std::array<std::uint8_t, 64>;

class sha1
{
public:
  sha1 () noexcept;

  void update (const void *data, std::size_t size) noexcept;
  void update (std::string_view text) noexcept;
  sha1_digest finish () noexcept;
  void reset () noexcept;

  static sha1_digest digest (const void *data, std::size_t size) noexcept;
  static sha1_digest digest (std::string_view text) noexcept;
  static bool hardware_accelerated () noexcept;

private:
  std::uint32_t m_state[5];
  std::uint64_t m_length;
  std::uint8_t m_buffer[64];
  std::size_t m_buffered;
};

class sha256
{
public:
  sha256 () noexcept;

  void update (const void *data, std::size_t size) noexcept;
  sha256_digest finish () noexcept;

  static sha256_digest digest (const void *data, std::size_t size) noexcept;

private:
  void block (const std::uint8_t *data) noexcept;

  std::uint32_t m_state[8];
  std::uint64_t m_length;
  std::uint8_t m_buffer[64];
  std::size_t m_buffered;
};

class sha512
{
public:
  sha512 () noexcept;

  void update (const void *data, std::size_t size) noexcept;
  sha512_digest finish () noexcept;

  static sha512_digest digest (const void *data, std::size_t size) noexcept;

private:
  void block (const std::uint8_t *data) noexcept;

  std::uint64_t m_state[8];
  std::uint64_t m_length;
  std::uint8_t m_buffer[128];
  std::size_t m_buffered;
};

struct file_digests
{
  sha1_digest sha1 {};
  sha512_digest sha512 {};
  std::uint64_t size = 0;
};

class md5
{
public:
  md5 () noexcept;

  void update (const void *data, std::size_t size) noexcept;
  md5_digest finish () noexcept;

  static md5_digest digest (const void *data, std::size_t size) noexcept;

private:
  void block (const std::uint8_t *data) noexcept;

  std::uint32_t m_state[4];
  std::uint64_t m_length;
  std::uint8_t m_buffer[64];
  std::size_t m_buffered;
};

std::string to_hex (const sha1_digest &digest);
std::optional<sha1_digest> parse_sha1 (std::string_view hex) noexcept;
result<sha1_digest> sha1_file (const std::filesystem::path &path);
result<file_digests> digest_file (const std::filesystem::path &path);

}
