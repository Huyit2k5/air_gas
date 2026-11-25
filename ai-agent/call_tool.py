"""Gọi một công cụ của MCP server airgas từ dòng lệnh.

    python call_tool.py get_status
    python call_tool.py control_device action=test_fan
"""
import asyncio
import os
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main(name: str, args: dict) -> None:
    params = StdioServerParameters(command=sys.executable, args=[str(Path(__file__).with_name("airgas_mcp.py"))], env=dict(os.environ))
    async with stdio_client(params) as (read, write), ClientSession(read, write) as session:
        await session.initialize()
        res = await session.call_tool(name, args)
        print(("LỖI: " if res.is_error else "") + "\n".join(c.text for c in res.content if c.type == "text"))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    asyncio.run(main(sys.argv[1], dict(a.split("=", 1) for a in sys.argv[2:])))
