# Hướng dẫn từng bước: ESP32 + MQ-2 + MQ-135 → MQTT → Home Assistant

Dành cho bộ thiết bị: **ESP32 DevKit 30 chân**, **MQ-2** (khí gas/khói), **MQ-135** (chất lượng
không khí), breadboard, dây jumper, điện trở.

Làm lần lượt từ bước 0 đến bước 9. Mỗi bước có mục "Xong khi" để biết đã đúng chưa.

---

## Bước 0 — Chuẩn bị

### Đồ cần có

| Thứ | Số lượng | Ghi chú |
|---|---|---|
| ESP32 DevKit 30 chân | 1 | Loại có chip USB-serial CP2102 hoặc CH340 |
| Module MQ-2 | 1 | Có 4 chân: VCC, GND, DOUT, AOUT |
| Module MQ-135 | 1 | Có 4 chân: VCC, GND, DOUT, AOUT |
| Breadboard | 1 | Loại 400 lỗ trở lên |
| Điện trở **10kΩ** | 2 | Chia áp (R1) |
| Điện trở **20kΩ** | 2 | Chia áp (R2) |
| Tụ **100nF** (104) | 2 | Không bắt buộc, giúp số đọc ổn định hơn |
| Dây jumper đực–đực | ~12 | Nối trên breadboard |
| Dây jumper đực–cái | ~6 | Nối chân module MQ vào breadboard (nếu module có chân đực thì đực–đực) |
| LED + điện trở 220–330Ω | tùy chọn | Có thể dùng LED onboard ở GPIO2 |
| Cáp micro-USB / USB-C (loại **có truyền dữ liệu**) + củ sạc 5V ≥ 1A | 1 | Cáp chỉ sạc sẽ không nạp được firmware |

> Không có điện trở 20kΩ? Dùng **2 điện trở 10kΩ** (chia đôi điện áp), khi cấu hình nhập tỉ lệ chia áp là **50**
> thay vì 67 (xem bước 5). Cách này vẫn an toàn.

### Phần mềm cần cài trên máy tính (Windows)

1. **Driver USB-serial** cho board: CP210x (Silicon Labs) hoặc CH340, tùy chip trên board. Cắm board vào,
   nếu Device Manager → *Ports (COM & LPT)* hiện cổng COM mới là xong.
2. **ESP-IDF v5.5.x**: tải *ESP-IDF Tools Installer* từ https://dl.espressif.com/dl/esp-idf/ , chọn chip
   **ESP32**. Cài xong sẽ có shortcut **"ESP-IDF 5.5 PowerShell"**.
3. **Docker Desktop** (chạy Mosquitto + Node-RED + Home Assistant). Máy bạn đã có.

---

## Bước 1 — ⚠️ Đọc phần an toàn trước khi nối dây

- Cảm biến MQ cần **5V** để đốt nóng. Chân `AOUT` của module có thể ra tới **5V**, trong khi chân ADC của
  ESP32 chỉ chịu tối đa **3.3V**. **Không nối AOUT thẳng vào GPIO** — phải qua mạch chia áp (bước 2).
- Không cấp 5V vào bất kỳ chân GPIO nào. 5V chỉ vào chân `VIN` (hoặc `5V`).
- Cảm biến nóng khi chạy (chạm vào có thể bỏng nhẹ). Không đặt gần vật dễ cháy.
- Đây là cảm biến giá rẻ, **không thay thế thiết bị báo gas đạt chuẩn** PCCC.

---

## Bước 2 — Nối dây

### 2.1 Sơ đồ chân ESP32 DevKit 30 chân

Nhìn theo nhãn in trên board (vị trí có thể lệch giữa các hãng, **luôn tin nhãn trên board**):

| Nhãn trên board | GPIO | Dùng để |
|---|---|---|
| `VIN` (hoặc `5V`) | — | Cấp 5V cho 2 cảm biến |
| `GND` | — | Nối đất chung |
| `VP` (có thể ghi `SVP`/`VP`) | **GPIO36** | Đọc AOUT của **MQ-2** |
| `VN` (có thể ghi `SVN`/`VN`) | **GPIO39** | Đọc AOUT của **MQ-135** |
| `D2` | GPIO2 | LED cảnh báo (LED onboard) |

`VP` và `VN` thường nằm ở phía đầu board, cạnh chân `EN`. `VIN` và `GND` thường ở hàng cuối cùng cùng
bên với `VP`/`VN`.

