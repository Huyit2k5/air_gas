# Air Gas Monitor

Thiết bị giám sát khí gas dùng ESP32 (ESP-IDF): đọc điện áp từ một cảm biến khí gas dạng
analog, so với ngưỡng cấu hình, và gửi cảnh báo qua Discord webhook khi phát hiện vượt
ngưỡng.

## Tính năng

- Đọc điện áp cảm biến qua ADC1 (mặc định GPIO36 / ADC1_CH0), có hiệu chuẩn ADC.
- **Ước tính nồng độ LPG (ppm)** từ đường cong Rs/Ro trong datasheet MQ-2, có tự hiệu
  chuẩn Ro (lưu vào NVS, chỉ chạy 1 lần ở lần boot đầu). So ngưỡng cảnh báo theo ppm thay
  vì mV thô.
- **Cảm biến chất lượng không khí MQ-135** (độc lập với MQ-2 ở trên) — ước tính CO2 tương
  đương (ppm), publish riêng, dùng chung cơ chế hiệu chuẩn Ro/NVS.
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
- Cảm biến khí gas MQ-2 (phát hiện rò gas), module ra chân `AOUT` dạng analog 0–5V.
- Cảm biến chất lượng không khí MQ-135 (CO2 tương đương), cùng dạng module `AOUT` 0–5V.
- Nguồn 5V cho cả 2 cảm biến (module MQ-x cần 5V để đốt nóng dây may-so bên trong, cấp
  3.3V sẽ không đủ để cảm biến hoạt động chính xác).
- 1 LED cảnh báo (LED rời + điện trở hạn dòng ~220–330Ω, hoặc dùng LED onboard có sẵn
  trên board DevKit nếu có).

### Sơ đồ kết nối (ESP32 ⟷ MQ-2 + MQ-135)

| Cảm biến MQ-2 (gas leak) | ESP32 | Ghi chú |
|---|---|---|
| `VCC` | `5V` (chân VIN/5V trên board) | Cấp nguồn cho bộ đốt của cảm biến |
| `GND` | `GND` | Nối đất chung |
| `AOUT` | `GPIO36` (ADC1_CH0, mặc định) | Tín hiệu analog, đổi được qua menuconfig |
| `DOUT` | không dùng | Firmware chỉ đọc ngưỡng qua ADC (`AOUT`), không dùng ngưỡng phần cứng trên `DOUT` |

| Cảm biến MQ-135 (air quality) | ESP32 | Ghi chú |
|---|---|---|
| `VCC` | `5V` | Chung nguồn 5V với MQ-2 (đảm bảo nguồn đủ dòng cho cả 2 bộ đốt) |
| `GND` | `GND` | Nối đất chung |
| `AOUT` | `GPIO39` (ADC1_CH3, mặc định) | Tín hiệu analog, đổi được qua menuconfig (`AQ_ADC_PIN`/`AQ_ADC_CHANNEL`) |
| `DOUT` | không dùng | |

⚠️ 2 cảm biến dùng chung nguồn 5V — kiểm tra bộ nguồn/cổng USB cấp đủ dòng (mỗi MQ-x
tiêu thụ ~150mA khi đốt nóng, tổng ~300mA cho cả 2, cộng thêm ESP32). Nếu dùng chung cổng
USB máy tính để cấp nguồn khi test, một số cổng yếu dòng có thể không đủ — nên dùng
adapter 5V/1A trở lên khi chạy thật.

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
| `main/gas_sensor.c/.h` | Cảm biến MQ-2 (gas leak): khởi tạo ADC1 dùng chung + hiệu chuẩn/đọc ppm LPG |
| `main/air_sensor.c/.h` | Cảm biến MQ-135 (air quality): đọc kênh ADC1 riêng, hiệu chuẩn/đọc CO2-tương đương |
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
  sẽ tắt tính năng publish MQTT (áp dụng cho cả MQ-2 lẫn MQ-135).
- **MQ-135 Air Quality Sensor** (menu con riêng):
  - **Enable MQ-135 air quality sensor**: bật/tắt cảm biến thứ 2 (mặc định bật).
  - **MQ-135 ADC pin / ADC channel**: chân đọc AOUT (mặc định GPIO39 / ADC1_CH3).
  - **MQ-135 sensor supply voltage VCC / load resistor RL / voltage divider ratio**:
    tương tự các tham số MQ-2, dùng để tính Rs chính xác cho MQ-135.
  - **Poor air quality threshold (ppm)**: ngưỡng cảnh báo chất lượng không khí kém, theo
    CO2 tương đương (mặc định 1500ppm).

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

