# HƯỚNG DẪN LẮP RÁP HỆ THỐNG GIÁM SÁT KHÍ GAS & CHẤT LƯỢNG KHÔNG KHÍ

Tài liệu hướng dẫn kết nối phần cứng cho hệ thống ESP32 kết hợp cảm biến MQ-2, MQ-135 và Còi báo động Active Buzzer (kèm điện trở bảo vệ).

---

## 1. DANH SÁCH LINH KIỆN & VẬT LIỆU
1. **01 Bo mạch ESP32** (38 chân, cổng Type-C).
2. **01 Breadboard MB-102** (400 lỗ).
3. **01 Module cảm biến khí gas MQ-2** (4 chân: VCC, GND, DO, AO).
4. **01 Module cảm biến chất lượng không khí MQ-135** (4 chân: VCC, GND, DO, AO).
5. **01 Còi Active Buzzer 5V** (loại 2 chân kim).
6. **01 Điện trở** ($100\Omega - 330\Omega$ hoặc $1k\Omega$).
7. **Dây cắm Jumper:**
   - Dây Đực - Cái (khoảng 5-7 sợi, độ dài 20cm).
   - Dây Đực - Đực (khoảng 5-6 sợi).

---

## 2. NGUYÊN TẮC BỐ TRÍ TRÊN BREADBOARD
- **ESP32:** Đặt nằm riêng bên ngoài breadboard trên mặt bàn để tránh che khuất lỗ cắm và hạn chế cấn chân.
- **Khu vực cắm:** Tất cả các chân linh kiện và điện trở nên cắm gọn ở **nửa trên (khu vực các hàng a, b, c, d, e)** của breadboard.
- **Rãnh giữa:** Rãnh giữa breadboard ngắt mạch hoàn toàn, không cắm chân linh kiện nhảy bắc cầu qua rãnh nếu không chủ ý nối mạch.
- **Thanh ray nguồn:** Sử dụng đồng nhất **thanh mép trên** (Vạch Đỏ `+` và Vạch Xanh `-`) để làm bus nguồn chung.

---

## 3. VỊ TRÍ CẮM LINH KIỆN LÊN BREADBOARD

### A. Cảm biến MQ-2 (4 chân)
Cắm dọc 4 chân vào các hàng từ 10 đến 13 (khu vực a–e):
- **Hàng 10:** Chân `VCC`
- **Hàng 11:** Chân `GND`
- **Hàng 12:** Chân `DO` *(để trống, không dùng)*
- **Hàng 13:** Chân `AO`

### B. Cảm biến MQ-135 (4 chân)
Cắm dọc 4 chân vào các hàng từ 17 đến 20 (khu vực a–e):
- **Hàng 17:** Chân `VCC`
- **Hàng 18:** Chân `GND`
- **Hàng 19:** Chân `DO` *(để trống, không dùng)*
- **Hàng 20:** Chân `AO`

### C. Còi Active Buzzer & Điện trở
Còi có 2 chân: chân dài (`+`) và chân ngắn (`-`).
- Cắm **chân `+` của còi** vào **Hàng 28**.
- Cắm **chân `-` của còi** vào **Hàng 30**.
- Cắm **Điện trở bảo vệ**:
  - Chân thứ nhất của điện trở: Cắm vào **Hàng 28** (cùng hàng với chân `+` của còi).
  - Chân thứ hai của điện trở: Cắm vào **Hàng 27** (hàng trống kế bên, cùng khu vực a–e).

---

## 4. CHI TIẾT ĐI DÂY KẾT NỐI (STEP-BY-STEP)

### Bước 1: Cấp nguồn chính từ ESP32 sang Breadboard
- **Dây 5V:** Dùng 1 dây **Đực - Cái**, đầu Cái cắm vào chân **VIN** (hoặc **5V**) trên ESP32 $\to$ đầu Đực cắm vào dải **thanh ĐỎ (+)** ở mép trên breadboard.
- **Dây Mass:** Dùng 1 dây **Đực - Cái**, đầu Cái cắm vào chân **GND** trên ESP32 $\to$ đầu Đực cắm vào dải **thanh XANH (-)** ở mép trên breadboard.

### Bước 2: Nối nguồn nội bộ cho các linh kiện (Dây Đực - Đực)
- **Nguồn cho MQ-2:**
  - Lỗ cùng Hàng 10 (`VCC`) $\to$ Cắm sang dải **thanh ĐỎ (+)**.
  - Lỗ cùng Hàng 11 (`GND`) $\to$ Cắm sang dải **thanh XANH (-)**.
- **Nguồn cho MQ-135:**
  - Lỗ cùng Hàng 17 (`VCC`) $\to$ Cắm sang dải **thanh ĐỎ (+)**.
  - Lỗ cùng Hàng 18 (`GND`) $\to$ Cắm sang dải **thanh XANH (-)**.
- **Nối đất cho Còi Buzzer:**
  - Lỗ cùng Hàng 30 (chân `-` còi) $\to$ Cắm sang dải **thanh XANH (-)**.

