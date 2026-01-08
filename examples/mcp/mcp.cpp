#include "agent.h"
#include "chat_loop.h"
#include "error.h"
#include "error_recovery_callback.h"
#include "logging_callback.h"
#include "mcp/mcp_client.h"
#include "model.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace agent_cpp;

static void
print_usage(int /*unused*/, char** argv)
{
    printf("\nexample usage:\n");
    printf("\n    %s -m model.gguf -u http://localhost:8080/mcp\n", argv[0]);
    printf("\n");
    printf("options:\n");
    printf("  -m <path>       Path to the GGUF model file (required)\n");
    printf("  -u <url>        MCP server URL (required)\n");
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;
    std::string mcp_url;

    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0) {
                if (i + 1 < argc) {
                    model_path = argv[++i];
                } else {
                    print_usage(argc, argv);
                    return 1;
                }
            } else if (strcmp(argv[i], "-u") == 0) {
                if (i + 1 < argc) {
                    mcp_url = argv[++i];
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

    if (model_path.empty() || mcp_url.empty()) {
        print_usage(argc, argv);
        return 1;
    }

    try {
        printf("Connecting to MCP server: %s\n", mcp_url.c_str());
        auto mcp_client = MCPClient::create(mcp_url);

        printf("Initializing MCP session...\n");
        if (!mcp_client->initialize("agent.cpp-mcp-example", "0.1.0")) {
            fprintf(stderr, "Failed to initialize MCP session\n");
            return 1;
        }

        printf("MCP session initialized.\n");

        auto tools = mcp_client->get_tools();

        printf("\nAvailable tools (%zu):\n", tools.size());
        for (const auto& tool : tools) {
            auto def = tool->get_definition();
            printf("  - %s: %s\n", def.name.c_str(), def.description.c_str());
        }
        printf("\n");

        if (tools.empty()) {
            printf("No tools available from MCP server.\n");
            return 0;
        }

        printf("Loading model...\n");
        std::shared_ptr<Model> model;
        try {
            model = Model::create(model_path);
        } catch (const ModelError& e) {
            fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
        printf("Model loaded successfully\n");

        const std::string instructions =
          "You are a helpful assistant with access to tools. "
          "Use these tools to help answer user questions. ";

        std::vector<std::unique_ptr<Callback>> callbacks;
        callbacks.push_back(std::make_unique<LoggingCallback>());
        callbacks.push_back(std::make_unique<ErrorRecoveryCallback>());

        Agent agent(std::move(model),
                    std::move(tools),
                    std::move(callbacks),
                    instructions);

        agent.load_or_create_cache("mcp.cache");

        printf("\nMCP Agent ready!\n");
        printf("   Connected to: %s\n", mcp_url.c_str());
        printf("   Type an empty line to quit.\n\n");

        run_chat_loop(agent);
        return 0;

    } catch (const MCPError& e) {
        fprintf(stderr, "MCP Error: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
