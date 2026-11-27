# Tra cứu lỗi thiết bị

## Xem lỗi ở đâu

| Chỗ xem | Cho biết gì |
|---|---|
| Web → mục **Tình trạng thiết bị** (nút "Thiết bị: …" góc trên) | Thiết bị nào lỗi và cách xử lý. Cập nhật mỗi 2 giây |
| Web → **Nhật ký** | Lúc nào thiết bị hỏng / hết hỏng, ESP32 khởi động lại vì sao |
| Log của ESP32 (Serial) | Chi tiết nhất – xem bằng lệnh bên dưới |
| Còi lúc bật ESP32 cổng | Mỗi cổng: **1 tiếng** = đầu đọc thẻ OK, **3 tiếng** = đầu đọc thẻ lỗi, **không kêu** = còi lỗi |
| LED ô đỗ | Xanh / đỏ **nháy luân phiên** = cảm biến ô đó lỗi |

Xem log ESP32 trên Pi (thay đường dẫn cổng cho đúng, Ctrl+C để thoát):

```bash
arduino-cli monitor -p /dev/serial/by-path/<cổng-ESP32> -c baudrate=115200
```

Bấm nút **EN** trên ESP32 để nó khởi động lại và in kết quả tự kiểm tra.

## Thiết bị nào tự kiểm tra được

| Thiết bị | Tự kiểm tra? | Cách |
|---|---|---|
| Đầu đọc thẻ RC522 | Có | Đọc thanh ghi phiên bản chip |
| Cảm biến siêu âm (cổng + 4 ô) | Có | Chân ECHO không phản hồi = lỏng dây / mất nguồn |
| OLED | Có | Hỏi địa chỉ I2C 0x3C |
| ESP32 mất kết nối | Có | Pi biết sau khoảng 20 giây |
| ESP32 sụt nguồn | Có | ESP32 ghi lại lý do khởi động lại |
| Webcam, EasyOCR, MQTT | Có | Pi tự kiểm tra |
| **Servo** | **Không** | Servo không gửi tín hiệu gì về ESP32. Chỉ biết gián tiếp: servo thiếu nguồn hay làm ESP32 **sụt nguồn** → web báo cảnh báo |
| **Còi, LED** | **Không** | Nghe / nhìn (còi kêu lúc bật nguồn) |

## Triệu chứng → nguyên nhân → cách sửa

### Quẹt thẻ nhưng không kêu

Mở log ESP32 cổng rồi quẹt lại:

| Log hiện | Nguyên nhân | Cách sửa |
|---|---|---|
| `[vao] Quet the XX:XX…` | ESP32 đọc được thẻ, **còi** hỏng / lỏng dây | Kiểm tra dây còi (cổng vào chân 27, cổng ra chân 17) và GND |
| Không hiện gì, web báo **RC522 LỖI** | Đầu đọc thẻ lỏng dây / mất nguồn | Kiểm tra dây SDA SCK MOSI MISO RST, nguồn **3.3V** (cấp 5V có thể làm hỏng RC522) |
| Không hiện gì, RC522 bình thường | Thẻ để quá xa, hoặc thẻ không phải loại 13.56 MHz | Áp sát thẻ vào giữa đầu đọc; thử thẻ khác |
| Không hiện gì cả, bấm EN cũng không có chữ `ESP32 CONG: san sang` | ESP32 chưa có code / code lỗi | **Nạp lại code** |

### Thanh chắn (servo) không nâng

