#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>

#include "core/base/sha1_impl.hpp"

#include <cstring>
#include <memory>

namespace b_launcher_base
{

namespace
{

constexpr std::uint32_t
rotl (std::uint32_t value, int bits) noexcept
{
  return value << bits | value >> (32 - bits);
}

std::uint32_t
load_be32 (const std::uint8_t *p) noexcept
{
  return static_cast<std::uint32_t> (p[0]) << 24
         | static_cast<std::uint32_t> (p[1]) << 16
         | static_cast<std::uint32_t> (p[2]) << 8 | p[3];
}

std::uint32_t
load_le32 (const std::uint8_t *p) noexcept
{
  return static_cast<std::uint32_t> (p[3]) << 24
         | static_cast<std::uint32_t> (p[2]) << 16
         | static_cast<std::uint32_t> (p[1]) << 8 | p[0];
}

void
sha1_compress_scalar (std::uint32_t state[5], const std::uint8_t *data,
                      std::size_t blocks) noexcept
{
  for (; blocks; --blocks, data += 64)
    {
      std::uint32_t w[80];
      for (int i = 0; i < 16; ++i)
        w[i] = load_be32 (data + i * 4);
      for (int i = 16; i < 80; ++i)
        w[i] = rotl (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
      std::uint32_t a = state[0];
      std::uint32_t b = state[1];
      std::uint32_t c = state[2];
      std::uint32_t d = state[3];
      std::uint32_t e = state[4];
      for (int i = 0; i < 80; ++i)
        {
          std::uint32_t f;
          std::uint32_t k;
          if (i < 20)
            {
              f = (b & c) | (~b & d);
              k = 0x5A827999;
            }
          else if (i < 40)
            {
              f = b ^ c ^ d;
              k = 0x6ED9EBA1;
            }
          else if (i < 60)
            {
              f = (b & c) | (b & d) | (c & d);
              k = 0x8F1BBCDC;
            }
          else
            {
              f = b ^ c ^ d;
              k = 0xCA62C1D6;
            }
          std::uint32_t t = rotl (a, 5) + f + e + k + w[i];
          e = d;
          d = c;
          c = rotl (b, 30);
          b = a;
          a = t;
        }
      state[0] += a;
      state[1] += b;
      state[2] += c;
      state[3] += d;
      state[4] += e;
    }
}

using compress_fn
    = void (*) (std::uint32_t *, const std::uint8_t *, std::size_t) noexcept;

compress_fn
select_compress () noexcept
{
  if (detail::sha1_shani_supported ())
    return detail::sha1_compress_shani;
  return sha1_compress_scalar;
}

compress_fn
compressor () noexcept
{
  static const compress_fn fn = select_compress ();
  return fn;
}

}

sha1::sha1 () noexcept
{
  reset ();
}

void
sha1::reset () noexcept
{
  m_state[0] = 0x67452301;
  m_state[1] = 0xEFCDAB89;
  m_state[2] = 0x98BADCFE;
  m_state[3] = 0x10325476;
  m_state[4] = 0xC3D2E1F0;
  m_length = 0;
  m_buffered = 0;
}

void
sha1::update (const void *data, std::size_t size) noexcept
{
  auto *bytes = static_cast<const std::uint8_t *> (data);
  m_length += size;
  if (m_buffered)
    {
      auto take = std::min (size, 64 - m_buffered);
      std::memcpy (m_buffer + m_buffered, bytes, take);
      m_buffered += take;
      bytes += take;
      size -= take;
      if (m_buffered < 64)
        return;
      compressor () (m_state, m_buffer, 1);
      m_buffered = 0;
    }
  if (size >= 64)
    {
      auto blocks = size / 64;
      compressor () (m_state, bytes, blocks);
      bytes += blocks * 64;
      size -= blocks * 64;
    }
  if (size)
    {
      std::memcpy (m_buffer, bytes, size);
      m_buffered = size;
    }
}

void
sha1::update (std::string_view text) noexcept
{
  update (text.data (), text.size ());
}

sha1_digest
sha1::finish () noexcept
{
  std::uint64_t bits = m_length * 8;
  std::uint8_t pad[72] = { 0x80 };
  std::size_t pad_size = m_buffered < 56 ? 56 - m_buffered : 120 - m_buffered;
  for (int i = 0; i < 8; ++i)
    pad[pad_size + i] = static_cast<std::uint8_t> (bits >> (56 - i * 8));
  update (pad, pad_size + 8);
  sha1_digest out;
  for (int i = 0; i < 5; ++i)
    {
      out[i * 4] = static_cast<std::uint8_t> (m_state[i] >> 24);
      out[i * 4 + 1] = static_cast<std::uint8_t> (m_state[i] >> 16);
      out[i * 4 + 2] = static_cast<std::uint8_t> (m_state[i] >> 8);
      out[i * 4 + 3] = static_cast<std::uint8_t> (m_state[i]);
    }
  reset ();
  return out;
}

sha1_digest
sha1::digest (const void *data, std::size_t size) noexcept
{
  sha1 h;
  h.update (data, size);
  return h.finish ();
}

sha1_digest
sha1::digest (std::string_view text) noexcept
{
  return digest (text.data (), text.size ());
}

bool
sha1::hardware_accelerated () noexcept
{
  return detail::sha1_shani_supported ();
}

sha256::sha256 () noexcept
    : m_state { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }
    , m_length (0)
    , m_buffered (0)
{
}

void
sha256::block (const std::uint8_t *data) noexcept
{
  static constexpr std::uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  };
  auto rotr = [] (std::uint32_t x, int n) {
    return x >> n | x << (32 - n);
  };
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = load_be32 (data + i * 4);
  for (int i = 16; i < 64; ++i)
    {
      auto s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
      auto s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
  std::uint32_t v[8];
  for (int i = 0; i < 8; ++i)
    v[i] = m_state[i];
  for (int i = 0; i < 64; ++i)
    {
      auto s1 = rotr (v[4], 6) ^ rotr (v[4], 11) ^ rotr (v[4], 25);
      auto ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      auto t1 = v[7] + s1 + ch + k[i] + w[i];
      auto s0 = rotr (v[0], 2) ^ rotr (v[0], 13) ^ rotr (v[0], 22);
      auto maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      for (int j = 7; j > 0; --j)
        v[j] = v[j - 1];
      v[4] += t1;
      v[0] = t1 + s0 + maj;
    }
  for (int i = 0; i < 8; ++i)
    m_state[i] += v[i];
}

void
sha256::update (const void *data, std::size_t size) noexcept
{
  auto *bytes = static_cast<const std::uint8_t *> (data);
  m_length += size;
  while (size)
    {
      auto take = std::min (size, 64 - m_buffered);
      std::memcpy (m_buffer + m_buffered, bytes, take);
      m_buffered += take;
      bytes += take;
      size -= take;
      if (m_buffered == 64)
        {
          block (m_buffer);
          m_buffered = 0;
        }
    }
}

sha256_digest
sha256::finish () noexcept
{
  std::uint64_t bits = m_length * 8;
  std::uint8_t pad[72] = { 0x80 };
  std::size_t pad_size = m_buffered < 56 ? 56 - m_buffered : 120 - m_buffered;
  for (int i = 0; i < 8; ++i)
    pad[pad_size + i] = static_cast<std::uint8_t> (bits >> (56 - i * 8));
  update (pad, pad_size + 8);
  sha256_digest out;
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 4; ++j)
      out[i * 4 + j] = static_cast<std::uint8_t> (m_state[i] >> (24 - j * 8));
  return out;
}

sha256_digest
sha256::digest (const void *data, std::size_t size) noexcept
{
  sha256 h;
  h.update (data, size);
  return h.finish ();
}

sha512::sha512 () noexcept
    : m_state { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
                0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL }
    , m_length (0)
    , m_buffered (0)
{
}

void
sha512::block (const std::uint8_t *data) noexcept
{
  static constexpr std::uint64_t k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
  };
  auto rotr = [] (std::uint64_t x, int n) {
    return x >> n | x << (64 - n);
  };
  std::uint64_t w[80];
  for (int i = 0; i < 16; ++i)
    {
      std::uint64_t v = 0;
      for (int j = 0; j < 8; ++j)
        v = v << 8 | data[i * 8 + j];
      w[i] = v;
    }
  for (int i = 16; i < 80; ++i)
    {
      auto s0 = rotr (w[i - 15], 1) ^ rotr (w[i - 15], 8) ^ (w[i - 15] >> 7);
      auto s1 = rotr (w[i - 2], 19) ^ rotr (w[i - 2], 61) ^ (w[i - 2] >> 6);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
  std::uint64_t v[8];
  for (int i = 0; i < 8; ++i)
    v[i] = m_state[i];
  for (int i = 0; i < 80; ++i)
    {
      auto s1 = rotr (v[4], 14) ^ rotr (v[4], 18) ^ rotr (v[4], 41);
      auto ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      auto t1 = v[7] + s1 + ch + k[i] + w[i];
      auto s0 = rotr (v[0], 28) ^ rotr (v[0], 34) ^ rotr (v[0], 39);
      auto maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      for (int j = 7; j > 0; --j)
        v[j] = v[j - 1];
      v[4] += t1;
      v[0] = t1 + s0 + maj;
    }
  for (int i = 0; i < 8; ++i)
    m_state[i] += v[i];
}

void
sha512::update (const void *data, std::size_t size) noexcept
{
  auto *bytes = static_cast<const std::uint8_t *> (data);
  m_length += size;
  while (size)
    {
      auto take = std::min (size, 128 - m_buffered);
      std::memcpy (m_buffer + m_buffered, bytes, take);
      m_buffered += take;
      bytes += take;
      size -= take;
      if (m_buffered == 128)
        {
          block (m_buffer);
          m_buffered = 0;
        }
    }
}

sha512_digest
sha512::finish () noexcept
{
  std::uint64_t bits = m_length * 8;
  std::uint8_t pad[144] = { 0x80 };
  std::size_t pad_size
      = m_buffered < 112 ? 112 - m_buffered : 240 - m_buffered;
  for (int i = 0; i < 8; ++i)
    pad[pad_size + i] = 0;
  for (int i = 0; i < 8; ++i)
    pad[pad_size + 8 + i] = static_cast<std::uint8_t> (bits >> (56 - i * 8));
  update (pad, pad_size + 16);
  sha512_digest out;
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 8; ++j)
      out[i * 8 + j] = static_cast<std::uint8_t> (m_state[i] >> (56 - j * 8));
  return out;
}

