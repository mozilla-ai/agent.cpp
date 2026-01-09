#include "oauth/token_storage.h"
#include "oauth/oauth_error.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <shlobj.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

namespace agent_cpp {

using json = nlohmann::json;

namespace {

std::string
get_default_storage_dir()
{
    std::string home_dir;

#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        home_dir = path;
    } else {
        const char* profile = std::getenv("USERPROFILE");
        home_dir = profile ? profile : "";
    }
    std::filesystem::path token_dir =
      std::filesystem::path(home_dir) / ".agent-cpp" / "tokens";
    return token_dir.string();
#else
    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    home_dir = home ? home : "/tmp";
    std::filesystem::path token_dir =
      std::filesystem::path(home_dir) / ".agent-cpp" / "tokens";
    return token_dir.string();
#endif
}

} // anonymous namespace

/// Tokens are stored as plain JSON with file permissions 0600,
class FileTokenStorage : public TokenStorage
{
  public:
    explicit FileTokenStorage(const std::string& storage_dir)
      : storage_dir_(storage_dir.empty() ? get_default_storage_dir()
                                         : storage_dir)
    {
        std::error_code ec;
        std::filesystem::create_directories(storage_dir_, ec);
        if (ec && !std::filesystem::exists(storage_dir_)) {
            throw TokenStorageError("Failed to create storage directory: " +
                                    ec.message());
        }
    }

    void save(const std::string& provider_name,
              const OAuthToken& token) override
    {
        try {
            json j;
            j["access_token"] = token.access_token;
            j["refresh_token"] = token.refresh_token;
            j["token_type"] = token.token_type;
            j["scope"] = token.scope;
            j["expires_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                                token.expires_at.time_since_epoch())
                                .count();

            std::string content = j.dump(2); // Pretty print for readability

            std::string filepath = get_token_path(provider_name);
            std::string temp_filepath = filepath + ".tmp";

#ifndef _WIN32
            mode_t old_umask = umask(0077);
#endif

            // Write to temporary file first for atomic operation
            std::ofstream file(temp_filepath);

#ifndef _WIN32
            // Restore original umask immediately
            umask(old_umask);
#endif

            if (!file) {
                throw TokenStorageError("Failed to open file for writing: " +
                                        temp_filepath);
            }

            file << content;
            file.flush();
            file.close();

            std::error_code ec;
            std::filesystem::rename(temp_filepath, filepath, ec);
            if (ec) {
                std::remove(temp_filepath.c_str());
                throw TokenStorageError("Failed to rename temp file: " +
                                        ec.message());
            }
        } catch (const TokenStorageError&) {
            throw;
        } catch (const std::exception& e) {
            throw TokenStorageError("Failed to save token: " +
                                    std::string(e.what()));
        }
    }

    std::optional<OAuthToken> load(
      const std::string& provider_name) const override
    {
        try {
            std::string filepath = get_token_path(provider_name);
            std::ifstream file(filepath);
            if (!file) {
                return std::nullopt;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

            json j = json::parse(content);

            OAuthToken token;
            token.access_token = j["access_token"].get<std::string>();
            token.refresh_token = j.value("refresh_token", "");
            token.token_type = j.value("token_type", "Bearer");
            token.scope = j.value("scope", "");

            int64_t expires_at_secs = j["expires_at"].get<int64_t>();
            token.expires_at = std::chrono::system_clock::time_point(
              std::chrono::seconds(expires_at_secs));

            return token;
        } catch (const std::exception&) {
            // Token file may be corrupted or in old format
            return std::nullopt;
        }
    }

    void remove(const std::string& provider_name) override
    {
        std::string filepath = get_token_path(provider_name);
        std::remove(filepath.c_str());
    }

    bool exists(const std::string& provider_name) override
    {
        std::string filepath = get_token_path(provider_name);
        std::ifstream file(filepath);
        return file.good();
    }

  private:
    std::string get_token_path(const std::string& provider_name) const
    {
        // Sanitize provider name for use as filename
        std::string safe_name;
        for (char c : provider_name) {
            if (std::isalnum(c) || c == '-' || c == '_') {
                safe_name += c;
            } else {
                safe_name += '_';
            }
        }
        std::filesystem::path token_path =
          std::filesystem::path(storage_dir_) / (safe_name + ".token");
        return token_path.string();
    }

    std::string storage_dir_;
};

std::unique_ptr<TokenStorage>
create_file_token_storage(const std::string& storage_dir)
{
    return std::make_unique<FileTokenStorage>(storage_dir);
}

} // namespace agent_cpp
