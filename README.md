# Air Gas Monitor

Trạm giám sát khí gas và chất lượng không khí dùng ESP32 (ESP-IDF v5.5.x). Hai cảm biến MQ đo
nồng độ, firmware tự khóa van gas, bật quạt hút và còi khi vượt ngưỡng, đồng thời gửi dữ liệu
qua MQTT lên Node-RED, Home Assistant và Discord. Có thêm AI agent (MCP + bot Discord) để hỏi
đáp và điều khiển từ xa.

> Mô hình học tập (đồ án IoT, nhóm 12, đề tài 4). Không thay thế thiết bị báo gas đạt chuẩn PCCC.

Tài liệu liên quan: [HUONG_DAN.md](HUONG_DAN.md) (lắp ráp và chạy từng bước),
[ai-agent/README.md](ai-agent/README.md) (MCP server và bot Discord), [CLAUDE.md](CLAUDE.md)
(ghi chú kiến trúc cho trợ lý lập trình).

## Kiến trúc 4 tầng

```
Sensing        MQ-2, MQ-135 → ESP32 → servo van, quạt, còi, LED
Network        Wi-Fi 2.4 GHz → MQTT (Mosquitto)
Middleware     Node-RED (gộp trạng thái, phát hiện sự kiện, chống spam, log CSV, HA discovery)
Application    Dashboard HTML 3D · Node-RED Dashboard · Home Assistant · Discord · AI agent
```

Lệnh điều khiển đi ngược lại: ứng dụng → MQTT `cmd` → ESP32 → cơ cấu chấp hành, ESP32 trả `ack`.
Cảnh báo tại chỗ (LED, còi, quạt, van) chạy ngay trên ESP32, không phụ thuộc Wi-Fi hay Internet.

## Tính năng

- Đọc MQ-2 (LPG ước tính, ppm) và MQ-135 (CO₂ tương đương, ppm) qua ADC1, có tự hiệu chuẩn Ro (lưu NVS).
- Phát hiện cảm biến mất nguồn (điện áp dưới 300 mV): đặt cờ `fault`, firmware vào chế độ an toàn
  (khóa van, quạt 100 %) thay vì coi là không khí sạch.
- Khi rò gas: LED, còi bíp nhanh, quạt 100 % (chạy thêm 60 s sau khi hết), servo khóa van. Van chốt
  ở trạng thái khóa, chỉ mở lại bằng lệnh `open` và bị từ chối khi đang báo động.
- Không khí kém: LED, còi bíp chậm, quạt theo `GAS_FAN_POOR_PERCENT` (mặc định 60 %).
- Gửi cảnh báo Discord trực tiếp từ ESP32 qua webhook, có cooldown (mặc định 60 s).
- Publish MQTT, nhận lệnh từ xa (khóa/mở van, thử quạt, thử còi, giả lập, hiệu chuẩn lại, đổi
  Wi-Fi, broker, webhook). Đổi Wi-Fi mới có chế độ thử 60 s, không kết nối được thì tự quay về mạng cũ.
- Giả lập rò gas hoặc không khí kém bằng lệnh MQTT, để kiểm thử mà không cần xả gas.

## Phần cứng

| Linh kiện | Chân ESP32 | Ghi chú |
|---|---|---|
| MQ-2 (gas), AOUT | `D35` (GPIO35, ADC1 CH7) | qua cầu chia áp 10 kΩ/10 kΩ |
| MQ-135 (không khí), AOUT | `D34` (GPIO34, ADC1 CH6) | qua cầu chia áp 10 kΩ/10 kΩ |
| Còi buzzer, chân + | `D4` (GPIO4) | chân − nối thanh GND |
| Quạt DC 5 V qua module MOSFET D4184 | `D18` (GPIO18) | PWM 500 Hz |
| Servo MG90S (van gas), tín hiệu | `D13` (GPIO13) | mở 0°, đóng 90° |
| LED cảnh báo | `GPIO2` | LED onboard |

- **Nguồn:** adapter 5 V riêng cấp cho 2 cảm biến, quạt và servo (qua jack DC ra thanh nguồn
  breadboard). ESP32 chạy bằng cáp USB, **để trống chân VIN và 3V3**, nối GND ESP32 với thanh GND
  chung. Cấp cảm biến từ chân VIN của ESP32 từng gây brownout lặp.
