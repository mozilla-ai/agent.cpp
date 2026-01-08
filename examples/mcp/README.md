# MCP Client Example

[MCP (Model Context Protocol)](https://modelcontextprotocol.io/) is an open protocol that allows AI applications to connect to external tools and data sources.

This example demonstrates how to connect to an MCP server via HTTP and use its tools with an agent.cpp agent.

## Building Blocks

### Tools

Tools are dynamically discovered from the MCP server at runtime. The client connects to the server, performs a handshake, and retrieves the available tool definitions.

### Callbacks

This example uses two shared callbacks from `examples/shared/`:

- **LoggingCallback**: Displays tool execution information with colored output showing which tools are called and their results.

- **ErrorRecoveryCallback**: Converts tool execution errors into JSON results, allowing the agent to see errors and potentially retry or adjust.

## Building

> [!IMPORTANT]
> Check the [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md) to find
> Cmake flags you might want to pass depending on your available hardware.

```bash
cd examples/mcp

git -C ../.. submodule update --init --recursive

# MCP requires OpenSSL for HTTPS support
cmake -B build -DAGENT_CPP_BUILD_MCP=ON
cmake --build build -j$(nproc)
```

### Using a custom llama.cpp

If you have llama.cpp already downloaded:

```bash
cmake -B build -DLLAMA_CPP_DIR=/path/to/your/llama.cpp -DAGENT_CPP_BUILD_MCP=ON
cmake --build build -j$(nproc)
```

## Usage

```bash
./build/mcp-example -m <path-to-model.gguf> -u <mcp-server-url>
```

Options:
- `-m <path>` - Path to the GGUF model file (required)
- `-u <url>` - MCP server URL (Streamable HTTP transport) (required)

## Example

This example includes a simple MCP server (`server.py`) with a `calculator` tool that performs basic math operations (similar to the calculator in `examples/shared`).

### 1. Start the MCP Server

The server uses [uv](https://docs.astral.sh/uv/) inline script metadata, so no installation is needed:

```bash
uv run server.py
```

This starts the MCP server on `http://localhost:8000/mcp`.

### 2. Run the Agent

```bash
./build/mcp-example -m ../../granite-4.0-micro-Q8_0.gguf -u "http://localhost:8000/mcp"
```

### 3. Example Conversation

```console
$ ./build/mcp-example -m ../../granite-4.0-micro-Q8_0.gguf -u "http://localhost:8000/mcp"
Connecting to MCP server: http://localhost:8000/mcp
Initializing MCP session...
MCP session initialized.

Available tools (1):
  - calculator: Perform basic mathematical operations.

Loading model...
Model loaded successfully

MCP Agent ready!
   Connected to: http://localhost:8000/mcp
   Type an empty line to quit.

> What is 42 multiplied by 17?

<tool_call>
{"name": "calculator", "arguments": "{\n  \"operation\": \"multiply\",\n  \"a\": 42,\n  \"b\": 17\n}"}
</tool_call>

[TOOL EXECUTION] Calling calculator
[TOOL RESULT]
{"result": 714}

42 multiplied by 17 equals **714**.
```
