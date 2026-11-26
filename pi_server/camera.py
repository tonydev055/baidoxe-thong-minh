"""Chụp ảnh từ webcam USB. Nếu không khai báo camera thì tạo ảnh giả lập để hệ thống vẫn chạy."""
import hashlib
import os
import threading
import time
from datetime import datetime, timedelta

import config

try:
    import cv2
    import numpy as np
except ImportError:          # chưa cài opencv: vẫn chạy được ở chế độ giả lập
    cv2 = None
    np = None

os.makedirs(config.THU_MUC_ANH, exist_ok=True)

CHO_KHUNG_GIAY = 1.5         # quẹt thẻ đúng lúc webcam đang mở lại: chờ tối đa bấy nhiêu giây
KHUNG_CU_GIAY = 2            # khung hình cũ hơn mức này coi như webcam đã mất


def bien_gia_lap(uid):
    """Biển số giả cố định theo mã thẻ (dùng khi không có camera) để lúc vào và lúc ra khớp nhau."""
    so = int(hashlib.md5(uid.encode()).hexdigest(), 16) % 100000
    return f"29A{so:05d}"


def _ten_file(cong):
    # Có cả mili-giây: 2 lần chụp trong cùng 1 giây không ghi đè lên nhau
    t = datetime.now()
    return f"{t:%Y%m%d_%H%M%S}_{t.microsecond // 1000:03d}_{cong}.jpg"


def _luu(ten, frame):
    """Ghi ảnh ra đĩa. Trả về tên file, hoặc None nếu ghi lỗi (đầy thẻ nhớ, sai quyền...)."""
    if cv2.imwrite(os.path.join(config.THU_MUC_ANH, ten), frame):
        return ten
    print(f"Khong ghi duoc anh {ten}", flush=True)
    return None


def _anh_gia_lap(cong, ten):
    if cv2 is None:
        return None, None
    frame = np.full((360, 640, 3), 55, np.uint8)
    cv2.putText(frame, "KHONG CO CAMERA", (140, 160), cv2.FONT_HERSHEY_SIMPLEX, 1.1, (200, 200, 200), 2)
    cv2.putText(frame, f"Cong {cong.upper()} - anh gia lap", (150, 210), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                (160, 160, 160), 2)
    return _luu(ten, frame), frame


def _khung_thong_bao(cong):
    frame = np.full((360, 640, 3), 40, np.uint8)
    cv2.putText(frame, f"Cong {cong.upper()}: chua co hinh", (110, 170), cv2.FONT_HERSHEY_SIMPLEX, 1.0,
                (200, 200, 200), 2)
    cv2.putText(frame, "Kiem tra webcam / config.py", (150, 215), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                (150, 150, 150), 2)
    return frame


class _LuongCamera:
    """Giữ webcam mở liên tục và luôn có sẵn khung hình mới nhất -> quẹt thẻ là chụp ngay."""

    def __init__(self, cong, nguon):
        self.cong, self.nguon = cong, nguon
        self.khung, self.luc = None, 0.0
        self.khoa = threading.Lock()
        threading.Thread(target=self._chay, daemon=True).start()

    def _mo(self):
        # Windows: webcam số (0, 1, ...) mở bằng DirectShow nhanh và ổn định hơn
        if os.name == "nt" and isinstance(self.nguon, int):
            cap = cv2.VideoCapture(self.nguon, cv2.CAP_DSHOW)
        else:
            cap = cv2.VideoCapture(self.nguon)
        if cap.isOpened():
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))   # nhẹ băng thông USB
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.CAMERA_RONG)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.CAMERA_CAO)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            print(f"Webcam {self.cong}: da mo", flush=True)
        return cap

    def _chay(self):
        cap = None
        while True:
            if cap is None:
                cap = self._mo()
                if not cap.isOpened():
                    cap.release()           # giải phóng thiết bị mở hỏng trước khi thử lại
                    cap = None
                    print(f"Webcam {self.cong}: khong mo duoc {self.nguon}, thu lai sau 5s", flush=True)
                    time.sleep(5)
                    continue
            ok, frame = cap.read()
            if ok and frame is not None:
                with self.khoa:
                    self.khung, self.luc = frame, time.time()
            else:
                cap.release()
                cap = None
                time.sleep(1)

    def lay(self):
        with self.khoa:
            if self.khung is not None and time.time() - self.luc < KHUNG_CU_GIAY:
                return self.khung.copy()
        return None

    def cho_lay(self, giay=CHO_KHUNG_GIAY):
        """Như lay() nhưng chờ thêm một chút nếu webcam vừa mất hình / đang mở lại."""
        het_han = time.time() + giay
        while True:
            frame = self.lay()
            if frame is not None or time.time() >= het_han:
                return frame
            time.sleep(0.1)


