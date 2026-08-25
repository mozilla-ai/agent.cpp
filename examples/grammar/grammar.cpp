#include "agent.h"
#include "chat_loop.h"
#include "error.h"
#include "model.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static constexpr const char* DEFAULT_GRAMMAR_PATH = "sentiment.gbnf";
static constexpr const char* DEFAULT_GRAMMAR_ROOT = "root";

static void
print_usage(int /*unused*/, char** argv)
{
    printf("\nexample usage:\n");
    printf("\n    %s -m model.gguf\n", argv[0]);
    printf("\n");
    printf("options:\n");
    printf("  -m <path>       Path to the GGUF model file (required)\n");
    printf("  -g <path>       Path to a GBNF grammar file (default: %s)\n",
           DEFAULT_GRAMMAR_PATH);
    printf("  -r <name>       Grammar root rule (default: %s)\n",
           DEFAULT_GRAMMAR_ROOT);
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;
    std::string grammar_path = DEFAULT_GRAMMAR_PATH;
    std::string grammar_root = DEFAULT_GRAMMAR_ROOT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 < argc) {
                model_path = argv[++i];
            } else {
                print_usage(argc, argv);
                return 1;
            }
        } else if (strcmp(argv[i], "-g") == 0) {
            if (i + 1 < argc) {
                grammar_path = argv[++i];
            } else {
                print_usage(argc, argv);
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0) {
            if (i + 1 < argc) {
                grammar_root = argv[++i];
            } else {
                print_usage(argc, argv);
                return 1;
            }
        } else {
            print_usage(argc, argv);
            return 1;
        }
    }

    if (model_path.empty()) {
        print_usage(argc, argv);
        return 1;
    }

    printf("Loading grammar from '%s'...\n", grammar_path.c_str());
    auto model_config = agent_cpp::ModelConfig{};
    try {
        model_config.grammar = agent_cpp::load_grammar_file(grammar_path);
    } catch (const agent_cpp::ModelError& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    model_config.grammar_root = grammar_root;
    model_config.n_ctx = 4096;
    model_config.temp = 0.0F;

    printf("Loading model...\n");
    std::shared_ptr<agent_cpp::Model> model;
    try {
        model = agent_cpp::Model::create(model_path, model_config);
    } catch (const agent_cpp::ModelError& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    printf("Model loaded successfully\n");

    // No tools are needed: the grammar alone constrains the output.
    std::vector<std::unique_ptr<agent_cpp::Tool>> tools;

    const std::string instructions =
      "You are a sentiment classifier. Given a message from the user, "
      "respond with your assessment of its sentiment. Do not explain your "
      "reasoning, just answer directly.";

    agent_cpp::Agent agent(
      std::move(model), std::move(tools), {}, instructions);

    printf("\nGrammar Demo ready!\n");
    printf("   Every response is constrained by sentiment.gbnf, so the "
           "model can only ever reply with {\"sentiment\": \"positive\" | "
           "\"negative\" | \"neutral\"}, regardless of what you type.\n");
    printf("   Type an empty line to quit.\n\n");

    run_chat_loop(agent);
    return 0;
}
