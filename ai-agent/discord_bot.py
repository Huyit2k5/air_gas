"""Bot Discord hỏi đáp về trạm gas, dùng GasAgent (agent.py).

Bot trả lời khi được nhắc tên (@bot), khi nhắn riêng, hoặc mọi tin trong kênh DISCORD_CHANNEL_ID.
Lệnh điều khiển thiết bị hiện nút Xác nhận / Huỷ; chỉ người hỏi mới bấm được.

    python discord_bot.py
"""
from __future__ import annotations

import asyncio
import logging
import os
import sys

import discord
import openai

from agent import ACTION_TEXT, GasAgent

log = logging.getLogger("airgas.bot")
HISTORY_TURNS = 6  # số lượt hỏi đáp gần nhất giữ lại cho mỗi kênh
CONFIRM_TIMEOUT_S = 60


class ConfirmView(discord.ui.View):
    """Hai nút Xác nhận / Huỷ cho một lệnh điều khiển, chỉ người hỏi được bấm."""

    def __init__(self, requester: discord.abc.User, action_text: str) -> None:
        super().__init__(timeout=CONFIRM_TIMEOUT_S)
        self.requester = requester
        self.action_text = action_text
        self.approved = False
        self.message: discord.Message | None = None

    async def interaction_check(self, interaction: discord.Interaction) -> bool:
        if interaction.user.id != self.requester.id:
            await interaction.response.send_message("Chỉ người ra lệnh mới xác nhận được.", ephemeral=True)
            return False
        return True

    async def _finish(self, interaction: discord.Interaction, approved: bool) -> None:
        self.approved = approved
        verdict = f"✅ {interaction.user.display_name} đã xác nhận" if approved else f"❌ {interaction.user.display_name} đã huỷ"
        await interaction.response.edit_message(content=f"{verdict}: {self.action_text}.", view=None)
        self.stop()

    @discord.ui.button(label="Xác nhận", style=discord.ButtonStyle.danger)
    async def yes(self, interaction: discord.Interaction, _: discord.ui.Button) -> None:
        await self._finish(interaction, True)

    @discord.ui.button(label="Huỷ", style=discord.ButtonStyle.secondary)
    async def no(self, interaction: discord.Interaction, _: discord.ui.Button) -> None:
        await self._finish(interaction, False)

    async def on_timeout(self) -> None:
        if self.message:
            await self.message.edit(content=f"⌛ Hết {CONFIRM_TIMEOUT_S} giây, không {self.action_text}.", view=None)


class GasBot(discord.Client):
    def __init__(self, channel_id: int) -> None:
        intents = discord.Intents.default()
        intents.message_content = True  # cần bật "Message Content Intent" trong Developer Portal
        super().__init__(intents=intents)
        self.channel_id = channel_id
        self.agent = GasAgent()
        self.histories: dict[int, list[dict]] = {}
        self.locks: dict[int, asyncio.Lock] = {}

    async def setup_hook(self) -> None:
        await self.agent.start()

    async def close(self) -> None:
        await self.agent.close()
        await super().close()

    async def on_ready(self) -> None:
        log.info("Đã đăng nhập %s, model %s, công cụ: %s", self.user, self.agent.model,
                 ", ".join(t["function"]["name"] for t in self.agent.tools))

    def _addressed(self, message: discord.Message) -> bool:
        return (
            isinstance(message.channel, discord.DMChannel)
            or (self.user is not None and self.user in message.mentions)
            or (self.channel_id != 0 and message.channel.id == self.channel_id)
        )

    async def on_message(self, message: discord.Message) -> None:
        if message.author.bot or not self._addressed(message):
            return
        question = message.content
        if self.user is not None:
            question = question.replace(f"<@{self.user.id}>", "").replace(f"<@!{self.user.id}>", "")
        question = question.strip()
        if not question:
            return

        async def confirm(action: str) -> bool:
            text = ACTION_TEXT.get(action, action)
            view = ConfirmView(message.author, text)
            view.message = await message.reply(
                f"⚠️ Bot muốn **{text}**. {message.author.mention} bấm **Xác nhận** trong {CONFIRM_TIMEOUT_S} giây.", view=view
            )
            await view.wait()
            return view.approved

        key = message.channel.id
        async with self.locks.setdefault(key, asyncio.Lock()):
            history = self.histories.setdefault(key, [])
            try:
                async with message.channel.typing():
                    answer = await self.agent.ask(history, question, confirm)
            except openai.AuthenticationError:
                answer = "Nhà cung cấp LLM từ chối API key, kiểm tra OPENAI_API_KEY."
            except openai.RateLimitError:
                answer = "LLM đang giới hạn tần suất hoặc hết hạn mức, thử lại sau ít phút."
            except openai.APIConnectionError:
                answer = "Không kết nối được tới LLM, kiểm tra OPENAI_BASE_URL và mạng."
            except openai.APIStatusError as e:
                log.exception("LLM error")
                answer = f"LLM trả lỗi {e.status_code}. Model có hỗ trợ gọi công cụ (tool calling) không?"
            else:
                history += [{"role": "user", "content": question}, {"role": "assistant", "content": answer}]
                del history[: -HISTORY_TURNS * 2]
        for i in range(0, len(answer), 1900):  # Discord giới hạn 2000 ký tự mỗi tin
            await message.reply(answer[i : i + 1900], mention_author=False)


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8")
    token = os.environ.get("DISCORD_BOT_TOKEN")
    if not token:
        raise SystemExit("Thiếu DISCORD_BOT_TOKEN trong ai-agent/.env")
    channel = os.environ.get("DISCORD_CHANNEL_ID", "").strip()
    bot = GasBot(int(channel) if channel.isdigit() else 0)
    for noisy in ("httpx", "httpx2", "mcp.client.streamable_http"):  # bỏ log mỗi request tới MCP/LLM
        logging.getLogger(noisy).setLevel(logging.WARNING)
    bot.run(token, log_level=logging.INFO, root_logger=True)


if __name__ == "__main__":
    main()
