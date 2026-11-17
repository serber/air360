// Host replacement for main/src/crypto_utils.cpp, which is built on mbedTLS.
// Base64 is implemented for real (small and deterministic); SHA-256 is not
// available on the host and reports failure so a test that reaches it fails
// loudly instead of silently using a fake digest.
#include "air360/crypto_utils.hpp"

namespace air360 {
namespace {

bool encode(const std::uint8_t* data, std::size_t size, std::string& out, bool url) {
    static constexpr char kStd[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static constexpr char kUrl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* table = url ? kUrl : kStd;
    if (data == nullptr && size != 0U) {
        return false;
    }
    out.clear();
    for (std::size_t index = 0U; index < size; index += 3U) {
        const std::uint32_t b0 = data[index];
        const std::uint32_t b1 = index + 1U < size ? data[index + 1U] : 0U;
        const std::uint32_t b2 = index + 2U < size ? data[index + 2U] : 0U;
        const std::uint32_t triple = (b0 << 16U) | (b1 << 8U) | b2;
        out.push_back(table[(triple >> 18U) & 0x3FU]);
        out.push_back(table[(triple >> 12U) & 0x3FU]);
        if (index + 1U < size) {
            out.push_back(table[(triple >> 6U) & 0x3FU]);
        } else if (!url) {
            out.push_back('=');
        }
        if (index + 2U < size) {
            out.push_back(table[triple & 0x3FU]);
        } else if (!url) {
            out.push_back('=');
        }
    }
    return true;
}

}  // namespace

bool encodeBase64(const std::uint8_t* data, std::size_t size, std::string& out_encoded) {
    return encode(data, size, out_encoded, false);
}

bool encodeBase64UrlNoPadding(
    const std::uint8_t* data, std::size_t size, std::string& out_encoded) {
    return encode(data, size, out_encoded, true);
}

bool sha256Digest(std::string_view, std::array<std::uint8_t, kSha256DigestBytes>&) {
    return false;
}

}  // namespace air360