- **Chia áp:** AOUT của module MQ ra tới 5 V, trong khi ADC ESP32 chỉ chịu 3,3 V. Hai điện trở 10 kΩ/10 kΩ
  chia đôi điện áp nên `GAS_ADC_DIVIDER_PERCENT` và `AQ_ADC_DIVIDER_PERCENT` đặt 50.
- Chân 34 và 35 chỉ có chế độ input, tránh GPIO0, 2, 12, 15 cho tín hiệu khác (chân cấu hình lúc khởi động).
- MQ-x nóng khi chạy và cần làm nóng 24 đến 48 giờ lần đầu để số liệu ổn định.
- Mô hình 3D của bố cục dây nằm trong `dashboard/air_gas_live.html` (bảng “Chân nối” cho biết dây
  đang cắm và chân firmware đọc).

Chân và hệ số chia áp thật nằm trong `sdkconfig` (35/34, 50 %). Giá trị mặc định trong
`main/Kconfig.projbuild` (36/39, 100 %) chỉ là mặc định khi tạo cấu hình mới.

## Cấu trúc thư mục

| Đường dẫn | Vai trò |
|---|---|
| `main/gas_main.c` | Vòng lặp chính: đọc cảm biến, so ngưỡng, điều khiển van, quạt, còi, LED, gửi MQTT và Discord |
| `main/gas_sensor.c`, `air_sensor.c` | MQ-2 (LPG) và MQ-135 (CO₂ tương đương): ADC, Rs/Ro, hiệu chuẩn, cờ `fault` |
| `main/gas_control.c/.h` | Phân tích lệnh JSON trên topic `cmd` (schema ghi trong `gas_control.h`). Chỉ lưu yêu cầu, vòng lặp chính mới thao tác cơ cấu |
| `main/gas_mqtt.c` | Publish `reading`, `alarm`, `status`, `info`, `ack`; nhận `cmd` |
| `main/gas_config.c` | Wi-Fi, broker, webhook lưu NVS, ghi đè giá trị Kconfig; cơ chế thử Wi-Fi mới |
| `main/gas_valve.c`, `gas_fan.c`, `gas_buzzer.c`, `gas_led.c` | Servo van, quạt PWM, còi, LED |
| `main/gas_network.c`, `gas_discord.c` | Wi-Fi STA tự kết nối lại; gửi Discord webhook |
| `node-red/` | Docker Compose (Mosquitto, Node-RED, Home Assistant, airgas-mcp, airgas-bot) và `flows.json` |
| `ai-agent/` | MCP server `airgas`, agent LLM, bot Discord |
| `dashboard/air_gas_live.html` | Trang HTML độc lập: mô hình 3D, giả lập, kết nối MQTT WebSocket |
| `diagram.json`, `wokwi.toml`, `sdkconfig.wokwi` | Mô phỏng Wokwi |

## Build và nạp firmware

Firmware hiện tại: **1.5.0** (`GAS_FW_VERSION` trong `main/gas_control.h`).

Cấu hình (Wi-Fi, webhook, ngưỡng) qua `idf.py menuconfig`, mục **Gas Monitor**. Các tùy chọn chính:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `GAS_WIFI_SSID`, `GAS_WIFI_PASSWORD` | trống | Wi-Fi 2.4 GHz |
| `GAS_DISCORD_WEBHOOK` | trống | Webhook Discord, trống thì tắt cảnh báo Discord |
| `GAS_MQTT_BROKER_URI` | `mqtt://172.16.20.130:1883` | Địa chỉ broker, dùng IP LAN của máy chạy Docker, không dùng `localhost` |
| `GAS_DEVICE_NAME` | `Gas-Monitor-01` | Tên thiết bị, nằm trong tên topic |
| `GAS_THRESHOLD_PPM` | 1000 | Ngưỡng LPG |
| `AQ_THRESHOLD_PPM` | 1500 | Ngưỡng không khí kém (CO₂eq) |
| `GAS_ADC_PIN`, `AQ_ADC_PIN` | 36, 39 (`sdkconfig` đặt 35, 34) | Chân AOUT của MQ-2, MQ-135 |
| `GAS_ADC_DIVIDER_PERCENT`, `AQ_ADC_DIVIDER_PERCENT` | 100 (`sdkconfig` đặt 50) | Phần trăm điện áp còn lại sau chia áp |
| `GAS_SENSOR_VCC_MV`, `GAS_LOAD_RESISTOR_OHM` | 5000, 5000 | Thông số mạch MQ-2 (MQ-135 dùng RL 20000) |
| `GAS_CHECK_PERIOD_MS`, `GAS_ALARM_COOLDOWN_S` | 1000, 60 | Chu kỳ đọc, cooldown giữa hai lần gửi Discord |
| `GAS_LED_GPIO`, `GAS_BUZZER_GPIO`, `GAS_FAN_GPIO`, `GAS_VALVE_GPIO` | 2, 4, 18, 13 | Chân LED, còi, quạt, servo |
| `GAS_FAN_POOR_PERCENT`, `GAS_FAN_RUNON_S` | 60, 60 | Tốc độ quạt khi không khí kém, thời gian chạy thêm sau rò gas |
| `GAS_VALVE_OPEN_DEG`, `GAS_VALVE_CLOSED_DEG` | 0, 90 | Góc servo mở và đóng van |

