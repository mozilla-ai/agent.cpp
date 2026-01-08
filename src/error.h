#pragma once

#include <stdexcept>
#include <string>

namespace agent_cpp {

/// @brief Base exception class for all agent.cpp errors
/// All agent.cpp exceptions derive from this, allowing users to catch
/// all library errors with a single catch block if desired.
class Error : public std::runtime_error
{
  public:
    explicit Error(const std::string& message)
      : std::runtime_error(message)
    {
    }
};

/// @brief Error during model initialization or inference
/// Thrown when model loading fails, context creation fails, or generation
/// encounters an unrecoverable error.
class ModelError : public Error
{
  public:
    explicit ModelError(const std::string& message)
      : Error("Model error: " + message)
    {
    }
};

/// @brief Error during tool execution
/// Thrown when a tool encounters an error during execution.
/// The tool_name() method provides context about which tool failed.
class ToolError : public Error
{
  private:
    std::string tool_name_;

  public:
    ToolError(const std::string& tool_name, const std::string& message)
      : Error("Tool '" + tool_name + "' error: " + message)
      , tool_name_(tool_name)
    {
    }

    /// @brief Get the name of the tool that failed
    [[nodiscard]] const std::string& tool_name() const noexcept
    {
        return tool_name_;
    }
};

/// @brief Tool was not found in the agent's tool registry
class ToolNotFoundError : public ToolError
{
  public:
    explicit ToolNotFoundError(const std::string& tool_name)
      : ToolError(tool_name, "tool not found")
    {
    }
};

/// @brief Error parsing tool arguments
class ToolArgumentError : public ToolError
{
  public:
    ToolArgumentError(const std::string& tool_name, const std::string& message)
      : ToolError(tool_name, "invalid arguments - " + message)
    {
    }
};

/// @brief Error during MCP client operations
/// Thrown when MCP connection, initialization, or tool calls fail.
class MCPError : public Error
{
  public:
    explicit MCPError(const std::string& message)
      : Error("MCP error: " + message)
    {
    }
};

/// @brief Exception to intentionally skip tool execution
/// This is not an error condition - it's a control flow mechanism.
/// Throw from before_tool_execution callback to skip a tool.
/// The message will be returned to the model as the tool result.
class ToolExecutionSkipped : public std::exception
{
  private:
    std::string message_;

  public:
    explicit ToolExecutionSkipped(
      const std::string& msg = "Tool execution skipped")
      : message_(msg)
    {
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return message_.c_str();
    }

    [[nodiscard]] const std::string& get_message() const noexcept
    {
        return message_;
    }
};

} // namespace agent_cpp
