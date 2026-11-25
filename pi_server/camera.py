"""Chụp ảnh từ webcam USB. Nếu không có camera thì tạo ảnh giả lập để hệ thống vẫn chạy."""
import hashlib
import os
import threading
import time
from datetime import datetime

import config

try:
    import cv2
    import numpy as np
except ImportError:          # chưa cài opencv: vẫn chạy được ở chế độ giả lập
    cv2 = None
    np = None

os.makedirs(config.THU_MUC_ANH, exist_ok=True)


def bien_gia_lap(uid):
    """Biển số giả cố định theo mã thẻ (dùng khi không có camera) để lúc vào và lúc ra khớp nhau."""
    so = int(hashlib.md5(uid.encode()).hexdigest(), 16) % 100000
    return f"29A{so:05d}"


def _ten_file(cong):
    return f"{datetime.now():%Y%m%d_%H%M%S}_{cong}.jpg"


def _anh_gia_lap(cong, ten):
    if cv2 is None:
        return None, None
    frame = np.full((360, 640, 3), 55, np.uint8)
    cv2.putText(frame, "KHONG CO CAMERA", (140, 160), cv2.FONT_HERSHEY_SIMPLEX, 1.1, (200, 200, 200), 2)
    cv2.putText(frame, f"Cong {cong.upper()} - anh gia lap", (150, 210), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                (160, 160, 160), 2)
    cv2.imwrite(os.path.join(config.THU_MUC_ANH, ten), frame)
    return ten, frame


class _LuongCamera:
    """Giữ webcam mở liên tục và luôn có sẵn khung hình mới nhất -> quẹt thẻ là chụp ngay."""

    def __init__(self, cong, nguon):
        self.cong, self.nguon = cong, nguon
        self.khung, self.luc = None, 0.0
        self.khoa = threading.Lock()
        threading.Thread(target=self._chay, daemon=True).start()

    def _mo(self):
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
            if cap is None or not cap.isOpened():
                cap = self._mo()
                if not cap.isOpened():
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
            if self.khung is not None and time.time() - self.luc < 2:
                return self.khung.copy()
        return None


_luong = {}


def khoi_dong():
    """Mở sẵn các webcam lúc chạy chương trình."""
    if cv2 is None or config.GIA_LAP_CAMERA:
        return
    for cong, nguon in config.CAMERA.items():
        if nguon is not None and cong not in _luong:
            _luong[cong] = _LuongCamera(cong, nguon)


def chup(cong):
    """
    Chụp 1 ảnh ở cổng 'vao' hoặc 'ra'.
    Trả về (ten_file | None, frame | None, la_camera_that: bool).
    """
    ten = _ten_file(cong)
    khoi_dong()
    luong = _luong.get(cong)
    if luong is not None:
        frame = luong.lay()
        if frame is not None:
            cv2.imwrite(os.path.join(config.THU_MUC_ANH, ten), frame)
            return ten, frame, True

    ten, frame = _anh_gia_lap(cong, ten)
    return ten, frame, False
