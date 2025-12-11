# Examples

This directory contains example applications demonstrating agent.cpp capabilities.

## Available Examples

- **[Context Engineering](./context-engineering/README.md)** - Use callbacks to manipulate the context between iterations of the agent loop.

- **[Memory](./memory/README.md)** - Use tools that allow an agent to store and retrieve relevant information across conversations.

- **[Shell](./shell/README.md)** - Allow an agent to write shell scripts to perform multiple actions at once. Demonstrates human-in-the-loop interactions via callbacks.

- **[Tracing](./tracing/README.md)** - Use callbacks to collect a record of the steps of the agent loop with OpenTelemetry.

## Shared Utilities

The [shared](./shared) directory contains reusable helper components used across multiple examples. These are **not part of the public API** but can be useful as reference implementations.

| File | Description |
|------|-------------|
| `calculator_tool.h` | A simple calculator tool for basic math operations (add, subtract, multiply, divide). Demonstrates how to implement a `Tool` with JSON Schema parameters. |
| `chat_loop.h` | Interactive chat loop that reads user input from stdin and prints agent responses. Handles colored output for TTY terminals. |
| `error_recovery_callback.h` | Callback that converts tool errors into JSON results, allowing the agent to see errors and retry gracefully instead of crashing. |
| `logging_callback.h` | Callback that logs tool calls and their results to stderr. Useful for debugging and understanding agent behavior. |
| `prompt_cache.h` | Utilities for building and caching the agent's system prompt tokens. Speeds up startup by reusing cached KV state. |