### Bước 3: Nối tín hiệu điều khiển và đọc dữ liệu (Dây Đực - Cái)
- **Tín hiệu MQ-2:**
  - Đầu Đực cắm vào **Hàng 13** (`AO` MQ-2) $\to$ Đầu Cái cắm vào **GPIO 34** (in là `34` hoặc `D34`) của ESP32.
- **Tín hiệu MQ-135:**
  - Đầu Đực cắm vào **Hàng 20** (`AO` MQ-135) $\to$ Đầu Cái cắm vào **GPIO 35** (in là `35` hoặc `D35`) của ESP32.
- **Điều khiển Còi:**
  - Đầu Đực cắm vào **Hàng 27** (nối tiếp qua con điện trở) $\to$ Đầu Cái cắm vào **GPIO 18** (in là `18` hoặc `D18`) của ESP32.

---

## 5. BẢNG TRA CỨU ĐẤU NỐI NHANH

| Thiết bị | Chân thiết bị | Vị trí trên Breadboard | Loại dây | Đích kết nối |
| :--- | :--- | :--- | :--- | :--- |
| **ESP32** | `VIN` (hoặc 5V) | Ngoài board | Đực - Cái | **Thanh ĐỎ (+)** |
| **ESP32** | `GND` | Ngoài board | Đực - Cái | **Thanh XANH (-)** |
| **MQ-2** | `VCC` | Hàng 10 | Đực - Đực | **Thanh ĐỎ (+)** |
| **MQ-2** | `GND` | Hàng 11 | Đực - Đực | **Thanh XANH (-)** |
| **MQ-2** | `AO` | Hàng 13 | Đực - Cái | Chân **GPIO 34** (ESP32) |
| **MQ-135** | `VCC` | Hàng 17 | Đực - Đực | **Thanh ĐỎ (+)** |
| **MQ-135** | `GND` | Hàng 18 | Đực - Đực | **Thanh XANH (-)** |
| **MQ-135** | `AO` | Hàng 20 | Đực - Cái | Chân **GPIO 35** (ESP32) |
| **Điện trở** | Chân 1 | Hàng 28 | Chân cắm trở | Nối chung chân `+` của còi |
| **Điện trở** | Chân 2 | Hàng 27 | Chân cắm trở | Nhận tín hiệu điều khiển |
| **Còi Buzzer** | Chân `+` | Hàng 28 | Chân còi | Nối chung Chân 1 điện trở |
| **Còi Buzzer** | Chân `-` | Hàng 30 | Đực - Đực | **Thanh XANH (-)** |
| **Tín hiệu còi**| - | Hàng 27 | Đực - Cái | Chân **GPIO 18** (ESP32) |

---

## 6. MÃ CODE KIỂM TRA TOÀN BỘ MẠCH (ARDUINO IDE)

```cpp
/*
 * Chuong trinh kiem tra he thong ESP32 + MQ-2 + MQ-135 + Buzzer
 */

const int PIN_MQ2 = 34;      // Doc Analog MQ-2
const int PIN_MQ135 = 35;    // Doc Analog MQ-135
const int PIN_BUZZER = 18;   // Kich coi Buzzer

// Nguong canh bao gia lap (ADC ESP32: 0 - 4095)
const int GAS_THRESHOLD = 2000; 

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Tat coi luc khoi dong

  Serial.println("==========================================");
  Serial.println(" HE THONG DANG KHOI DONG...");
  Serial.println("==========================================");
  
  // Test tieng bip coi khi khoi dong thanh cong
  digitalWrite(PIN_BUZZER, HIGH);
  delay(150);
  digitalWrite(PIN_BUZZER, LOW);
  delay(100);
}

void loop() {
  int gasVal = analogRead(PIN_MQ2);
  int airVal = analogRead(PIN_MQ135);

  // In gia tri doc duoc ra Serial Monitor
  Serial.print("MQ-2 (Gas/Khoi): ");
  Serial.print(gasVal);
  Serial.print(" | MQ-135 (Chat luong KK): ");
  Serial.println(airVal);

  // Kiem tra nguong gas
  if (gasVal > GAS_THRESHOLD) {
    Serial.println("-> CANH BAO: Phat hien khi gas cao!");
    // Phat coi bao dong ngat quang
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
    delay(200);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    delay(1000); // Doc moi giay
  }
}
```

---

## 7. CÁC LƯU Ý QUAN TRỌNG KHI VẬN HÀNH
1. **Lớp sấy cảm biến (Preheating):** Khi cấp nguồn, hai đầu kim loại tròn của MQ-2 và MQ-135 sẽ ấm lên sau 2–5 phút. Đây là hiện tượng bình thường do cuộn dây sấy bên trong hoạt động.
2. **Bóc tem còi:** Nhớ bóc miếng tem niêm phong tròn trên miệng còi ra để âm thanh thoát ra rõ và đanh hơn.
3. **Thử nghiệm an toàn:**
   - Thử còi: Đổi tạm dây từ GPIO 18 sang chân 3V3/VIN xem còi kêu không.
   - Thử MQ-2: Dùng bật lửa xì nhẹ gas (không bật lửa) gần cảm biến, giá trị ADC sẽ vọt lên gần mức 4000 ngay lập tức.