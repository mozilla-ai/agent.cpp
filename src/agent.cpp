#include "agent.h"
#include "error.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace agent_cpp {

using json = nlohmann::json;

Agent::Agent(std::shared_ptr<Model> model,
             std::vector<std::unique_ptr<Tool>> tools,
             std::vector<std::unique_ptr<Callback>> callbacks,
             const std::string& instructions)
  : model(std::move(model))
  , tools(std::move(tools))
  , callbacks(std::move(callbacks))
  , instructions(instructions)
{
}

void
Agent::ensure_system_message(std::vector<common_chat_msg>& messages)
{
    if (!instructions.empty()) {
        bool has_instructions = !messages.empty() &&
                                messages[0].role == "system" &&
                                messages[0].content == instructions;

        if (!has_instructions) {
            common_chat_msg system_msg;
            system_msg.role = "system";
            system_msg.content = instructions;
            messages.insert(messages.begin(), system_msg);
        }
    }
}

std::vector<common_chat_tool>
Agent::get_tool_definitions() const
{
    std::vector<common_chat_tool> tool_definitions;
    tool_definitions.reserve(tools.size());
    for (const auto& tool : tools) {
        tool_definitions.push_back(tool->get_definition());
    }
    return tool_definitions;
}

std::string
Agent::run_loop(std::vector<common_chat_msg>& messages,
                const ResponseCallback& callback)
{
    ensure_system_message(messages);

    for (const auto& cb : callbacks) {
        cb->before_agent_loop(messages);
    }

    std::vector<common_chat_tool> tool_definitions = get_tool_definitions();

    while (true) {
        for (const auto& cb : callbacks) {
            cb->before_llm_call(messages);
        }

        auto parsed_msg = model->generate(messages, tool_definitions, callback);

        for (const auto& cb : callbacks) {
            cb->after_llm_call(parsed_msg);
        }

        messages.push_back(parsed_msg);

        if (parsed_msg.tool_calls.empty()) {
            std::string response = parsed_msg.content;
            for (const auto& cb : callbacks) {
                cb->after_agent_loop(messages, response);
            }
            return response;
        }

        for (const auto& tool_call : parsed_msg.tool_calls) {
            std::string tool_name = tool_call.name;
            std::string tool_arguments = tool_call.arguments;

            ToolResult result("");
            bool tool_skipped = false;

            try {
                for (const auto& cb : callbacks) {
                    cb->before_tool_execution(tool_name, tool_arguments);
                }
            } catch (const ToolExecutionSkipped& e) {
                json response;
                response["skipped"] = e.get_message();
                result = response.dump();
                tool_skipped = true;
            }

            if (!tool_skipped) {
                try {
                    json args;
                    try {
                        args = json::parse(tool_arguments);
                    } catch (const json::parse_error& e) {
                        throw ToolArgumentError(tool_name, e.what());
                    }

                    auto tool_it = std::find_if(
                      tools.begin(),
                      tools.end(),
                      [&tool_name](const std::unique_ptr<Tool>& t) {
                          return t->get_name() == tool_name;
                      });

                    if (tool_it == tools.end()) {
                        throw ToolNotFoundError(tool_name);
                    }

                    result = (*tool_it)->execute(args);
                } catch (const std::exception& e) {
                    result = ToolResult::from_exception(e);
                }
            }

            // Single callback invocation - callbacks can convert errors to
            // results
            for (const auto& cb : callbacks) {
                cb->after_tool_execution(tool_name, result);
            }

            // If still an error after callbacks, re-throw
            if (result.has_error()) {
                throw ToolError(tool_name, result.error().message);
            }

            common_chat_msg tool_msg;
            tool_msg.role = "tool";
            tool_msg.content = result.output();
            tool_msg.tool_call_id = tool_call.id;
            tool_msg.tool_name = tool_name;
            messages.push_back(tool_msg);
        }
    }
}

std::vector<llama_token>
Agent::build_prompt_tokens()
{
    if (!model) {
        return {};
    }

    std::vector<common_chat_msg> system_messages;
    if (!instructions.empty()) {
        common_chat_msg system_msg;
        system_msg.role = "system";
        system_msg.content = instructions;
        system_messages.push_back(system_msg);
    }

    std::vector<common_chat_tool> tool_definitions = get_tool_definitions();

    common_chat_templates_inputs inputs;
    inputs.messages = system_messages;
    inputs.tools = tool_definitions;
    inputs.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    inputs.add_generation_prompt = false;
    inputs.enable_thinking = false;

    auto params = common_chat_templates_apply(model->get_templates(), inputs);

    return model->tokenize(params.prompt);
}

bool
Agent::load_or_create_cache(const std::string& cache_path)
{
    if (!model) {
        return false;
    }

    if (std::filesystem::exists(cache_path)) {
        auto cached_tokens = model->load_cache(cache_path);
        if (!cached_tokens.empty()) {
            printf("Loaded prompt cache from '%s' (%zu tokens)\n",
                   cache_path.c_str(),
                   cached_tokens.size());
            return true;
        }
    }

    auto prompt_tokens = build_prompt_tokens();
    if (prompt_tokens.empty()) {
        return true;
    }

    printf("Creating prompt cache at '%s' (%zu tokens)\n",
           cache_path.c_str(),
           prompt_tokens.size());

    // warms the KV cache
    model->generate_from_tokens(prompt_tokens);

    return model->save_cache(cache_path);
}

} // namespace agent_cpp
