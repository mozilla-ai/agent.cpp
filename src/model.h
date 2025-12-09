#pragma once

#include "chat.h"
#include "llama.h"
#include <functional>
#include <memory>
#include <string>

// Callback for streaming response chunks
using ResponseCallback = std::function<void(const std::string& chunk)>;

// Model configuration with sensible defaults
struct ModelConfig
{
    float min_p = 0.0F;
    float top_p = 1.0F;
    int top_k = 0;
    float temp = 0.0F;
    uint32_t seed = LLAMA_DEFAULT_SEED;
    common_chat_format chat_format = COMMON_CHAT_FORMAT_HERMES_2_PRO;
    int n_ctx = 10240;
    int n_batch = -1;
};

// Model interface - encapsulates model initialization and text generation
class Model
{
  public:
    // Initialize the model from a GGUF file
    // Returns nullptr on failure
    // Optional sampler_config allows customizing sampling parameters
    static std::unique_ptr<Model> create(
      const std::string& model_path,
      const ModelConfig& sampler_config = ModelConfig{});

    // Destructor - Frees sampler, context, and model resources
    ~Model();

    // Generate text from chat messages and tools
    // Applies chat templates, tokenizes, and generates response
    // Returns parsed message with role set to "assistant"
    common_chat_msg generate(const std::vector<common_chat_msg>& messages,
                             const std::vector<common_chat_tool>& tools,
                             const ResponseCallback& callback = nullptr);

    // Generate text from pre-tokenized input, only processing new tokens
    // Uses KV cache efficiently by tracking previously processed tokens
    std::string generate_from_tokens(
      const std::vector<llama_token>& all_tokens,
      const ResponseCallback& callback = nullptr);

    // Tokenize a prompt string into tokens
    // Returns empty vector on failure
    std::vector<llama_token> tokenize(const std::string& prompt) const;

    // Get the chat templates
    [[nodiscard]] common_chat_templates* get_templates() const
    {
        return templates.get();
    }

    // Get the vocabulary for tokenization
    [[nodiscard]] const llama_vocab* get_vocab() const
    {
        return llama_model_get_vocab(model);
    }

    // Get the context for KV cache management
    [[nodiscard]] llama_context* get_context() const { return ctx; }

    // Save the current KV cache state (processed_tokens) to a file
    // Returns true on success, false on failure
    bool save_cache(const std::string& cache_path);

    // Load KV cache state from a file
    // Returns the tokens that were cached, or empty vector on failure
    // The loaded state will be applied to the context
    std::vector<llama_token> load_cache(const std::string& cache_path);

  private:
    // Set the internal cache state (used when loading from prompt cache)
    void set_cache_state(const std::vector<llama_token>& tokens)
    {
        processed_tokens = tokens;
        n_past = static_cast<int>(tokens.size());
    }
    Model() = default;

    bool initialize(const std::string& model_path,
                    const ModelConfig& sampler_config);

    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    llama_sampler* sampler = nullptr;
    std::shared_ptr<common_chat_templates> templates;
    std::vector<llama_token> processed_tokens; // Track tokens in KV cache
    int n_past = 0;                            // Track position in KV cache
    ModelConfig config_;                       // Store model configuration
};
