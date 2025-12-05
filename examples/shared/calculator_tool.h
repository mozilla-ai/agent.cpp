#pragma once

#include "tool.h"

// A simple calculator tool for basic mathematical operations.
// Shared across examples to avoid code duplication.
class CalculatorTool : public Tool
{
  public:
    CalculatorTool() = default;

    common_chat_tool get_definition() const override
    {
        json schema = {
            { "type", "object" },
            { "properties",
              { { "operation",
                  { { "type", "string" },
                    { "enum", { "add", "subtract", "multiply", "divide" } },
                    { "description",
                      "The mathematical operation to perform" } } },
                { "a",
                  { { "type", "number" },
                    { "description", "First operand" } } },
                { "b",
                  { { "type", "number" },
                    { "description", "Second operand" } } } } },
            { "required", { "operation", "a", "b" } }
        };

        return { "calculator",
                 "Perform basic mathematical operations",
                 schema.dump() };
    }

    std::string get_name() const override { return "calculator"; }

    std::string execute(const json& arguments) override
    {
        std::string op = arguments.at("operation").get<std::string>();
        double a = arguments.at("a").get<double>();
        double b = arguments.at("b").get<double>();

        json response;
        double result = 0;

        if (op == "add") {
            result = a + b;
        } else if (op == "subtract") {
            result = a - b;
        } else if (op == "multiply") {
            result = a * b;
        } else if (op == "divide") {
            if (b == 0) {
                response["error"] = "Division by zero";
                return response.dump();
            }
            result = a / b;
        } else {
            response["error"] = "Unknown operation: " + op;
            return response.dump();
        }

        response["result"] = result;
        return response.dump();
    }
};
