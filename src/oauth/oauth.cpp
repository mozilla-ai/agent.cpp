#include "oauth/oauth.h"
#include "oauth/oauth_error.h"
#include "oauth/token_storage.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include <openssl/evp.h>
#include <openssl/rand.h>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

namespace agent_cpp {

using json = nlohmann::json;

namespace {

std::string
url_encode(const std::string& value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase << '%' << std::setw(2)
                    << static_cast<int>(static_cast<unsigned char>(c))
                    << std::nouppercase;
        }
    }

    return escaped.str();
}

std::string
url_decode(const std::string& value)
{
    std::ostringstream decoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int hex_val;
            std::istringstream hex_stream(value.substr(i + 1, 2));
            if (hex_stream >> std::hex >> hex_val) {
                decoded << static_cast<char>(hex_val);
                i += 2;
            } else {
                decoded << value[i];
            }
        } else if (value[i] == '+') {
            decoded << ' ';
        } else {
            decoded << value[i];
        }
    }
    return decoded.str();
}

/// Uses OpenSSL RAND_bytes for OAuth state and code verifier
std::string
generate_random_string(size_t length)
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "0123456789";
    constexpr size_t charset_size = sizeof(charset) - 1;

    std::vector<unsigned char> random_bytes(length);
    if (RAND_bytes(random_bytes.data(), static_cast<int>(length)) != 1) {
        throw OAuthError("Failed to generate secure random bytes");
    }

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[random_bytes[i] % charset_size];
    }
    return result;
}

std::string
base64_url_encode(const std::string& data)
{
    static const char* base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string result;
    int val = 0;
    int valb = -6;

    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result += base64_chars[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }

    if (valb > -6) {
        result += base64_chars[((val << 8) >> (valb + 8)) & 0x3F];
    }

    return result;
}

std::string
sha256(const std::string& input)
{
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    if (EVP_Digest(
          input.data(), input.size(), hash, &hash_len, EVP_sha256(), nullptr) !=
        1) {
        throw OAuthError("Failed to compute SHA-256 hash");
    }

    return std::string(reinterpret_cast<char*>(hash), hash_len);
}

std::pair<std::string, std::string>
parse_url(const std::string& url)
{
    size_t scheme_end = url.find("://");
    size_t host_start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;
    size_t path_start = url.find('/', host_start);

    if (path_start != std::string::npos) {
        return { url.substr(0, path_start), url.substr(path_start) };
    }
    return { url, "/" };
}

void
parse_redirect_uri(const std::string& uri,
                   std::string& host,
                   int& port,
                   std::string& path)
{
    // Default values
    host = "localhost";
    port = 8089;
    path = "/callback";

    size_t scheme_end = uri.find("://");
    if (scheme_end == std::string::npos) {
        return;
    }

    size_t host_start = scheme_end + 3;
    size_t port_start = uri.find(':', host_start);
    size_t path_start = uri.find('/', host_start);

    if (port_start != std::string::npos && port_start < path_start) {
        host = uri.substr(host_start, port_start - host_start);
        size_t port_end =
          (path_start != std::string::npos) ? path_start : uri.length();
        port = std::stoi(uri.substr(port_start + 1, port_end - port_start - 1));
    } else if (path_start != std::string::npos) {
        host = uri.substr(host_start, path_start - host_start);
    } else {
        host = uri.substr(host_start);
    }

    if (path_start != std::string::npos) {
        path = uri.substr(path_start);
    }
}

std::map<std::string, std::string>
parse_query_params(const std::string& query)
{
    std::map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;

    while (std::getline(iss, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            params[pair.substr(0, eq_pos)] = pair.substr(eq_pos + 1);
        }
    }
    return params;
}

void
open_browser(const std::string& url)
{
#ifdef _WIN32
    std::string cmd = "start \"\" \"" + url + "\" >nul 2>&1";
    std::system(cmd.c_str());
#elif __APPLE__
    std::string cmd = "open \"" + url + "\" 2>/dev/null &";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + url + "\" 2>/dev/null &";
    std::system(cmd.c_str());
#endif
}

} // anonymous namespace

class ServerGuard
{
  public:
    ServerGuard(httplib::Server& server, std::thread& thread)
      : server_(server)
      , thread_(thread)
    {
    }

