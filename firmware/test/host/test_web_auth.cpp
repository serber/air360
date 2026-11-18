#include "air360/web_auth.hpp"
#include "nvs.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace air360::web;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void testCredentialsAndAccess() {
    WebAuth auth;
    nvs_test::blobs["network_cfg"] = {1, 2, 3};
    require(auth.load() == ESP_OK && !auth.enabled(), "existing device without a password");
    require(auth.access("", 0, false) == Access::allow, "open provisioning");

    WebCredential credential{};
    for (unsigned i = 0; i < credential.salt.size(); ++i) {
        credential.salt[i] = i;
    }
    const PasswordHash expected{
        0x04, 0xb1, 0x8a, 0x3e, 0x16, 0x77, 0x06, 0xe3, 0x6f, 0xbe, 0x55, 0x48, 0xfa, 0x7d, 0xdc, 0xf8,
        0xf1, 0x27, 0x41, 0xf3, 0xad, 0x44, 0x8b, 0x24, 0x01, 0xc8, 0xb5, 0xbb, 0xb9, 0xaa, 0x44, 0x96
    };
    require(derivePasswordHash("air360 test", credential.salt, credential.hash) &&
            credential.hash == expected, "PBKDF2-HMAC-SHA256 known answer");
    require(auth.store(&credential) == ESP_OK && auth.enabled(), "enable password");
    require(auth.access("", 0, true) == Access::redirectToLogin, "private page redirects");
    require(auth.access("air360_session=unknown", 0, false) == Access::unauthorized, "API and POST require login");
    require(!auth.verify("") && !auth.verify("wrong") && auth.verify("air360 test"), "password verification");
    auth.sessions.add("first", 0);
    require(auth.access("other=x; air360_session=first; b=v", 1, false) == Access::allow, "valid session");
    require(auth.access("xair360_session=first", 2, false) == Access::unauthorized, "cookie name exact match");

    WebAuth rebooted;
    require(rebooted.load() == ESP_OK && rebooted.verify("air360 test"), "credential persists");
    require(rebooted.access("air360_session=first", 0, false) == Access::unauthorized, "session lost at reboot");
    const auto current_record = nvs_test::blobs.at("web_auth");
    require(current_record.size() == 52 && current_record[48] == 0x10 && current_record[49] == 0x27 &&
            current_record[50] == 0 && current_record[51] == 0, "new record stores 10000 iterations");
    nvs_test::blobs.at("web_auth").back() = 0xff;
    require(rebooted.load() != ESP_OK && rebooted.enabled() && !rebooted.verify("air360 test"),
            "unsupported iteration count rejected");
    nvs_test::blobs.at("web_auth").resize(48);
    require(rebooted.load() == ESP_ERR_INVALID_SIZE && rebooted.enabled() && !rebooted.verify("air360 test"),
            "record without iteration count rejected");
    auth.sessions.add("first", 1);
    require(derivePasswordHash("changed", credential.salt, credential.hash) &&
            auth.store(&credential) == ESP_OK, "replace credential");
    require(!auth.verify("air360 test") && auth.verify("changed"), "only new password works");
    require(rebooted.load() == ESP_OK && rebooted.verify("changed") &&
            nvs_test::blobs.at("web_auth").size() == current_record.size(), "changed password persists");
    require(auth.access("air360_session=first", 2, false) == Access::unauthorized, "change logs out all clients");

    nvs_test::blobs["web_auth"].resize(1);
    require(auth.load() != ESP_OK && auth.enabled() && !auth.verify("changed"), "corrupt blob fails closed");
    require(auth.store(nullptr) == ESP_OK && !auth.enabled(), "physical recovery can erase corrupt credential");
    require(nvs_test::blobs.count("web_auth") == 0 &&
            nvs_test::blobs.at("network_cfg") == std::vector<std::uint8_t>({1, 2, 3}), "recovery preserves config");
    require(auth.load() == ESP_OK && auth.access("", 0, false) == Access::allow, "recovery survives reboot");
}

void testSessions() {
    Sessions sessions;
    require(!sessions.touch("", 0), "empty table");
    sessions.add("idle", 0);
    require(sessions.touch("idle", 1799) && !sessions.touch("idle", 3599), "idle boundary after refresh");
    sessions.add("active", 0);
    for (int t = 1; t < 86400; t += 900) {
        require(sessions.touch("active", t), "active until absolute expiry");
    }
    require(!sessions.touch("active", 86400), "absolute expiry despite activity");
    sessions.clear();
    for (int i = 0; i < 4; ++i) {
        sessions.add(std::to_string(i), i);
    }
    require(sessions.touch("0", 4), "refresh oldest");
    sessions.add("new", 5);
    require(!sessions.touch("1", 6) && sessions.touch("0", 6), "evict least recently used");
    sessions.remove("new");
    require(!sessions.touch("new", 6) && sessions.touch("2", 6), "logout affects one client");
    sessions.clear();
    require(!sessions.touch("0", 6) && !sessions.touch("2", 6), "explicit invalidation");

    require(isWebPage("/diagnostics") && !isWebPage("/ota/status") &&
            !isWebPage("//remote.example") && !isWebPage("/login"), "safe page destinations");
}
}  // namespace

int main() {
    testCredentialsAndAccess();
    testSessions();
}
