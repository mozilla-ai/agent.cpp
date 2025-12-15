# agent.cpp

Building blocks for **local** agents in C++.

> [!NOTE]
> This library is designed for running small language models locally using [llama.cpp](https://github.com/ggml-org/llama.cpp). If you want to call external LLM APIs, this is not the right fit.

# Examples

- **[Context Engineering](./examples/context-engineering/README.md)** - Use callbacks to manipulate the context between iterations of the agent loop.

- **[Memory](./examples/memory/README.md)** - Use tools that allow an agent to store and retrieve relevant information across conversations.

- **[Multi-Agent](./examples/multi-agent/README.md)** - Build a multi-agent system with weight sharing where a main agent delegates to specialized sub-agents.

- **[Shell](./examples/shell/README.md)** - Allow an agent to write shell scripts to perform multiple actions at once. Demonstrates human-in-the-loop interactions via callbacks.

- **[Tracing](./examples/tracing/README.md)** - Use callbacks to collect a record of the steps of the agent loop with OpenTelemetry.

You need to download a GGUF model in order to run the examples, the default model configuration is set for `granite-4.0-micro`:

```bash
wget https://huggingface.co/ibm-granite/granite-4.0-micro-GGUF/resolve/main/granite-4.0-micro-Q8_0.gguf
```

> [!IMPORTANT]
> If you use a different model, you will probably have to adjust the values in `ModelConfig`.

# Building Blocks

We define an `agent` with the following building blocks:

- [Agent Loop](./#agent-loop)
- [Callbacks](./#callbacks)
- [Instructions](./#instructions)
- [Model](./#model)
- [Tools](./#tools)

## Agent Loop

In the current LLM (Large Language Models) world, and `agent` is usually a simple loop that intersperses `Model Calls` and `Tool Executions`, until a stop condition is met.

> [!IMPORTANT]
> There are different ways to implement stop conditions.
> By default, we let the agent decide when to end the loop, by generating an output *without* tool executions.
> You can implement additional stop conditions via callbacks.

## Callbacks

Callbacks allow you to hook into the agent lifecycle at specific points:

- `before_agent_loop` / `after_agent_loop` - Run logic at the start/end of the agent loop
- `before_llm_call` / `after_llm_call` - Intercept or modify messages before/after model inference
- `before_tool_execution` / `after_tool_execution` - Validate, skip, or handle tool calls and their results

Use callbacks for logging, context manipulation, human-in-the-loop approval, or error recovery.

## Instructions

A system prompt that defines the agent's behavior and capabilities. Passed to the `Agent` constructor and automatically prepended to conversations.

## Model

Encapsulates **local** LLM initialization and inference using [llama.cpp](https://github.com/ggml-org/llama.cpp). This is tightly coupled to llama.cpp and requires models in GGUF format.

Handles:

- Loading GGUF model files (quantized models recommended for efficiency)
- Chat template application and tokenization
- Text generation with configurable sampling (temperature, top_p, top_k, etc.)
- KV cache management for efficient prompt caching

## Tools

Tools extend the agent's capabilities beyond text generation. Each tool defines:

- **Name and description** - Helps the model understand when to use it
- **Parameters schema** - JSON Schema defining expected arguments
- **Execute function** - The actual implementation

When the model decides to use a tool, the agent parses the tool call, executes it, and feeds the result back into the conversation.

# Usage

**C++ Standard:** Requires **C++17** or higher.

## Option 1: FetchContent (Recommended)

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

## Option 2: Installed Package

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

## Option 3: Git Submodule

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