| Hiện tượng | Nguyên nhân | Cách sửa |
|---|---|---|
| Thanh chắn mở mãi không hạ | **Bình thường**: thanh chắn chỉ hạ khi xe đã đi qua cảm biến. Quá 30 giây (cổng vào) / 15 giây (cổng ra) web sẽ hỏi nhân viên. Mở nhầm thì bấm **Đóng cổng** trên web. Xe đã qua mà không hạ: cảm biến cổng lỗi hoặc đặt lệch | Xem dòng cảm biến bên dưới |
| Web báo "Đang mở", servo đứng im | Servo **không có nguồn 5V** hoặc lỏng dây tín hiệu (vào: 32, ra: 33) | Đo / kiểm tra dây đỏ (5V), nâu (GND), cam (tín hiệu) |
| Servo kêu rè rè, giật nhưng không quay đủ | Nguồn yếu hoặc thanh chắn bị kẹt | Cấp nguồn 5V khỏe hơn; kiểm tra cơ khí |
| Mỗi lần mở cổng ESP32 khởi động lại, web báo **"SỤT NGUỒN"** | Servo kéo dòng mạnh làm sụt nguồn ESP32 | Cấp **nguồn 5V riêng cho servo** (adapter ≥ 2A), **nối chung GND** với ESP32. Không lấy nguồn servo từ chân 3V3 của ESP32 |
| Servo quay sai góc / ngược chiều | Góc trong code chưa hợp | Sửa `GOC_DONG`, `GOC_MO` trong `esp32_2_cong.ino` rồi **nạp lại** |
| Web không đổi sang "Đang mở" | Pi không gửi lệnh mở (biển không khớp, bãi đầy…) | Xem Nhật ký trên web |

### Cảm biến siêu âm

| Hiện tượng | Nguyên nhân | Cách sửa |
|---|---|---|
| Web báo **LỖI cảm biến**, LED ô nháy | Lỏng dây TRIG / ECHO hoặc mất nguồn 5V | Kiểm tra dây và nguồn 5V của HC-SR04 |
| Cảm biến "bình thường" nhưng ô báo sai | Cảm biến đặt lệch, xe quá thấp / quá xa | Chỉnh góc; sửa `NGUONG_CM` rồi **nạp lại** |
| Cổng đóng ngay khi xe đang đi qua | Cảm biến cổng không thấy xe | Chỉnh cảm biến chiếu ngang đường xe; sửa `NGUONG_CM` |

### Khác

| Hiện tượng | Cách sửa |
|---|---|
| Web báo ESP32 **MẤT KẾT NỐI** | Kiểm tra nguồn ESP32; xem log: lỗi WiFi (sai tên / mật khẩu / mạng 5GHz) hay lỗi MQTT (sai `MQTT_HOST`, Mosquitto chưa chạy) |
| Web báo ESP32 **"Chưa nhận được báo cáo"** | ESP32 chưa bật, sai WiFi / `MQTT_HOST`, hoặc **chưa nạp code mới** (code cũ không có tự kiểm tra) |
| Quẹt thẻ kêu 1 tiếng ngắn rồi 1 tiếng dài | Pi từ chối (thẻ đang trong bãi, bãi đầy, biển không khớp) hoặc ESP32 chưa nối MQTT – xem Nhật ký |
| OLED tắt, web báo **OLED LỖI** | Kiểm tra dây SDA(21) SCL(22), nguồn. Cắm lại dây là OLED tự sáng lại, không cần nạp lại |
| Webcam **LỖI** | Kiểm tra cáp USB; chạy `python kiem_tra_cam.py` để xem đường dẫn webcam; sửa `CAMERA` trong `config.py` |

## Khi nào cần nạp lại code ESP32?

**Nạp lại code KHÔNG sửa được lỗi dây hay nguồn.** Hầu hết lỗi "không kêu", "không quay", "không đọc thẻ" là do dây / nguồn.

Chỉ cần nạp lại khi:

1. Đổi cấu hình trong file `.ino`: tên / mật khẩu WiFi, `MQTT_HOST`, chân cắm, góc servo, ngưỡng cảm biến.
2. Bấm EN mà log **không hiện chữ gì**, hiện ký tự loạn, hoặc ESP32 khởi động lại liên tục với lý do `loi_chuong_trinh` / `bi_treo`.
3. Muốn dùng tính năng mới (ví dụ phần tự kiểm tra thiết bị này).

Nếu chưa chắc: bấm **EN** trên ESP32 trước. Thấy `ESP32 CONG: san sang` / `ESP32 O DO: san sang` là code vẫn còn, lỗi nằm ở phần cứng.
