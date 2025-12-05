#pragma once

#include "callbacks.h"
#include <cstdio>
#include <string>
#include <unistd.h>

// Logging callback to display tool execution information.
// Shared across examples to provide consistent tool call logging.
class LoggingCallback : public Callback
{
  public:
    void before_tool_execution(std::string& tool_name,
                               std::string& /*arguments*/) override
    {
        if (isatty(fileno(stderr))) {
            fprintf(stderr,
                    "\n\033[34m[TOOL EXECUTION] Calling %s\033[0m\n",
                    tool_name.c_str());
        } else {
            fprintf(
              stderr, "\n[TOOL EXECUTION] Calling %s\n", tool_name.c_str());
        }
    }

    void after_tool_execution(std::string& /*tool_name*/,
                              std::string& result) override
    {
        if (isatty(fileno(stderr))) {
            fprintf(
              stderr, "\033[34m[TOOL RESULT]\033[0m\n%s\n", result.c_str());
        } else {
            fprintf(stderr, "[TOOL RESULT]\n%s\n", result.c_str());
        }
    }
};
