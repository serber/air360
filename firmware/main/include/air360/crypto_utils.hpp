#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace air360 {

constexpr std::size_t kSha256DigestBytes = 32U;

[[nodiscard]] bool encodeBase64(
    const std::uint8_t* data,
    std::size_t size,
    std::string& out_encoded);

[[nodiscard]] bool encodeBase64UrlNoPadding(
    const std::uint8_t* data,
    std::size_t size,
    std::string& out_encoded);

[[nodiscard]] bool sha256Digest(
    std::string_view input,
    std::array<std::uint8_t, kSha256DigestBytes>& out_digest);

}  // namespace air360