### 2.2 Cách nối mỗi cảm biến (làm giống nhau cho cả 2)

Mỗi cảm biến cần 1 mạch chia áp 10kΩ + 20kΩ:

```
 AOUT ──[ R1 = 10kΩ ]──┬────────────── chân ESP32 (VP hoặc VN)
                       │
                       ├──[ C = 100nF ]── GND   (tùy chọn)
                       │
                       └──[ R2 = 20kΩ ]── GND
```

Điện áp tới ESP32 = AOUT × 20 / (10 + 20) = **AOUT × 2/3**. Khi AOUT = 5V thì ESP32 chỉ nhận 3.33V (giới hạn
an toàn).

### 2.3 Bảng nối đầy đủ

**Cấp nguồn (breadboard):**

| Từ | Đến |
|---|---|
| ESP32 `VIN` | Ray **+** của breadboard |
| ESP32 `GND` | Ray **–** của breadboard |

**MQ-2:**

| Chân MQ-2 | Nối tới |
|---|---|
| `VCC` | Ray **+** |
| `GND` | Ray **–** |
| `AOUT` | R1 (10kΩ), đầu kia của R1 là **điểm giữa A** |
| `DOUT` | **Không nối** |

Điểm giữa A: nối tới ESP32 **`VP` (GPIO36)**, nối xuống ray **–** qua R2 (20kΩ) (và tụ 100nF nếu có).

**MQ-135:**

| Chân MQ-135 | Nối tới |
|---|---|
| `VCC` | Ray **+** |
| `GND` | Ray **–** |
| `AOUT` | R1 (10kΩ), đầu kia của R1 là **điểm giữa B** |
| `DOUT` | **Không nối** |

Điểm giữa B: nối tới ESP32 **`VN` (GPIO39)**, nối xuống ray **–** qua R2 (20kΩ) (và tụ 100nF nếu có).

**LED cảnh báo (tùy chọn):** nhiều board 30 chân đã có LED onboard nối GPIO2 — nếu vậy không cần nối gì thêm.
Muốn LED rời: `D2` → điện trở 220–330Ω → chân dài (+) của LED, chân ngắn (–) → GND.

### 2.4 Kiểm tra trước khi cấp nguồn

- [ ] `AOUT` **không** nối thẳng vào `VP`/`VN`, luôn có R1 ở giữa.
- [ ] Mỗi điểm giữa có R2 nối xuống GND.
- [ ] `VCC` của cảm biến nối ray `+` (5V từ `VIN`), **không** nối vào `3V3`.
- [ ] Mọi `GND` (ESP32, MQ-2, MQ-135) chung một ray `–`.
- [ ] `DOUT` bỏ trống.

**Xong khi:** đủ 5 ô trên đều được tick.

---

## Bước 3 — Preheat cảm biến (làm trước khi nạp firmware)

Firmware **tự hiệu chuẩn một lần** ở lần boot đầu tiên (lấy mẫu 5 giây rồi lưu vào bộ nhớ). Nếu lúc đó
cảm biến còn **nguội**, số liệu hiệu chuẩn sẽ sai và bị lưu mãi. Vì vậy cần làm nóng cảm biến *trước*.

1. Cắm cáp USB vào ESP32 (board chưa cần firmware), module MQ sẽ nóng lên và đèn nguồn trên module sáng.
2. Đặt ở nơi **thoáng, không khí sạch** (không gần bếp, khói, nước hoa, cồn, người hút thuốc).
3. Chờ **tối thiểu 10–30 phút**. Lần dùng đầu tiên lý tưởng là chạy liên tục **24 giờ** (burn-in) để cảm biến ổn
   định.
4. Giữ nguyên nguồn USB, làm tiếp bước 4, 5, 6 (Docker, cấu hình, build) rồi mới nạp ở bước 7. Nếu rút nguồn, cảm biến nguội lại và
   phải đợi lại.

**Xong khi:** đã cắm nguồn liên tục ít nhất 10 phút trong không khí sạch.

---

## Bước 4 — Khởi động Docker (Mosquitto + Node-RED + Home Assistant)

Mở PowerShell:

```
cd D:\Documents\BaiTap\KiCuiCung\IOT\air_gas\node-red
docker compose up -d
docker compose ps
```

`docker compose ps` phải hiện 3 container `mosquitto`, `node-red`, `homeassistant` ở trạng thái **Up**. Lần
đầu có thể mất vài phút để tải image.

### Mở cổng firewall cho ESP32

