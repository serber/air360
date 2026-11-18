#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "esp_err.h"

namespace air360::web {

using Salt = std::array<std::uint8_t, 16>;
using PasswordHash = std::array<std::uint8_t, 32>;
constexpr std::uint32_t kPasswordIterations = 10'000;
struct WebCredential {
    Salt salt;
    PasswordHash hash;
    std::uint32_t iterations = kPasswordIterations;
};

bool derivePasswordHash(std::string_view password, const Salt& salt, PasswordHash& output,
                        std::uint32_t iterations = kPasswordIterations);
bool matchesPassword(const WebCredential& credential, std::string_view password);
std::string_view sessionCookie(std::string_view cookies);
bool isWebPage(std::string_view path);

class Sessions {
  public:
    // All times are monotonic seconds. The HTTP server task owns the table.
    void add(const std::string& token, std::int64_t now);
    bool touch(std::string_view token, std::int64_t now);
    void remove(std::string_view token);
    void clear();

  private:
    struct Entry {
        std::string token;
        std::int64_t created = 0;
        std::int64_t last_used = 0;
        bool active(std::int64_t now) const;
    };
    std::array<Entry, 4> entries_{};
};

enum class Access { allow, redirectToLogin, unauthorized };

class WebAuth {
  public:
    esp_err_t load();
    esp_err_t store(const WebCredential* credential);
    bool enabled() const { return enabled_; }
    bool verify(std::string_view password) const;
    Access access(std::string_view cookies, std::int64_t now, bool page_request);
    Sessions sessions;

  private:
    WebCredential credential_{};
    bool enabled_ = false;
    bool readable_ = false;
};

}  // namespace air360::web
