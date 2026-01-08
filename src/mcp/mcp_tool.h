#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "mcp/mcp_client.h"
#include "tool.h"

namespace agent_cpp {

using json = nlohmann::json;

class MCPTool : public Tool
{
  public:
    MCPTool(std::shared_ptr<MCPClient> client, MCPToolDefinition definition);

    common_chat_tool get_definition() const override;
    std::string execute(const json& arguments) override;
    std::string get_name() const override { return definition_.name; }

  private:
    std::shared_ptr<MCPClient> client_;
    MCPToolDefinition definition_;
};

}
