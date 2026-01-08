#include "mcp/mcp_client.h"
#include "mcp/mcp_tool.h"
#include "test_utils.h"

using agent_cpp::json;
using agent_cpp::MCPClient;
using agent_cpp::MCPContentItem;
using agent_cpp::MCPTool;
using agent_cpp::MCPToolDefinition;
using agent_cpp::MCPToolResult;

namespace {

// Test MCPToolDefinition structure
TEST(test_mcp_tool_definition)
{
    MCPToolDefinition def;
    def.name = "test_tool";
    def.title = "Test Tool";
    def.description = "A test tool";
    def.input_schema =
      json({ { "type", "object" },
             { "properties", { { "arg1", { { "type", "string" } } } } } });

    ASSERT_EQ(def.name, "test_tool");
    ASSERT_EQ(def.title, "Test Tool");
    ASSERT_EQ(def.description, "A test tool");
    ASSERT_TRUE(def.input_schema.contains("type"));
    ASSERT_EQ(def.input_schema["type"].get<std::string>(), "object");
}

// Test MCPToolResult structure
TEST(test_mcp_tool_result)
{
    MCPToolResult result;
    result.is_error = false;

    MCPContentItem item;
    item.type = "text";
    item.text = "Hello, World!";
    result.content.push_back(item);

    result.structured_content = json({ { "message", "Hello" } });

    ASSERT_FALSE(result.is_error);
    ASSERT_EQ(result.content.size(), 1);
    ASSERT_EQ(result.content[0].type, "text");
    ASSERT_EQ(result.content[0].text, "Hello, World!");
    ASSERT_EQ(result.structured_content["message"].get<std::string>(), "Hello");
}

// Test MCPContentItem for different types
TEST(test_mcp_content_item_types)
{
    // Text content
    MCPContentItem text_item;
    text_item.type = "text";
    text_item.text = "Sample text";
    ASSERT_EQ(text_item.type, "text");
    ASSERT_EQ(text_item.text, "Sample text");

    // Image content
    MCPContentItem image_item;
    image_item.type = "image";
    image_item.data = "base64encodeddata";
    image_item.mime_type = "image/png";
    ASSERT_EQ(image_item.type, "image");
    ASSERT_EQ(image_item.mime_type, "image/png");
}

// Test MCPClient creation
TEST(test_mcp_client_creation)
{
    auto client = MCPClient::create("http://localhost:8080/mcp");
    ASSERT_FALSE(client->is_initialized());
}

// Test MCPClient URL parsing (HTTP)
TEST(test_mcp_client_http_url)
{
    auto client = MCPClient::create("http://example.com:3000/api/mcp");
    ASSERT_FALSE(client->is_initialized());
}

// Test MCPClient URL parsing (HTTPS)
TEST(test_mcp_client_https_url)
{
    auto client = MCPClient::create("https://example.com/mcp");
    ASSERT_FALSE(client->is_initialized());
}

// Test protocol version constant
TEST(test_mcp_protocol_version)
{
    ASSERT_STREQ(agent_cpp::MCP_PROTOCOL_VERSION, "2025-11-25");
}

// Test MCPTool get_definition
TEST(test_mcp_tool_get_definition)
{
    auto client = MCPClient::create("http://localhost:8080/mcp");

    // Create tool definition
    MCPToolDefinition def;
    def.name = "calculator";
    def.description = "Perform calculations";
    def.input_schema = json({ { "type", "object" },
                              { "properties",
                                { { "operation", { { "type", "string" } } },
                                  { "a", { { "type", "number" } } },
                                  { "b", { { "type", "number" } } } } },
                              { "required", { "operation", "a", "b" } } });

    // Create MCPTool
    MCPTool tool(client, def);

    ASSERT_EQ(tool.get_name(), "calculator");

    auto chat_tool = tool.get_definition();
    ASSERT_EQ(chat_tool.name, "calculator");
    ASSERT_EQ(chat_tool.description, "Perform calculations");
    ASSERT_FALSE(chat_tool.parameters.empty());

    // Verify the parameters can be parsed as JSON
    json params = json::parse(chat_tool.parameters);
    ASSERT_EQ(params["type"].get<std::string>(), "object");
}

// Test MCPTool with empty schema
TEST(test_mcp_tool_empty_schema)
{
    auto client = MCPClient::create("http://localhost:8080/mcp");

    MCPToolDefinition def;
    def.name = "get_time";
    def.description = "Get current time";
    // No input schema set

    MCPTool tool(client, def);

    auto chat_tool = tool.get_definition();
    ASSERT_EQ(chat_tool.name, "get_time");

    // Should have a default empty schema
    json params = json::parse(chat_tool.parameters);
    ASSERT_EQ(params["type"].get<std::string>(), "object");
}

}

int
main()
{
    std::cout << "\n=== Running MCP Client Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_mcp_tool_definition);
        RUN_TEST(test_mcp_tool_result);
        RUN_TEST(test_mcp_content_item_types);
        RUN_TEST(test_mcp_client_creation);
        RUN_TEST(test_mcp_client_http_url);
        RUN_TEST(test_mcp_client_https_url);
        RUN_TEST(test_mcp_protocol_version);
        RUN_TEST(test_mcp_tool_get_definition);
        RUN_TEST(test_mcp_tool_empty_schema);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