Build và nạp (PowerShell, ESP-IDF ở `D:\Program\Espressif`, board DOIT DevKit V1 trên COM7):

```powershell
$env:IDF_TOOLS_PATH='D:\Program\Espressif'
cmd /c 'call D:\Program\Espressif\frameworks\esp-idf-v5.5.5\export.bat && idf.py set-target esp32 && idf.py build && idf.py -p COM7 flash monitor'
```

`sdkconfig` cục bộ chứa mật khẩu Wi-Fi và webhook Discord thật. **Xóa hai giá trị này trước khi commit.**
Firmware gần như không in log sau khi khởi động, nên Serial im lặng là bình thường.

### Mô phỏng Wokwi

```
idf.py -B build_wokwi -D SDKCONFIG=build_wokwi/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.wokwi" build
```

Sau đó chạy “Wokwi: Start Simulator” trong VS Code. ESP32 mô phỏng gặp broker trên máy tính qua
`host.wokwi.internal`.

## Ước tính ppm và hiệu chuẩn

MQ không xuất ppm trực tiếp mà đổi điện trở Rs theo nồng độ. Firmware tính:

1. Đọc điện áp AOUT qua ADC (đã bù chia áp), giới hạn 3300 mV.
2. `Rs = RL * (VCC - Vout) / Vout`.
3. Ro lấy lúc hiệu chuẩn (trung bình 50 mẫu trong 5 giây), lưu NVS.
4. MQ-2 (LPG): `ppm = 574.25 * (Rs/Ro)^-2.222`, với `Ro = Rs / 9.83` trong không khí sạch.
   MQ-135 (CO₂eq): `ppm = 116.6 * (Rs/Ro)^-2.769`, hiệu chuẩn lấy mốc 400 ppm CO₂ nền.

Hiệu chuẩn tự chạy một lần ở lần khởi động đầu. Muốn hiệu chuẩn lại: gửi `{"recalibrate":true}`
(nút “Hiệu chuẩn cảm biến” trong Home Assistant hoặc `control_device`), làm khi không khí sạch.
Chỉ dùng `idf.py erase-flash` khi cần xóa luôn Wi-Fi, broker và webhook đã lưu.

Đây là ước tính, không phải phép đo chuẩn: chỉ tính LPG, sai số lớn của MQ giá rẻ, phụ thuộc vào
việc lúc hiệu chuẩn có thật sự là không khí sạch. MQ-135 phản ứng yếu với mùi nhẹ, nếu muốn nhạy hơn
cần làm nóng vài chục giờ rồi hiệu chuẩn lại, hoặc hạ `AQ_THRESHOLD_PPM`.

## MQTT

`<device>` là `GAS_DEVICE_NAME`. Broker Mosquitto cổng 1883 (và 9001 cho WebSocket), cho phép ẩn danh.

| Topic | Hướng | Payload | QoS / retained |
|---|---|---|---|
| `airgas/<device>/reading` | ESP32 → broker | `{"ppm":4,"mv":1900,"threshold":1000,"alarm":false,"sim":false,"fault":false}` | 0 |
| `airquality/<device>/reading` | ESP32 → broker | `{"co2_ppm":400,"mv":1930,"threshold":1500,"poor":false,"sim":false,"fault":false}` | 0 |
| `airgas/<device>/alarm` | ESP32 → broker | `{"ppm":1600,"mv":1950,"threshold":1000,"device":"..."}` | 1 |
| `airgas/<device>/status` | ESP32 → broker | `online` hoặc `offline` (LWT, dạng chữ) | 1, retained |
| `airgas/<device>/info` | ESP32 → broker | `{"fw":"1.5.0","ip":"...","rssi":-56,"ssid":"...","sim":"off","valve":"open","fan":0}` | 1, retained |
| `airgas/<device>/ack` | ESP32 → broker | `{"cmd":"valve","ok":true,"msg":"closed"}` | 1 |
| `airgas/<device>/cmd` | broker → ESP32 | lệnh JSON, xem bên dưới | |