    ~ServerGuard()
    {
        if (server_.is_running()) {
            server_.stop();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Non-copyable, non-movable
    ServerGuard(const ServerGuard&) = delete;
    ServerGuard& operator=(const ServerGuard&) = delete;
    ServerGuard(ServerGuard&&) = delete;
    ServerGuard& operator=(ServerGuard&&) = delete;

  private:
    httplib::Server& server_;
    std::thread& thread_;
};

class OAuthClientImpl : public OAuthClient
{
  public:
    OAuthClientImpl(const OAuthConfig& config,
                    const TokenStorageConfig& storage_config)
      : config_(config)
      , storage_(create_file_token_storage(storage_config.storage_dir))

    {
    }

    std::optional<OAuthToken> get_token(
      const AuthUrlCallback& auth_url_callback,
      const StatusCallback& status_callback,
      int timeout_seconds) override
    {
        // First, check in-memory cache
        {
            std::lock_guard<std::mutex> lock(cached_token_mutex_);
            if (cached_token_ && !cached_token_->is_expired()) {
                if (status_callback) {
                    status_callback("Using cached token");
                }
                return cached_token_;
            }
        }

        auto cached = get_cached_token();
        if (cached && !cached->is_expired()) {
            {
                std::lock_guard<std::mutex> lock(cached_token_mutex_);
                cached_token_ = cached;
            }
            if (status_callback) {
                status_callback("Using cached token");
            }
            return cached;
        }

        if (cached && cached->can_refresh()) {
            if (status_callback) {
                status_callback("Refreshing expired token...");
            }
            auto refreshed = refresh_token(*cached);
            if (refreshed) {
                return refreshed;
            }
        }

        return perform_auth_flow(
          auth_url_callback, status_callback, timeout_seconds);
    }

    std::optional<OAuthToken> get_cached_token() override
    {
        return storage_->load(config_.provider_name);
    }

    std::optional<OAuthToken> refresh_token(const OAuthToken& token) override
    {
        if (token.refresh_token.empty()) {
            return std::nullopt;
        }

        try {
            std::ostringstream body;
            body << "grant_type=refresh_token";
            body << "&refresh_token=" << url_encode(token.refresh_token);
            body << "&client_id=" << url_encode(config_.client_id);

            if (!config_.client_secret.empty()) {
                body << "&client_secret=" << url_encode(config_.client_secret);
            }

            auto res = perform_http_post(config_.token_url, body.str());

            if (!res) {
                throw TokenRefreshError("Failed to connect to token endpoint");
            }

            if (res->status == 400 || res->status == 401) {
                // Server rejected refresh token - this is expected when token
                // is revoked/expired
                {
                    std::lock_guard<std::mutex> lock(cached_token_mutex_);
                    cached_token_.reset();
                }
                return std::nullopt;
            }

            if (res->status != 200) {
                throw TokenRefreshError("Token endpoint returned status " +
                                        std::to_string(res->status));
            }

            auto new_token = parse_token_response(res->body);
            if (new_token) {
                storage_->save(config_.provider_name, *new_token);
                {
                    std::lock_guard<std::mutex> lock(cached_token_mutex_);
                    cached_token_ = new_token;
                }
            }
            return new_token;
        } catch (const TokenRefreshError&) {
            throw; // Re-throw refresh errors
        } catch (const std::exception& e) {
            throw TokenRefreshError(std::string("Unexpected error: ") +
                                    e.what());
        }
    }

    void clear_tokens() override
    {
        {
            std::lock_guard<std::mutex> lock(cached_token_mutex_);
            cached_token_.reset();
        }
        storage_->remove(config_.provider_name);
    }

    [[nodiscard]] bool has_valid_token() const override
    {
        std::lock_guard<std::mutex> lock(cached_token_mutex_);
        auto token = storage_->load(config_.provider_name);
        return token && !token->is_expired();
    }

  private:
    std::optional<OAuthToken> perform_auth_flow(
      const AuthUrlCallback& auth_url_callback,
      const StatusCallback& status_callback,
      int timeout_seconds)
    {
        std::string state = generate_random_string(32);
        std::string code_verifier = generate_random_string(64);
        std::string code_challenge = base64_url_encode(sha256(code_verifier));

        std::ostringstream auth_url;
        auth_url << config_.authorize_url;
        auth_url << "?response_type=code";
        auth_url << "&client_id=" << url_encode(config_.client_id);
        auth_url << "&redirect_uri=" << url_encode(config_.redirect_uri);
        auth_url << "&state=" << url_encode(state);

        if (!config_.scope.empty()) {
            auth_url << "&scope=" << url_encode(config_.scope);
        }

        auth_url << "&code_challenge=" << url_encode(code_challenge);
        auth_url << "&code_challenge_method=S256";

        std::string host;
        int port;
        std::string callback_path;
        parse_redirect_uri(config_.redirect_uri, host, port, callback_path);

        // Force IPv4 for localhost to avoid IPv6 resolution issues
        if (host == "localhost") {
            host = "127.0.0.1";
        }

        std::mutex mtx;
        std::condition_variable cv;
        std::atomic<bool> done{ false };
        std::string received_code;
        std::string received_state;
        std::string error_msg;

        httplib::Server server;

        server.Get(
          callback_path,
          [&](const httplib::Request& req, httplib::Response& res) {
              std::lock_guard<std::mutex> lock(mtx);

              // Guard against duplicate callbacks
              if (done.load()) {
                  res.set_content("Already processed", "text/plain");
                  return;
              }

              if (req.has_param("error")) {
                  error_msg = url_decode(req.get_param_value("error"));
                  if (req.has_param("error_description")) {
                      error_msg +=
                        ": " +
                        url_decode(req.get_param_value("error_description"));
                  }
              } else if (req.has_param("code")) {
                  received_code = url_decode(req.get_param_value("code"));
                  received_state = url_decode(req.get_param_value("state"));
              }

              res.set_content(
                "<!DOCTYPE html><html><head><title>Authorization "
                "Complete</title></head>"
                "<body style='font-family: sans-serif; text-align: center; "
                "padding-top: 50px;'>"
                "<h1>Authorization Complete!</h1>"
                "<p>You can close this window and return to your "
                "application.</p>"
                "</body></html>",
                "text/html");

              done = true;
              cv.notify_one();
          });

        std::atomic<bool> server_started{ false };
        std::atomic<bool> server_failed{ false };

        std::thread server_thread([&]() {
            if (!server.listen(host, port)) {
                server_failed = true;
            }
        });

        ServerGuard guard(server, server_thread);

        constexpr int max_wait_ms = 5000;
        int waited_ms = 0;
        while (!server.is_running() && !server_failed &&
               waited_ms < max_wait_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited_ms += 10;
        }

        if (server_failed || !server.is_running()) {
            throw CallbackServerError("Failed to start callback server on " +
                                      host + ":" + std::to_string(port) +
                                      " (port may be in use)");
        }

        if (status_callback) {
            status_callback("Started callback server on " + host + ":" +
                            std::to_string(port));
        }

        auth_url_callback(auth_url.str());

        try {
            open_browser(auth_url.str());
        } catch (...) {
            // Silently ignore if browser opening fails
        }

        if (status_callback) {
            status_callback("Waiting for authorization...");
        }

        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(timeout_seconds), [&] {
                    return done.load();
                })) {
                throw AuthorizationTimeoutError();
            }
        }

