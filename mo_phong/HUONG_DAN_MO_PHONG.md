# Mô phỏng 2 ESP32 trên Wokwi

| Thư mục | ESP32 | Thiết bị |
|---|---|---|
| `esp32_1_o_do` | Số 1 – các ô đỗ | 4 HC-SR04, 4 LED xanh, 4 LED đỏ, OLED 128x64 |
| `esp32_2_cong` | Số 2 – cổng vào/ra | 2 RC522, 2 HC-SR04 (sau thanh chắn), 2 servo, 2 còi |

Mỗi thư mục có 3 file: `sketch.ino` (code), `diagram.json` (sơ đồ nối dây), `libraries.txt` (thư viện).

## Cách chạy

1. Vào **https://wokwi.com** → đăng nhập → **New Project** → chọn **ESP32**.
2. Tab **sketch.ino**: xoá hết, dán nội dung file `sketch.ino`.
3. Tab **diagram.json**: xoá hết, dán nội dung file `diagram.json`. Sơ đồ linh kiện và dây nối sẽ hiện ra bên phải.
4. Bấm mũi tên cạnh tên tab → **New file...** → đặt tên `libraries.txt` → dán nội dung file `libraries.txt`.
5. Bấm nút **▶ (Start simulation)**. Lần đầu mất khoảng 30–60 giây để biên dịch.
6. Làm 2 lần cho 2 ESP32 (2 project riêng).

## Thử ESP32 số 1 (ô đỗ)

- Ban đầu 4 LED xanh sáng, OLED hiện `TRONG 4/4`.
- Bấm vào một cảm biến HC-SR04 → kéo thanh **distance** xuống **dưới 6 cm** → sau khoảng 1 giây LED ô đó chuyển đỏ, OLED giảm số chỗ trống.
- Kéo cả 4 cảm biến xuống dưới 6 cm → OLED hiện `HET CHO!`.
- Serial Monitor in: `O 1: CO XE (4.0 cm)`.

## Thử ESP32 số 2 (cổng)

1. Bấm vào đầu đọc RFID **bên trái (cổng vào)** → chọn một thẻ để quẹt → còi kêu "bíp", servo trái quay 90° (mở chắn).
2. Bấm cảm biến HC-SR04 **bên trái** → kéo khoảng cách xuống **dưới 8 cm** (xe đang đi qua) → Serial in `[vao] Xe dang di qua`.
3. Kéo khoảng cách lên lại **trên 8 cm** (xe đã qua) → sau 1 giây servo quay về 0° (đóng chắn).
4. Thử quẹt thẻ rồi **không** cho xe qua → sau 15 giây chắn tự đóng.
5. Cổng ra (RFID, cảm biến, servo bên phải) làm tương tự.

## Hai công tắc trong code

| Công tắc | Mô phỏng | Mạch thật |
|---|---|---|
| `DUNG_MQTT` | `0` – chạy độc lập, quẹt thẻ là mở | `1` – gửi lên Pi qua MQTT, chờ Pi đọc biển số rồi mới mở |
| `COI_THU_DONG` (chỉ ESP32 số 2) | `1` – còi thụ động của Wokwi | `0` – module còi chủ động 3 chân |

Khi `DUNG_MQTT 1`: sửa `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST` (IP Pi) ở đầu file.

Các topic MQTT:

| Topic | Hướng | Nội dung |
|---|---|---|
| `baidoxe/o/1` … `baidoxe/o/4` | ESP32 số 1 → Pi | `1` có xe, `0` trống (retain) |
| `baidoxe/trong` | ESP32 số 1 → Pi | Số chỗ trống (retain) |
| `baidoxe/the/vao`, `baidoxe/the/ra` | ESP32 số 2 → Pi | Mã thẻ vừa quẹt, ví dụ `A1:B2:C3:D4` |
| `baidoxe/lenh/vao`, `baidoxe/lenh/ra` | Pi → ESP32 số 2 | `mo` hoặc `tuchoi` |
| `baidoxe/cong/vao`, `baidoxe/cong/ra` | ESP32 số 2 → Pi | `mo` / `dong` (retain) |

## ⚠️ Khác biệt khi nối mạch thật (HC-SR04 loại thường, không có chữ P)

Trong Wokwi, chân ECHO nối thẳng vào ESP32 cho gọn. **Mạch thật bắt buộc phải có cầu chia áp**, vì HC-SR04 thường trả tín hiệu **5V**, còn ESP32 chỉ chịu **3.3V**.

**Mỗi cảm biến** nối như sau:

```
HC-SR04          ESP32
  VCC ─────────── VIN (5V)
  TRIG ────────── GPIO TRIG
  ECHO ──[1kΩ]──┬── GPIO ECHO
                │
              [2kΩ]
                │
  GND ──────────┴── GND
```

- 6 cảm biến cần **6 điện trở 1 kΩ + 6 điện trở 2 kΩ**. Không có 2 kΩ thì dùng **2.2 kΩ**, hoặc **2 điện trở 1 kΩ nối tiếp**.
- Chân **VIN** chỉ có 5V khi ESP32 được cấp nguồn qua cổng USB.
- **Servo thật** lấy nguồn từ **adapter 5V 2A**, không lấy từ VIN như trong mô phỏng. Nhớ **nối chung GND** của adapter với ESP32.
- **RC522** luôn nối **3.3V**, không bao giờ nối 5V.
