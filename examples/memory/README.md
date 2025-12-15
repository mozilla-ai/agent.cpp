# Memory Example

A memory system allows the agent to proactively store relevant information to
be reused across different conversations.

## Building Blocks

### Instructions

We give the agent some additional hints on how to use the tools:

```c++
    const std::string instructions =
      "You are a helpful assistant with memory capabilities. "
      "You can remember information about the user using the write_memory tool"
      " and recall it later using the read_memory tool. "
      "When the user shares personal information (like their name, preferences,or important facts)"
      ", you must use write_memory to store it."
      "When needed, use list_memory to check if you have relevant stored memories."
```

### Tools

This example implements a simple memory system (a single JSON file) with 3 tools:

- **list_memory**: Lists all the keys currently stored.
- **read_memory**: Given a key, reads a previously stored value.
- **write_memory**: Writes information with a key-value pair.

## Building

> [!IMPORTANT]
> Check the [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md) to find
> Cmake flags you might want to pass depending on your available hardware.

```bash
cd examples/memory

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
./build/memory-example -m "path-to-model.gguf"
```

## Example

Start one conversation and provide some personal information.
The agent will use the `write_memory` tool to write to the memory:

```console
$ ./build/memory-example -m ../../granite-4.0-micro-Q8_0.gguf
> My name is David and I love surfing
```

If you close the previous one and start a new conversation,
 the agent can use the `list_memories` and `read_memory` tools to read the previously stored information.

```console
$ ./build/memory-example -m ../../granite-4.0-micro-Q8_0.gguf
> What do you know about me?

```