sha512_digest
sha512::digest (const void *data, std::size_t size) noexcept
{
  sha512 h;
  h.update (data, size);
  return h.finish ();
}

md5::md5 () noexcept
    : m_state { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 }
    , m_length (0)
    , m_buffered (0)
{
}

void
md5::block (const std::uint8_t *data) noexcept
{
  static constexpr std::uint32_t k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
  };
  static constexpr int r[64]
      = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
          5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
          4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
          6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
  std::uint32_t m[16];
  for (int i = 0; i < 16; ++i)
    m[i] = load_le32 (data + i * 4);
  std::uint32_t a = m_state[0];
  std::uint32_t b = m_state[1];
  std::uint32_t c = m_state[2];
  std::uint32_t d = m_state[3];
  for (int i = 0; i < 64; ++i)
    {
      std::uint32_t f;
      int g;
      if (i < 16)
        {
          f = (b & c) | (~b & d);
          g = i;
        }
      else if (i < 32)
        {
          f = (d & b) | (~d & c);
          g = (5 * i + 1) % 16;
        }
      else if (i < 48)
        {
          f = b ^ c ^ d;
          g = (3 * i + 5) % 16;
        }
      else
        {
          f = c ^ (b | ~d);
          g = (7 * i) % 16;
        }
      std::uint32_t t = d;
      d = c;
      c = b;
      b = b + rotl (a + f + k[i] + m[g], r[i]);
      a = t;
    }
  m_state[0] += a;
  m_state[1] += b;
  m_state[2] += c;
  m_state[3] += d;
}

