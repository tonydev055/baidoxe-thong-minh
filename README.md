# Bãi đỗ xe thông minh (Secote Vn)

Mô hình bãi đỗ xe thu nhỏ dùng cảm biến siêu âm phát hiện chỗ trống, quẹt thẻ RFID + webcam đọc biển số ở cổng.

| Thư mục | Nội dung |
|---|---|
| `pi_server/` | Phần mềm Raspberry Pi 5: MQTT, đọc biển số, web quản lý – xem `pi_server/HUONG_DAN_PI.md` |
| `mo_phong/esp32_1_o_do/` | ESP32 số 1: 4 cảm biến ô đỗ, LED, OLED |
| `mo_phong/esp32_2_cong/` | ESP32 số 2: cổng vào/ra (RFID, servo, cảm biến, còi) |
| `esp32_that/` | Code 2 ESP32 cho mạch thật (WiFi + MQTT + tự kiểm tra thiết bị) |
| `HUONG_DAN_LOI.md` | Tra cứu lỗi từng thiết bị, khi nào cần nạp lại code |

## Linh kiện (mô hình 4 ô đỗ)

| Linh kiện | SL | Vị trí / công dụng |
|---|---|---|
| Raspberry Pi 5 + thẻ microSD 32 GB | 1 | Máy chủ: Mosquitto, Flask, SQLite, OpenCV, EasyOCR |
| Nguồn USB-C 27 W cho Pi 5 | 1 | Cấp nguồn Raspberry Pi |
| ESP32-WROOM-32 DevKit | 2 | Số 1: 4 ô đỗ. Số 2: 2 cổng |
| Cảm biến siêu âm HC-SR04 | 6 | 4 ở ô đỗ, 2 sau thanh chắn (biết xe đã qua) |
| Đầu đọc RFID RC522 + thẻ / móc khoá | 2 | 1 đầu đọc mỗi cổng, nối SPI chung |
| Webcam USB (720p) | 2 | Chụp biển số cổng vào / cổng ra |
| Servo SG90 | 2 | Thanh chắn cổng vào / ra |
| Adapter 5 V / 2 A | 1 | Nguồn riêng cho 2 servo (nối chung GND với ESP32) |
| Module còi chủ động 3 chân | 2 | Báo đã nhận thẻ / từ chối |
| Màn OLED SSD1306 I²C 128×64 | 1 | Hiện số chỗ trống |
| LED đỏ / LED xanh | 4 / 4 | Mỗi ô 1 đỏ (có xe), 1 xanh (trống) |
| Điện trở 220 Ω | 8 | Hạn dòng cho mỗi LED |
| Điện trở 4,7 kΩ | 18 | Chia áp chân ECHO của HC-SR04 (3 điện trở / cảm biến) |
| Breadboard 830 lỗ | 3 | |
| Breadboard 400 lỗ | 1 | |
| Dây cắm, cáp USB cho ESP32, mô hình, xe và biển số in | 1 bộ | |

Các file `HUONG_DAN.md` và `SmartParking.ino` ở thư mục gốc là bản thử đầu tiên dùng Arduino Uno + LCD 16×2, không dùng trong mô hình hiện tại.

## Cài trên Pi

```bash
git clone https://github.com/tonydev055/baidoxe-thong-minh.git baidoxe
cd baidoxe/pi_server
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python app.py
```

Cập nhật code mới: `git pull` trong thư mục `baidoxe`.
