#include "air360/web_auth.hpp"

#include <algorithm>

#ifdef ESP_PLATFORM
#include "psa/crypto.h"
extern "C" {
#include "mbedtls/constant_time.h"
}
#else
#include <openssl/crypto.h>
#include <openssl/evp.h>
#endif

namespace air360::web {

bool derivePasswordHash(std::string_view password, const Salt& salt, PasswordHash& output,
                        std::uint32_t iterations) {
#ifdef ESP_PLATFORM
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(password.data());
    const bool ok =
        psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256)) == PSA_SUCCESS &&
        psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST, iterations) == PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt.data(), salt.size()) == PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_PASSWORD, bytes, password.size()) == PSA_SUCCESS &&
        psa_key_derivation_output_bytes(&op, output.data(), output.size()) == PSA_SUCCESS;
    psa_key_derivation_abort(&op);
    return ok;
#else
    return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
        salt.size(), static_cast<int>(iterations), EVP_sha256(), output.size(), output.data()) == 1;
#endif
}

bool matchesPassword(const WebCredential& credential, std::string_view password) {
    PasswordHash digest{};
    if (password.empty() || !derivePasswordHash(password, credential.salt, digest, credential.iterations)) {
        return false;
    }
#ifdef ESP_PLATFORM
    return mbedtls_ct_memcmp(digest.data(), credential.hash.data(), digest.size()) == 0;
#else
    return CRYPTO_memcmp(digest.data(), credential.hash.data(), digest.size()) == 0;
#endif
}

bool isWebPage(std::string_view path) {
    return path == "/" || path == "/config" || path == "/diagnostics" ||
           path == "/backends" || path == "/sensors";
}

std::string_view sessionCookie(std::string_view cookies) {
    constexpr std::string_view prefix = "air360_session=";
    while (!cookies.empty()) {
        const auto end = cookies.find(';');
        auto field = cookies.substr(0, end);
        field.remove_prefix(std::min(field.find_first_not_of(" \t"), field.size()));
        if (field.substr(0, prefix.size()) == prefix) {
            return field.substr(prefix.size());
        }
        if (end == cookies.npos) {
            break;
        }
        cookies.remove_prefix(end + 1);
    }
    return {};
}

bool Sessions::Entry::active(std::int64_t now) const {
    return !token.empty() && now - last_used < 30 * 60 && now - created < 24 * 60 * 60;
}

void Sessions::add(const std::string& token, std::int64_t now) {
    auto slot = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (!it->active(now)) {
            slot = it;
            break;
        }
        if (it->last_used < slot->last_used) {
            slot = it;
        }
    }
    *slot = {token, now, now};
}

bool Sessions::touch(std::string_view token, std::int64_t now) {
    for (auto& entry : entries_) {
        if (entry.active(now) && entry.token == token) {
            entry.last_used = now;
            return true;
        }
    }
    return false;
}

void Sessions::remove(std::string_view token) {
    for (auto& entry : entries_) {
        if (entry.token == token) {
            entry = {};
        }
    }
}

void Sessions::clear() {
    entries_ = {};
}

}  // namespace air360::web
