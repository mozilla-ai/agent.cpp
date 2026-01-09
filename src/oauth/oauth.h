#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace agent_cpp {

struct OAuthToken
{
    std::string access_token;
    std::string refresh_token;
    std::string token_type;
    std::string scope;
    std::chrono::system_clock::time_point expires_at;

    [[nodiscard]] bool is_expired(
      std::chrono::seconds buffer = std::chrono::seconds(60)) const
    {
        return std::chrono::system_clock::now() + buffer >= expires_at;
    }

    [[nodiscard]] bool can_refresh() const { return !refresh_token.empty(); }
};

struct OAuthConfig
{
    std::string client_id;
    std::string client_secret;
    std::string authorize_url;
    std::string token_url;
    std::string redirect_uri = "http://localhost:8089/callback";
    std::string scope;

    std::string provider_name = "default";
};

struct TokenStorageConfig
{
    std::string
      storage_dir; // Directory to store tokens (default: ~/.agent-cpp/tokens)
};

using AuthUrlCallback = std::function<void(const std::string& url)>;

using StatusCallback = std::function<void(const std::string& message)>;

class OAuthClient;
class TokenStorage;

std::unique_ptr<OAuthClient>
create_oauth_client(
  const OAuthConfig& config,
  const TokenStorageConfig& storage_config = TokenStorageConfig{});

class OAuthClient
{
  public:
    virtual ~OAuthClient() = default;

    virtual std::optional<OAuthToken> get_token(
      const AuthUrlCallback& auth_url_callback,
      const StatusCallback& status_callback = nullptr,
      int timeout_seconds = 300) = 0;

    virtual std::optional<OAuthToken> get_cached_token() = 0;

    virtual std::optional<OAuthToken> refresh_token(
      const OAuthToken& token) = 0;

    virtual void clear_tokens() = 0;

    [[nodiscard]] virtual bool has_valid_token() const = 0;
};

} // namespace agent_cpp
