# Multi-Agent Example

This example demonstrates how to build a multi-agent system where a main
orchestrator agent delegates tasks to specialized sub-agents.

## Building Blocks

### Weight Sharing

All agents share the same `ModelWeights` instance, which means:
- The model file is loaded only **once** (the expensive operation)
- Each agent has its own `Model` instance with independent KV cache/context
- Significant VRAM savings when running multiple agents

```cpp
// Load weights once (heavy VRAM usage)
auto weights = agent_cpp::ModelWeights::create("model.gguf");

// Create multiple agents sharing the same weights (lightweight)
MathAgent math_agent(weights);      // Has calculator tool
auto main_agent = create_main_agent(weights, &math_agent);  // Has delegate tool
```

### Delegation via Tools

The main agent delegates tasks to specialized agents through tools:

```cpp
class DelegateMathTool : public agent_cpp::Tool {
    MathAgent* math_agent_;  // Reference to sub-agent

    std::string execute(const json& args) override {
        // Call the specialized agent
        return math_agent_->solve(args["problem"]);
    }
};
```

## Building

> [!IMPORTANT]
> Check the [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md) to find
> Cmake flags you might want to pass depending on your available hardware.

```bash
cd examples/multi-agent

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
./build/multi-agent-example -m "path-to-model.gguf"
```

## Example

```console
$ ./build/multi-agent-example -m ../../granite-4.0-micro-Q8_0.gguf

> If I have 156 apples and give away 47, how many remain?'

<tool_call>
{"name": "delegate_to_math_expert", "arguments": {"problem": "156 - 47"}}
</tool_call>
[TOOL EXECUTION] Calling delegate_to_math_expert

[DELEGATION] Delegating to Math Agent: 156 - 47
[DELEGATION] Math Agent response: The result of 156 - 47 is 109.
[TOOL RESULT]
{"solution":"The result of 156 - 47 is 109."}
After giving away 47 apples from 156, you have **109 apples remaining**.

>

👋 Goodbye!
```
