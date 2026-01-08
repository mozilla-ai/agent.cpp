# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "mcp[cli]",
# ]
# ///
from mcp.server.fastmcp import FastMCP
from typing import Literal

mcp = FastMCP(
    "agent.cpp Example Server",
    host="0.0.0.0",
    port=8000,
)


@mcp.tool()
def calculator(
    operation: Literal["add", "subtract", "multiply", "divide"],
    a: float,
    b: float,
) -> dict:
    """Perform basic mathematical operations.

    Args:
        operation: The mathematical operation to perform (add, subtract, multiply, divide)
        a: First operand
        b: Second operand
    """
    if operation == "add":
        result = a + b
    elif operation == "subtract":
        result = a - b
    elif operation == "multiply":
        result = a * b
    elif operation == "divide":
        if b == 0:
            return {"error": "Division by zero"}
        result = a / b
    else:
        return {"error": f"Unknown operation: {operation}"}

    return {"result": result}


if __name__ == "__main__":
    print("Starting MCP server on http://localhost:8000/mcp")
    mcp.run(transport="streamable-http")
