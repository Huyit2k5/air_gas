# Air Gas Monitor

Thiết bị giám sát khí gas dùng ESP32 (ESP-IDF): đọc điện áp từ một cảm biến khí gas dạng
analog, so với ngưỡng cấu hình, và gửi cảnh báo qua Discord webhook khi phát hiện vượt
ngưỡng.

## Tính năng

- Đọc điện áp cảm biến qua ADC1 (mặc định GPIO36 / ADC1_CH0), có hiệu chuẩn ADC.
- **Ước tính nồng độ LPG (ppm)** từ đường cong Rs/Ro trong datasheet MQ-2, có tự hiệu
  chuẩn Ro (lưu vào NVS, chỉ chạy 1 lần ở lần boot đầu). So ngưỡng cảnh báo theo ppm thay
  vì mV thô.
- Kết nối Wi-Fi (chỉ 2.4GHz), tự kết nối lại nếu bị rớt sóng.
- Gửi cảnh báo tới kênh Discord qua webhook khi vượt ngưỡng, có cooldown giữa các lần gửi
  để tránh spam.
- **Đèn LED cảnh báo cục bộ**: sáng ngay khi vượt ngưỡng, hoạt động độc lập với Wi-Fi/
  Discord — giám sát cảm biến và bật đèn không phụ thuộc việc có mạng hay không, đảm bảo
  vẫn cảnh báo được tại chỗ khi mất Internet.
- **Publish dữ liệu qua MQTT** tới broker (ví dụ Mosquitto) để tích hợp Node-RED/Home
  Assistant/dashboard riêng — gửi định kỳ mọi lần đọc (`airgas/<device>/reading`) và
  riêng khi có cảnh báo (`airgas/<device>/alarm`).
- Log trạng thái liên tục qua serial (mức OK / cảnh báo).

## Phần cứng

- Board ESP32 (đã build/test trên ESP32 dòng gốc, ESP-IDF v5.5.5).
- Cảm biến khí gas analog, module ra chân `AOUT` dạng analog 0–5V (ví dụ MQ-2, MQ-135,
  MQ-6...).
- Nguồn 5V cho cảm biến (hầu hết module MQ-x cần 5V để đốt nóng dây may-so bên trong,
  cấp 3.3V sẽ không đủ để cảm biến hoạt động chính xác).
- 1 LED cảnh báo (LED rời + điện trở hạn dòng ~220–330Ω, hoặc dùng LED onboard có sẵn
  trên board DevKit nếu có).

### Sơ đồ kết nối (ESP32 ⟷ cảm biến gas MQ-x)

| Cảm biến MQ-x | ESP32 | Ghi chú |
|---|---|---|
| `VCC` | `5V` (chân VIN/5V trên board) | Cấp nguồn cho bộ đốt của cảm biến |
| `GND` | `GND` | Nối đất chung |
| `AOUT` | `GPIO36` (ADC1_CH0, mặc định) | Tín hiệu analog, đổi được qua menuconfig |
| `DOUT` | không dùng | Firmware chỉ đọc ngưỡng qua ADC (`AOUT`), không dùng ngưỡng phần cứng trên `DOUT` |

| LED cảnh báo | ESP32 | Ghi chú |
|---|---|---|
| Chân dương (qua điện trở ~220–330Ω) | `GPIO2` (mặc định) | Đổi được qua menuconfig (`GAS_LED_GPIO`) |
| Chân âm | `GND` | |

Nếu board có sẵn LED onboard nối vào GPIO2 (phổ biến trên nhiều board ESP32 DevKit), có
thể để mặc định và không cần lắp LED rời — chỉ cần đổi `GAS_LED_GPIO` nếu board bạn dùng
chân khác.

