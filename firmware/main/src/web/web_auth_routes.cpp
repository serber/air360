#include "air360/web_server.hpp"

#include <string>
#include <string_view>

#include "air360/web_server_internal.hpp"
#include "air360/web_ui.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "psa/crypto.h"

namespace air360 {
namespace {

constexpr char kTag[] = "air360.web";
constexpr char kCookieFlags[] = "; HttpOnly; SameSite=Strict; Path=/";
constexpr char kExpiredCookie[] = "air360_session=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/";

std::string header(httpd_req_t* request, const char* name) {
    const auto size = httpd_req_get_hdr_value_len(request, name);
    if (size == 0) {
        return {};
    }
    std::string value(size + 1, '\0');
    if (httpd_req_get_hdr_value_str(request, name, value.data(), value.size()) != ESP_OK) {
        return {};
    }
    value.resize(size);
    return value;
}

bool sameOrigin(httpd_req_t* request) {
    const auto origin = header(request, "Origin");
    if (!origin.empty()) {
        return origin == "http://" + header(request, "Host");
    }
    const auto site = header(request, "Sec-Fetch-Site");
    return site.empty() || site == "same-origin" || site == "none";
}

esp_err_t redirect(httpd_req_t* request, const char* location) {
    httpd_resp_set_status(request, request->method == HTTP_GET ? "302 Found" : "303 See Other");
    httpd_resp_set_hdr(request, "Location", location);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t reject(httpd_req_t* request, const char* status, const char* json) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    // Close rejected uploads immediately, before httpd tries to drain the body.
    httpd_resp_set_hdr(request, "Connection", "close");
    httpd_resp_sendstr(request, json);
    return ESP_FAIL;
}

esp_err_t readAuthForm(httpd_req_t* request, web::FormFields& fields) {
    std::string body;
    const auto err = web::readRequestBody(request, body);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_SIZE) {
            web::sendRequestBodyTooLarge(request);
        } else {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Could not read form");
        }
        return err;
    }
    fields = web::parseFormBody(body);
    return ESP_OK;
}

std::int64_t nowSeconds() {
    return esp_timer_get_time() / 1000000;
}

std::string newToken() {
    std::array<std::uint8_t, 16> bytes{};
    if (psa_crypto_init() != PSA_SUCCESS || psa_generate_random(bytes.data(), bytes.size()) != PSA_SUCCESS) {
        return {};
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        token += hex[byte >> 4];
        token += hex[byte & 15];
    }
    return token;
}

}  // namespace

void WebServer::setPasswordEnabled(bool enabled) {
    config_->local_auth_enabled = enabled;
    status_service_->setWebPasswordEnabled(enabled);
}

esp_err_t WebServer::storeCredential(const web::WebCredential* credential) {
    const auto err = auth_.store(credential);
    setPasswordEnabled(auth_.enabled());
    return err;
}

esp_err_t WebServer::dispatch(httpd_req_t* request, Handler handler) {
    if (auth_.enabled()) {
        std::string_view uri(request->uri);
        const auto path = uri.substr(0, uri.find('?'));
        const bool page = request->method == HTTP_GET && web::isWebPage(path) &&
            header(request, "Accept").find("application/json") == std::string::npos;
        switch (auth_.access(header(request, "Cookie"), nowSeconds(), page)) {
            case web::Access::redirectToLogin:
                return redirect(request, ("/login?next=" + std::string(path)).c_str());
            case web::Access::unauthorized:
                return reject(request, "401 Unauthorized", R"({"error":"login_required"})");
            case web::Access::allow:
                break;
        }
    }
    if (request->method == HTTP_POST && (auth_.enabled() || handler == handlePassword) &&
        !sameOrigin(request)) {
        return reject(request, "403 Forbidden", R"({"error":"cross_origin_request"})");
    }
    if (handler == handleConfig) {
        httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    }
    return handler(request);
}

esp_err_t WebServer::handleLogin(httpd_req_t* request) {
    auto& server = *static_cast<WebServer*>(request->user_ctx);
    if (!server.auth_.enabled()) {
        return redirect(request, "/");
    }
    bool wrong_password = false;
    if (request->method == HTTP_POST) {
        if (!sameOrigin(request)) {
            return reject(request, "403 Forbidden", R"({"error":"cross_origin_request"})");
        }
        web::FormFields fields;
        if (auto err = readAuthForm(request, fields); err != ESP_OK) {
            return err;
        }
        if (server.auth_.verify(web::findFormValue(fields, "password"))) {
            const auto token = newToken();
            if (token.empty()) {
                return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Session unavailable");
            }
            server.auth_.sessions.add(token, nowSeconds());
            const auto cookie = "air360_session=" + token + kCookieFlags;
            httpd_resp_set_hdr(request, "Set-Cookie", cookie.c_str());
            std::string_view uri(request->uri);
            const auto query = uri.find('?');
            const auto next = query == uri.npos ? "" : web::findFormValue(
                web::parseFormBody(std::string(uri.substr(query + 1))), "next");
            return redirect(request, web::isWebPage(next) ? next.c_str() : "/");
        }
        wrong_password = true;
        httpd_resp_set_status(request, "401 Unauthorized");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    const auto html = renderTemplate(WebTemplateKey::kLogin, {
        {"LOGIN_ERROR_HIDDEN", wrong_password ? "" : " hidden"},
    });
    return httpd_resp_sendstr(request, html.c_str());
}

esp_err_t WebServer::handleLogout(httpd_req_t* request) {
    auto& server = *static_cast<WebServer*>(request->user_ctx);
    const auto cookies = header(request, "Cookie");
    server.auth_.sessions.remove(web::sessionCookie(cookies));
    httpd_resp_set_hdr(request, "Set-Cookie", kExpiredCookie);
    return redirect(request, "/login");
}

esp_err_t WebServer::handlePassword(httpd_req_t* request) {
    auto& server = *static_cast<WebServer*>(request->user_ctx);
    web::FormFields fields;
    if (auto err = readAuthForm(request, fields); err != ESP_OK) {
        return err;
    }
    if (server.auth_.enabled() && !server.auth_.verify(web::findFormValue(fields, "current"))) {
        return reject(request, "401 Unauthorized", R"({"error":"incorrect_current_password"})");
    }
    const auto action = web::findFormValue(fields, "action");
    web::WebCredential credential{};
    if (action == "set") {
        const auto password = web::findFormValue(fields, "password");
        if (password.empty()) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Enter a new password");
        }
        if (psa_crypto_init() != PSA_SUCCESS ||
            psa_generate_random(credential.salt.data(), credential.salt.size()) != PSA_SUCCESS ||
            !web::derivePasswordHash(password, credential.salt, credential.hash)) {
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Password hashing failed");
        }
    } else if (action != "remove") {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown password action");
    }
    if (server.storeCredential(action == "set" ? &credential : nullptr) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save password");
    }
    httpd_resp_set_hdr(request, "Set-Cookie", kExpiredCookie);
    return redirect(request, action == "set" ? "/login?next=/config" : "/config");
}

esp_err_t WebServer::queuePasswordRecovery() {
    return httpd_queue_work(handle_, [](void* context) {
        auto& server = *static_cast<WebServer*>(context);
        if (server.storeCredential(nullptr) == ESP_OK) {
            server.on_password_recovered_();
        } else {
            ESP_LOGE(kTag, "Could not clear web password");
        }
    }, this);
}

}  // namespace air360
