# Context Engineering Example

This example demonstrates how to use callbacks to modify the input messages (context) before they are sent to the LLM. Specifically, it shows how to limit the number of tool calls preserved in the context to prevent context window overflow during long conversations.

## Building Blocks

### Callbacks

This example uses three callbacks:

- **ContextTrimmerCallback**: Implements `before_llm_call` to modify messages before they are sent to the LLM. It keeps only the N most recent tool call pairs (assistant message with tool_calls + tool responses), trimming older ones to prevent context window overflow during long conversations.

- **LoggingCallback**: Shared callback from `examples/shared/` that logs tool execution information, displaying which tool is being called and its results.

- **ErrorRecoveryCallback**: Shared callback from `examples/shared/` that converts tool execution errors into JSON results, allowing the agent to see errors and potentially retry or adjust.

### Tools

This example uses the shared `CalculatorTool` from `examples/shared/` that performs basic mathematical operations (add, subtract, multiply, divide). This makes it easy to generate multiple tool calls and verify that old ones are being trimmed while the agent still functions correctly.

## Building

```bash
cd examples/context-engineering

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
./build/context-engineering-example -m "path-to-model.gguf"

# Custom limit: keep 3 most recent tool calls
./build/context-engineering-example -m "path-to-model.gguf" -n 3
```

## Example

Start a conversation and ask for multiple calculations.

Notice how older tool calls are trimmed from the context while the agent continues to work correctly:

```console
$ ./build/context-engineering-example -m ../../granite-4.0-micro-Q8_0.gguf -n 2
> Calculate 3 + 4

<tool_call>
{"name": "calculator", "arguments": {"a": 3, "b": 4, "operation": "add"}}
</tool_call>
[TOOL EXECUTION] Calling calculator
[TOOL RESULT]
{"result":7.0}
The result of 3 + 4 is 7.

> Now multiply that by 7
<tool_call>
{"name": "calculator", "arguments": {"a":7.0,"b":7,"operation":"multiply"}}
</tool_call>
[TOOL EXECUTION] Calling calculator
[TOOL RESULT]
{"result":49.0}
[CONTEXT] Trimmed messages:
[CONTEXT] - [tool]: {"result":7.0}
[CONTEXT] - [assistant]: tool_calls: [calculator({"a":3,"b":4,"operation":"add"})]
The result of 7 * 7 is 49.

> Now sum 10 to that
<tool_call>
{"name": "calculator", "arguments": {"a":49.0,"b":10,"operation":"add"}}
</tool_call>
[TOOL EXECUTION] Calling calculator
[TOOL RESULT]
{"result":59.0}
[CONTEXT] Trimmed messages:
[CONTEXT] - [tool]: {"result":49.0}
[CONTEXT] - [assistant]: tool_calls: [calculator({"a":7.0,"b":7,"operation":"multiply...
After summing 10 to 49, the result is 59.
>
```
