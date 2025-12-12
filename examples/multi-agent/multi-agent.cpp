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
#include "prompt_cache.h"
#include "tool.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

using agent_cpp::json;

class MathAgent;

/**
 * DelegateMathTool - A tool that delegates math problems to a specialized
 * math agent.
 *
 * This demonstrates the key pattern: the main agent can call other agents
 * through tools. Each agent maintains its own conversation state but
 * shares the same model weights.
 */
class DelegateMathTool : public agent_cpp::Tool
{
  private:
    MathAgent* math_agent_;

  public:
    explicit DelegateMathTool(MathAgent* math_agent)
      : math_agent_(math_agent)
    {
    }

    common_chat_tool get_definition() const override
    {
        json schema = {
            { "type", "object" },
            { "properties",
              { { "problem",
                  { { "type", "string" },
                    { "description",
                      "A mathematical problem or calculation to solve. "
                      "Examples: 'What is 42 * 17?', 'Calculate 100 / 4 + 25', "
                      "'Add 3.14 and 2.86'" } } } } },
            { "required", { "problem" } }
        };

        return { "delegate_to_math_expert",
                 "Delegate a mathematical problem to a specialized math "
                 "expert agent. Use this when the user asks for calculations, "
                 "arithmetic, or any math-related questions. The math expert "
                 "has access to a calculator and is specialized in solving "
                 "mathematical problems accurately.",
                 schema.dump() };
    }

    std::string get_name() const override { return "delegate_to_math_expert"; }

    std::string execute(const json& arguments) override;
};

/**
 * MathAgent - A specialized agent for mathematical calculations.
 *
 * Has access to a calculator tool and specialized instructions for
 * solving math problems accurately.
 */
class MathAgent
{
  private:
    std::unique_ptr<agent_cpp::Agent> agent_;

    static const std::string& get_instructions()
    {
        static const std::string instructions =
          "You are a specialized mathematical assistant. Your sole purpose is "
          "to solve mathematical problems accurately.\n\n"
          "Guidelines:\n"
          "1. ALWAYS use the calculator tool for ANY arithmetic operation\n"
          "2. Break down complex problems into simple calculator operations\n"
          "3. Show your work step by step\n"
          "4. Double-check your results\n"
          "5. Be precise - avoid rounding unless explicitly asked\n\n"
          "You have access to a calculator that can: add, subtract, multiply, "
          "and divide.";
        return instructions;
    }

  public:
    MathAgent(std::shared_ptr<agent_cpp::ModelWeights> weights,
              const std::string& cache_path)
    {
        auto model = agent_cpp::Model::create_with_weights(weights);

        std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
        tools.push_back(std::make_unique<CalculatorTool>());

        std::vector<std::unique_ptr<agent_cpp::Callback>> callbacks;
        callbacks.push_back(std::make_unique<ErrorRecoveryCallback>());

        agent_ = std::make_unique<agent_cpp::Agent>(std::move(model),
                                                    std::move(tools),
                                                    std::move(callbacks),
                                                    get_instructions());

        load_or_create_agent_cache(*agent_, cache_path);
    }

    std::string solve(const std::string& problem)
    {
        std::vector<common_chat_msg> messages;
        common_chat_msg user_msg;
        user_msg.role = "user";
        user_msg.content = problem;
        messages.push_back(user_msg);

        // Each call starts fresh (could add memory/context if needed)
        return agent_->run_loop(messages);
    }
};

std::string
DelegateMathTool::execute(const json& arguments)
{
    std::string problem = arguments.at("problem").get<std::string>();

    fprintf(
      stderr, "\n[DELEGATION] Delegating to Math Agent: %s\n", problem.c_str());

    std::string result = math_agent_->solve(problem);

    fprintf(stderr, "[DELEGATION] Math Agent response: %s\n", result.c_str());

    json response;
    response["solution"] = result;
    return response.dump();
}

/**
 * MainAgent - The orchestrator agent that coordinates with specialized experts.
 *
 * This agent decides when to delegate tasks and coordinates between
 * specialized agents.
 */
class MainAgent
{
  private:
    std::unique_ptr<agent_cpp::Agent> agent_;

    static const std::string& get_instructions()
    {
        static const std::string instructions =
          "You are a helpful assistant that coordinates with specialized "
          "experts "
          "to provide accurate answers.\n\n"
          "When the user asks a mathematical question or needs calculations:\n"
          "- Use the 'delegate_to_math_expert' tool to get accurate results\n"
          "- The math expert has a calculator and specializes in arithmetic\n\n"
          "For general questions, answer directly. For math questions, always "
          "delegate to ensure accuracy.";
        return instructions;
    }

  public:
    MainAgent(std::shared_ptr<agent_cpp::ModelWeights> weights,
              MathAgent* math_agent,
              const std::string& cache_path)
    {
        auto model = agent_cpp::Model::create_with_weights(weights);

        std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
        tools.push_back(std::make_unique<DelegateMathTool>(math_agent));

        std::vector<std::unique_ptr<agent_cpp::Callback>> callbacks;
        callbacks.push_back(std::make_unique<LoggingCallback>());
        callbacks.push_back(std::make_unique<ErrorRecoveryCallback>());

        agent_ = std::make_unique<agent_cpp::Agent>(std::move(model),
                                                    std::move(tools),
                                                    std::move(callbacks),
                                                    get_instructions());

        load_or_create_agent_cache(*agent_, cache_path);
    }

    agent_cpp::Agent& get() { return *agent_; }
};

void
print_usage(const char* program)
{
    fprintf(stderr, "Usage: %s -m <model_path>\n", program);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -m <path>  Path to GGUF model file (required)\n");
    fprintf(stderr, "  -h         Show this help message\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s -m granite-4.0-micro-Q8_0.gguf\n", program);
}

int
main(int argc, char** argv)
{
    std::string model_path;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (model_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        fprintf(stderr, "Loading model weights from: %s\n", model_path.c_str());
        fprintf(stderr, "(Weights are shared between all agents)\n\n");

        auto weights = agent_cpp::ModelWeights::create(model_path);

        fprintf(stderr, "Creating Math Agent (specialized sub-agent)...\n");
        MathAgent math_agent(weights, "math_agent.cache");

        fprintf(stderr, "Creating Main Agent (orchestrator)...\n");
        MainAgent main_agent(weights, &math_agent, "main_agent.cache");

        fprintf(stderr, "\nMulti-Agent System Ready\n");
        fprintf(stderr, "\nTry asking math questions like:\n");
        fprintf(stderr,
                "  'If I have 156 apples and give away 47, how many "
                "remain?'\n\n");

        run_chat_loop(main_agent.get());

    } catch (const agent_cpp::Error& e) {
        fprintf(stderr, "Agent error: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
