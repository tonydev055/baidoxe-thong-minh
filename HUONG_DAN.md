# Mô hình bãi đỗ xe thu nhỏ dùng cảm biến siêu âm

**Đề tài:** Mô hình bãi đỗ xe thu nhỏ, sử dụng cảm biến siêu âm để phát hiện vị trí trống và xây dựng hệ thống quản lý chỗ đỗ xe thông minh.

## 1. Chức năng
- Mỗi ô đỗ có 1 cảm biến siêu âm HC-SR04 để phát hiện ô **có xe** hay **trống**.
- Mỗi ô có 2 đèn: **xanh = trống**, **đỏ = có xe**.
- LCD hiển thị số chỗ còn trống và trạng thái từng ô (`X` = có xe, `O` = trống). Hết chỗ thì báo "BAI DA DAY".
- Gửi trạng thái lên máy tính qua cổng Serial (9600 baud), ví dụ: `TRONG:2/4 O1:1 O2:0 O3:0 O4:1`.

## 2. Linh kiện

| STT | Linh kiện | SL |
|---|---|---|
| 1 | Arduino Uno R3 + cáp USB | 1 |
| 2 | Cảm biến siêu âm HC-SR04 | 4 |
| 3 | LED xanh | 4 |
| 4 | LED đỏ | 4 |
| 5 | Điện trở 220 Ω | 8 |
| 6 | LCD 16x2 + module I2C | 1 |
| 7 | Breadboard, dây cắm | – |
| 8 | Bìa formex/carton, xe đồ chơi | – |

## 3. Sơ đồ nối dây

| Ô | HC-SR04 (TRIG / ECHO) | LED xanh | LED đỏ |
|---|---|---|---|
| 1 | D2 / D3 | D10 | A0 |
| 2 | D4 / D5 | D11 | A1 |
| 3 | D6 / D7 | D12 | A2 |
| 4 | D8 / D9 | D13 | A3 |

- LED: chân dài (+) → điện trở 220 Ω → chân Arduino; chân ngắn (–) → GND.
- LCD I2C: SDA → A4, SCL → A5.
- VCC của các HC-SR04 và LCD → 5V; tất cả GND → GND.

```
 ┌──────────────── MÔ HÌNH (nhìn từ trên) ────────────────┐
 │  [SR04]   [SR04]   [SR04]   [SR04]   ← cuối mỗi ô     │
 │ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐                    │
 │ │ Ô 1  │ │ Ô 2  │ │ Ô 3  │ │ Ô 4  │                    │
 │ └──────┘ └──────┘ └──────┘ └──────┘                    │
 │  ●xanh●đỏ ●xanh●đỏ ●xanh●đỏ ●xanh●đỏ  ← đèn mỗi ô      │
 │                                                        │
 │                ĐƯỜNG VÀO          [ LCD ]              │
 └────────────────────────────────────────────────────────┘
```

## 4. Nguyên lý
Cảm biến phát sóng siêu âm và đo thời gian sóng dội về: `khoảng cách = thời gian × 0,0343 / 2` (cm).
Nếu khoảng cách nhỏ hơn ngưỡng `NGUONG` (mặc định 6 cm) thì ô có xe. Phải đọc giống nhau 3 lần liên tiếp mới đổi trạng thái, để tránh nhiễu.

## 5. Nạp chương trình
1. Arduino IDE → Library Manager → cài **LiquidCrystal I2C** (Frank de Brabander).
2. Mở `SmartParking.ino`, chọn **Arduino Uno** và cổng COM → **Upload**.
3. Mở Serial Monitor (9600) để xem trạng thái.

## 6. Lưu ý
- Chỉnh `NGUONG` theo mô hình: đo khoảng cách khi ô trống và khi có xe, lấy giá trị ở giữa.
- LCD không hiện chữ: vặn biến trở sau module I2C, hoặc đổi `0x27` thành `0x3F`.
- HC-SR04 đo không chính xác dưới 2 cm, nên đặt cảm biến cách xe ít nhất 3 cm.