Cảm biến MQ-135 (air quality) dùng **cùng cơ chế** (đọc AOUT → tính Rs → hiệu chuẩn Ro
lưu NVS namespace riêng `aq_cal` → quy ra CO2 tương đương qua công thức
`ppm = 116.6 * (Rs/Ro) ^ -2.769`), chỉ khác ở điểm mốc hiệu chuẩn: vì không khí thường
(kể cả ngoài trời) luôn có sẵn CO2 nền ~400ppm chứ không phải 0, nên lần boot đầu firmware
giả định môi trường đang ở mức nền 400ppm (không khí thoáng bình thường) thay vì "sạch
tuyệt đối" như MQ-2. Muốn hiệu chuẩn lại MQ-135 độc lập với MQ-2: cũng dùng
`idf.py erase-flash && idf.py flash` (erase-flash xóa cả 2 namespace `gas_cal` và
`aq_cal` cùng lúc, không tách được).

## Node-RED + MQTT broker (docker)

Repo có sẵn stack Docker riêng ở [`node-red/`](node-red/) chạy **Mosquitto** (MQTT
broker, port 1883), **Node-RED** (port 1880), và **Home Assistant** (port 8123):

```
cd node-red
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
Dashboard có **2 tab riêng trong sidebar điều hướng**:

**Tab "Gas Monitor"** — http://localhost:1880/dashboard/gas
- Group "Trạng thái hiện tại": gauge ppm (kim đồng hồ, tô màu xanh/vàng/đỏ quanh ngưỡng),
  badge trạng thái (nền đỏ "GAS ALARM" / nền xanh "OK"), dòng "Cập nhật lúc..." (biết
  ngay nếu dữ liệu bị đứng), badge Online/Offline của thiết bị (dựa trên MQTT
  status + Last Will).
- Group "Lịch sử": biểu đồ đường ppm theo thời gian, full-width, tự xóa dữ liệu cũ hơn
  1 giờ.

**Tab "Air Quality"** — http://localhost:1880/dashboard/air-quality
- Bố cục tương tự: gauge CO2-tương đương (ppm), badge "KHÔNG KHÍ TỐT"/"KHÔNG KHÍ KÉM",
  timestamp cập nhật, biểu đồ lịch sử riêng.

Truy cập trang chủ dashboard tại http://localhost:1880/dashboard sẽ thấy menu điều hướng
để chuyển giữa 2 tab.

Nếu cần cài lại package dashboard sau khi xóa volume `node_red_data`, chạy:
```
docker exec -w /data node-red npm install @flowfuse/node-red-dashboard
docker restart node-red
```

### Home Assistant

Chạy như 1 lựa chọn giám sát **song song** với Node-RED Dashboard (không thay thế, cả 2
đều đọc chung dữ liệu từ Mosquitto, dùng cái nào tùy bạn). Kiểu cài đặt: **Home Assistant
Container** (chỉ HA Core chạy trong Docker, không phải "Home Assistant OS" — do đó
**không có** Add-on Store, không tự OTA update toàn hệ thống; đổi lại nhẹ và chạy chung
máy tính với các service khác trong cùng `docker-compose.yml`).

**Cấu hình đã có sẵn** trong `node-red/homeassistant/config/configuration.yaml` — khai
báo 7 entity MQTT theo dạng YAML tĩnh (không cần MQTT Discovery từ firmware):

| Entity | Nguồn | Loại |
|---|---|---|
| Gas ppm | `airgas/<device>/reading` → `ppm` | sensor |
| Gas raw mV | `airgas/<device>/reading` → `mv` | sensor (diagnostic) |
| Gas Alarm | `airgas/<device>/reading` → `alarm` | binary_sensor (gas) |
| Air Quality CO2eq | `airquality/<device>/reading` → `co2_ppm` | sensor |
| Air Quality raw mV | `airquality/<device>/reading` → `mv` | sensor (diagnostic) |
| Poor Air Quality | `airquality/<device>/reading` → `poor` | binary_sensor (problem) |
| Gas Monitor Device Online | `airgas/<device>/status` (MQTT LWT) | binary_sensor (connectivity) |

#### Setup lần đầu (thủ công, 1 lần)

1. Mở **http://localhost:8123**, tạo tài khoản qua giao diện (tên/username/password —
   không có sẵn, tự đặt).
2. **Settings → Devices & Services → Add Integration → MQTT** → nhập host `mosquitto`,
   port `1883` (không cần user/pass, Mosquitto đang cho phép anonymous trong LAN) →
   Submit.
3. Xong bước 2, cả 7 entity ở bảng trên **tự xuất hiện** trong dashboard "Overview" mặc
   định (mục Favorites) — không cần tự thêm card thủ công.

#### Truy cập từ điện thoại

Yêu cầu điện thoại **cùng mạng Wi-Fi** với máy chạy Docker. Dùng địa chỉ
`http://<IP-LAN-máy-chủ>:8123` (không dùng `localhost` — điện thoại là thiết bị khác,
không hiểu `localhost` là máy tính bạn). IP này là **IP động theo DHCP**, có thể đổi mỗi
lần kết nối lại mạng — kiểm tra lại bằng `ipconfig` nếu không vào được. Khuyên đặt IP tĩnh
cho máy chủ qua router (DHCP reservation theo MAC) để khỏi phải tra lại IP mỗi lần.

