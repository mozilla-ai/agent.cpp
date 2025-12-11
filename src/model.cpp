#include "model.h"
#include "chat.h"
#include "error.h"
#include <algorithm>
#include <cstdio>

namespace agent_cpp {

std::shared_ptr<ModelWeights>
ModelWeights::create(const std::string& model_path)
{
    std::shared_ptr<ModelWeights> weights(new ModelWeights());

    ggml_backend_load_all();

    llama_model_params model_params = llama_model_default_params();
    weights->model_ =
      llama_model_load_from_file(model_path.c_str(), model_params);
    if (weights->model_ == nullptr) {
        throw ModelError("unable to load model from '" + model_path + "'");
    }

    auto tmpls = common_chat_templates_init(weights->model_,
                                            /* chat_template_override */ "");
    if (!tmpls) {
        throw ModelError("failed to initialize chat templates");
    }
    weights->templates_ =
      std::shared_ptr<common_chat_templates>(std::move(tmpls));

    return weights;
}

ModelWeights::~ModelWeights()
{
    if (model_ != nullptr) {
        llama_model_free(model_);
    }
}

std::shared_ptr<Model>
Model::create(const std::string& model_path, const ModelConfig& model_config)
{
    auto weights = ModelWeights::create(model_path);
    return create_with_weights(std::move(weights), model_config);
}

std::shared_ptr<Model>
Model::create_with_weights(std::shared_ptr<ModelWeights> weights,
                           const ModelConfig& model_config)
{
    std::shared_ptr<Model> model(new Model());
    model->weights_ = std::move(weights);
    model->initialize_context(model_config);
    return model;
}

Model::~Model()
{
    if (sampler_ != nullptr) {
        llama_sampler_free(sampler_);
    }
    if (ctx_ != nullptr) {
        llama_free(ctx_);
    }
    // weights_ is automatically released when ref count drops to zero
}

Model::Model(Model&& other) noexcept
  : weights_(std::move(other.weights_))
  , ctx_(other.ctx_)
  , sampler_(other.sampler_)
  , processed_tokens_(std::move(other.processed_tokens_))
  , n_past_(other.n_past_)
  , config_(other.config_)
{
    other.ctx_ = nullptr;
    other.sampler_ = nullptr;
    other.n_past_ = 0;
}

Model&
Model::operator=(Model&& other) noexcept
{
    if (this != &other) {
        if (sampler_ != nullptr) {
            llama_sampler_free(sampler_);
        }
        if (ctx_ != nullptr) {
            llama_free(ctx_);
        }

        weights_ = std::move(other.weights_);
        ctx_ = other.ctx_;
        sampler_ = other.sampler_;
        processed_tokens_ = std::move(other.processed_tokens_);
        n_past_ = other.n_past_;
        config_ = other.config_;

        other.ctx_ = nullptr;
        other.sampler_ = nullptr;
        other.n_past_ = 0;
    }
    return *this;
}

void
Model::initialize_context(const ModelConfig& model_config)
{
    config_ = model_config;

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = model_config.n_ctx;
    ctx_params.n_batch = model_config.n_batch;

    ctx_ = llama_init_from_model(weights_->get_model(), ctx_params);
    if (ctx_ == nullptr) {
        throw ModelError("failed to create llama context");
    }

    sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler_,
                            llama_sampler_init_top_k(model_config.top_k));
    llama_sampler_chain_add(sampler_,
                            llama_sampler_init_top_p(model_config.top_p, 1));
    llama_sampler_chain_add(sampler_,
                            llama_sampler_init_min_p(model_config.min_p, 1));
    llama_sampler_chain_add(sampler_,
                            llama_sampler_init_temp(model_config.temp));
    llama_sampler_chain_add(sampler_,
                            llama_sampler_init_dist(model_config.seed));
}

std::vector<llama_token>
Model::tokenize(const std::string& prompt) const
{
    const llama_vocab* vocab = weights_->get_vocab();
    // Use processed_tokens to determine if this is the first tokenization
    // This is important for cache loading: even if KV cache memory is
    // populated, we need IS_FIRST=true if we're tokenizing a full prompt from
    // scratch to ensure consistent BOS token handling for prefix matching
    const bool IS_FIRST = processed_tokens_.empty();

    const int N_PROMPT_TOKENS = -llama_tokenize(
      vocab, prompt.c_str(), prompt.size(), nullptr, 0, IS_FIRST, true);
    std::vector<llama_token> prompt_tokens(N_PROMPT_TOKENS);
    if (llama_tokenize(vocab,
                       prompt.c_str(),
                       prompt.size(),
                       prompt_tokens.data(),
                       prompt_tokens.size(),
                       IS_FIRST,
                       true) < 0) {
        return {}; // Return empty vector on failure
    }
    return prompt_tokens;
}

