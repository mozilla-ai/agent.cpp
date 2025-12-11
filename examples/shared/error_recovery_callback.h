#pragma once

#include "callbacks.h"
#include "tool_result.h"
#include <nlohmann/json.hpp>
#include <string>

// Error recovery callback that converts tool errors to JSON results.
// This allows the agent to see the error and potentially retry or adjust.
// Use this when you want resilient agents that don't crash on tool failures.
class ErrorRecoveryCallback : public agent_cpp::Callback
{
  public:
    void after_tool_execution(std::string& tool_name,
                              agent_cpp::ToolResult& result) override
    {
        if (result.has_error()) {
            nlohmann::json err;
            err["error"] = true;
            err["tool"] = tool_name;
            err["message"] = result.error().message;
            result.recover(err.dump()); // Explicitly recover from error
        }
    }
};
