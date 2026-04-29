#include "air360/crypto_utils.hpp"

#include <algorithm>

#include "mbedtls/base64.h"
#include "mbedtls/md.h"

namespace air360 {

bool encodeBase64(
    const std::uint8_t* data,
    std::size_t size,
    std::string& out_encoded) {
    out_encoded.clear();

    if (data == nullptr && size > 0U) {
        return false;
    }

    if (size == 0U) {
        return true;
    }

    std::size_t encoded_length = 0U;
    const int size_result = mbedtls_base64_encode(nullptr, 0U, &encoded_length, data, size);
    if (size_result != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || encoded_length == 0U) {
        return false;
    }

    out_encoded.resize(encoded_length);
    const int encode_result = mbedtls_base64_encode(
        reinterpret_cast<unsigned char*>(out_encoded.data()),
        out_encoded.size(),
        &encoded_length,
        data,
        size);
    if (encode_result != 0) {
        out_encoded.clear();
        return false;
    }

    out_encoded.resize(encoded_length);
    return true;
}

bool encodeBase64UrlNoPadding(
    const std::uint8_t* data,
    std::size_t size,
    std::string& out_encoded) {
    if (!encodeBase64(data, size, out_encoded)) {
        return false;
    }

    std::replace(out_encoded.begin(), out_encoded.end(), '+', '-');
    std::replace(out_encoded.begin(), out_encoded.end(), '/', '_');
    while (!out_encoded.empty() && out_encoded.back() == '=') {
        out_encoded.pop_back();
    }

    return true;
}

bool sha256Digest(
    std::string_view input,
    std::array<std::uint8_t, kSha256DigestBytes>& out_digest) {
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) {
        return false;
    }

    const int result = mbedtls_md(
        md_info,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size(),
        out_digest.data());
    return result == 0;
}

}  // namespace air360