Nếu điện thoại không vào được dù đúng IP: kiểm tra **Windows Firewall** trên máy chủ có
chặn cổng 8123 không (thêm inbound rule cho phép TCP 8123, profile Private).

Muốn cài app chính thức: tải app **"Home Assistant"** trên App Store/Google Play, nhập
đúng địa chỉ trên để đăng nhập — có thêm tiện ích nhận thông báo đẩy.

#### Truy cập từ xa (ngoài mạng nhà, tùy chọn, chưa cấu hình)

Cách hiện tại chỉ dùng được trong cùng mạng LAN. Muốn xem được từ Internet (4G/5G, không
cùng Wi-Fi), cần thêm 1 trong: **Nabu Casa Cloud** (dịch vụ chính thức, có phí ~$6.5/
tháng, dễ cấu hình nhất, không cần mở port router) hoặc tự cấu hình port forwarding +
domain/dynamic DNS + HTTPS (phức tạp hơn, tự chịu trách nhiệm bảo mật). Chưa triển khai
phần này trong project.

### Cấu trúc topic MQTT

| Topic | Khi nào gửi | Payload |
|---|---|---|
| `airgas/<device>/reading` | Mỗi chu kỳ đọc cảm biến (`GAS_CHECK_PERIOD_MS`) | `{"ppm":420,"mv":650,"threshold":1000,"alarm":false}` |
| `airgas/<device>/alarm` | Khi vượt ngưỡng, theo cùng cooldown với Discord | `{"ppm":1250,"mv":950,"threshold":1000,"device":"Gas-Monitor-01"}` |
| `airgas/<device>/status` | Khi kết nối MQTT (retained "online"), tự động "offline" (retained, qua LWT) nếu mất kết nối đột ngột | `online` hoặc `offline` (plain text, không phải JSON) |
| `airquality/<device>/reading` | Mỗi chu kỳ đọc cảm biến MQ-135 | `{"co2_ppm":650,"mv":720,"threshold":1500,"poor":false}` |

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

- Chưa có cảm biến nhiệt độ/độ ẩm để bù trừ đường cong Rs/Ro (cả MQ-2 lẫn MQ-135 đều bị
  ảnh hưởng bởi nhiệt độ/độ ẩm môi trường theo datasheet).
- Ước tính ppm chỉ tính 1 loại khí cố định mỗi cảm biến (LPG cho MQ-2, CO2-tương đương
  cho MQ-135), dùng hằng số cố định trong code — chưa hỗ trợ chọn loại khí khác hay nhập
  lại hệ số đường cong.
- Hiệu chuẩn Ro (cả 2 cảm biến) tin tưởng hoàn toàn vào giả định về môi trường ở lần boot
  đầu (không khí sạch cho MQ-2, mức CO2 nền ~400ppm cho MQ-135), không có cách nào để
  firmware tự biết môi trường lúc đó có đúng như giả định hay không — không có nút/lệnh
  hiệu chuẩn lại thủ công, chỉ có thể ép qua `idf.py erase-flash`.
- MQ-2 và MQ-135 dùng chung 1 ADC unit (`ADC_UNIT_1`) qua `gas_sensor_get_adc_unit()` —
  `gas_sensor_init()` (MQ-2) phải chạy trước `air_sensor_init()` (MQ-135), thứ tự này
  không được kiểm tra/báo lỗi rõ ràng nếu bị đổi nhầm trong `gas_main.c`.
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