```
   MQ-x sensor                 ESP32                  LED cảnh báo
  ┌───────────┐              ┌──────────┐            ┌─────┐
  │       VCC ├──────────────┤ 5V       │            │ LED │
  │       GND ├──────────────┤ GND      ├────────────┤  -  │
  │      AOUT ├──────────────┤ GPIO36   │      ┌──────┤  +  │
  │      DOUT │ (không nối)  │   GPIO2  ├──[R]─┘      └─────┘
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
| `main/gas_led.c/.h` | Điều khiển đèn LED cảnh báo cục bộ qua GPIO |
| `main/gas_mqtt.c/.h` | Publish dữ liệu cảm biến/cảnh báo lên MQTT broker |
| `main/Kconfig.projbuild` | Các tùy chọn cấu hình qua `idf.py menuconfig` |

## Cấu hình

```
idf.py menuconfig
```

Vào mục **Gas Monitor** và điền:

- **Wi-Fi SSID / Wi-Fi password**: thông tin mạng Wi-Fi 2.4GHz.
- **Discord webhook URL**: dạng `https://discord.com/api/webhooks/...`.
- **MQ-2 sensor supply voltage VCC (mV)**: điện áp cấp cho cảm biến (mặc định 5000mV —
  giữ đúng 5V theo datasheet, cấp 3.3V sẽ làm sai lệch ước tính ppm).
- **MQ-2 module load resistor RL (ohm)**: giá trị điện trở tải trên module (mặc định
  5000Ω, phổ biến trên các module giá rẻ — kiểm tra lại board của bạn nếu có ghi chú).
- **AOUT voltage divider ratio (%)**: nếu bạn có thêm cầu phân áp trước khi vào GPIO36,
  nhập % điện áp thực tế còn lại tới chân ADC (mặc định 100 = nối thẳng không qua chia
  áp). Cần đúng giá trị này để tính ngược lại điện áp AOUT thật.
- **Gas alarm threshold (ppm)**: ngưỡng cảnh báo theo nồng độ LPG ước tính (mặc định
  1000ppm — xem lưu ý độ chính xác bên dưới).
- **Sensor check period (ms)**: chu kỳ đọc cảm biến (mặc định 1000ms).
- **Cooldown between alarm sends (s)**: thời gian chờ tối thiểu giữa 2 lần gửi Discord
  (mặc định 60s).
- **Device name**: tên hiển thị trong tin nhắn Discord.
- **Alarm LED GPIO pin**: chân GPIO điều khiển đèn cảnh báo (mặc định GPIO2).
- **MQTT broker URI**: địa chỉ broker MQTT dạng `mqtt://<IP-LAN-máy-chủ>:1883`. Để trống
  sẽ tắt tính năng publish MQTT.

Lưu (phím `S`) rồi thoát (`Q`).

## Ước tính ppm từ MQ-2 — cách hoạt động và độ chính xác

MQ-2 không xuất ra % khí trực tiếp — nó là cảm biến bán dẫn có **điện trở Rs thay đổi
theo nồng độ khí**, không tuyến tính. Firmware quy đổi theo các bước:

1. **Đọc điện áp AOUT** qua ADC (có bù cầu phân áp nếu bạn cấu hình
   `GAS_ADC_DIVIDER_PERCENT` khác 100).
2. **Tính Rs** theo mạch phân áp trên module: `Rs = RL * (VCC - Vout) / Vout`.
3. **Hiệu chuẩn Ro** (điện trở cảm biến trong không khí sạch chuẩn) — thực hiện **tự
   động 1 lần duy nhất** ở lần boot đầu tiên: lấy trung bình 50 mẫu trong 5 giây, giả định
   không khí lúc đó sạch (không có gas), rồi `Ro = Rs_trung_bình / 9.83` (hằng số
   `Rs/Ro` trong không khí sạch, theo datasheet MQ-2). Giá trị Ro được lưu vào NVS, các
   lần khởi động sau sẽ đọc lại, không hiệu chuẩn lại nữa.
4. **Quy đổi ppm (LPG)** theo công thức hồi quy từ đường cong datasheet:
   `ppm = 574.25 * (Rs/Ro) ^ -2.222`.

⚠️ **Đây là ước tính, không phải phép đo chuẩn hóa:**
- Chỉ tính cho khí **LPG** — nếu bạn muốn phát hiện khí khác (CO, khói, methane...), cần
  đường cong hệ số khác (a, b khác trong công thức trên), hiện code chưa hỗ trợ chọn loại
  khí.
