#include "model.h"
#include "chat.h"
#include <cstdio>
#include <stdexcept>

std::unique_ptr<Model>
Model::create(const std::string& model_path, const ModelConfig& sampler_config)
{
    std::unique_ptr<Model> model(new Model());
    if (!model->initialize(model_path, sampler_config)) {
        return nullptr;
    }
    return model;
}

Model::~Model()
{
    if (sampler != nullptr) {
        llama_sampler_free(sampler);
    }
    if (ctx != nullptr) {
        llama_free(ctx);
    }
    if (model != nullptr) {
        llama_model_free(model);
    }
}

bool
Model::initialize(const std::string& model_path,
                  const ModelConfig& sampler_config)
{
    config_ = sampler_config;
    ggml_backend_load_all();

    llama_model_params model_params = llama_model_default_params();
    model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (model == nullptr) {
        fprintf(stderr,
                "%s: error: unable to load model from %s\n",
                __func__,
                model_path.c_str());
        return false;
    }

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 0; // Use default context size (model's max)
    ctx_params.n_batch = -1;

    ctx = llama_init_from_model(model, ctx_params);
    if (ctx == nullptr) {
        fprintf(
          stderr, "%s: error: failed to create the llama_context\n", __func__);
        return false;
    }

    sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler,
                            llama_sampler_init_top_k(sampler_config.top_k));
    llama_sampler_chain_add(sampler,
                            llama_sampler_init_top_p(sampler_config.top_p, 1));
    llama_sampler_chain_add(sampler,
                            llama_sampler_init_min_p(sampler_config.min_p, 1));
    llama_sampler_chain_add(sampler,
                            llama_sampler_init_temp(sampler_config.temp));
    llama_sampler_chain_add(sampler,
                            llama_sampler_init_dist(sampler_config.seed));

    auto tmpls =
      common_chat_templates_init(model, /* chat_template_override */ "");
    if (!tmpls) {
        fprintf(
          stderr, "%s: error: failed to initialize chat templates\n", __func__);
        return false;
    }
    templates = std::shared_ptr<common_chat_templates>(std::move(tmpls));

    return true;
}

std::vector<llama_token>
Model::tokenize(const std::string& prompt) const
{
    const llama_vocab* vocab = llama_model_get_vocab(model);
    const bool IS_FIRST =
      llama_memory_seq_pos_max(llama_get_memory(ctx), 0) == -1;

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

    auto params = common_chat_templates_apply(templates.get(), inputs);

    // Tokenize the prompt
    std::vector<llama_token> prompt_tokens = tokenize(params.prompt);
    if (prompt_tokens.empty()) {
        fprintf(stderr, "failed to tokenize the prompt\n");
        return {};
    }

    std::string response = generate_from_tokens(prompt_tokens, callback);

    common_chat_syntax syntax;
    syntax.format = config_.chat_format;
    syntax.parse_tool_calls = true;

    auto parsed_msg = common_chat_parse(response, false, syntax);
    parsed_msg.role = "assistant";

    return parsed_msg;
}

std::string
Model::generate_from_tokens(const std::vector<llama_token>& all_tokens,
                            const ResponseCallback& callback)
{
    const llama_vocab* vocab = llama_model_get_vocab(model);
    std::string response{};
    const int n_ctx = llama_n_ctx(ctx);
    const int n_batch = llama_n_batch(ctx);

    // Find common prefix length between processed tokens and new tokens
    size_t common_prefix = 0;
    while (common_prefix < processed_tokens.size() &&
           common_prefix < all_tokens.size() &&
           processed_tokens[common_prefix] == all_tokens[common_prefix]) {
        common_prefix++;
    }

    // If tokens diverged, clear KV cache from divergence point onwards
    if (common_prefix < processed_tokens.size()) {
        llama_memory_t mem = llama_get_memory(ctx);
        llama_memory_seq_rm(mem, 0, common_prefix, -1);
        processed_tokens.resize(common_prefix);
        n_past = common_prefix;
    }

    size_t i = common_prefix;
    while (i < all_tokens.size()) {
        size_t batch_size = std::min(all_tokens.size() - i, (size_t)n_batch);

        if (n_past + (int)batch_size > n_ctx) {
            fprintf(stderr, "context size exceeded\n");
            return response;
        }

        std::vector<llama_token> batch_tokens(
          all_tokens.begin() + i, all_tokens.begin() + i + batch_size);

        llama_batch batch =
          llama_batch_get_one(batch_tokens.data(), batch_tokens.size());

        if (llama_decode(ctx, batch) != 0) {
            throw std::runtime_error("failed to decode");
        }

        n_past += batch_tokens.size();
        processed_tokens.insert(
          processed_tokens.end(), batch_tokens.begin(), batch_tokens.end());
        i += batch_size;
    }

    llama_token new_token_id{};
    while (true) {
        new_token_id = llama_sampler_sample(sampler, ctx, -1);

        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break;
        }

        char buf[256];
        int n =
          llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            throw std::runtime_error("failed to convert token to piece");
        }
        std::string piece(buf, n);

        if (callback) {
            callback(piece);
        }
        response += piece;

        if (n_past + 1 > n_ctx) {
            fprintf(stderr, "context size exceeded\n");
            break;
        }

        llama_batch batch = llama_batch_get_one(&new_token_id, 1);
        if (llama_decode(ctx, batch) != 0) {
            throw std::runtime_error("failed to decode");
        }

        n_past++;
        processed_tokens.push_back(new_token_id);
    }

    return response;
}
