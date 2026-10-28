#pragma once

#include <cstddef>
#include <cstdint>

namespace b_launcher_base::detail
{

bool sha1_shani_supported () noexcept;
void sha1_compress_shani (std::uint32_t state[5], const std::uint8_t *data,
                          std::size_t blocks) noexcept;

}
