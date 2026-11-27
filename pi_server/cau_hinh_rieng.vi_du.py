"""
Cấu hình RIÊNG của máy này (không đưa lên GitHub, git pull không ghi đè).

Cách dùng trên Pi:
    cp cau_hinh_rieng.vi_du.py cau_hinh_rieng.py
    nano cau_hinh_rieng.py

Chỉ cần ghi những giá trị muốn khác với config.py. Dòng nào không cần thì xoá hoặc để dấu # ở đầu.
"""

# Đường dẫn 2 webcam (ls /dev/v4l/by-path/)
CAMERA = {
    "vao": "/dev/v4l/by-path/platform-xhci-hcd.0-usb-0:2:1.0-video-index0",
    "ra": "/dev/v4l/by-path/platform-xhci-hcd.1-usb-0:2:1.0-video-index0",
}

# Mật khẩu đăng nhập trang web (BẮT BUỘC đổi)
WEB_TAI_KHOAN = "admin"
WEB_MAT_KHAU = "doi-mat-khau-nay"

# Chưa bật mật khẩu MQTT mà muốn ẩn cảnh báo trên web: bỏ dấu # ở dòng dưới
# AN_CANH_BAO_MQTT = True

# Tài khoản MQTT - điền sau khi chạy  sudo bash bat_mat_khau_mqtt.sh
# (phải giống MQTT_USER / MQTT_PASS trong bi_mat.h của 2 ESP32)
# MQTT_USER = "baidoxe"
# MQTT_PASS = "mat-khau-mqtt"
