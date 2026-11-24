"""Đọc biển số từ ảnh bằng EasyOCR (nếu đã cài)."""
import re
import threading

import config

try:
    import cv2
except ImportError:
    cv2 = None

_reader = None
_khoa = threading.Lock()
_da_thu = False

CHU_CHO_PHEP = "ABCDEFGHKLMNPRSTUVXYZ0123456789"
MAU_BIEN = re.compile(r"\d{2}[A-Z]{1,2}\d{4,5}")     # vd: 29A12345, 30LD1234


def chuan_hoa(bien):
    return re.sub(r"[^A-Z0-9]", "", (bien or "").upper())


def _lay_reader():
    global _reader, _da_thu
    with _khoa:
        if _reader is None and not _da_thu:
            _da_thu = True
            try:
                import easyocr
                print("Dang nap EasyOCR (lan dau mat 10-30 giay)...", flush=True)
                _reader = easyocr.Reader(["en"], gpu=False)
                print("EasyOCR san sang", flush=True)
            except Exception as e:     # chưa cài hoặc lỗi -> bỏ qua OCR
                print(f"Khong dung duoc EasyOCR: {e}", flush=True)
        return _reader


def nap_truoc():
    """Gọi lúc khởi động để lần quẹt thẻ đầu tiên không phải chờ nạp mô hình."""
    threading.Thread(target=_lay_reader, daemon=True).start()


def _cat_vung(frame, vung):
    if vung is None:
        return frame
    h, w = frame.shape[:2]
    x, y, rw, rh = vung
    return frame[int(y * h):int((y + rh) * h), int(x * w):int((x + rw) * w)]


def doc_bien_so(frame, cong):
    """Trả về chuỗi biển số đã chuẩn hoá (vd '29A12345') hoặc None nếu không đọc được."""
    reader = _lay_reader()
    if reader is None or frame is None or cv2 is None:
        return None
    anh = _cat_vung(frame, config.VUNG_BIEN_SO.get(cong))
    xam = cv2.cvtColor(anh, cv2.COLOR_BGR2GRAY)
    try:
        doan = reader.readtext(xam, detail=0, allowlist=CHU_CHO_PHEP + "-.")
    except Exception as e:
        print(f"Loi OCR: {e}", flush=True)
        return None
    chuoi = chuan_hoa("".join(doan))
    khop = MAU_BIEN.search(chuoi)
    if khop:
        return khop.group(0)
    return chuoi or None
