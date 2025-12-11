# agent.cpp

Building blocks for **local** agents in C++.

> **Note:** This library is designed for running small language models locally using [llama.cpp](https://github.com/ggml-org/llama.cpp). It does not support cloud-based APIs (OpenAI, Anthropic, etc.). If you need to call external LLM APIs, this library is not the right fit.

## Building Blocks

We define an `agent` with the following building blocks:

- [Agent Loop](./#agent-loop)
- [Callbacks](./#callbacks)
- [Instructions](./#instructions)
- [Model](./#model)
- [Tools](./#tools)

Minimal example:

```cpp
#include "agent.h"
#include "callbacks.h"
#include "model.h"
#include "tool.h"
#include <iostream>

class CalculatorTool : public agent_cpp::Tool {
  public:
    common_chat_tool get_definition() const override {
        agent_cpp::json schema = {
            {"type", "object"},
            {"properties", {
                {"a", {{"type", "number"}, {"description", "First operand"}}},
                {"b", {{"type", "number"}, {"description", "Second operand"}}}
            }},
            {"required", {"a", "b"}}
        };
        return {"multiply", "Multiply two numbers", schema.dump()};
    }

    std::string get_name() const override { return "multiply"; }

    std::string execute(const agent_cpp::json& args) override {
        double a = args.at("a").get<double>();
        double b = args.at("b").get<double>();
        return std::to_string(a * b);
    }
};

class LoggingCallback : public agent_cpp::Callback {
  public:
    void before_tool_execution(std::string& tool_name, std::string& args) override {
        std::cerr << "[TOOL] Calling " << tool_name << " with " << args << "\n";
    }
};

int main() {
    auto model = agent_cpp::Model::create("model.gguf");

    std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
    tools.push_back(std::make_unique<CalculatorTool>());

    std::vector<std::unique_ptr<agent_cpp::Callback>> callbacks;
    callbacks.push_back(std::make_unique<LoggingCallback>());

    agent_cpp::Agent agent(
        std::move(model),
        std::move(tools),
        std::move(callbacks),
        "You are a helpful assistant."
    );

    std::vector<common_chat_msg> messages = {{"user", "What is 42 * 17?"}};
    std::string response = agent.run_loop(messages);
    std::cout << response << std::endl;
}
```

## Agent Loop

In the current LLM (Large Language Models) world, and `agent` is usually a simple loop that intersperses `Model Calls` and `Tool Executions`, until a stop condition is met:

```mermaid
graph TD
    User([User Input]) --> Model[Model Call]
    Model --> Decision{Stop Condition Met?}
    Decision -->|Yes| End([End])
    Decision -->|No| Tool[Tool Execution]
    Tool --> Model
```

There are different ways to implement the stop condition.
By default we let the agent decide by generating an output *without* tool executions.
You can implement additional stop conditions via callbacks.

### Callbacks

Callbacks allow you to hook into the agent lifecycle at specific points:

- `before_agent_loop` / `after_agent_loop` - Run logic at the start/end of the agent loop
- `before_llm_call` / `after_llm_call` - Intercept or modify messages before/after model inference
- `before_tool_execution` / `after_tool_execution` - Validate, skip, or handle tool calls and their results

Use callbacks for logging, context manipulation, human-in-the-loop approval, or error recovery.

### Instructions

A system prompt that defines the agent's behavior and capabilities. Passed to the `Agent` constructor and automatically prepended to conversations.

### Model

Encapsulates **local** LLM initialization and inference using [llama.cpp](https://github.com/ggml-org/llama.cpp). This is tightly coupled to llama.cpp and requires models in GGUF format.

> **Architectural note:** The `Model` class is not backend-agnostic. It is built specifically for local inference with llama.cpp. There is no abstraction layer for swapping in cloud-based providers like OpenAI or Anthropic.

Handles:

- Loading GGUF model files (quantized models recommended for efficiency)
- Chat template application and tokenization
- Text generation with configurable sampling (temperature, top_p, top_k, etc.)
- KV cache management for efficient prompt caching

### Tools

Tools extend the agent's capabilities beyond text generation. Each tool defines:

- **Name and description** - Helps the model understand when to use it
- **Parameters schema** - JSON Schema defining expected arguments
- **Execute function** - The actual implementation

When the model decides to use a tool, the agent parses the tool call, executes it, and feeds the result back into the conversation.

## Examples

See [examples/README.md](./examples/README.md) for complete examples demonstrating callbacks, tools, human-in-the-loop interactions, and tracing.

## Usage

### Option 1: FetchContent (Recommended)

The easiest way to integrate agent.cpp into your CMake project:

```cmake
include(FetchContent)
FetchContent_Declare(
    agent-cpp
    GIT_REPOSITORY https://github.com/mozilla-ai/agent.cpp
    GIT_TAG main  # or a specific release tag like v0.1.0
)
FetchContent_MakeAvailable(agent-cpp)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE agent-cpp::agent)
```

### Option 2: Installed Package

Build and install agent.cpp, then use `find_package`:

```bash
# Clone and build
git clone --recursive https://github.com/mozilla-ai/agent.cpp
cd agent.cpp
cmake -B build -DAGENT_CPP_INSTALL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install (use --prefix for custom location)
cmake --install build --prefix ~/.local/agent-cpp
```

Then in your project:

```cmake
# If installed to a custom prefix, tell CMake where to find it
list(APPEND CMAKE_PREFIX_PATH "~/.local/agent-cpp")

find_package(agent-cpp REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE agent-cpp::agent)
```

### Option 3: Git Submodule

Add agent.cpp as a submodule and include it directly:

```bash
git submodule add https://github.com/mozilla-ai/agent.cpp agent.cpp
git submodule update --init --recursive
```

```cmake
add_subdirectory(agent.cpp)
target_link_libraries(my_app PRIVATE agent-cpp::agent)
```

### Hardware Acceleration

This project uses [llama.cpp](https://github.com/ggml-org/llama.cpp) as a submodule. You can enable hardware-specific acceleration by passing the appropriate CMake flags when building. For example:

```bash
# CUDA (NVIDIA GPUs)
cmake -B build -DGGML_CUDA=ON

# OpenBLAS (CPU)
cmake -B build -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=OpenBLAS
```

For a complete list of build options and backend-specific instructions, see the [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md).

### Technical Details

**C++ Standard:** Requires **C++17** or higher.

**Thread Safety:** The `Agent` and `Model` classes are **not thread-safe**. A single `Agent` instance should not be accessed concurrently from multiple threads. If you need concurrent agents, create separate instances per thread.

**Exceptions:** All exceptions derive from `agent_cpp::Error` (which extends `std::runtime_error`), allowing you to catch all library errors with a single `catch (const agent_cpp::Error&)` block.

To handle tool errors gracefully without exceptions propagating, check the [`error_recovery_callback](./examples/shared) which converts tool errors into JSON results that the model can see and retry.