void
md5::update (const void *data, std::size_t size) noexcept
{
  auto *bytes = static_cast<const std::uint8_t *> (data);
  m_length += size;
  while (size)
    {
      auto take = std::min (size, 64 - m_buffered);
      std::memcpy (m_buffer + m_buffered, bytes, take);
      m_buffered += take;
      bytes += take;
      size -= take;
      if (m_buffered == 64)
        {
          block (m_buffer);
          m_buffered = 0;
        }
    }
}

md5_digest
md5::finish () noexcept
{
  std::uint64_t bits = m_length * 8;
  std::uint8_t pad[72] = { 0x80 };
  std::size_t pad_size = m_buffered < 56 ? 56 - m_buffered : 120 - m_buffered;
  for (int i = 0; i < 8; ++i)
    pad[pad_size + i] = static_cast<std::uint8_t> (bits >> (i * 8));
  update (pad, pad_size + 8);
  md5_digest out;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      out[i * 4 + j] = static_cast<std::uint8_t> (m_state[i] >> (j * 8));
  return out;
}

md5_digest
md5::digest (const void *data, std::size_t size) noexcept
{
  md5 h;
  h.update (data, size);
  return h.finish ();
}

std::string
to_hex (const sha1_digest &digest)
{
  return to_hex (std::span<const std::uint8_t> (digest));
}

std::optional<sha1_digest>
parse_sha1 (std::string_view hex) noexcept
{
  sha1_digest out;
  if (!from_hex (hex, out))
    return std::nullopt;
  return out;
}

result<file_digests>
digest_file (const std::filesystem::path &path)
{
  constexpr std::size_t buffer_size = 1 << 20;
  thread_local std::unique_ptr<std::uint8_t[]> buffer (
      new std::uint8_t[buffer_size]);
  BLB_TRY_ASSIGN (auto file,
                  fs::native_file::open (path, fs::native_file::mode::read));
  sha1 h1;
  sha512 h512;
  file_digests out;
  for (;;)
    {
      BLB_TRY_ASSIGN (auto n, file.read (buffer.get (), buffer_size));
      if (n == 0)
        break;
      h1.update (buffer.get (), n);
      h512.update (buffer.get (), n);
      out.size += n;
    }
  out.sha1 = h1.finish ();
  out.sha512 = h512.finish ();
  return out;
}

result<sha1_digest>
sha1_file (const std::filesystem::path &path)
{
  constexpr std::size_t buffer_size = 1 << 20;
  thread_local std::unique_ptr<std::uint8_t[]> buffer (
      new std::uint8_t[buffer_size]);
  BLB_TRY_ASSIGN (auto file,
                  fs::native_file::open (path, fs::native_file::mode::read));
  sha1 h;
  for (;;)
    {
      BLB_TRY_ASSIGN (auto n, file.read (buffer.get (), buffer_size));
      if (n == 0)
        break;
      h.update (buffer.get (), n);
    }
  return h.finish ();
}

}
