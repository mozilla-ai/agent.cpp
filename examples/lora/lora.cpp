#include "agent.h"
#include "chat_loop.h"
#include "error.h"
#include "model.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr float DEFAULT_SCALE = 1.0F;

static void
print_usage(int /*unused*/, char** argv)
{
    printf("\nexample usage:\n");
    printf("\n    %s -m model.gguf -l adapter.gguf\n", argv[0]);
    printf("\n");
    printf("options:\n");
    printf("  -m <path>       Path to the base GGUF model file (required)\n");
    printf("  -l <path>       Path to the GGUF LoRA adapter (required)\n");
    printf("  -s <scale>      Adapter scale (default: %.1f)\n", DEFAULT_SCALE);
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;
    std::string adapter_path;
    float scale = DEFAULT_SCALE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "-l") == 0 ||
            strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                print_usage(argc, argv);
                return 1;
            }
            const char* value = argv[++i];
            if (strcmp(argv[i - 1], "-m") == 0) {
                model_path = value;
            } else if (strcmp(argv[i - 1], "-l") == 0) {
                adapter_path = value;
            } else {
                try {
                    size_t parsed = 0;
                    scale = std::stof(value, &parsed);
                    if (parsed != strlen(value)) {
                        throw std::invalid_argument("trailing characters");
                    }
                } catch (const std::exception&) {
                    fprintf(stderr, "error: -s must be a number\n");
                    return 1;
                }
            }
        } else {
            print_usage(argc, argv);
            return 1;
        }
    }

    if (model_path.empty() || adapter_path.empty()) {
        print_usage(argc, argv);
        return 1;
    }

    auto model_config = agent_cpp::ModelConfig{};
    model_config.n_ctx = 4096;
    model_config.temp = 0.0F;
    model_config.loras.push_back({ adapter_path, scale });

    printf("Loading base model '%s'...\n", model_path.c_str());
    printf("Loading LoRA adapter '%s' at scale %.3f...\n",
           adapter_path.c_str(),
           scale);

    std::shared_ptr<agent_cpp::Model> model;
    try {
        model = agent_cpp::Model::create(model_path, model_config);
    } catch (const agent_cpp::ModelError& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    printf("Model and LoRA adapter loaded successfully\n");

    std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
    const std::string instructions =
      "Follow the task prompt template expected by the loaded adapter. "
      "The adapter may require a task-specific instruction, conversation "
      "context, and output format rather than a general chat response.";

    agent_cpp::Agent agent(
      std::move(model), std::move(tools), {}, instructions);

    printf("\nLoRA Demo ready!\n");
    printf("   The adapter is applied for every response at scale %.3f.\n",
           scale);
    printf(
      "   Use the adapter's documented task prompt template when chatting.\n");
    printf("   Type an empty line to quit.\n\n");

    run_chat_loop(agent);
    return 0;
}
