# Grammar Example

This example shows how to constrain model output with a [GBNF grammar](https://github.com/ggml-org/llama.cpp/blob/master/grammars/README.md). Instead of relying on prompt instructions alone, the grammar is enforced at the sampler level, so every response matches it.

The demo is a minimal sentiment classifier: whatever the user types, the model replies with a single well-formed JSON object shaped like `{"sentiment": "positive" | "negative" | "neutral"}`.

## Building Blocks

### Grammar

[`sentiment.gbnf`](./sentiment.gbnf) defines the grammar used in this example:

```gbnf
root   ::= "{" ws "\"sentiment\":" ws sentiment ws "}"
sentiment ::= "\"positive\"" | "\"negative\"" | "\"neutral\""
ws     ::= [ \t\n]*
```

Load it with `agent_cpp::load_grammar_file` and set it on `ModelConfig::grammar`:

```cpp
auto model_config = agent_cpp::ModelConfig{};
model_config.grammar = agent_cpp::load_grammar_file("sentiment.gbnf");
model_config.grammar_root = "root"; // optional, this is the default
```

No tools are used in this example. The grammar alone is enough to constrain every response, and it would also block any tool call the agent tried to make: the grammar applies to every token the model generates, so a tool-call format that the grammar does not allow can never be produced.

## Building

> [!IMPORTANT]
> Check the [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md) to find
> CMake flags you might want to pass depending on your available hardware.

```bash
cd examples/grammar

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

## Usage

```bash
./build/grammar-example -m "path-to-model.gguf"

# Use a different grammar file
./build/grammar-example -m "path-to-model.gguf" -g "path-to-grammar.gbnf"

# Use a different grammar root rule
./build/grammar-example -m "path-to-model.gguf" -g "path-to-grammar.gbnf" -r "start"
```

## Example

```console
$ ./build/grammar-example -m ../../granite-4.0-micro-Q8_0.gguf
> This library is exactly what I needed, thank you!
{"sentiment": "positive"}

> The build failed three times before I figured out the flag I was missing.
{"sentiment": "negative"}

> The package arrived on Tuesday.
{"sentiment": "neutral"}
>
```
