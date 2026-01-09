#pragma once

#include "oauth/oauth.h"
#include <string>

namespace agent_cpp {

class TokenStorage
{
  public:
    virtual ~TokenStorage() = default;

    virtual void save(const std::string& provider_name,
                      const OAuthToken& token) = 0;

    virtual std::optional<OAuthToken> load(
      const std::string& provider_name) const = 0;

    virtual void remove(const std::string& provider_name) = 0;

    virtual bool exists(const std::string& provider_name) = 0;
};

std::unique_ptr<TokenStorage>
create_file_token_storage(const std::string& storage_dir = "");

} // namespace agent_cpp