Lệnh gửi lên `cmd` (chi tiết trong `main/gas_control.h`):

```json
{"sim":"gas","duration":60,"discord":true,"actuators":true}   // "gas" | "air" | "off"
{"valve":"open"}  {"valve":"close"}                           // mở bị từ chối khi đang báo động
{"fan_test":60,"duration":10}                                 // quạt 0-100 %, 5-60 s, báo động gas vẫn ưu tiên
{"buzzer_test":3}                                             // còi 1-10 s, 0 là dừng
{"recalibrate":true}
{"wifi":{"ssid":"...","pass":"..."}}  {"broker":"mqtt://<ip>:1883"}  {"discord_webhook":"https://..."}
```

Khi giả lập rò gas, còi vẫn kêu theo trạng thái báo động kể cả `"actuators":false`.

## Docker: Mosquitto, Node-RED, Home Assistant, AI agent

```
cd node-red
docker compose up -d
```

Giữ nguyên tên project compose (`name: node-red`), nếu đổi thì mất các volume dữ liệu.

| Service | Cổng | Việc |
|---|---|---|
| `mosquitto` | 1883, 9001 | MQTT broker (TCP và WebSocket) |
| `node-red` | 1880 | Flow xử lý và dashboard |
| `homeassistant` | 8123 | Giao diện giám sát, tạo thực thể bằng MQTT discovery |
| `airgas-mcp` | `127.0.0.1:8001` | MCP server (streamable HTTP, bắt buộc `MCP_AUTH_TOKEN`) |
| `airgas-bot` | không mở | Bot Discord hỏi đáp |

Biến môi trường: `node-red/.env` (`HERMES_WEBHOOK_SECRET`, `DISCORD_WEBHOOK_URL` tuỳ chọn) và
`ai-agent/.env` (xem `ai-agent/.env.example`). Hai file này không được commit.

ESP32 và máy chạy Docker phải cùng mạng. Địa chỉ broker trong firmware là IP LAN của máy chạy Docker
(đổi bằng lệnh `{"broker":"..."}` hoặc menuconfig). Nếu IP đổi, thiết bị sẽ hiện offline.

### Node-RED (`node-red/flows.json`)

Flow có 4 tab:

1. **Luồng giám sát:** nhận `reading`, `info`, `status`, `ack`, gộp thành một trạng thái, phát hiện chuyển trạng thái và sinh sự kiện
   `danger`, `safe`, `poor`, `fault`, `offline`, `valve`, mỗi sự kiện kèm một dòng thông báo tiếng Việt.
2. **Luồng khẩn cấp:** chống spam 30 s, soạn tin Discord, báo Hermes Agent qua webhook ký HMAC
   (`host.docker.internal:8644`), ghi log `alerts.csv`.
3. **Dashboard và xác nhận:** trang Node-RED Dashboard 2.0 tại `http://localhost:1880/dashboard/gas`
   (thẻ trạng thái, gauge LPG và CO₂eq, biểu đồ 1 giờ, nút mở/khóa van, giả lập rò gas, giả lập không khí kém, tắt giả lập).
4. **Home Assistant và log:** phát MQTT discovery và ghi `airgas.csv` mỗi 10 s.

Log CSV nằm trong volume Node-RED: `/data/logs/airgas.csv`, `/data/logs/alerts.csv`.
Cần package dashboard (`@flowfuse/node-red-dashboard`), nếu mất volume thì cài lại:

```
docker exec -w /data node-red npm install @flowfuse/node-red-dashboard
docker restart node-red
```

Triển khai flow đã sửa (Admin API có xác thực nên dùng `docker cp`):

```
docker cp node-red/flows.json node-red:/data/flows.json && docker restart node-red
```

Xuất flow đang chạy ra repo: `docker cp node-red:/data/flows.json node-red/flows.json`.

### Home Assistant

