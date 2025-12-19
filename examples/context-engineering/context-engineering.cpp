#include "agent.h"
#include "calculator_tool.h"
#include "callbacks.h"
#include "chat.h"
#include "chat_loop.h"
#include "error.h"
#include "error_recovery_callback.h"
#include "llama.h"
#include "logging_callback.h"
#include "model.h"
#include "tool.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <unistd.h>

static constexpr size_t DEFAULT_MAX_TOOL_CALLS = 1;

// Callback that trims old tool calls from the context
// This demonstrates how to use before_llm_call to modify the messages
// before they are sent to the LLM, keeping only the N most recent tool calls
class ContextTrimmerCallback : public agent_cpp::Callback
{
  private:
    size_t max_recent_tool_calls;

  public:
    explicit ContextTrimmerCallback(size_t max_calls = DEFAULT_MAX_TOOL_CALLS)
      : max_recent_tool_calls(max_calls)
    {
    }

    void before_llm_call(std::vector<common_chat_msg>& messages) override
    {
        // Find all tool call pairs (assistant with tool_calls + tool response)
        // A tool call pair consists of:
        // 1. An assistant message with tool_calls
        // 2. One or more tool response messages
        std::vector<size_t> tool_call_indices;
        for (size_t i = 0; i < messages.size(); i++) {
            if (messages[i].role == "assistant" &&
                !messages[i].tool_calls.empty()) {
                tool_call_indices.push_back(i);
            }
        }

        if (tool_call_indices.size() > max_recent_tool_calls) {
            size_t num_to_remove =
              tool_call_indices.size() - max_recent_tool_calls;

            // We need to remove the oldest tool call pairs
            // For each tool call to remove, we remove:
            // - The assistant message with tool_calls
            // - All subsequent tool response messages until the next
            // non-tool message
            std::vector<size_t> indices_to_remove;

            for (size_t i = 0; i < num_to_remove; i++) {
                size_t start_idx = tool_call_indices[i];

                indices_to_remove.push_back(start_idx);

                for (size_t j = start_idx + 1; j < messages.size(); j++) {
                    if (messages[j].role == "tool") {
                        indices_to_remove.push_back(j);
                    } else {
                        break;
                    }
                }
            }

            std::sort(indices_to_remove.begin(),
                      indices_to_remove.end(),
                      std::greater<size_t>());

            if (isatty(fileno(stderr))) {
                fprintf(stderr, "\033[34m[CONTEXT] Trimmed messages:\033[0m\n");
            } else {
                fprintf(stderr, "[CONTEXT] Trimmed messages:\n");
            }
            for (size_t idx : indices_to_remove) {
                const auto& msg = messages[idx];
                std::string display_content = msg.content;
                if (msg.role == "assistant" && !msg.tool_calls.empty()) {
                    display_content = "tool_calls: [";
                    for (size_t i = 0; i < msg.tool_calls.size(); i++) {
                        if (i > 0) {
                            display_content += ", ";
                        }
                        display_content += msg.tool_calls[i].name + "(" +
                                           msg.tool_calls[i].arguments + ")";
                    }
                    display_content += "]";
                }
                if (isatty(fileno(stderr))) {
                    fprintf(stderr,
                            "\033[34m[CONTEXT] - [%s]: %.60s%s\033[0m\n",
                            msg.role.c_str(),
                            display_content.c_str(),
                            display_content.size() > 60 ? "..." : "");
                } else {
                    fprintf(stderr,
                            "[CONTEXT] - [%s]: %.60s%s\n",
                            msg.role.c_str(),
                            display_content.c_str(),
                            display_content.size() > 60 ? "..." : "");
                }
                messages.erase(messages.begin() +
                               static_cast<std::ptrdiff_t>(idx));
            }
        }
    }
};

static void
print_usage(int /*unused*/, char** argv)
{
    printf("\nexample usage:\n");
    printf("\n    %s -m model.gguf\n", argv[0]);
    printf("\n");
    printf("options:\n");
    printf("  -m <path>       Path to the GGUF model file (required)\n");
    printf(
      "  -n <number>     Maximum recent tool calls to keep (default: %zu)\n",
      DEFAULT_MAX_TOOL_CALLS);
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;
    size_t max_tool_calls = DEFAULT_MAX_TOOL_CALLS;

    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0) {
                if (i + 1 < argc) {
                    model_path = argv[++i];
                } else {
                    print_usage(argc, argv);
                    return 1;
                }
            } else if (strcmp(argv[i], "-n") == 0) {
                if (i + 1 < argc) {
                    max_tool_calls = static_cast<size_t>(std::stoi(argv[++i]));
                    if (max_tool_calls == 0) {
                        fprintf(stderr, "error: -n must be at least 1\n");
                        return 1;
                    }
                } else {
                    print_usage(argc, argv);
                    return 1;
                }
            } else {
                print_usage(argc, argv);
                return 1;
            }
        } catch (std::exception& e) {
            fprintf(stderr, "error: %s\n", e.what());
            print_usage(argc, argv);
            return 1;
        }
    }

    if (model_path.empty()) {
        print_usage(argc, argv);
        return 1;
    }

    printf("Loading model...\n");
    std::shared_ptr<agent_cpp::Model> model;
    try {
        model = agent_cpp::Model::create(model_path);
    } catch (const agent_cpp::ModelError& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    printf("Model loaded successfully\n");

    printf("Setting up tools...\n");
    std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
    tools.push_back(std::make_unique<CalculatorTool>());
    printf("Configured tools: calculator\n");

    const std::string instructions =
      "You are a helpful assistant that can solve basic calculations. "
      "When the user provides a mathematical problem, use the 'calculator' "
      "tool "
      "to compute the result. Only use the tool when necessary."
      "If the user asks a composed calculation, break it down into steps and "
      "use the tool for each step."
      "For example, if the user asks 'What is (3 + 5) * 2?', first calculate "
      "'3 + 5' using the tool, then use the result to calculate the final "
      "answer.";

    printf("Context engineering: keeping %zu most recent tool calls\n",
           max_tool_calls);

    std::vector<std::unique_ptr<agent_cpp::Callback>> callbacks;
    callbacks.push_back(
      std::make_unique<ContextTrimmerCallback>(max_tool_calls));
    callbacks.push_back(std::make_unique<LoggingCallback>());
    callbacks.push_back(std::make_unique<ErrorRecoveryCallback>());

    agent_cpp::Agent agent(
      std::move(model), std::move(tools), std::move(callbacks), instructions);

    agent.load_or_create_cache("context-engineering.cache");

    printf("\nContext Engineering Demo ready!\n");
    printf("   Try to ask multiple calculations (i.e. 3+4, then 4 * 6) and");
    printf("   watch how old tool calls are trimmed from context.\n");
    printf("   Type an empty line to quit.\n\n");

    run_chat_loop(agent);
    return 0;
}
