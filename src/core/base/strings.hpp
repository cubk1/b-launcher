#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base
{

// ASCII case folding, independent of the process locale.
std::string to_lower (std::string_view text);
bool iequals (std::string_view a, std::string_view b) noexcept;
// ASCII case-insensitive substring search; an empty needle matches any text.
bool icontains (std::string_view text, std::string_view needle) noexcept;
// Removes space, tab, CR and LF; the returned view borrows the input.
std::string_view trim (std::string_view text) noexcept;
std::vector<std::string_view> split (std::string_view text, char separator);
std::string join (std::span<const std::string> parts,
                  std::string_view separator);
std::string replace_all (std::string_view text, std::string_view from,
                         std::string_view to);
std::string strip_formatting (std::string_view text);
std::optional<std::int64_t> parse_int (std::string_view text) noexcept;
// Editable arguments use double quotes; double a quote inside a quoted value.
// Backslashes are literal, so Windows paths retain their original spelling.
std::vector<std::string> split_arguments (std::string_view text);
std::string join_arguments (std::span<const std::string> arguments);

std::string to_hex (std::span<const std::uint8_t> bytes);
bool from_hex (std::string_view text, std::span<std::uint8_t> out);
std::string base64_encode (std::span<const std::uint8_t> bytes,
                           bool url_safe = false, bool pad = true);
std::optional<std::string> base64_decode (std::string_view text);
std::string url_encode (std::string_view text);
std::string
form_encode (std::span<const std::pair<std::string, std::string>> fields);
std::string random_bytes_hex (std::size_t count);
void random_bytes (std::span<std::uint8_t> out);

std::filesystem::path path_from_utf8 (std::string_view text);
std::string path_to_utf8 (const std::filesystem::path &path);
std::string path_to_native (const std::filesystem::path &path);

int compare_versions (std::string_view a, std::string_view b);

}
