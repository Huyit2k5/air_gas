# Air Gas Monitor

Thiết bị giám sát khí gas dùng ESP32 (ESP-IDF): đọc điện áp từ một cảm biến khí gas dạng
analog, so với ngưỡng cấu hình, và gửi cảnh báo qua Discord webhook khi phát hiện vượt
ngưỡng.

## Tính năng

- Đọc điện áp cảm biến qua ADC1 (mặc định GPIO36 / ADC1_CH0), có hiệu chuẩn ADC.
- So sánh với ngưỡng cảnh báo cấu hình được (mV).
- Kết nối Wi-Fi (chỉ 2.4GHz), tự kết nối lại nếu bị rớt sóng.
- Gửi cảnh báo tới kênh Discord qua webhook khi vượt ngưỡng, có cooldown giữa các lần gửi
  để tránh spam.
- Log trạng thái liên tục qua serial (mức OK / cảnh báo).

## Phần cứng

- Board ESP32 (đã build/test trên ESP32 dòng gốc, ESP-IDF v5.5.5).
- Cảm biến khí gas analog, module ra chân `AOUT` dạng analog 0–5V (ví dụ MQ-2, MQ-135,
  MQ-6...).
- Nguồn 5V cho cảm biến (hầu hết module MQ-x cần 5V để đốt nóng dây may-so bên trong,
  cấp 3.3V sẽ không đủ để cảm biến hoạt động chính xác).

### Sơ đồ kết nối (ESP32 ⟷ cảm biến gas MQ-x)

| Cảm biến MQ-x | ESP32 | Ghi chú |
|---|---|---|
| `VCC` | `5V` (chân VIN/5V trên board) | Cấp nguồn cho bộ đốt của cảm biến |
| `GND` | `GND` | Nối đất chung |
| `AOUT` | `GPIO36` (ADC1_CH0, mặc định) | Tín hiệu analog, đổi được qua menuconfig |
| `DOUT` | không dùng | Firmware chỉ đọc ngưỡng qua ADC (`AOUT`), không dùng ngưỡng phần cứng trên `DOUT` |

```
   MQ-x sensor                 ESP32
  ┌───────────┐              ┌──────────┐
  │       VCC ├──────────────┤ 5V       │
  │       GND ├──────────────┤ GND      │
  │      AOUT ├──────────────┤ GPIO36   │
  │      DOUT │ (không nối)  │          │
  └───────────┘              └──────────┘
```

⚠️ **Lưu ý an toàn:**
- Đầu ra `AOUT` của module MQ-x thường đã có sẵn cầu phân áp trên board, cho ra 0–5V —
  vẫn cao hơn mức 3.3V mà chân ADC ESP32 chịu được. Kiểm tra thông số module bạn dùng;
  nếu output có thể vượt 3.3V, cần thêm cầu phân áp bằng 2 điện trở (ví dụ 10kΩ/20kΩ)
  hoặc module chia áp trước khi đưa vào GPIO36, tránh làm hỏng chân ADC.
- Không cấp 5V trực tiếp vào bất kỳ chân GPIO nào của ESP32 (chỉ cấp vào chân `5V`/`VIN`).
- Cảm biến MQ-x cần thời gian làm nóng (preheat) 24–48 giờ lần đầu và vài phút mỗi lần
  bật nguồn để cho số liệu ổn định; giá trị đọc được trong vài phút đầu có thể không
  chính xác.
- Nếu board ESP32 của bạn không có chân `5V` riêng (một số board mini chỉ có 3.3V ra),
  cần dùng nguồn 5V ngoài (qua mạch nạp USB hoặc adapter) cấp riêng cho cảm biến, chung
  GND với ESP32.

## Cấu trúc source

| File | Vai trò |
|---|---|
| `main/gas_main.c` | Vòng lặp chính: đọc cảm biến, kiểm tra ngưỡng, điều phối cooldown |
| `main/gas_sensor.c/.h` | Khởi tạo & đọc ADC (adc_oneshot + adc_cali) |
| `main/gas_network.c/.h` | Kết nối Wi-Fi STA, tự reconnect khi rớt sóng |
| `main/gas_discord.c/.h` | Gửi cảnh báo qua Discord webhook (HTTP POST JSON) |
| `main/Kconfig.projbuild` | Các tùy chọn cấu hình qua `idf.py menuconfig` |

## Cấu hình

```
idf.py menuconfig
```

Vào mục **Gas Monitor** và điền:

- **Wi-Fi SSID / Wi-Fi password**: thông tin mạng Wi-Fi 2.4GHz.
- **Discord webhook URL**: dạng `https://discord.com/api/webhooks/...`.
- **Gas alarm threshold (mV)**: ngưỡng cảnh báo (mặc định 800mV).
- **Sensor check period (ms)**: chu kỳ đọc cảm biến (mặc định 1000ms).
- **Cooldown between alarm sends (s)**: thời gian chờ tối thiểu giữa 2 lần gửi Discord
  (mặc định 60s).
- **Device name**: tên hiển thị trong tin nhắn Discord.

Lưu (phím `S`) rồi thoát (`Q`).

## Build & flash

```
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

(thay `COMx` bằng cổng COM thực tế của board).

## Kiểm tra sau khi lắp phần cứng

1. Cấp nguồn, để cảm biến làm nóng vài phút trước khi tin vào số liệu.
2. Theo dõi log qua `idf.py -p COMx monitor`, quan sát dòng
   `OK: <mV> mV (threshold <ngưỡng> mV)` chạy liên tục theo chu kỳ cấu hình.
3. Đưa nguồn khí thử (bật lửa gas không châm lửa, cồn, khói...) gần cảm biến trong môi
   trường thông thoáng, quan sát giá trị mV tăng lên.
4. Khi giá trị vượt ngưỡng, log in `!!! GAS ALARM !!!` và (nếu đã cấu hình webhook) tin
   nhắn cảnh báo sẽ xuất hiện trong kênh Discord kèm giá trị mV đo được.
5. Rút nguồn khí thử, thông thoáng lại khu vực, xác nhận giá trị mV giảm về mức nền và
   log quay lại trạng thái `OK`.

### Test tạm khi chưa lắp cảm biến

Nếu chỉ muốn kiểm tra phần Wi-Fi/Discord/logic cooldown mà chưa có cảm biến trong tay, có
thể giả lập tín hiệu bằng dây nối trực tiếp vào GPIO36 (không đại diện cho hành vi cảm
biến thật, chỉ dùng tạm để test luồng phần mềm):

- Nối GPIO36 xuống `GND` → log in `OK: 0 mV`.
- Nối GPIO36 lên `3.3V` (không dùng 5V) → vượt ngưỡng, log in `!!! GAS ALARM !!!` và gửi
  Discord.
- Dùng chiết áp giữa `3.3V`–`GND`, chân giữa nối GPIO36, để dò ngưỡng chính xác hơn.

## Hạn chế hiện tại

- Chỉ hỗ trợ 1 cảm biến analog duy nhất, chưa có cảm biến nhiệt độ/độ ẩm hay loại khác.
- Chưa có cơ chế lưu cấu hình Wi-Fi qua provisioning (SmartConfig/BLE) — phải cấu hình
  cứng qua menuconfig trước khi build.
- Payload gửi Discord ở dạng JSON đơn giản, chưa escape ký tự đặc biệt trong
  `Device name` nếu người dùng đổi tên chứa dấu `"` hoặc `\`.
