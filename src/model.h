#pragma once

#include "chat.h"
#include "llama.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace agent_cpp {

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
    // When nullopt (default), the format is auto-detected from the model's chat
    // template.
    std::optional<common_chat_format> chat_format = std::nullopt;
    int n_ctx = 10240;
    int n_batch = -1;
    int n_threads =
      static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));
    int n_threads_batch =
      static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));
    ggml_type cache_type_k = GGML_TYPE_F16;
    ggml_type cache_type_v = GGML_TYPE_F16;
};

// Forward declaration
class Model;

/// @brief Immutable model weights that can be shared across multiple Model
/// instances.
///
/// ModelWeights loads the model file once and holds the heavy VRAM/memory
/// resources. Multiple Model instances (each with their own context/KV cache)
/// can share the same ModelWeights, enabling concurrent agents without loading
/// weights multiple times.
class ModelWeights
{
    friend class Model;

  public:
    /// @brief Load model weights from a GGUF file
    /// @param model_path Path to the GGUF model file
    /// @return Shared pointer to the loaded weights
    /// @throws agent_cpp::ModelError if loading fails
    static std::shared_ptr<ModelWeights> create(const std::string& model_path);

    ~ModelWeights();

    // Non-copyable, non-movable (shared via shared_ptr)
    ModelWeights(const ModelWeights&) = delete;
    ModelWeights& operator=(const ModelWeights&) = delete;
    ModelWeights(ModelWeights&&) = delete;
    ModelWeights& operator=(ModelWeights&&) = delete;

    /// @brief Get the underlying llama_model pointer
    [[nodiscard]] llama_model* get_model() const { return model_; }

    /// @brief Get the chat templates
    [[nodiscard]] common_chat_templates* get_templates() const
    {
        return templates_.get();
    }

    /// @brief Get the vocabulary for tokenization
    [[nodiscard]] const llama_vocab* get_vocab() const
    {
        return llama_model_get_vocab(model_);
    }

  private:
    ModelWeights() = default;

    llama_model* model_ = nullptr;
    std::shared_ptr<common_chat_templates> templates_;
};

// Model interface - encapsulates context and text generation
// Each Model instance has its own context (KV cache) but can share weights
class Model
{
  public:
    /// @brief Initialize the model from a GGUF file
    /// @param model_path Path to the GGUF model file
    /// @param model_config Optional configuration
    /// @return Shared pointer to the initialized Model
    /// @throws agent_cpp::ModelError if model loading or initialization fails
    static std::shared_ptr<Model> create(
      const std::string& model_path,
      const ModelConfig& model_config = ModelConfig{});

    /// @brief Create a new Model instance sharing weights with existing weights
    /// @param weights Shared pointer to ModelWeights
    /// @param model_config Optional configuration
    /// @return Shared pointer to the new Model
    /// @throws agent_cpp::ModelError if context creation fails
    ///
    /// This enables multiple agents with independent contexts (KV caches)
    /// sharing the same model weights, avoiding duplicate VRAM usage.
    static std::shared_ptr<Model> create_with_weights(
      std::shared_ptr<ModelWeights> weights,
      const ModelConfig& model_config = ModelConfig{});

    // Destructor - Frees sampler and context (weights are ref-counted)
    ~Model();

    // Delete copy operations to prevent double-free
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Move operations
    Model(Model&& other) noexcept;
    Model& operator=(Model&& other) noexcept;

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
        return weights_->get_templates();
    }

    // Get the vocabulary for tokenization
    [[nodiscard]] const llama_vocab* get_vocab() const
    {
        return weights_->get_vocab();
    }

    // Get the context for KV cache management
    [[nodiscard]] llama_context* get_context() const { return ctx_; }

    // Get the shared weights (for creating additional Model instances)
    [[nodiscard]] std::shared_ptr<ModelWeights> get_weights() const
    {
        return weights_;
    }

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
        processed_tokens_ = tokens;
        n_past_ = static_cast<int>(tokens.size());
    }
    Model() = default;

    void initialize_context(const ModelConfig& model_config);

    std::shared_ptr<ModelWeights> weights_;
    llama_context* ctx_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    std::vector<llama_token> processed_tokens_; // Track tokens in KV cache
    int n_past_ = 0;                            // Track position in KV cache
    ModelConfig config_;
};

} // namespace agent_cpp