- Độ chính xác phụ thuộc hoàn toàn vào **thời điểm hiệu chuẩn Ro có đúng là không khí
  sạch hay không** — nếu lần boot đầu tiên vô tình có mùi gas nhẹ trong phòng, Ro sẽ bị
  lệch và mọi ppm tính sau đó đều sai theo.
- Cảm biến MQ-x giá rẻ vốn có sai số lớn (có thể lệch vài chục %), bị ảnh hưởng bởi nhiệt
  độ/độ ẩm, và cần thời gian làm nóng dài (xem mục an toàn phần cứng). **Không dùng số
  ppm này làm căn cứ an toàn cháy nổ chính thức** (không thay thế thiết bị báo gas đạt
  chuẩn PCCC) — chỉ nên coi là cảnh báo sớm mang tính tham khảo.
- Muốn buộc hiệu chuẩn lại (ví dụ nghi Ro bị lệch, hoặc đổi cảm biến khác): xóa namespace
  NVS `gas_cal`, đơn giản nhất là erase toàn bộ flash rồi flash lại:
  ```
  idf.py erase-flash
  idf.py flash
  ```
  Nhớ đảm bảo không khí xung quanh cảm biến thực sự sạch trong 5 giây đầu sau khi cấp
  nguồn.

## Node-RED + MQTT broker (docker)

Repo có sẵn stack Docker riêng ở [`../node-red/`](../node-red/) chạy **Mosquitto** (MQTT
broker, port 1883) và **Node-RED** (port 1880):

```
cd ../node-red
docker compose up -d
```

- Node-RED UI: http://localhost:1880
- Flow "Air Gas MQTT" đã có sẵn 2 node `mqtt in` subscribe `airgas/+/reading` và
  `airgas/+/alarm`, nối tới debug hiển thị trong sidebar Node-RED, và tới dashboard trực
  quan bên dưới.
- ESP32 publish tới broker này qua **địa chỉ IP LAN của máy chạy Docker**, không dùng
  `localhost` (vì ESP32 là thiết bị khác trên mạng). Đặt đúng IP đó vào **MQTT broker
  URI** ở menuconfig, ví dụ `mqtt://172.16.20.130:1883` — IP có thể đổi nếu máy dùng
  DHCP, kiểm tra lại bằng `ipconfig` nếu ESP32 báo không kết nối được broker.
- ESP32 và máy chạy Docker phải cùng mạng LAN (ESP32 nối Wi-Fi tới cùng router).

### Dashboard trực quan

Đã cài sẵn **Node-RED Dashboard 2.0** (`@flowfuse/node-red-dashboard`) trong container.
Truy cập tại: **http://localhost:1880/dashboard**

Trang "Gas Monitor" hiển thị:
- **Gauge mV** — kim đồng hồ điện áp đọc được realtime, tô màu theo vùng (xanh/vàng/đỏ
  quanh ngưỡng cảnh báo).
- **Biểu đồ lịch sử** — đường mV theo thời gian, tự xóa dữ liệu cũ hơn 1 giờ.
- **Trạng thái** — chữ "🟢 OK" / "🔴 GAS ALARM" cập nhật theo mỗi lần đọc.

Nếu cần cài lại package dashboard sau khi xóa volume `node_red_data`, chạy:
```
docker exec -w /data node-red npm install @flowfuse/node-red-dashboard
docker restart node-red
```

### Cấu trúc topic MQTT

| Topic | Khi nào gửi | Payload |
|---|---|---|
| `airgas/<device>/reading` | Mỗi chu kỳ đọc cảm biến (`GAS_CHECK_PERIOD_MS`) | `{"ppm":420,"mv":650,"threshold":1000,"alarm":false}` |
| `airgas/<device>/alarm` | Khi vượt ngưỡng, theo cùng cooldown với Discord | `{"ppm":1250,"mv":950,"threshold":1000,"device":"Gas-Monitor-01"}` |

`<device>` là giá trị `GAS_DEVICE_NAME` cấu hình trong menuconfig.

## Build & flash

