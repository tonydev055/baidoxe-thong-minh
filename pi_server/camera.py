"""Chụp ảnh từ webcam USB. Nếu không có camera thì tạo ảnh giả lập để hệ thống vẫn chạy."""
import hashlib
import os
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


def chup(cong):
    """
    Chụp 1 ảnh ở cổng 'vao' hoặc 'ra'.
    Trả về (ten_file | None, frame | None, la_camera_that: bool).
    """
    ten = _ten_file(cong)
    nguon = config.CAMERA.get(cong)

    if cv2 is not None and nguon is not None and not config.GIA_LAP_CAMERA:
        cap = cv2.VideoCapture(nguon)
        try:
            if cap.isOpened():
                cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.CAMERA_RONG)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.CAMERA_CAO)
                for _ in range(5):          # bỏ vài khung đầu để camera tự chỉnh sáng
                    cap.read()
                ok, frame = cap.read()
                if ok and frame is not None:
                    cv2.imwrite(os.path.join(config.THU_MUC_ANH, ten), frame)
                    return ten, frame, True
        finally:
            cap.release()

    ten, frame = _anh_gia_lap(cong, ten)
    return ten, frame, False
