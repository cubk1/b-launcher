#include "core/base/sha1_impl.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)               \
    || defined(_M_IX86)
#define BLB_SHA1_X86 1
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define BLB_SHA_TARGET
#else
#include <cpuid.h>
#define BLB_SHA_TARGET __attribute__ ((target ("sha,sse4.1,ssse3")))
#endif
#endif

namespace b_launcher_base::detail
{

#if defined(BLB_SHA1_X86)

namespace
{

void
cpuid (int leaf, int subleaf, unsigned regs[4]) noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
  int out[4];
  __cpuidex (out, leaf, subleaf);
  for (int i = 0; i < 4; ++i)
    regs[i] = static_cast<unsigned> (out[i]);
#else
  __cpuid_count (leaf, subleaf, regs[0], regs[1], regs[2], regs[3]);
#endif
}

bool
detect () noexcept
{
  unsigned regs[4] = {};
  cpuid (0, 0, regs);
  if (regs[0] < 7)
    return false;
  cpuid (1, 0, regs);
  bool sse41 = regs[2] & (1u << 19);
  bool ssse3 = regs[2] & (1u << 9);
  cpuid (7, 0, regs);
  bool sha = regs[1] & (1u << 29);
  return sse41 && ssse3 && sha;
}

}

bool
sha1_shani_supported () noexcept
{
  static const bool supported = detect ();
  return supported;
}

BLB_SHA_TARGET inline __m128i
load_block (const std::uint8_t *data, int offset, __m128i mask) noexcept
{
  return _mm_shuffle_epi8 (
      _mm_loadu_si128 (reinterpret_cast<const __m128i *> (data + offset)),
      mask);
}

#define BLB_SHA1_ROUND4(e_in, e_out, msg, func)                               \
  e_in = _mm_sha1nexte_epu32 (e_in, msg);                                     \
  e_out = abcd;                                                               \
  abcd = _mm_sha1rnds4_epu32 (abcd, e_in, func)