_luong = {}
_khoa_luong = threading.Lock()


def khoi_dong():
    """Mở sẵn các webcam lúc chạy chương trình (gọi nhiều lần, từ nhiều luồng vẫn an toàn)."""
    if cv2 is None or config.GIA_LAP_CAMERA:
        return
    with _khoa_luong:              # tránh 2 request cùng lúc mở 2 luồng cho cùng 1 webcam
        for cong, nguon in config.CAMERA.items():
            if nguon is not None and cong not in _luong:
                _luong[cong] = _LuongCamera(cong, nguon)


def co_camera_that(cong):
    """True nếu cổng này có khai báo webcam thật (không phải chế độ giả lập)."""
    khoi_dong()
    return cong in _luong


def jpeg_truc_tiep(cong, rong=640):
    """Ảnh JPEG mới nhất của webcam (dùng cho khung xem trực tiếp trên web)."""
    if cv2 is None:
        return None
    khoi_dong()
    luong = _luong.get(cong)
    frame = luong.lay() if luong is not None else None
    if frame is None:
        frame = _khung_thong_bao(cong)
    h, w = frame.shape[:2]
    if w > rong:
        frame = cv2.resize(frame, (rong, int(h * rong / w)))
    ok, jpg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 70])
    return jpg.tobytes() if ok else None


def chup(cong):
    """
    Chụp 1 ảnh ở cổng 'vao' hoặc 'ra'.
    Trả về (ten_file | None, frame | None, la_camera_that: bool).

    - Có webcam và có hình     -> (ảnh thật, frame, True)
    - Có webcam nhưng mất hình -> (ảnh thông báo, None, True): KHÔNG dùng biển giả lập,
      biển số sẽ là "không đọc được" để người trông xe sửa tay / kiểm tra lúc ra.
    - Không khai báo webcam    -> (ảnh giả lập, frame, False)
    """
    ten = _ten_file(cong)
    if co_camera_that(cong):
        frame = _luong[cong].cho_lay()
        if frame is not None:
            return _luu(ten, frame), frame, True
        print(f"Webcam {cong}: khong co hinh luc chup", flush=True)
        return _luu(ten, _khung_thong_bao(cong)), None, True

    ten, frame = _anh_gia_lap(cong, ten)
    return ten, frame, False


def xoa_anh(*ten_files):
    """Xoá các file ảnh (bỏ qua tên rỗng / file không còn)."""
    for ten in ten_files:
        if not ten:
            continue
        duong_dan = os.path.join(config.THU_MUC_ANH, os.path.basename(ten))
        try:
            os.remove(duong_dan)
        except FileNotFoundError:
            pass
        except OSError as e:
            print(f"Khong xoa duoc anh {ten}: {e}", flush=True)


def xoa_anh_cu(so_ngay, giu_lai=()):
    """Xoá ảnh cũ hơn so_ngay ngày để không đầy thẻ nhớ. Không xoá các file trong giu_lai."""
    if not so_ngay or so_ngay <= 0:
        return 0
    moc = (datetime.now() - timedelta(days=so_ngay)).timestamp()
    giu_lai = set(giu_lai)
    da_xoa = 0
    for ten in os.listdir(config.THU_MUC_ANH):
        if not ten.lower().endswith(".jpg") or ten in giu_lai:
            continue
        duong_dan = os.path.join(config.THU_MUC_ANH, ten)
        try:
            if os.path.getmtime(duong_dan) < moc:
                os.remove(duong_dan)
                da_xoa += 1
        except OSError:
            pass
    return da_xoa
