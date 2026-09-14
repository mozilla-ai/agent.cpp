# LoRA Example

This example loads a GGUF LoRA adapter onto a base model and runs an interactive chat. The adapter is applied to the model context with the requested scale, while the base model remains in the normal GGUF file.

## Building

> [!IMPORTANT]
> Check the [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md) to find
> CMake flags you might want to pass depending on your available hardware.

```bash
cd examples/lora

git -C ../.. submodule update --init --recursive

cmake -B build
cmake --build build -j$(nproc)
```

### Using a custom llama.cpp

If you have llama.cpp already downloaded:

```bash
cmake -B build -DLLAMA_CPP_DIR=/path/to/your/llama.cpp
cmake --build build -j$(nproc)
```

## Requirements

The adapter must be GGUF and compatible with the base model. The architecture must match, and the same base model must be used for conversion and inference.

Some adapter families also ship an aLoRA (Activated LoRA) variant alongside the standard one, in a separate folder such as `alora/`. This example only supports standard LoRA adapters — use the `lora/` variant, not `alora/`, if both are offered.

## Preparing an Adapter

If the adapter is already GGUF, skip to [Usage](#usage). Otherwise, convert it with llama.cpp's [`convert_lora_to_gguf.py`](https://github.com/ggml-org/llama.cpp/blob/master/convert_lora_to_gguf.py). You can download models from any registry; for Hugging Face, `huggingface-cli` is one option. The conversion input must contain the adapter configuration and weights. Use the same compatible base model for conversion and inference.

### Worked Example: Granite Query Rewriting

This example uses uv for dependency and environment management. The example below assumes you are in a uv-managed environment for the llama.cpp dependency setup. Run from the repository root:

```bash
wget https://huggingface.co/ibm-granite/granite-4.0-micro-GGUF/resolve/main/granite-4.0-micro-Q8_0.gguf

cd deps/llama.cpp
uv venv
source .venv/bin/activate
uv pip install -r requirements.txt
```

If your environment is configured with a package index that exposes an older `requests` build first, retry with:

```bash
uv pip install --index-strategy unsafe-best-match -r requirements.txt
```

```bash
hf download ibm-granite/granitelib-rag-r1.0 \
  "query_rewrite/granite-4.0-micro/lora/adapter_config.json" \
  "query_rewrite/granite-4.0-micro/lora/adapter_model.safetensors" \
  --local-dir ./granite-rag-adapters

python convert_lora_to_gguf.py \
  granite-rag-adapters/query_rewrite/granite-4.0-micro/lora \
  --base-model-id ibm-granite/granite-4.0-micro \
  --outfile ../../query_rewrite_lora.gguf
```

Return to the repository root and run the adapter with the binary built above:

```bash
cd ../..

./examples/lora/build/lora-example \
  -m ./granite-4.0-micro-Q8_0.gguf \
  -l ./query_rewrite_lora.gguf \
  -s 0.25
```

## Usage

```bash
./build/lora-example \
  -m /path/to/model.gguf \
  -l /path/to/adapter.gguf \
  -s 0.25
```

`-s` is the adapter scale; it defaults to `1.0` if omitted. A scale of `0` disables the adapter for the context without unloading it. Multiple adapters can be stacked through `ModelConfig::loras`; this example keeps the command line focused on one adapter.

## Using the Adapter's Prompt Template

A task-specific adapter also needs its documented prompt format. The base model's chat template handles the conversation, and the adapter task tells the model what to do with the latest user turn. Provide the LLM with the task below, and it should follow those instructions.

Use the exact prompt format documented for your adapter, and do not rely on a generic chat prompt. Loading successfully does not guarantee useful output unless the adapter-specific task instructions and output schema are supplied to the model. If you send a plain question with no task framing and no conversation history, the adapter has nothing to rewrite and will just echo the input back unchanged — this is the most common first-run surprise.

## Example

```console
$ ./examples/lora/build/lora-example \
  -m ./granite-4.0-micro-Q8_0.gguf \
  -l ./query_rewrite_lora.gguf \
  -s 0.25
  
> Task: Rewrite the latest question as a self-contained question.

Here is the latest question rewritten as a self-contained question:

What is the capital city of France?

> What about Germany?
What is the capital city of Germany?
>
```

This is the pattern to follow for a task-specific adapter: provide the task, the latest user question, and the expected rewritten output.
