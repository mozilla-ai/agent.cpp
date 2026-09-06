#include "model.h"
#include "test_utils.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef LLAMA_TEMPLATES_DIR
#error "LLAMA_TEMPLATES_DIR must be defined"
#endif

namespace {

std::string
read_file(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Mirrors how Model::generate() renders a turn, so the parser under test is
// fed the same common_chat_params the real code path produces.
common_chat_params
apply_template(const std::string& template_name, bool with_tools)
{
    auto tmpls = common_chat_templates_init(
      nullptr, read_file(std::string(LLAMA_TEMPLATES_DIR) + "/" + template_name));

    common_chat_msg user_msg;
    user_msg.role = "user";
    user_msg.content = "Calculate 3 + 4";

    common_chat_templates_inputs inputs;
    inputs.messages = { user_msg };
    if (with_tools) {
        common_chat_tool calculator;
        calculator.name = "calculator";
        calculator.description = "Performs arithmetic";
        calculator.parameters =
          R"({"type":"object","properties":{"a":{"type":"number"},)"
          R"("b":{"type":"number"},"operation":{"type":"string"}},)"
          R"("required":["a","b","operation"]})";
        inputs.tools = { calculator };
    }
    inputs.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    inputs.add_generation_prompt = true;
    inputs.enable_thinking = false;

    return common_chat_templates_apply(tmpls.get(), inputs);
}

}

// Regression test for tool calls being returned as raw text instead of being
// parsed. llama.cpp ggml-org/llama.cpp#18675 moved parsing to a PEG parser
// derived from the chat template; parse_response() must forward both the
// derived parser and the generation prompt it was built against, otherwise
// common_chat_parse() falls back to a pure-content parser.
TEST(test_tool_call_is_parsed_from_response)
{
    auto params = apply_template("ibm-granite-granite-4.0.jinja", true);
    ASSERT_TRUE(!params.parser.empty());

    const std::string response =
      "<tool_call>\n"
      R"({"name": "calculator", "arguments": {"a": 3, "b": 4, "operation": "add"}})"
      "\n</tool_call>";

    auto parsed = agent_cpp::parse_response(params, response);

    ASSERT_EQ(parsed.role, std::string("assistant"));
    ASSERT_EQ(parsed.tool_calls.size(), static_cast<size_t>(1));
    ASSERT_EQ(parsed.tool_calls[0].name, std::string("calculator"));
    ASSERT_TRUE(parsed.tool_calls[0].arguments.find("\"operation\"") !=
                std::string::npos);
    ASSERT_TRUE(parsed.content.empty());
}

// A plain answer must still come back as content, with no spurious tool calls.
TEST(test_plain_response_is_parsed_as_content)
{
    auto params = apply_template("ibm-granite-granite-4.0.jinja", true);

    auto parsed = agent_cpp::parse_response(params, "The result of 3 + 4 is 7.");

    ASSERT_TRUE(parsed.tool_calls.empty());
    ASSERT_EQ(parsed.content, std::string("The result of 3 + 4 is 7."));
}

// Templates rendered without tools still parse ordinary content.
TEST(test_response_without_tools_is_parsed_as_content)
{
    auto params = apply_template("ibm-granite-granite-4.0.jinja", false);

    auto parsed = agent_cpp::parse_response(params, "Hello!");

    ASSERT_TRUE(parsed.tool_calls.empty());
    ASSERT_EQ(parsed.content, std::string("Hello!"));
}

int
main()
{
    std::cout << "\n=== Running Chat Parser Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_tool_call_is_parsed_from_response);
        RUN_TEST(test_plain_response_is_parsed_as_content);
        RUN_TEST(test_response_without_tools_is_parsed_as_content);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