        // ServerGuard will handle cleanup automatically

        if (!error_msg.empty()) {
            throw OAuthError(error_msg);
        }

        if (received_state != state) {
            throw OAuthError("State mismatch");
        }

        if (status_callback) {
            status_callback("Authorization received, exchanging code...");
        }

        return exchange_code(received_code, code_verifier);
    }

    httplib::Result perform_http_post(const std::string& url,
                                      const std::string& body)
    {
        auto [host, path] = parse_url(url);

        httplib::Client client(host);
        client.set_connection_timeout(10);
        client.set_read_timeout(30);

        return client.Post(path, body, "application/x-www-form-urlencoded");
    }

    std::optional<OAuthToken> exchange_code(const std::string& code,
                                            const std::string& code_verifier)
    {
        std::ostringstream body;
        body << "grant_type=authorization_code";
        body << "&code=" << url_encode(code);
        body << "&redirect_uri=" << url_encode(config_.redirect_uri);
        body << "&client_id=" << url_encode(config_.client_id);

        if (!config_.client_secret.empty()) {
            body << "&client_secret=" << url_encode(config_.client_secret);
        }

        body << "&code_verifier=" << url_encode(code_verifier);

        auto res = perform_http_post(config_.token_url, body.str());

        if (!res) {
            throw TokenExchangeError("Failed to connect to token endpoint");
        }

        if (res->status != 200) {
            throw TokenExchangeError("Token endpoint returned status " +
                                     std::to_string(res->status) + ": " +
                                     res->body);
        }

        auto token = parse_token_response(res->body);
        if (token) {
            storage_->save(config_.provider_name, *token);
            {
                std::lock_guard<std::mutex> lock(cached_token_mutex_);
                cached_token_ = token;
            }
        }
        return token;
    }

    std::optional<OAuthToken> parse_token_response(const std::string& body)
    {
        try {
            json j = json::parse(body);

            OAuthToken token;
            token.access_token = j["access_token"].get<std::string>();
            token.refresh_token = j.value("refresh_token", "");
            token.token_type = j.value("token_type", "Bearer");
            token.scope = j.value("scope", "");

            int expires_in = j.value("expires_in", 3600);
            token.expires_at = std::chrono::system_clock::now() +
                               std::chrono::seconds(expires_in);

            return token;
        } catch (const json::exception& e) {
            throw TokenExchangeError("Failed to parse token response: " +
                                     std::string(e.what()));
        }
    }

    OAuthConfig config_;
    std::unique_ptr<TokenStorage> storage_;
    mutable std::optional<OAuthToken> cached_token_;
    mutable std::mutex cached_token_mutex_;
};

std::unique_ptr<OAuthClient>
create_oauth_client(const OAuthConfig& config,
                    const TokenStorageConfig& storage_config)
{
    return std::make_unique<OAuthClientImpl>(config, storage_config);
}

} // namespace agent_cpp
