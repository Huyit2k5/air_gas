"""Thử MCP server như một client thật: khởi động airgas_mcp.py qua stdio, liệt kê công cụ
và gọi các công cụ chỉ đọc. Không gọi control_device.

    python test_mcp.py
"""
import asyncio
import json
import os
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

SERVER = Path(__file__).with_name("airgas_mcp.py")


async def main() -> None:
    params = StdioServerParameters(command=sys.executable, args=[str(SERVER)], env=dict(os.environ))
    async with stdio_client(params) as (read, write), ClientSession(read, write) as session:
        await session.initialize()
        tools = (await session.list_tools()).tools
        print("Công cụ:", ", ".join(t.name for t in tools))
        for name, args in [("get_status", {}), ("get_alarm_events", {"hours": 24}), ("get_history", {"metric": "gas", "hours": 6})]:
            res = await session.call_tool(name, args)
            text = "\n".join(c.text for c in res.content if c.type == "text")
            print(f"\n=== {name} {json.dumps(args)}{' (LỖI)' if res.is_error else ''}\n{text}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    asyncio.run(main())
