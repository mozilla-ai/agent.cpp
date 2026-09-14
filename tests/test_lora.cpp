#include "error.h"
#include "model.h"
#include "test_utils.h"

TEST(test_model_config_loras_defaults)
{
    agent_cpp::ModelConfig config;

    ASSERT_TRUE(config.loras.empty());
}

TEST(test_lora_adapter_config_default_scale)
{
    agent_cpp::LoraAdapterConfig lora_config;
    lora_config.path = "adapter.gguf";

    ASSERT_EQ(lora_config.scale, 1.0F);
}

TEST(test_model_config_loras_accepts_multiple_adapters)
{
    agent_cpp::ModelConfig config;
    config.loras.push_back({ "math_lora.gguf", 1.0F });
    config.loras.push_back({ "style_lora.gguf", 0.5F });

    ASSERT_EQ(config.loras.size(), (size_t)2);
    ASSERT_EQ(config.loras[0].path, "math_lora.gguf");
    ASSERT_EQ(config.loras[1].scale, 0.5F);
}

// Full adapter loading requires a loaded GGUF model and context. Verify
// manually in examples/lora.

int
main()
{
    std::cout << "\n=== Running LoRA Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_model_config_loras_defaults);
        RUN_TEST(test_lora_adapter_config_default_scale);
        RUN_TEST(test_model_config_loras_accepts_multiple_adapters);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
