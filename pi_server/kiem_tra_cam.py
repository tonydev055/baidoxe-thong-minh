"""
Chụp thử các webcam số 0..4, ghép thành 1 ảnh có ghi số để biết webcam nào là số mấy.
Chạy:  python kiem_tra_cam.py   -> mở file kiem_tra_cam.jpg
Sau đó điền vào config.py:  CAMERA = {"vao": <số webcam cổng vào>, "ra": <số webcam cổng ra>}
"""
import os
import cv2
import numpy as np

anh = []
for so in range(5):
    cap = cv2.VideoCapture(so, cv2.CAP_DSHOW) if os.name == "nt" else cv2.VideoCapture(so)
    if not cap.isOpened():
        continue
    frame = None
    for _ in range(8):                       # bỏ vài khung đầu cho webcam tự chỉnh sáng
        ok, f = cap.read()
        if ok:
            frame = f
    cap.release()
    if frame is None:
        continue
    frame = cv2.resize(frame, (480, 270))
    cv2.rectangle(frame, (0, 0), (210, 56), (0, 0, 0), -1)
    cv2.putText(frame, f"So {so}", (10, 44), cv2.FONT_HERSHEY_SIMPLEX, 1.5, (0, 255, 255), 3)
    anh.append(frame)
    print(f"Webcam so {so}: OK")

if not anh:
    print("Khong thay webcam nao.")
else:
    while len(anh) % 2:
        anh.append(np.zeros_like(anh[0]))
    hang = [np.hstack(anh[i:i + 2]) for i in range(0, len(anh), 2)]
    duong_dan = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kiem_tra_cam.jpg")
    cv2.imwrite(duong_dan, np.vstack(hang))
    print("Da luu:", duong_dan)
