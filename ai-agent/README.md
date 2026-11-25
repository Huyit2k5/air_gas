# AI agent cho trạm cảnh báo gas

MCP server `airgas` cho phép AI đọc số liệu và điều khiển trạm gas qua Home Assistant.
Bot Discord hỏi đáp bằng tiếng Việt, dùng LLM qua API tương thích OpenAI và các công cụ của server này.

```
ESP32 ──MQTT──▶ Mosquitto ──▶ Node-RED ──MQTT discovery──▶ Home Assistant
                                                              ▲ REST API
                                              airgas_mcp.py ──┘ (MCP, stdio)
                                                    ▲
                     agent.py (LLM tương thích OpenAI + tool calling)
                                                    ▲
                                            discord_bot.py
```

| File | Vai trò |
|---|---|
| `airgas_mcp.py` | MCP server, gọi REST API của Home Assistant |
| `agent.py` | Vòng lặp agent: LLM chọn công cụ, gọi MCP, trả lời. `python agent.py` để chat thử trong terminal |
| `discord_bot.py` | Bot Discord dùng `agent.py`, hỏi xác nhận bằng nút bấm trước khi điều khiển |
| `test_mcp.py`, `call_tool.py` | Thử MCP server mà không cần LLM |

## Công cụ MCP

| Công cụ | Loại | Tác dụng |
|---|---|---|
| `get_status` | chỉ đọc | Số liệu hiện tại: gas, CO2, mức cảnh báo, van, quạt, còi, firmware, Wi-Fi |
| `get_history(metric, hours)` | chỉ đọc | Lịch sử 1–168 giờ. Số liệu dạng số: min/max/trung bình và 24 điểm. Trạng thái: các lần thay đổi |
| `get_alarm_events(hours)` | chỉ đọc | Các lần rò gas và không khí kém, giờ bắt đầu, giờ kết thúc, số phút |
| `control_device(action)` | điều khiển | `open_valve`, `close_valve`, `test_fan`, `test_buzzer`, `recalibrate` (bấm các nút tương ứng trong HA) |

- `open_valve` bị từ chối khi đang báo động rò gas. Firmware cũng từ chối lần nữa.
- `control_device` được đánh dấu `destructive_hint`, nên client MCP cần hỏi người dùng trước khi gọi.
- Server tìm thực thể theo **tên hiển thị** do Node-RED đặt (ví dụ "Nồng độ gas"), không theo `entity_id`. Vì vậy đổi tên `entity_id` trong HA không làm hỏng server.

## Cài đặt

1. Tạo token Home Assistant: mở http://localhost:8123, bấm vào tên mình (góc dưới bên trái), chọn **Security**, rồi **Long-lived access tokens** > **Create token**.
2. Cài thư viện và điền token:
   ```powershell
   cd ai-agent
   python -m venv .venv
   .venv\Scripts\pip install -r requirements.txt
   copy .env.example .env      # rồi dán token vào HA_TOKEN
   ```
3. Chạy thử. Script này khởi động server, liệt kê công cụ và gọi 3 công cụ chỉ đọc, không điều khiển gì:
   ```powershell
   .venv\Scripts\python test_mcp.py
   ```

Cần các container Mosquitto, Node-RED và Home Assistant đang chạy (`docker compose up -d` trong `node-red/`).

Gọi riêng một công cụ: `.venv\Scripts\python call_tool.py control_device action=test_buzzer`.

## Bot Discord

1. **LLM:** điền `OPENAI_BASE_URL`, `OPENAI_API_KEY`, `LLM_MODEL` trong `.env` (ví dụ có sẵn trong `.env.example`). Model phải hỗ trợ **tool calling**. Gemini, Groq, OpenRouter, OpenAI và Ollama đều dùng được.
2. **Chat thử trong terminal** trước khi lên Discord:
   ```powershell
   .venv\Scripts\python agent.py
   ```
   Hỏi thử: "gas bây giờ bao nhiêu?", "hôm nay có rò gas không?", "cho còi kêu thử". Lệnh điều khiển hỏi `y/N` trước khi chạy.
