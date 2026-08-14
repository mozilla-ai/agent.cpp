#include "error.h"
#include "model.h"
#include "test_utils.h"
#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string
write_temp_file(const std::string& filename, const std::string& contents)
{
    std::ofstream file(filename);
    file << contents;
    return filename;
}

}

TEST(test_model_config_grammar_defaults)
{
    agent_cpp::ModelConfig config;

    ASSERT_TRUE(config.grammar.empty());
    ASSERT_EQ(config.grammar_root, "root");
}

TEST(test_load_grammar_file_reads_contents)
{
    const std::string filename = "test_grammar_tmp.gbnf";
    const std::string grammar = "root ::= \"yes\" | \"no\"\n";

    write_temp_file(filename, grammar);

    std::string loaded = agent_cpp::load_grammar_file(filename);
    std::remove(filename.c_str());

    ASSERT_EQ(loaded, grammar);
}

TEST(test_load_grammar_file_missing_file_throws)
{
    bool threw = false;
    try {
        agent_cpp::load_grammar_file("nonexistent_file_12345.gbnf");
    } catch (const agent_cpp::ModelError&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

// This needs a loaded GGUF model and a real llama_context, which this test
// file does not exercise. Verify the turn-to-turn grammar reset manually in
// examples/grammar.

int
main()
{
    std::cout << "\n=== Running Grammar Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_model_config_grammar_defaults);
        RUN_TEST(test_load_grammar_file_reads_contents);
        RUN_TEST(test_load_grammar_file_missing_file_throws);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