```
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

(thay `COMx` bằng cổng COM thực tế của board).

## Kiểm tra sau khi lắp phần cứng

1. Cấp nguồn trong không khí sạch (không gas) và **giữ nguyên như vậy trong 5 giây đầu**
   — đây là lúc firmware tự hiệu chuẩn Ro (chỉ chạy 1 lần, xem mục "Ước tính ppm" ở
   trên). Sau đó để cảm biến làm nóng thêm vài phút trước khi tin vào số liệu.
2. Theo dõi log qua `idf.py -p COMx monitor`, quan sát dòng
   `OK: ~<ppm> ppm (raw <mV> mV, threshold <ngưỡng> ppm)` chạy liên tục theo chu kỳ cấu
   hình.
3. Đưa nguồn khí thử (bật lửa gas không châm lửa, cồn, khói...) gần cảm biến trong môi
   trường thông thoáng, quan sát giá trị ppm tăng lên.
4. Khi giá trị vượt ngưỡng: đèn LED cảnh báo bật sáng ngay (kể cả khi chưa có Wi-Fi), log
   in `!!! GAS ALARM !!!`, và nếu đã kết nối Wi-Fi + cấu hình webhook thì tin nhắn cảnh
   báo sẽ xuất hiện trong kênh Discord kèm giá trị ppm ước tính.
5. Rút nguồn khí thử, thông thoáng lại khu vực, xác nhận đèn LED tắt, giá trị ppm giảm về
   mức nền và log quay lại trạng thái `OK`.
6. Thử tắt Wi-Fi router hoặc để sai password, xác nhận đèn LED vẫn hoạt động bình thường
   theo cảm biến (chỉ riêng phần gửi Discord bị bỏ qua, có log
   `No Wi-Fi, skipping Discord send`).

### Test tạm khi chưa lắp cảm biến

Nếu chỉ muốn kiểm tra phần Wi-Fi/Discord/MQTT/logic cooldown mà chưa có cảm biến trong
tay, có thể giả lập tín hiệu bằng dây nối trực tiếp vào GPIO36 (không đại diện cho hành
vi cảm biến thật hay giá trị ppm chính xác, chỉ dùng tạm để test luồng phần mềm — do công
thức Rs tỷ lệ nghịch, điện áp cao giả lập vẫn sẽ đẩy ppm ước tính lên rất cao):

- Nối GPIO36 xuống `GND` → Rs rất lớn → ppm ước tính gần 0, log in trạng thái `OK`.
- Nối GPIO36 lên `3.3V` (không dùng 5V) → Rs rất nhỏ → ppm ước tính tăng vọt, vượt
  ngưỡng, log in `!!! GAS ALARM !!!` và gửi Discord/MQTT.
- Dùng chiết áp giữa `3.3V`–`GND`, chân giữa nối GPIO36, để dò điểm ngưỡng chính xác hơn.

## Hạn chế hiện tại

- Chỉ hỗ trợ 1 cảm biến analog duy nhất, chưa có cảm biến nhiệt độ/độ ẩm hay loại khác.
- Ước tính ppm chỉ tính cho LPG, dùng hằng số cố định trong code — chưa hỗ trợ chọn loại
  khí khác hay nhập lại hệ số đường cong.
- Hiệu chuẩn Ro tin tưởng hoàn toàn vào giả định "không khí sạch" ở lần boot đầu, không
  có cách nào để firmware tự biết môi trường lúc đó có thật sự sạch hay không.
- Đèn LED chỉ có 2 trạng thái sáng/tắt, chưa có nháy/còi để phân biệt mức độ nghiêm
  trọng hay báo trạng thái mất kết nối Wi-Fi.
- ADC đọc 1 mẫu/chu kỳ, chưa lọc trung bình để giảm nhiễu tức thời.
- Chưa có cơ chế lưu cấu hình Wi-Fi qua provisioning (SmartConfig/BLE) — phải cấu hình
  cứng qua menuconfig trước khi build.
- Payload gửi Discord ở dạng JSON đơn giản, chưa escape ký tự đặc biệt trong
  `Device name` nếu người dùng đổi tên chứa dấu `"` hoặc `\`.
- Mosquitto trong `node-red/docker-compose.yml` cấu hình `allow_anonymous true`, không
  mật khẩu/TLS — chấp nhận được trong mạng LAN nhà riêng, nhưng không nên expose port
  1883 ra Internet nguyên trạng.
