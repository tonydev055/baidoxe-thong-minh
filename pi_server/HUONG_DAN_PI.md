# Cài phần mềm bãi đỗ xe trên Raspberry Pi 5

## Các file

| File | Chức năng |
|---|---|
| `app.py` | Chương trình chính: nhận MQTT, xử lý xe vào/ra, trang web |
| `config.py` | Cấu hình: MQTT, camera, giá tiền |
| `db.py` | Lưu dữ liệu vào SQLite (`baidoxe.db`) |
| `camera.py` | Chụp ảnh webcam (không có camera thì tạo ảnh giả lập) |
| `ocr.py` | Đọc biển số bằng EasyOCR |
| `templates/index.html` | Giao diện web quản lý |
| `gia_lap.py` | Giả lập ESP32 để test khi chưa lắp mạch |

## 1. Chép thư mục `pi_server` lên Pi

Trên laptop (thay `<user>` và `<IP-Pi>`):

```bash
scp -r pi_server <user>@<IP-Pi>:~/
```

## 2. Cài thư viện (trên Pi)

```bash
cd ~/pi_server
```

```bash
python3 -m venv venv
```

```bash
source venv/bin/activate
```

```bash
pip install -r requirements.txt
```

Cài EasyOCR để đọc biển số (khá nặng, mất 5–15 phút):

```bash
pip install easyocr
```

Chưa cài EasyOCR thì hệ thống vẫn chạy, chỉ là không đọc được biển số (có thể sửa tay trên web).

## 3. Khai báo webcam

```bash
ls /dev/v4l/by-path/
```

Chọn 2 dòng kết thúc bằng `video-index0` (mỗi webcam 1 dòng), điền vào `config.py`:

```python
CAMERA = {
    "vao": "/dev/v4l/by-path/....-video-index0",   # webcam cắm cổng USB 3.0 phía trên
    "ra":  "/dev/v4l/by-path/....-video-index0",   # webcam cắm cổng USB 3.0 phía dưới
}
```

## 4. Chạy

```bash
python app.py
```

Mở trình duyệt trên laptop: **`http://<IP-Pi>:5000`**

## 5. Test khi chưa có ESP32

**Cách 1 – ngay trên trang web**: phần **"Công cụ thử"** cuối trang có nút quẹt thẻ cổng vào/ra và nút đổi trạng thái từng ô.

**Cách 2 – giả lập MQTT** (mở Terminal thứ 2 trên Pi, `source venv/bin/activate` trước):

```bash
python gia_lap.py nghe
```

```bash
python gia_lap.py o 1 1
```

```bash
python gia_lap.py the vao A1:B2:C3:D4
```

```bash
python gia_lap.py the ra A1:B2:C3:D4
```

## 6. Cho chương trình tự chạy khi bật Pi

```bash
sudo nano /etc/systemd/system/baidoxe.service
```

Dán vào (sửa `<user>` cho đúng):

```
[Unit]
Description=Bai do xe thong minh
After=network-online.target mosquitto.service

[Service]
User=<user>
WorkingDirectory=/home/<user>/pi_server
ExecStart=/home/<user>/pi_server/venv/bin/python app.py
Restart=always

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now baidoxe
```

## Luồng xử lý

| Sự kiện | Pi làm gì |
|---|---|
| ESP32 số 1 gửi `baidoxe/o/1 = 1` | Cập nhật ô 1 có xe trên web |
| Quẹt thẻ cổng vào (`baidoxe/the/vao`) | Còn chỗ → chụp webcam vào, đọc biển, lưu, gửi `baidoxe/lenh/vao = mo`. Hết chỗ hoặc thẻ đang trong bãi → `tuchoi` |
| Quẹt thẻ cổng ra (`baidoxe/the/ra`) | Chụp webcam ra, đọc biển. **Khớp** → tính tiền, `mo`. **Không khớp** → `tuchoi`, web hiện cảnh báo kèm 2 ảnh để người trông xe bấm "Cho ra" hoặc "Không cho ra" |

## Giá tiền

Sửa trong `config.py`. Mặc định để **demo theo phút**: 10.000đ phút đầu, 5.000đ mỗi phút tiếp theo.
Chạy thật theo giờ: đổi `DON_VI_PHUT = 60`.