Mở **PowerShell quyền Administrator** và chạy:

```
New-NetFirewallRule -DisplayName "Mosquitto 1883" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow -Profile Private
New-NetFirewallRule -DisplayName "Home Assistant 8123" -Direction Inbound -Protocol TCP -LocalPort 8123 -Action Allow -Profile Private
```

Nếu mạng Wi-Fi của máy đang ở chế độ *Public* thì đổi sang *Private* (Settings → Network → Wi-Fi → Properties).

### Lấy IP của máy tính

```
ipconfig
```

Ghi lại **IPv4 Address** của card Wi-Fi (dạng `192.168.x.x` hoặc `172.16.x.x`). Đây là địa chỉ broker mà
ESP32 sẽ dùng. Máy và ESP32 phải cùng router.

> IP có thể đổi khi router cấp lại (DHCP). Khuyên đặt IP tĩnh cho máy trong router (DHCP reservation theo
> MAC) để khỏi phải nạp lại firmware.

**Xong khi:** `docker compose ps` hiện 3 container Up và bạn có IP máy.

---

## Bước 5 — Cấu hình firmware

Mở **"ESP-IDF 5.5 PowerShell"** (từ Start menu), rồi:

```
cd D:\Documents\BaiTap\KiCuiCung\IOT\air_gas
idf.py fullclean
idf.py set-target esp32
idf.py menuconfig
```

> `fullclean` cần thiết vì thư mục `build/` cũ trỏ tới đường dẫn của máy khác.

Trong menuconfig dùng phím mũi tên + Enter, vào mục **Gas Monitor** và nhập:

| Mục | Giá trị |
|---|---|
| Gas sensor ADC pin | `36` |
| ADC channel | `0` |
| MQ-2 sensor supply voltage VCC (mV) | `5000` |
| MQ-2 module load resistor RL (ohm) | `5000` (nếu board MQ-2 của bạn ghi khác thì sửa theo) |
| AOUT voltage divider ratio (%) | `67` (nếu dùng 2 điện trở 10k thì `50`) |
| Gas alarm threshold (ppm) | `1000` |
| Sensor check period (ms) | `1000` |
| Cooldown between alarm sends (s) | `60` |
| Alarm LED GPIO pin | `2` |
| Wi-Fi SSID | tên Wi-Fi **2.4GHz** của bạn |
| Wi-Fi password | mật khẩu Wi-Fi |
| Discord webhook URL | dán webhook **mới** (để trống nếu không dùng Discord) |
| Device name | giữ `Gas-Monitor-01` |
| MQTT broker URI | `mqtt://<IP máy ở bước 4>:1883`, ví dụ `mqtt://192.168.1.50:1883` |

Mục con **MQ-135 Air Quality Sensor**:

| Mục | Giá trị |
|---|---|
| Enable MQ-135 air quality sensor | bật (`y`) |
| MQ-135 ADC pin | `39` |
| MQ-135 ADC channel | `3` |
| MQ-135 supply voltage VCC (mV) | `5000` |
| MQ-135 load resistor RL (ohm) | `20000` (kiểm tra theo module của bạn) |
| MQ-135 AOUT voltage divider ratio (%) | `67` (hoặc `50`) |
| Poor air quality threshold (ppm) | `1500` |

Bấm `S` để lưu, `Q` để thoát.

> **Tên device phải giữ `Gas-Monitor-01`** — cấu hình Home Assistant đang ghi cứng tên này trong topic
> MQTT. Nếu muốn đổi tên thì phải sửa cả `node-red/homeassistant/config/configuration.yaml`.
>
> Chỉ dùng Wi-Fi **2.4GHz**. ESP32 không bắt được 5GHz.
>
> **Không commit** file `sdkconfig` lên git, nó chứa mật khẩu Wi-Fi và webhook của bạn.

**Xong khi:** đã lưu menuconfig không báo lỗi.

---

## Bước 6 — Build

```
idf.py build
```

Lần đầu mất vài phút. Thành công khi cuối log có dòng kiểu
`Project build complete. To flash, run: idf.py flash`.

Lỗi thường gặp: nếu báo thiếu component hoặc lỗi đường dẫn, chạy lại `idf.py fullclean` rồi `idf.py build`.

---

## Bước 7 — Nạp firmware vào ESP32

### 7.1 Tìm cổng COM

