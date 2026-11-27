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

**Cách 1 – ngay trên trang web**: phần **"Công cụ thử"** cuối trang có nút quẹt thẻ cổng vào/ra, nút **"Xe đi qua cổng vào/ra"** (giả lập xe đi qua cảm biến) và nút đổi trạng thái từng ô.

**Test lặp lại một thẻ / dọn bãi:**

| Nút | Tác dụng |
|---|---|
| **Xe vừa vào → Xoá để quẹt lại** | Xoá lượt xe vào gần nhất (kèm ảnh) để quẹt lại đúng thẻ đó ở cổng vào |
| **Xoá** (từng dòng trong "Xe đang trong bãi") | Xoá 1 lượt bất kỳ |
| **Xoá tất cả (dọn sau khi test)** | Xoá mọi xe trong bãi / chờ kiểm tra. Lịch sử xe đã ra vẫn giữ |

Ảnh cũ hơn `GIU_ANH_NGAY` ngày (mặc định 30, trong `config.py`) được tự xoá để không đầy thẻ nhớ.

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

```bash
python gia_lap.py qua vao
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
| Quẹt thẻ cổng vào (`baidoxe/the/vao`) | Còn chỗ → chụp webcam vào, đọc biển, lưu lượt ("Đang vào cổng"), gửi `baidoxe/lenh/vao = mo`. Hết chỗ hoặc thẻ đang trong bãi → `tuchoi` |
| Quẹt thẻ cổng ra (`baidoxe/the/ra`) | Chụp webcam ra, đọc biển. **Khớp** → tính tiền, lượt chuyển "Đang ra", `mo`. **Không khớp** → `tuchoi`, web hiện cảnh báo kèm 2 ảnh để người trông xe bấm "Cho ra" hoặc "Không cho ra" |
| Xe đi qua cảm biến cổng (`baidoxe/qua/vao` hoặc `/ra`) | ESP32 hạ thanh chắn. Cổng vào: xe chuyển "trong bãi". Cổng ra: kết thúc lượt, ghi vào lịch sử |
| Mở cổng mà xe chưa đi qua | Thanh chắn **không tự hạ**. Sau `CHO_XAC_NHAN_VAO_GIAY` (30 giây) ở cổng vào / `CHO_XAC_NHAN_RA_GIAY` (15 giây) ở cổng ra, web hiện cảnh báo để nhân viên chọn |
| Nhân viên chọn ở cổng vào | **Xe vẫn vào** → giữ cổng mở chờ xe. **Xe không vào** → xoá thẻ khỏi bãi, gửi `baidoxe/lenh/vao = dong` |
| Nhân viên chọn ở cổng ra | **Xe vẫn ra** → giữ cổng mở, xe qua cảm biến thì kết thúc lượt như thường. **Xe không ra** → gửi `dong`, xe vẫn tính là trong bãi (không tính tiền) |

## Giá tiền

Sửa trong `config.py`. Mặc định để **demo theo phút**: 10.000đ phút đầu, 5.000đ mỗi phút tiếp theo.
Chạy thật theo giờ: đổi `DON_VI_PHUT = 60`.
