#include "agent.h"
#include "callbacks.h"
#include "chat.h"
#include "chat_loop.h"
#include "error.h"
#include "error_recovery_callback.h"
#include "llama.h"
#include "model.h"
#include "prompt_cache.h"
#include "tool.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

using agent_cpp::json;
using agent_cpp::ToolExecutionSkipped;

// Shell command execution tool
// This demonstrates how an agent can combine multiple operations into a single
// shell script, instead of having to call individual tools like ls, mkdir,
// touch, etc.
class ShellTool : public agent_cpp::Tool
{
  public:
    ShellTool() = default;

    common_chat_tool get_definition() const override
    {
        json schema = {
            { "type", "object" },
            { "properties",
              {
                { "command",
                  { { "type", "string" },
                    { "description",
                      "The shell command or script to execute. Can be a single "
                      "command or a multi-line shell script. Use bash syntax. "
                      "Examples:\n"
                      "- Single command: 'ls -la'\n"
                      "- Multiple commands: 'mkdir -p mydir && cd mydir && "
                      "touch file.txt'\n"
                      "- Script with logic: 'for f in *.txt; do echo "
                      "\"Processing $f\"; done'\n"
                      "- Pipes and redirects: 'cat file.txt | grep pattern | "
                      "wc -l'" } } },
              } },
            { "required", { "command" } }
        };

        return {
            "shell",
            "Execute shell commands or scripts. This tool allows you to run "
            "any bash command or multi-line script. You can combine multiple "
            "operations in a single call using shell scripting features like "
            "&&, ||, pipes, loops, conditionals, etc. This is more efficient "
            "than calling individual file operation tools separately.",
            schema.dump()
        };
    }

    std::string get_name() const override { return "shell"; }

    // WARNING: SECURITY RISK - This executes arbitrary shell commands!
    // In production, you MUST implement safeguards such as:
    // - User confirmation via a Callback (see ShellConfirmationCallback below)
    // - Command allowlisting/denylisting
    // - Sandboxing (containers, chroot, seccomp, etc.)
    // - Input validation and sanitization
    // Without these, a model could execute destructive commands like
    // 'rm -rf /' or exfiltrate sensitive data.
    std::string execute(const json& arguments) override
    {
        std::string command = arguments.at("command").get<std::string>();

        json response;

        std::string full_command = command + " 2>&1";

        std::array<char, 4096> buffer;
        std::string output;

        FILE* pipe = popen(full_command.c_str(), "r");
        if (!pipe) {
            response["output"] = "Error: Failed to execute command";
            return response.dump();
        }

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }

        pclose(pipe);

        response["output"] = output.empty() ? "(no output)" : output;

        return response.dump();
    }
};

class ShellConfirmationCallback : public agent_cpp::Callback
{
  public:
    void before_tool_execution(std::string& tool_name,
                               std::string& arguments) override
    {
        std::string command;
        try {
            json args = json::parse(arguments);
            command = args.value("command", arguments);
        } catch (...) {
            command = arguments;
        }

        if (isatty(fileno(stdout))) {
            printf("\n\033[34mSHELL COMMAND CONFIRMATION REQUIRED\033[0m\n");
            printf("\033[1mCommand to execute:\033[0m\n");
            printf("  \033[36m%s\033[0m\n\n", command.c_str());
            printf("Execute this command? [\033[32my\033[0m]es / "
                   "[\033[31mn\033[0m]o / [\033[34me\033[0m]dit: ");
        } else {
            printf("\nSHELL COMMAND CONFIRMATION REQUIRED\n");
            printf("Command to execute:\n");
            printf("  %s\n\n", command.c_str());
            printf("Execute this command? [y]es / [n]o / [e]dit: ");
        }
        fflush(stdout);

        std::string response;
        std::getline(std::cin, response);

        std::transform(
          response.begin(), response.end(), response.begin(), ::tolower);

        if (response.empty() || response == "y" || response == "yes") {
            printf("Executing...\n");
        } else if (response == "e" || response == "edit") {
            printf("Enter new command:\n");
            std::string new_command;
            std::getline(std::cin, new_command);

            if (!new_command.empty()) {
                json new_args;
                new_args["command"] = new_command;
                arguments = new_args.dump();
                printf("Command updated.\n");
            } else {
                printf("Empty command, using original.\n");
            }
        } else {
            printf("Command execution cancelled by user.\n");
            throw ToolExecutionSkipped(
              "Command execution was cancelled by user");
        }
    }

    void after_tool_execution(std::string& /*tool_name*/,
                              std::string& result) override
    {
        if (isatty(fileno(stderr))) {
            fprintf(
              stderr, "\033[34m[SHELL OUTPUT]\033[0m\n%s\n", result.c_str());
        } else {
            fprintf(stderr, "[SHELL OUTPUT]\n%s\n", result.c_str());
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
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;

    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0) {
                if (i + 1 < argc) {
                    model_path = argv[++i];
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

    printf("Setting up shell tool...\n");
    std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
    tools.push_back(std::make_unique<ShellTool>());
    printf("Shell tool configured\n");

    printf("Loading model...\n");
    std::shared_ptr<agent_cpp::Model> model;
    try {
        model = agent_cpp::Model::create(model_path);
    } catch (const agent_cpp::ModelError& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    printf("Model loaded successfully\n\n");

    const std::string instructions =
      "You are a helpful assistant with shell command execution capabilities. "
      "You have access to a 'shell' tool that can execute bash commands and "
      "scripts.\n"
      "Instead of calling multiple individual tools (like "
      "separate ls, mkdir, touch tools), you can combine everything into a "
      "single shell command or script.";

    std::vector<std::unique_ptr<agent_cpp::Callback>> callbacks;
    callbacks.push_back(std::make_unique<ShellConfirmationCallback>());
    callbacks.push_back(std::make_unique<ErrorRecoveryCallback>());

    agent_cpp::Agent agent(
      std::move(model), std::move(tools), std::move(callbacks), instructions);

    load_or_create_agent_cache(agent, "shell.cache");

    printf("Shell Agent ready!\n");
    printf("   This agent can execute shell commands and scripts.\n");
    printf("Type an empty line to quit.\n\n");

    run_chat_loop(agent);
    return 0;
}