Cắm board vào USB. Mở **Device Manager → Ports (COM & LPT)** xem cổng mới xuất hiện (ví dụ `COM7`). Máy bạn
đang có sẵn COM3–COM6 là cổng Bluetooth, **không phải** của board. Chạy thử rút/cắm board để biết cổng nào
là của board.

### 7.2 Xóa flash sạch (chỉ lần đầu)

Để xóa mọi giá trị hiệu chuẩn cũ trong bộ nhớ:

```
idf.py -p COM7 erase-flash
```

(thay `COM7` bằng cổng của bạn)

### 7.3 Nạp và xem log

Cảm biến đã preheat ở bước 3 nên nạp ngay:

```
idf.py -p COM7 flash monitor
```

Nếu báo `Connecting......_____` mãi không vào: **giữ nút `BOOT`** trên board trong lúc nó đang kết nối, thả ra
khi thấy bắt đầu ghi.

Sau khi nạp xong, firmware tự chạy và monitor in log. Thoát monitor bằng **Ctrl + ]**.

### Log đúng sẽ trông như thế này

Lần boot đầu (có dòng hiệu chuẩn, mất 5 giây mỗi cảm biến, **giữ không khí sạch**):

```
No saved calibration -- calibrating Ro now, assuming the sensor is currently in CLEAN AIR ...
Calibration done: Rs_avg=... ohm, Ro=... ohm
Air quality sensor ready ...
Calibration done: ...
Connected to Wi-Fi, IP = 192.168.x.x
Connected to MQTT broker
OK: ~4 ppm (raw 2xxx mV, threshold 1000 ppm)
Air quality OK: ~400 ppm CO2eq (raw ... mV)
```

- MQ-2 trong không khí sạch sau hiệu chuẩn sẽ hiện **khoảng vài ppm** (vì đã coi đó là mức 0).
- MQ-135 sẽ hiện **khoảng 400 ppm** (vì đã coi đó là mức CO₂ nền).

Các lần boot sau sẽ in `Loaded MQ-2 calibration from NVS` và bỏ qua bước hiệu chuẩn.

**Xong khi:** thấy `Connected to Wi-Fi`, `Connected to MQTT broker` và các dòng `OK: ...` / `Air quality OK: ...`
chạy liên tục mỗi giây.

---

## Bước 8 — Kiểm tra dữ liệu và vào Home Assistant

### 8.1 Xem tin nhắn MQTT thô

```
docker exec mosquitto mosquitto_sub -t "#" -v
```

Thấy dòng như sau là ESP32 đang gửi dữ liệu về (thoát bằng Ctrl + C):

```
airgas/Gas-Monitor-01/status online
airgas/Gas-Monitor-01/reading {"ppm":4,"mv":...,"threshold":1000,"alarm":false}
airquality/Gas-Monitor-01/reading {"co2_ppm":400,"mv":...,"threshold":1500,"poor":false}
```

### 8.2 Home Assistant

1. Mở http://localhost:8123 và đăng nhập (tài khoản đã tạo trước, ví dụ `admin`).
2. **Settings → Devices & Services**: thẻ **MQTT** phải ở trạng thái đã kết nối (broker `mosquitto`, cổng `1883`).
   Nếu chưa có thì **Add Integration → MQTT** → host `mosquitto`, port `1883`.
3. **Settings → Devices & Services → Entities**, gõ `gas` hoặc `air`. Có 7 entity, mỗi cái sẽ có giá trị thật
   sau vài giây:

| Entity | Ý nghĩa |
|---|---|
| Gas ppm | Nồng độ LPG ước tính (MQ-2) |
| Gas raw mV | Điện áp thô (chẩn đoán) |
| Gas Alarm | Bật khi vượt ngưỡng |
| Air Quality CO2eq | CO₂ tương đương (MQ-135) |
| Air Quality raw mV | Điện áp thô (chẩn đoán) |
| Poor Air Quality | Bật khi không khí kém |
| Gas Monitor Device Online | Thiết bị có đang online không |

4. Thêm lên trang chủ: **Overview → ⋮ → Edit dashboard → Add card → Gauge / Entities**, chọn các entity trên.

### 8.3 Dashboard Node-RED (tùy chọn)

- http://localhost:1880/dashboard/gas
- http://localhost:1880/dashboard/air-quality

Nếu Node-RED trống: vào http://localhost:1880 → menu ☰ → **Import** → chọn file
`node-red/flows.json` → Deploy. Rồi cài package dashboard:

```
docker exec -w /data node-red npm install @flowfuse/node-red-dashboard
docker restart node-red
```

