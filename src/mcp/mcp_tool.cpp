#include "mcp/mcp_tool.h"
#include "mcp/mcp_client.h"

namespace agent_cpp {

MCPTool::MCPTool(std::shared_ptr<MCPClient> client,
                 MCPToolDefinition definition)
  : client_(std::move(client))
  , definition_(std::move(definition))
{
}

common_chat_tool
MCPTool::get_definition() const
{
    common_chat_tool tool;
    tool.name = definition_.name;
    tool.description = definition_.description;

    if (!definition_.input_schema.is_null()) {
        tool.parameters = definition_.input_schema.dump();
    } else {
        tool.parameters = R"({"type": "object", "properties": {}})";
    }

    return tool;
}

std::string
MCPTool::execute(const json& arguments)
{
    MCPToolResult result = client_->call_tool(definition_.name, arguments);

    json response;

    if (result.is_error) {
        std::string error_msg;
        for (const auto& item : result.content) {
            if (item.type == "text") {
                error_msg += item.text;
            }
        }
        response["error"] =
          error_msg.empty() ? "Tool execution error" : error_msg;
    } else if (!result.structured_content.is_null()) {
        response = result.structured_content;
    } else {
        std::string text_content;
        for (const auto& item : result.content) {
            if (item.type == "text") {
                text_content += item.text;
            }
        }

        if (!text_content.empty()) {
            try {
                response = json::parse(text_content);
            } catch (const json::parse_error&) {
                response["result"] = text_content;
            }
        } else {
            response["result"] = "success";
        }
    }

    return response.dump();
}

} // namespace agent_cpp