common_chat_msg
Model::generate(const std::vector<common_chat_msg>& messages,
                const std::vector<common_chat_tool>& tools,
                const ResponseCallback& callback)
{
    common_chat_templates_inputs inputs;
    inputs.messages = messages;
    inputs.tools = tools;
    inputs.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    inputs.add_generation_prompt = true;
    inputs.enable_thinking = false;

    auto params =
      common_chat_templates_apply(weights_->get_templates(), inputs);

    // Tokenize the prompt
    std::vector<llama_token> prompt_tokens = tokenize(params.prompt);
    if (prompt_tokens.empty()) {
        throw ModelError("failed to tokenize prompt");
    }

    std::string response = generate_from_tokens(prompt_tokens, callback);

    common_chat_syntax syntax;
    // Use explicitly configured format, or fall back to auto-detected format
    syntax.format = config_.chat_format.value_or(params.format);
    syntax.parse_tool_calls = true;

    auto parsed_msg = common_chat_parse(response, false, syntax);
    parsed_msg.role = "assistant";

    return parsed_msg;
}

std::string
Model::generate_from_tokens(const std::vector<llama_token>& all_tokens,
                            const ResponseCallback& callback)
{
    const llama_vocab* vocab = weights_->get_vocab();
    std::string response{};
    const int n_ctx = llama_n_ctx(ctx_);
    const int n_batch = llama_n_batch(ctx_);

    // Find common prefix length between processed tokens and new tokens
    size_t common_prefix = 0;
    while (common_prefix < processed_tokens_.size() &&
           common_prefix < all_tokens.size() &&
           processed_tokens_[common_prefix] == all_tokens[common_prefix]) {
        common_prefix++;
    }

    // If tokens diverged, clear KV cache from divergence point onwards
    if (common_prefix < processed_tokens_.size()) {
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_seq_rm(mem, 0, common_prefix, -1);
        processed_tokens_.resize(common_prefix);
        n_past_ = common_prefix;
    }

    size_t i = common_prefix;
    while (i < all_tokens.size()) {
        size_t batch_size = std::min(all_tokens.size() - i, (size_t)n_batch);

        if (n_past_ + (int)batch_size > n_ctx) {
            throw ModelError("context size exceeded");
        }

        std::vector<llama_token> batch_tokens(
          all_tokens.begin() + i, all_tokens.begin() + i + batch_size);

        llama_batch batch =
          llama_batch_get_one(batch_tokens.data(), batch_tokens.size());

        if (llama_decode(ctx_, batch) != 0) {
            throw ModelError("failed to decode batch");
        }

        n_past_ += batch_tokens.size();
        processed_tokens_.insert(
          processed_tokens_.end(), batch_tokens.begin(), batch_tokens.end());
        i += batch_size;
    }

    llama_token new_token_id{};
    while (true) {
        new_token_id = llama_sampler_sample(sampler_, ctx_, -1);

        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break;
        }

        char buf[256];
        int n =
          llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            throw ModelError("failed to convert token to piece");
        }
        std::string piece(buf, n);

        if (callback) {
            callback(piece);
        }
        response += piece;

        if (n_past_ + 1 > n_ctx) {
            throw ModelError("context size exceeded during generation");
        }

        llama_batch batch = llama_batch_get_one(&new_token_id, 1);
        if (llama_decode(ctx_, batch) != 0) {
            throw ModelError("failed to decode token");
        }

        n_past_++;
        processed_tokens_.push_back(new_token_id);
    }

    return response;
}

bool
Model::save_cache(const std::string& cache_path)
{
    return llama_state_save_file(ctx_,
                                 cache_path.c_str(),
                                 processed_tokens_.data(),
                                 processed_tokens_.size());
}

std::vector<llama_token>
Model::load_cache(const std::string& cache_path)
{
    // Start with a reasonable capacity, will be resized based on actual count
    std::vector<llama_token> tokens(llama_n_ctx(ctx_));
    size_t n_token_count_out = 0;

    if (!llama_state_load_file(ctx_,
                               cache_path.c_str(),
                               tokens.data(),
                               tokens.capacity(),
                               &n_token_count_out)) {
        return {};
    }

    tokens.resize(n_token_count_out);
    set_cache_state(tokens);
    return tokens;
}

} // namespace agent_cpp