### 8.4 Xem từ điện thoại

Điện thoại cùng Wi-Fi, mở `http://<IP máy>:8123`. Cài app **Home Assistant** chính thức nếu muốn nhận thông báo.

**Xong khi:** các entity hiện giá trị số thật (không còn "Unknown").

---

## Bước 9 — Thử cảm biến thật

Thử trong nơi **thoáng gió**, không có nguồn lửa:

| Thử | Cảm biến | Kết quả mong đợi |
|---|---|---|
| Xịt gas bật lửa (**không châm lửa**) cách cảm biến ~10 cm | MQ-2 | `Gas ppm` tăng vọt, vượt ngưỡng thì LED sáng, `Gas Alarm` bật, Discord báo (nếu cấu hình) |
| Thổi hơi thở vào gần MQ-135 | MQ-135 | `Air Quality CO2eq` tăng |
| Cồn sát khuẩn, nước hoa gần MQ-135 | MQ-135 | Tăng nhẹ |
| Để thoáng lại | cả 2 | Giảm dần về mức nền sau vài phút |

Lưu ý: cảm biến phản ứng **chậm**, cần vài chục giây để lên và vài phút để xuống. LED cảnh báo sáng độc lập
với Wi-Fi: rút mạng vẫn sáng khi vượt ngưỡng.

---

## Xử lý sự cố

| Triệu chứng | Nguyên nhân hay gặp | Cách xử lý |
|---|---|---|
| Không thấy cổng COM mới | Cáp chỉ sạc, hoặc thiếu driver | Đổi cáp có dữ liệu, cài driver CP210x/CH340 |
| `Connecting.......` rồi lỗi khi flash | Board không vào chế độ nạp | Giữ nút `BOOT` khi đang kết nối |
| Board reset liên tục, log lạ | Nguồn yếu (2 cảm biến + ESP32 ngốn ~500mA) | Dùng củ sạc 5V ≥ 1A, cáp ngắn tốt; tránh cổng USB yếu |
| Không có `Connected to Wi-Fi` | Sai SSID/mật khẩu hoặc mạng 5GHz | Dùng Wi-Fi 2.4GHz, chạy lại menuconfig, build, flash |
| `Connected to Wi-Fi` nhưng MQTT lỗi (`transport error`) | Sai IP broker, firewall chặn 1883, máy và ESP32 khác mạng | Kiểm tra IP bằng `ipconfig`, mở firewall (bước 4), cùng router |
| HA hiện `Unavailable` / `Unknown` mãi | ESP32 chưa gửi, hoặc đổi tên device | Chạy `mosquitto_sub` xem có dữ liệu không; giữ tên `Gas-Monitor-01` |
| `Gas Monitor Device Online` luôn Offline | ESP32 mất kết nối MQTT | Xem log ESP32, kiểm tra broker |
| ppm nhảy loạn hoặc luôn rất cao/thấp | Hiệu chuẩn khi cảm biến còn nguội, hoặc lúc đó có khí lạ | Preheat kỹ, `idf.py -p COMx erase-flash`, nạp lại trong không khí sạch |
| Số đọc kẹt ở giá trị lớn nhất | Sai tỉ lệ chia áp, hoặc chân ADC chạm 5V | Kiểm tra lại chia áp và `divider ratio` trong menuconfig |
| Build lỗi đường dẫn | Thư mục `build/` cũ | `idf.py fullclean` rồi build lại |

### Muốn hiệu chuẩn lại

```
idf.py -p COM7 erase-flash
idf.py -p COM7 flash monitor
```

Lệnh này xóa cả 2 hiệu chuẩn (MQ-2 và MQ-135). Làm lại trong không khí sạch, sau khi cảm biến đã nóng.

---

## Lưu ý bảo mật cho repo

- File `sdkconfig` chứa mật khẩu Wi-Fi và Discord webhook. Nếu repo từng được push lên GitHub, hãy **xóa webhook cũ và
  tạo cái mới**, đổi mật khẩu Wi-Fi.
- Nên tạo `.gitignore` bỏ qua: `build/`, `sdkconfig`, `sdkconfig.old`, `build_log*.txt`, và
  `node-red/homeassistant/config/.storage`, `home-assistant_v2.db*`, `.cache`.
- Mosquitto đang cho phép kết nối ẩn danh (không mật khẩu) — chỉ dùng trong mạng nhà, **đừng mở cổng 1883 ra
  Internet**.
