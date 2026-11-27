"""
Cấu hình hệ thống bãi đỗ xe - chạy trên Raspberry Pi 5.

KHÔNG sửa file này trên Pi (git pull sẽ báo lỗi). Muốn đổi giá trị nào thì
tạo file cau_hinh_rieng.py (chép từ cau_hinh_rieng.vi_du.py) và ghi đè ở đó:
đường dẫn webcam, mật khẩu web, mật khẩu MQTT... File đó không đưa lên GitHub.
"""
import os

# ---------------- MQTT (Mosquitto chạy trên chính Pi) ----------------
MQTT_HOST = os.environ.get("BAIDOXE_MQTT_HOST", "localhost")
MQTT_PORT = 1883
# Tài khoản MQTT. Để trống nếu broker cho phép ẩn danh (allow_anonymous true).
# Bật mật khẩu cho Mosquitto: chạy  sudo bash bat_mat_khau_mqtt.sh  (xem HUONG_DAN_PI.md)
MQTT_USER = ""
MQTT_PASS = ""

# ---------------- Bãi đỗ ----------------
SO_O = 4                # số ô đỗ

# ---------------- Camera ----------------
# Đường dẫn webcam cổng vào / cổng ra.
# Nên dùng đường dẫn theo cổng USB (không đổi sau khi khởi động lại):
#   ls /dev/v4l/by-path/   -> chọn các dòng kết thúc bằng "video-index0"
# Ví dụ: "/dev/v4l/by-path/platform-xhci-hcd.0-usb-0:1:1.0-video-index0"
# Chạy thử trên laptop Windows: dùng số webcam (0, 1, ...) - chạy kiem_tra_cam.py để biết số.
# Đặt None cho cổng không có webcam (cổng đó sẽ dùng ảnh + biển số giả lập).
if os.name == "nt":
    CAMERA = {
        "vao": 0,
        "ra": 1,
    }
else:
    CAMERA = {
        "vao": "/dev/video0",
        "ra": "/dev/video2",
    }
CAMERA_RONG = 1280
CAMERA_CAO = 720

# Vùng chứa biển số trong ảnh (tỉ lệ 0..1): (x, y, rộng, cao). None = dùng cả ảnh.
# Ví dụ biển số nằm ở giữa, nửa dưới ảnh: (0.25, 0.45, 0.5, 0.4)
VUNG_BIEN_SO = {
    "vao": None,
    "ra": None,
}

# Đặt biến môi trường BAIDOXE_GIA_LAP=1 để bỏ qua camera thật (thử trên máy tính).
GIA_LAP_CAMERA = os.environ.get("BAIDOXE_GIA_LAP", "0") == "1"

# ---------------- Tính tiền ----------------
# Demo: tính theo PHÚT để thấy tiền tăng nhanh. Chạy thật: đổi DON_VI_PHUT = 60 (theo giờ).
DON_VI_PHUT = 1
GIA_LUOT_DAU = 10000    # tiền cho đơn vị thời gian đầu tiên
GIA_LUOT_TIEP = 5000    # tiền cho mỗi đơn vị tiếp theo (chưa đủ vẫn tính tròn)

# ---------------- Thanh chắn ----------------
# Thanh chắn chỉ hạ khi xe đã đi qua cảm biến. Quá số giây này (tính từ lúc mở cổng) mà xe
# chưa qua thì trang web hỏi nhân viên:
#   cổng vào: xoá thẻ (xe không vào nữa) hay chờ tiếp
#   cổng ra : xe vẫn ra (giữ cổng mở) hay không ra (đóng cổng, xe vẫn trong bãi)
CHO_XAC_NHAN_VAO_GIAY = 30
CHO_XAC_NHAN_RA_GIAY = 15

# Biển số lúc ra được coi là khớp nếu giống lúc vào từ mức này trở lên (0..1).
# 1.0 = phải GIỐNG HỆT. Khác dù chỉ 1 ký tự (29A12345 / 29A12346) cũng không tự mở,
# mà chuyển người trông xe xem 2 ảnh rồi bấm "Cho ra" / "Không cho ra".
# (Hạ xuống 0.9 nếu OCR hay đọc sai 1 ký tự và chấp nhận rủi ro cho nhầm xe.)
NGUONG_GIONG_BIEN = 1.0

# ---------------- Web & lưu trữ ----------------
WEB_PORT = 5000
# Tài khoản đăng nhập trang web quản lý. ĐỔI mật khẩu trong cau_hinh_rieng.py.
# Còn để mật khẩu mặc định thì trang web và Nhật ký sẽ nhắc.
WEB_TAI_KHOAN = "admin"
WEB_MAT_KHAU = os.environ.get("BAIDOXE_WEB_MAT_KHAU", "baidoxe123")
WEB_MAT_KHAU_MAC_DINH = "baidoxe123"
WEB_GIU_DANG_NHAP_GIO = 12          # đăng nhập 1 lần dùng được bấy nhiêu giờ

THU_MUC_GOC = os.path.dirname(os.path.abspath(__file__))
THU_MUC_ANH = os.path.join(THU_MUC_GOC, "anh")
GIU_ANH_NGAY = 30       # tự xoá ảnh cũ hơn bấy nhiêu ngày (0 = không bao giờ xoá)
DB_FILE = os.path.join(THU_MUC_GOC, "baidoxe.db")
KHOA_BI_MAT_FILE = os.path.join(THU_MUC_GOC, "khoa_bi_mat.txt")   # khoá ký phiên đăng nhập (tự tạo)

# ---------------- Ghi đè bằng cấu hình riêng (không đưa lên GitHub) ----------------
try:
    from cau_hinh_rieng import *  # noqa: F401,F403
except ImportError:
    pass
