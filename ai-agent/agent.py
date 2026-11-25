"""Agent hỏi đáp về trạm gas: LLM qua API tương thích OpenAI + công cụ từ MCP server airgas_mcp.py.

Dùng làm thư viện cho discord_bot.py, hoặc chat thử ngay trong terminal:

    python agent.py
"""
from __future__ import annotations

import asyncio
import json
import os
import sys
from collections.abc import Awaitable, Callable
from contextlib import AsyncExitStack
from datetime import datetime, timedelta, timezone
from pathlib import Path

from dotenv import load_dotenv
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.client.streamable_http import create_mcp_http_client, streamable_http_client
from openai import AsyncOpenAI

HERE = Path(__file__).parent
load_dotenv(HERE / ".env")
VN_TZ = timezone(timedelta(hours=7))

# Công cụ điều khiển thiết bị thật: luôn hỏi người dùng trước khi chạy
CONFIRM_TOOLS = {"control_device"}
ACTION_TEXT = {
    "open_valve": "MỞ van gas",
    "close_valve": "KHÓA van gas",
    "test_fan": "chạy thử quạt hút 10 giây",
    "test_buzzer": "cho còi kêu thử 3 giây",
    "recalibrate": "hiệu chuẩn lại cảm biến",
}

SYSTEM_PROMPT = """Bạn là trợ lý của trạm cảnh báo rò rỉ gas Gas-Monitor-01 (ESP32, cảm biến MQ-2 đo gas,
MQ-135 đo chất lượng không khí, van gas điều khiển bằng servo, quạt hút, còi), do Nhóm 12 làm.

- Trả lời bằng tiếng Việt, ngắn gọn, dễ hiểu.
- Mọi số liệu phải lấy từ công cụ (get_status, get_history, get_alarm_events). Không bịa số.
- Ngưỡng báo động: gas 1000 ppm, CO2 tương đương 1500 ppm. ppm là ước tính từ cảm biến giá rẻ.
- Muốn điều khiển thiết bị thì gọi control_device. Hệ thống sẽ tự hỏi người dùng xác nhận,
  bạn không cần hỏi lại. Nếu người dùng từ chối thì báo là chưa làm gì.
- Nếu đang báo động rò gas: nhắc mở cửa cho thoáng, không bật lửa, không bật/tắt công tắc điện,
  rời khỏi khu vực nếu mùi gas nặng. Không mở van khi còn báo động.
- Khi được hỏi ngoài phạm vi trạm gas thì trả lời ngắn và lái về chủ đề trạm gas."""

Confirm = Callable[[str], Awaitable[bool]]


class GasAgent:
    def __init__(self) -> None:
        missing = [k for k in ("OPENAI_API_KEY", "LLM_MODEL") if not os.environ.get(k)]
        if missing:
            raise SystemExit(f"Thiếu {', '.join(missing)} trong ai-agent/.env")
        self.llm = AsyncOpenAI(base_url=os.environ.get("OPENAI_BASE_URL") or None, api_key=os.environ["OPENAI_API_KEY"])
        self.model = os.environ["LLM_MODEL"]
        self.max_steps = 8
        self.tools: list[dict] = []
        self._stack = AsyncExitStack()
        self._session: ClientSession | None = None

    async def start(self) -> None:
        """Nối MCP server và lấy danh sách công cụ. Có MCP_URL thì nối qua HTTP (Docker, Tailscale),
        không có thì tự chạy airgas_mcp.py làm tiến trình con qua stdio."""
        url = os.environ.get("MCP_URL")
        if url:
            token = os.environ.get("MCP_AUTH_TOKEN", "")
            http = await self._stack.enter_async_context(
                create_mcp_http_client(headers={"Authorization": f"Bearer {token}"} if token else None)
            )
            read, write = await self._stack.enter_async_context(streamable_http_client(url, http_client=http))
        else:
            params = StdioServerParameters(command=sys.executable, args=[str(HERE / "airgas_mcp.py")], env=dict(os.environ))
            read, write = await self._stack.enter_async_context(stdio_client(params))
        self._session = await self._stack.enter_async_context(ClientSession(read, write))
        await self._session.initialize()
        listed = await self._session.list_tools()
        # MCP tool -> định dạng function tool của API OpenAI
        self.tools = [
            {"type": "function", "function": {"name": t.name, "description": t.description or "", "parameters": t.input_schema}}
            for t in listed.tools
        ]

    async def close(self) -> None:
        await self._stack.aclose()

    async def _call_tool(self, name: str, args: dict) -> str:
        assert self._session is not None, "gọi start() trước"
        res = await self._session.call_tool(name, args)
        text = "\n".join(c.text for c in res.content if c.type == "text") or "(không có nội dung)"
        return ("LỖI: " + text) if res.is_error else text

    async def ask(self, history: list[dict], question: str, confirm: Confirm) -> str:
        """Trả lời một câu hỏi. `history` là các lượt trước dạng {"role", "content"};
        `confirm(action)` hỏi người dùng có cho điều khiển thiết bị không."""
        now = datetime.now(VN_TZ).strftime("%H:%M %d/%m/%Y")
        messages: list[dict] = [
            {"role": "system", "content": f"{SYSTEM_PROMPT}\n\nBây giờ là {now} (giờ Việt Nam)."},
            *history,
            {"role": "user", "content": question},
        ]
        for _ in range(self.max_steps):
            resp = await self.llm.chat.completions.create(model=self.model, messages=messages, tools=self.tools)
            msg = resp.choices[0].message
            calls = [tc for tc in (msg.tool_calls or []) if tc.type == "function"]
            if not calls:
                return (msg.content or "").strip() or "(mô hình không trả lời gì)"
            messages.append({"role": "assistant", "content": msg.content or "", "tool_calls": [tc.model_dump(exclude_none=True) for tc in calls]})
            for tc in calls:
                messages.append({"role": "tool", "tool_call_id": tc.id, "content": await self._run_call(tc.function.name, tc.function.arguments, confirm)})
        return "Câu này cần quá nhiều bước, bạn hỏi cụ thể hơn giúp mình nhé."

    async def _run_call(self, name: str, raw_args: str | None, confirm: Confirm) -> str:
        try:
            args = json.loads(raw_args or "{}")
        except json.JSONDecodeError:
            return "LỖI: tham số không phải JSON hợp lệ, hãy gọi lại."
        if name in CONFIRM_TOOLS and not await confirm(str(args.get("action", ""))):
            return "Người dùng KHÔNG xác nhận (từ chối hoặc hết thời gian). Chưa gửi lệnh nào."
        return await self._call_tool(name, args)


async def _cli() -> None:
    """Chat thử trong terminal. Lệnh điều khiển hỏi y/N trước khi chạy."""
    agent = GasAgent()
    await agent.start()
    print(f"Đã nối MCP, công cụ: {', '.join(t['function']['name'] for t in agent.tools)}. Model: {agent.model}")
    print("Gõ câu hỏi (Enter trống để thoát).")

    async def confirm(action: str) -> bool:
        ans = await asyncio.to_thread(input, f"⚠️  Cho phép {ACTION_TEXT.get(action, action)}? (y/N) ")
        return ans.strip().lower() in ("y", "yes", "c", "có", "co")

    history: list[dict] = []
    try:
        while q := (await asyncio.to_thread(input, "\nBạn: ")).strip():
            answer = await agent.ask(history, q, confirm)
            print(f"Bot: {answer}")
            history += [{"role": "user", "content": q}, {"role": "assistant", "content": answer}]
            del history[:-12]
    finally:
        await agent.close()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdin.reconfigure(encoding="utf-8")
    asyncio.run(_cli())
