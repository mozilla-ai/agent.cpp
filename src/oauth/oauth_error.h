#pragma once

#include "error.h"

namespace agent_cpp {

class OAuthError : public Error
{
  public:
    explicit OAuthError(const std::string& message)
      : Error("OAuth error: " + message)
    {
    }
};

class TokenExchangeError : public OAuthError
{
  public:
    explicit TokenExchangeError(const std::string& message)
      : OAuthError("Token exchange failed: " + message)
    {
    }
};

class TokenRefreshError : public OAuthError
{
  public:
    explicit TokenRefreshError(const std::string& message)
      : OAuthError("Token refresh failed: " + message)
    {
    }
};

class AuthorizationTimeoutError : public OAuthError
{
  public:
    explicit AuthorizationTimeoutError()
      : OAuthError("Authorization timed out or was cancelled")
    {
    }
};

class CallbackServerError : public OAuthError
{
  public:
    explicit CallbackServerError(const std::string& message)
      : OAuthError("Callback server error: " + message)
    {
    }
};

class TokenStorageError : public OAuthError
{
  public:
    explicit TokenStorageError(const std::string& message)
      : OAuthError("Token storage error: " + message)
    {
    }
};

} // namespace agent_cpp
