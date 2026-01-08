#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tool.h"

// Forward declarations
namespace httplib {
class Client;
}

namespace agent_cpp {
class MCPClient;
}

namespace agent_cpp {

using json = nlohmann::json;

constexpr const char* MCP_PROTOCOL_VERSION = "2025-11-25";

struct MCPToolDefinition
{
    std::string name;
    std::string title;
    std::string description;
    json input_schema;
    json output_schema;
};

struct MCPContentItem
{
    std::string type; // "text", "image", "audio", etc.
    std::string text; // For text content
    std::string data; // For binary content (base64)
    std::string mime_type;
};

struct MCPToolResult
{
    std::vector<MCPContentItem> content;
    json structured_content;
    bool is_error = false;
};

/// @brief Configuration options for MCPClient
struct MCPClientConfig
{
    int connection_timeout_sec = 10; ///< Connection timeout in seconds
    int read_timeout_sec = 30;       ///< Read timeout in seconds
    int write_timeout_sec = 10;      ///< Write timeout in seconds
};

class MCPClient : public std::enable_shared_from_this<MCPClient>
{
  public:
    /// @brief Create an MCPClient instance
    /// @param url The MCP server URL
    /// @param config Optional configuration for timeouts
    /// @note Use this factory method instead of direct construction to enable
    ///       proper lifetime management with MCPTool objects.
    static std::shared_ptr<MCPClient> create(
      const std::string& url,
      const MCPClientConfig& config = MCPClientConfig{});

    ~MCPClient();

    MCPClient(const MCPClient&) = delete;
    MCPClient& operator=(const MCPClient&) = delete;

    bool initialize(const std::string& client_name = "agent.cpp",
                    const std::string& client_version = "0.1.0");

    void close();

    bool is_initialized() const { return initialized_; }

    std::vector<MCPToolDefinition> list_tools();

    MCPToolResult call_tool(const std::string& name,
                            const json& arguments = json::object());

    std::vector<std::unique_ptr<Tool>> get_tools();

  private:
    MCPClient(const std::string& url, const MCPClientConfig& config);

    std::string url_;
    std::string path_;
    std::unique_ptr<httplib::Client> http_client_;

    std::string session_id_;
    std::string protocol_version_;
    bool initialized_ = false;
    bool has_tools_ = false;
    std::atomic<int> request_id_{ 0 };
    std::mutex send_mutex_;

    std::vector<MCPToolDefinition> tool_cache_;
    bool tools_cached_ = false;

    json send_request(const std::string& method,
                      const json& params = json::object());

    void send_notification(const std::string& method,
                           const json& params = json::object());

    json parse_response(const std::string& response);
};

}
