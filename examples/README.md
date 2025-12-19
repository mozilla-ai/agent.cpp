# Examples

This directory contains example applications demonstrating agent.cpp capabilities.

## Shared Utilities

The [shared](./shared) directory contains reusable helper components used across multiple examples. These are **not part of the public API** but can be useful as reference implementations.

| File | Description |
|------|-------------|
| `calculator_tool.h` | A simple calculator tool for basic math operations (add, subtract, multiply, divide). Demonstrates how to implement a `Tool` with JSON Schema parameters. |
| `chat_loop.h` | Interactive chat loop that reads user input from stdin and prints agent responses. Handles colored output for TTY terminals. |
| `error_recovery_callback.h` | Callback that converts tool errors into JSON results, allowing the agent to see errors and retry gracefully instead of crashing. |
| `logging_callback.h` | Callback that logs tool calls and their results to stderr. Useful for debugging and understanding agent behavior. |