3. **Tạo bot:** vào https://discord.com/developers/applications và chọn **New Application**. Vào mục **Bot**, bấm **Reset Token** rồi chép token vào `DISCORD_BOT_TOKEN`, và **bật "Message Content Intent"**.
4. **Mời bot vào server:** vào **OAuth2 > URL Generator**, chọn scope `bot`, quyền **Send Messages** và **Read Message History**, rồi mở link được tạo.
5. **Chạy bot:**
   ```powershell
   .venv\Scripts\python discord_bot.py
   ```

Bot trả lời khi được `@nhắc tên`, khi nhắn riêng, hoặc mọi tin trong kênh `DISCORD_CHANNEL_ID` (nếu có đặt). Mỗi kênh nhớ 6 lượt hỏi đáp gần nhất. Lệnh điều khiển (mở/khóa van, thử quạt, thử còi, hiệu chuẩn) hiện nút **Xác nhận / Huỷ**. Chỉ người ra lệnh mới bấm được, và sau 60 giây không bấm thì lệnh bị bỏ.

## Chạy bằng Docker (cùng stack với Mosquitto, Node-RED, HA)

Trong `node-red/docker-compose.yml` có 2 service riêng, đều đọc biến từ `ai-agent/.env`:

| Service | Image | Việc |
|---|---|---|
| `airgas-mcp` | `Dockerfile.mcp` | MCP server qua streamable HTTP, gọi HA ở `http://homeassistant:8123`. Mở cổng `127.0.0.1:8001` trên máy |
| `airgas-bot` | `Dockerfile.bot` | Bot Discord, nối MCP qua `http://airgas-mcp:8000/mcp` |

```powershell
cd node-red
docker compose up -d --build airgas-mcp airgas-bot
docker logs -f airgas-bot
```

- MCP qua HTTP **bắt buộc** `MCP_AUTH_TOKEN` (header `Authorization: Bearer <token>`), vì server mở/khóa được van gas.
- Sửa code xong thì chạy lại `docker compose up -d --build` để build lại image.
- Dùng Ollama trên máy thì trong container phải đặt `OPENAI_BASE_URL=http://host.docker.internal:11434/v1` (không dùng `localhost`).

## Mở MCP qua Tailscale (tuỳ chọn, mặc định tắt)

Chỉ cần khi một máy khác muốn gọi MCP server, ví dụ laptop của bạn cùng nhóm dùng Claude Desktop hoặc Cursor. Bot trong Docker không cần bước này.

```powershell
tailscale serve --bg --https=9443 http://127.0.0.1:8001
```

- Địa chỉ: `https://laptop-mbdl3hhk.tail41c4c8.ts.net:9443/mcp`, kèm header `Authorization: Bearer <MCP_AUTH_TOKEN>`.
- Chỉ máy trong tailnet truy cập được (**không** bật Funnel cho cổng này). Cổng 443 của máy đang dùng Funnel cho dự án khác, đừng gắn MCP vào đó.
- Tắt: `tailscale serve --https=9443 off`.
- Một máy khác trong tailnet chạy bot hoặc agent thì đặt `MCP_URL` bằng địa chỉ trên và `MCP_AUTH_TOKEN`.

## Dùng với client MCP khác

Qua HTTP (Docker hoặc Tailscale), client nào hỗ trợ streamable HTTP và header tuỳ chỉnh đều dùng được:

```json
{
  "mcpServers": {
    "airgas": {
      "type": "http",
      "url": "https://laptop-mbdl3hhk.tail41c4c8.ts.net:9443/mcp",
      "headers": { "Authorization": "Bearer <MCP_AUTH_TOKEN>" }
    }
  }
}
```

Hoặc chạy cục bộ qua stdio:

```json
{
  "mcpServers": {
    "airgas": {
      "command": "D:/Documents/BaiTap/KiCuiCung/IOT/air_gas/ai-agent/.venv/Scripts/python.exe",
      "args": ["D:/Documents/BaiTap/KiCuiCung/IOT/air_gas/ai-agent/airgas_mcp.py"]
    }
  }
}
```

Server tự đọc `HA_URL` và `HA_TOKEN` từ `ai-agent/.env`.