Node-RED tạo thiết bị **“Trạm cảnh báo gas”** bằng MQTT discovery với các thực thể: Nồng độ gas, CO₂ tương đương,
Mức cảnh báo (`BINH_THUONG`, `KHONG_KHI_KEM`, `NGUY_HIEM`, `LOI_CAM_BIEN`), Rò rỉ gas, Van gas, Quạt hút, Còi báo động,
Phiên bản firmware, Sóng Wi-Fi, và các nút: Xác nhận an toàn (mở van), Khoá van, Thử quạt 10 giây, Thử còi 3 giây, Hiệu chuẩn cảm biến.
`node-red/homeassistant/config/configuration.yaml` còn khai báo thêm vài entity MQTT tĩnh kiểu cũ (Gas ppm, Gas Alarm…), dùng song song.

Thiết lập lần đầu: mở `http://localhost:8123`, tạo tài khoản, thêm integration **MQTT** (host `mosquitto`, cổng `1883`).
Tạo Long-lived access token cho AI agent ở hồ sơ người dùng, Security. Truy cập từ điện thoại bằng
`http://<IP-LAN-máy-chủ>:8123` (mở cổng 8123 trên Windows Firewall nếu cần).

### Dashboard HTML

Mở `dashboard/air_gas_live.html` trực tiếp bằng trình duyệt. Có hai chế độ: “Mô phỏng trong trang”
(tính theo công thức firmware, không cần thiết bị) và “Thiết bị thật (MQTT)” kết nối `ws://localhost:9001`
(đổi thành IP LAN nếu mở từ máy khác). Trang hiển thị mô hình 3D, gói MQTT, trạng thái van, quạt, còi
và cho gửi các lệnh `sim`, `valve`, `fan_test`, `buzzer_test`, `recalibrate`.

### AI agent

MCP server `airgas` (`ai-agent/airgas_mcp.py`) gọi REST API của Home Assistant, cung cấp 4 công cụ:
`get_status`, `get_history`, `get_alarm_events` (chỉ đọc) và `control_device` (mở/khóa van, thử quạt, thử còi, hiệu chuẩn).
Bot Discord dùng MCP này kèm một LLM tương thích OpenAI, lệnh điều khiển luôn hỏi xác nhận bằng nút.
Chi tiết cài đặt trong [ai-agent/README.md](ai-agent/README.md).

## Kiểm tra sau khi lắp phần cứng

1. Cấp nguồn adapter, cắm USB cho ESP32, đợi vài phút cho cảm biến nóng. Ở lần boot đầu giữ không khí sạch 5 giây để hiệu chuẩn.
2. Xem `airgas/<device>/reading` bằng `docker exec mosquitto mosquitto_sub -R -t "airgas/#" -v`. Không khí thường cho khoảng 1800 đến 1900 mV, vài ppm LPG, CO₂eq 400 ppm, không có `fault`.
3. Thử còi: `{"buzzer_test":3}`. Thử quạt: `{"fan_test":60,"duration":10}`. Thử van: `{"valve":"close"}` rồi `{"valve":"open"}`.
4. Giả lập rò gas: `{"sim":"gas","duration":60}` (hoặc nút trên dashboard). Kỳ vọng: LED sáng, còi bíp nhanh, quạt 100 %, van khóa, Home Assistant chuyển `NGUY_HIEM`, Discord nhận tin có chữ [GIẢ LẬP].
5. Hết giả lập, van vẫn khóa cho tới khi gửi lệnh mở (nút “Xác nhận an toàn” hoặc `{"valve":"open"}`).
6. Rút dây VCC của một cảm biến: mức chuyển `LOI_CAM_BIEN`, van đóng, quạt 100 %.

Dấu hiệu lỗi thường gặp: Serial lặp lại dòng khởi động (`Brownout`) là do nguồn, ADC dao động mạnh và đổi khi chạm
vào board là do chân AOUT bị hở.

## Hạn chế

- Chưa bù nhiệt độ, độ ẩm cho đường cong Rs/Ro. Mỗi cảm biến chỉ ước tính một loại khí cố định (LPG, CO₂eq).
- Hiệu chuẩn Ro tin vào giả định môi trường lúc hiệu chuẩn.
- Mosquitto đang `allow_anonymous true`, không TLS: chỉ dùng trong LAN, không mở cổng 1883, 9001 ra Internet.
- Node-RED gửi “offline” giả mỗi lần khởi động lại. Hàm “1 điểm / 5 s” trong Node-RED còn báo lỗi `Invalid property expression`.
- Nội dung tin `danger` luôn ghi “đã khóa van, bật quạt hút và còi” kể cả khi giả lập với `"actuators":false`.
- Quạt, servo trong mô hình không phải van an toàn chuyên dụng.
