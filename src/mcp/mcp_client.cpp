#include "mcp/mcp_client.h"
#include "error.h"
#include "mcp/mcp_tool.h"

#include <sstream>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

namespace agent_cpp {

namespace {

// Parse URL into host and path components
void
parse_url(const std::string& url, std::string& host, std::string& path)
{
    // Find scheme end
    size_t scheme_end = url.find("://");
    size_t host_start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;

    // Find path start
    size_t path_start = url.find('/', host_start);
    if (path_start != std::string::npos) {
        host = url.substr(0, path_start);
        path = url.substr(path_start);
    } else {
        host = url;
        path = "/";
    }
}

} // anonymous namespace

std::shared_ptr<MCPClient>
MCPClient::create(const std::string& url, const MCPClientConfig& config)
{
    return std::shared_ptr<MCPClient>(new MCPClient(url, config));
}

MCPClient::MCPClient(const std::string& url, const MCPClientConfig& config)
  : url_(url)
{
    std::string host;
    parse_url(url, host, path_);

    http_client_ = std::make_unique<httplib::Client>(host);
    http_client_->set_connection_timeout(config.connection_timeout_sec);
    http_client_->set_read_timeout(config.read_timeout_sec);
    http_client_->set_write_timeout(config.write_timeout_sec);
}

MCPClient::~MCPClient()
{
    close();
}

json
MCPClient::parse_response(const std::string& response)
{
    std::istringstream stream(response);
    std::string line;
    std::string data;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.find("data:") == 0) {
            data = line.substr(5);
            size_t start = data.find_first_not_of(" \t");
            if (start != std::string::npos) {
                data = data.substr(start);
            }
        }
    }

    if (data.empty()) {
        return json::object();
    }

    try {
        return json::parse(data);
    } catch (const json::parse_error& e) {
        throw MCPError("Failed to parse SSE data: " + std::string(e.what()));
    }
}

