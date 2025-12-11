#include "error.h"
#include "model.h"
#include "test_utils.h"
#include <memory>

TEST(test_model_weights_invalid_path_throws)
{
    bool threw = false;
    try {
        auto weights =
          agent_cpp::ModelWeights::create("/nonexistent/path/model.gguf");
    } catch (const agent_cpp::ModelError& e) {
        threw = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("nonexistent") != std::string::npos);
    }
    ASSERT_TRUE(threw);
}

TEST(test_model_create_invalid_path_throws)
{
    bool threw = false;
    try {
        auto model = agent_cpp::Model::create("/nonexistent/path/model.gguf");
    } catch (const agent_cpp::ModelError& e) {
        threw = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("nonexistent") != std::string::npos);
    }
    ASSERT_TRUE(threw);
}

int
main()
{
    std::cout << "\n=== Running Model Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_model_weights_invalid_path_throws);
        RUN_TEST(test_model_create_invalid_path_throws);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
