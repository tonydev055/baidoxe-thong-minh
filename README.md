# Bãi đỗ xe thông minh (Team1 IOT – SIC)

Mô hình bãi đỗ xe thu nhỏ dùng cảm biến siêu âm phát hiện chỗ trống, quẹt thẻ RFID + webcam đọc biển số ở cổng.

| Thư mục | Nội dung |
|---|---|
| `pi_server/` | Phần mềm Raspberry Pi 5: MQTT, đọc biển số, web quản lý – xem `pi_server/HUONG_DAN_PI.md` |
| `mo_phong/esp32_1_o_do/` | ESP32 số 1: 4 cảm biến ô đỗ, LED, OLED |
| `mo_phong/esp32_2_cong/` | ESP32 số 2: cổng vào/ra (RFID, servo, cảm biến, còi) |

## Cài trên Pi

```bash
git clone https://github.com/tonydev055/baidoxe-thong-minh.git baidoxe
cd baidoxe/pi_server
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python app.py
```

Cập nhật code mới: `git pull` trong thư mục `baidoxe`.