BLB_SHA_TARGET void
sha1_compress_shani (std::uint32_t state[5], const std::uint8_t *data,
                     std::size_t blocks) noexcept
{
  const __m128i mask
      = _mm_set_epi64x (0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);
  __m128i abcd = _mm_loadu_si128 (reinterpret_cast<const __m128i *> (state));
  __m128i e0 = _mm_set_epi32 (static_cast<int> (state[4]), 0, 0, 0);
  abcd = _mm_shuffle_epi32 (abcd, 0x1B);

  while (blocks--)
    {
      __m128i abcd_save = abcd;
      __m128i e0_save = e0;
      __m128i e1;
      __m128i msg0 = load_block (data, 0, mask);
      e0 = _mm_add_epi32 (e0, msg0);
      e1 = abcd;
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 0);

      __m128i msg1 = load_block (data, 16, mask);
      BLB_SHA1_ROUND4 (e1, e0, msg1, 0);
      msg0 = _mm_sha1msg1_epu32 (msg0, msg1);

      __m128i msg2 = load_block (data, 32, mask);
      BLB_SHA1_ROUND4 (e0, e1, msg2, 0);
      msg1 = _mm_sha1msg1_epu32 (msg1, msg2);
      msg0 = _mm_xor_si128 (msg0, msg2);

      __m128i msg3 = load_block (data, 48, mask);
      e1 = _mm_sha1nexte_epu32 (e1, msg3);
      e0 = abcd;
      msg0 = _mm_sha1msg2_epu32 (msg0, msg3);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 0);
      msg2 = _mm_sha1msg1_epu32 (msg2, msg3);
      msg1 = _mm_xor_si128 (msg1, msg3);

      e0 = _mm_sha1nexte_epu32 (e0, msg0);
      e1 = abcd;
      msg1 = _mm_sha1msg2_epu32 (msg1, msg0);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 0);
      msg3 = _mm_sha1msg1_epu32 (msg3, msg0);
      msg2 = _mm_xor_si128 (msg2, msg0);

      e1 = _mm_sha1nexte_epu32 (e1, msg1);
      e0 = abcd;
      msg2 = _mm_sha1msg2_epu32 (msg2, msg1);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 1);
      msg0 = _mm_sha1msg1_epu32 (msg0, msg1);
      msg3 = _mm_xor_si128 (msg3, msg1);

      e0 = _mm_sha1nexte_epu32 (e0, msg2);
      e1 = abcd;
      msg3 = _mm_sha1msg2_epu32 (msg3, msg2);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 1);
      msg1 = _mm_sha1msg1_epu32 (msg1, msg2);
      msg0 = _mm_xor_si128 (msg0, msg2);

      e1 = _mm_sha1nexte_epu32 (e1, msg3);
      e0 = abcd;
      msg0 = _mm_sha1msg2_epu32 (msg0, msg3);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 1);
      msg2 = _mm_sha1msg1_epu32 (msg2, msg3);
      msg1 = _mm_xor_si128 (msg1, msg3);

      e0 = _mm_sha1nexte_epu32 (e0, msg0);
      e1 = abcd;
      msg1 = _mm_sha1msg2_epu32 (msg1, msg0);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 1);
      msg3 = _mm_sha1msg1_epu32 (msg3, msg0);
      msg2 = _mm_xor_si128 (msg2, msg0);

      e1 = _mm_sha1nexte_epu32 (e1, msg1);
      e0 = abcd;
      msg2 = _mm_sha1msg2_epu32 (msg2, msg1);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 1);
      msg0 = _mm_sha1msg1_epu32 (msg0, msg1);
      msg3 = _mm_xor_si128 (msg3, msg1);

      e0 = _mm_sha1nexte_epu32 (e0, msg2);
      e1 = abcd;
      msg3 = _mm_sha1msg2_epu32 (msg3, msg2);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 2);
      msg1 = _mm_sha1msg1_epu32 (msg1, msg2);
      msg0 = _mm_xor_si128 (msg0, msg2);

      e1 = _mm_sha1nexte_epu32 (e1, msg3);
      e0 = abcd;
      msg0 = _mm_sha1msg2_epu32 (msg0, msg3);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 2);
      msg2 = _mm_sha1msg1_epu32 (msg2, msg3);
      msg1 = _mm_xor_si128 (msg1, msg3);

      e0 = _mm_sha1nexte_epu32 (e0, msg0);
      e1 = abcd;
      msg1 = _mm_sha1msg2_epu32 (msg1, msg0);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 2);
      msg3 = _mm_sha1msg1_epu32 (msg3, msg0);
      msg2 = _mm_xor_si128 (msg2, msg0);

      e1 = _mm_sha1nexte_epu32 (e1, msg1);
      e0 = abcd;
      msg2 = _mm_sha1msg2_epu32 (msg2, msg1);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 2);
      msg0 = _mm_sha1msg1_epu32 (msg0, msg1);
      msg3 = _mm_xor_si128 (msg3, msg1);

      e0 = _mm_sha1nexte_epu32 (e0, msg2);
      e1 = abcd;
      msg3 = _mm_sha1msg2_epu32 (msg3, msg2);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 2);
      msg1 = _mm_sha1msg1_epu32 (msg1, msg2);
      msg0 = _mm_xor_si128 (msg0, msg2);

      e1 = _mm_sha1nexte_epu32 (e1, msg3);
      e0 = abcd;
      msg0 = _mm_sha1msg2_epu32 (msg0, msg3);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 3);
      msg2 = _mm_sha1msg1_epu32 (msg2, msg3);
      msg1 = _mm_xor_si128 (msg1, msg3);

      e0 = _mm_sha1nexte_epu32 (e0, msg0);
      e1 = abcd;
      msg1 = _mm_sha1msg2_epu32 (msg1, msg0);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 3);
      msg3 = _mm_sha1msg1_epu32 (msg3, msg0);
      msg2 = _mm_xor_si128 (msg2, msg0);

      e1 = _mm_sha1nexte_epu32 (e1, msg1);
      e0 = abcd;
      msg2 = _mm_sha1msg2_epu32 (msg2, msg1);
      abcd = _mm_sha1rnds4_epu32 (abcd, e1, 3);
      msg3 = _mm_xor_si128 (msg3, msg1);

      e0 = _mm_sha1nexte_epu32 (e0, msg2);
      e1 = abcd;
      msg3 = _mm_sha1msg2_epu32 (msg3, msg2);
      abcd = _mm_sha1rnds4_epu32 (abcd, e0, 3);

      BLB_SHA1_ROUND4 (e1, e0, msg3, 3);

      e0 = _mm_sha1nexte_epu32 (e0, e0_save);
      abcd = _mm_add_epi32 (abcd, abcd_save);
      data += 64;
    }

  abcd = _mm_shuffle_epi32 (abcd, 0x1B);
  _mm_storeu_si128 (reinterpret_cast<__m128i *> (state), abcd);
  state[4] = static_cast<std::uint32_t> (_mm_extract_epi32 (e0, 3));
}

#else

bool
sha1_shani_supported () noexcept
{
  return false;
}

void
sha1_compress_shani (std::uint32_t *, const std::uint8_t *,
                     std::size_t) noexcept
{
}

#endif

}