json
MCPClient::send_request(const std::string& method, const json& params)
{
    std::lock_guard<std::mutex> lock(send_mutex_);

    int id = ++request_id_;

    json request = { { "jsonrpc", "2.0" }, { "id", id }, { "method", method } };

    if (!params.empty()) {
        request["params"] = params;
    }

    std::string request_body = request.dump();

    httplib::Headers headers = { { "Content-Type", "application/json" },
                                 { "Accept",
                                   "application/json, text/event-stream" } };

    if (!session_id_.empty()) {
        headers.emplace("Mcp-Session-Id", session_id_);
    }

    auto res =
      http_client_->Post(path_, headers, request_body, "application/json");

    if (!res) {
        throw MCPError("HTTP request failed: " +
                       httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        throw MCPError("HTTP error: " + std::to_string(res->status) + " " +
                       res->body);
    }

    // Extract session ID from response headers
    auto session_it = res->headers.find("Mcp-Session-Id");
    if (session_it != res->headers.end()) {
        session_id_ = session_it->second;
    }

    // Get content type
    std::string content_type;
    auto ct_it = res->headers.find("Content-Type");
    if (ct_it != res->headers.end()) {
        content_type = ct_it->second;
    }

    json response;

    if (content_type.find("text/event-stream") != std::string::npos) {
        response = parse_response(res->body);
    } else {
        try {
            response = json::parse(res->body);
        } catch (const json::parse_error& e) {
            throw MCPError("Failed to parse response: " +
                           std::string(e.what()));
        }
    }

    if (response.contains("error")) {
        auto& error = response["error"];
        std::string msg = error.value("message", "Unknown error");
        int code = error.value("code", 0);
        throw MCPError("JSON-RPC error " + std::to_string(code) + ": " + msg);
    }

    return response["result"];
}

void
MCPClient::send_notification(const std::string& method, const json& params)
{
    std::lock_guard<std::mutex> lock(send_mutex_);

    json notification = { { "jsonrpc", "2.0" }, { "method", method } };

    if (!params.empty()) {
        notification["params"] = params;
    }

    std::string request_body = notification.dump();

    httplib::Headers headers = { { "Content-Type", "application/json" },
                                 { "Accept",
                                   "application/json, text/event-stream" } };

    if (!session_id_.empty()) {
        headers.emplace("Mcp-Session-Id", session_id_);
    }

    // Fire and forget for notifications
    http_client_->Post(path_, headers, request_body, "application/json");
}

bool
MCPClient::initialize(const std::string& client_name,
                      const std::string& client_version)
{
    if (initialized_) {
        return true;
    }

    json params = { { "protocolVersion", MCP_PROTOCOL_VERSION },
                    { "capabilities", json::object() },
                    { "clientInfo",
                      { { "name", client_name },
                        { "version", client_version } } } };

    try {
        json result = send_request("initialize", params);

        if (result.contains("protocolVersion")) {
            protocol_version_ = result["protocolVersion"].get<std::string>();
        }

        if (result.contains("capabilities")) {
            auto& caps = result["capabilities"];
            has_tools_ = caps.contains("tools");
        }

        send_notification("notifications/initialized");

        initialized_ = true;
        return true;

    } catch (const MCPError&) {
        throw;
    } catch (const std::exception& e) {
        throw MCPError(std::string("Initialization failed: ") + e.what());
    }
}

void
MCPClient::close()
{
    initialized_ = false;
    tools_cached_ = false;
    tool_cache_.clear();
    session_id_.clear();
}

std::vector<MCPToolDefinition>
MCPClient::list_tools()
{
    if (!initialized_) {
        throw MCPError("MCP client not initialized");
    }

    if (!has_tools_) {
        return {};
    }

    if (tools_cached_) {
        return tool_cache_;
    }

    std::vector<MCPToolDefinition> all_tools;
    std::string cursor;

    do {
        json params = json::object();
        if (!cursor.empty()) {
            params["cursor"] = cursor;
        }

        json result = send_request("tools/list", params);

        if (result.contains("tools") && result["tools"].is_array()) {
            for (const auto& tool_json : result["tools"]) {
                MCPToolDefinition tool;
                tool.name = tool_json.value("name", "");
                tool.title = tool_json.value("title", "");
                tool.description = tool_json.value("description", "");

                if (tool_json.contains("inputSchema")) {
                    tool.input_schema = tool_json["inputSchema"];
                }
                if (tool_json.contains("outputSchema")) {
                    tool.output_schema = tool_json["outputSchema"];
                }

                all_tools.push_back(std::move(tool));
            }
        }

        cursor.clear();
        if (result.contains("nextCursor") && !result["nextCursor"].is_null()) {
            cursor = result["nextCursor"].get<std::string>();
        }

    } while (!cursor.empty());

    tool_cache_ = all_tools;
    tools_cached_ = true;

    return all_tools;
}

MCPToolResult
MCPClient::call_tool(const std::string& name, const json& arguments)
{
    if (!initialized_) {
        throw MCPError("MCP client not initialized");
    }

    json params = { { "name", name }, { "arguments", arguments } };

    json result = send_request("tools/call", params);

    MCPToolResult tool_result;

    if (result.contains("content") && result["content"].is_array()) {
        for (const auto& item : result["content"]) {
            MCPContentItem content_item;
            content_item.type = item.value("type", "");
            content_item.text = item.value("text", "");
            content_item.data = item.value("data", "");
            content_item.mime_type = item.value("mimeType", "");
            tool_result.content.push_back(std::move(content_item));
        }
    }

    if (result.contains("structuredContent")) {
        tool_result.structured_content = result["structuredContent"];
    }

    tool_result.is_error = result.value("isError", false);

    return tool_result;
}

std::vector<std::unique_ptr<Tool>>
MCPClient::get_tools()
{
    auto definitions = list_tools();

    std::vector<std::unique_ptr<Tool>> tools;
    tools.reserve(definitions.size());

    for (auto& def : definitions) {
        tools.push_back(
          std::make_unique<MCPTool>(shared_from_this(), std::move(def)));
    }

    return tools;
}

} // namespace agent_cpp
