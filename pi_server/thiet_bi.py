"""
Theo dõi tình trạng từng thiết bị và báo lỗi.

- 2 ESP32 tự kiểm tra rồi gửi báo cáo:  baidoxe/tb/<cong|o_do>         (JSON, retain)
  và trạng thái online (LWT):           baidoxe/tb/<cong|o_do>/online  ("1" / "0", retain)
- Pi tự kiểm tra: kết nối MQTT, 2 webcam, EasyOCR.
- Có thay đổi (hỏng / hết hỏng) thì ghi vào Nhật ký; trang web hiện bảng "Tình trạng thiết bị".
"""
import json
import threading
import time
import traceback

import camera
import db
import ocr

TEN_ESP = {"cong": "ESP32 cổng (số 2)", "o_do": "ESP32 ô đỗ (số 1)"}

# Các mục ESP32 tự đo được: (khoá trong báo cáo, tên hiển thị, cách xử lý khi lỗi)
MUC = {
    "cong": [
        ("rc522_vao", "Đầu đọc thẻ cổng vào (RC522)",
         "Kiểm tra dây SDA(5) SCK(18) MOSI(23) MISO(19) RST(22) và nguồn 3.3V (KHÔNG cấp 5V)"),
        ("rc522_ra", "Đầu đọc thẻ cổng ra (RC522)",
         "Kiểm tra dây SDA(4) SCK(18) MOSI(23) MISO(19) RST(22) và nguồn 3.3V (KHÔNG cấp 5V)"),
        ("sa_vao", "Cảm biến siêu âm cổng vào", "Kiểm tra dây TRIG(25) ECHO(34) và nguồn 5V"),
        ("sa_ra", "Cảm biến siêu âm cổng ra", "Kiểm tra dây TRIG(26) ECHO(35) và nguồn 5V"),
    ],
    "o_do": [
        ("sa1", "Cảm biến ô 1", "Kiểm tra dây TRIG(25) ECHO(34) và nguồn 5V"),
        ("sa2", "Cảm biến ô 2", "Kiểm tra dây TRIG(26) ECHO(35) và nguồn 5V"),
        ("sa3", "Cảm biến ô 3", "Kiểm tra dây TRIG(27) ECHO(36) và nguồn 5V"),
        ("sa4", "Cảm biến ô 4", "Kiểm tra dây TRIG(14) ECHO(39) và nguồn 5V"),
        ("oled", "Màn hình OLED", "Kiểm tra dây SDA(21) SCL(22) và nguồn 3.3V"),
    ],
}

# Các thiết bị KHÔNG có tín hiệu phản hồi nên không tự đo được -> hướng dẫn kiểm tra bằng mắt
KHONG_DO_DUOC = {
    "cong": [
        ("Servo thanh chắn (vào / ra)",
         "Không tự đo được. Web báo 'Đang mở' mà thanh chắn không nâng: servo thiếu nguồn 5V hoặc lỏng dây tín hiệu (32 / 33)"),
        ("Còi (vào / ra)",
         "Không tự đo được. Lúc bật ESP32 mỗi cổng phải kêu 1 tiếng (3 tiếng = đầu đọc thẻ lỗi). Không kêu: còi lỏng dây (27 / 17) hoặc hỏng"),
    ],
    "o_do": [
        ("LED các ô",
         "Không tự đo được. Xanh = trống, đỏ = có xe, xanh/đỏ nháy luân phiên = cảm biến ô đó lỗi. Không sáng: LED lỏng dây / cắm ngược"),
    ],
}

LY_DO_KHOI_DONG = {
    "bat_nguon": "vừa cấp điện",
    "sut_nguon": "SỤT NGUỒN – thường do servo kéo dòng: cấp nguồn 5V riêng cho servo, nối chung GND",
    "loi_chuong_trinh": "lỗi chương trình (panic)",
    "bi_treo": "chương trình bị treo (watchdog)",
    "khoi_dong_lai": "khởi động lại bằng phần mềm",
    "nut_reset": "bấm nút EN / RST",
    "khac": "không rõ",
}
LY_DO_XAU = {"sut_nguon", "loi_chuong_trinh", "bi_treo"}

CAM = {
    "ok": ("ok", "Đang có hình"),
    "loi": ("loi", "Không có hình: kiểm tra cáp USB webcam, đường dẫn CAMERA trong config.py (chạy kiem_tra_cam.py)"),
    "gia_lap": ("khong_do", "Chế độ giả lập (BAIDOXE_GIA_LAP=1) – không dùng webcam"),
    "khong_khai_bao": ("khong_do", "Không khai báo trong config.py – dùng ảnh và biển số giả lập"),
    "khong_opencv": ("loi", "Chưa cài opencv: pip install opencv-python-headless"),
}
OCR = {
    "ok": ("ok", "EasyOCR sẵn sàng"),
    "dang_nap": ("canh_bao", "Đang nạp mô hình (10–30 giây sau khi chạy app.py)"),
    "chua_nap": ("canh_bao", "Chưa nạp mô hình"),
    "chua_cai": ("loi", "Không dùng được EasyOCR (pip install easyocr) – biển số sẽ là 'không đọc được', sửa tay trên web"),
}

BAO_CAO_CU_GIAY = 120         # ESP32 gửi 30 giây/lần; quá mức này coi như mất liên lạc

_khoa = threading.Lock()
_esp = {k: {"online": None, "bc": None, "luc": 0.0} for k in TEN_ESP}


def _ghi(noi_dung, muc="info"):
    try:
        db.ghi_nhat_ky(noi_dung, muc)
    except Exception:
        traceback.print_exc()


# ---------------- Nhận tin từ ESP32 ----------------
def nhan(ten, phan_cuoi, noi_dung):
    """Gọi từ on_message cho topic baidoxe/tb/<ten> và baidoxe/tb/<ten>/online."""
    if ten not in _esp:
        return
    ten_hien = TEN_ESP[ten]
    with _khoa:
        tt = _esp[ten]
        if phan_cuoi == "online":
            moi = noi_dung == "1"
            cu, tt["online"] = tt["online"], moi
            if moi and cu is False:
                _ghi(f"{ten_hien}: đã kết nối lại")
            elif not moi and cu is not False:
                _ghi(f"{ten_hien}: MẤT KẾT NỐI – kiểm tra nguồn điện, WiFi, MQTT_HOST", "canh_bao")
            return

        try:
            bc = json.loads(noi_dung)
        except ValueError:
            return
        cu = tt["bc"]
        tt["bc"], tt["luc"] = bc, time.time()
        if tt["online"] is None:
            tt["online"] = True

    # Vừa khởi động lại? (lần đầu nhận báo cáo, hoặc uptime giảm)
    uptime = bc.get("uptime", 0)
    if (cu is None and uptime < 120) or (cu is not None and uptime < cu.get("uptime", 0)):
        ly_do = bc.get("reset", "khac")
        _ghi(f"{ten_hien} vừa khởi động – lý do: {LY_DO_KHOI_DONG.get(ly_do, ly_do)}",
             "canh_bao" if ly_do in LY_DO_XAU else "info")

    for khoa, ten_muc, cach_sua in MUC[ten]:
        if khoa not in bc:
            continue
        ok = bool(bc[khoa])
        truoc = None if cu is None or khoa not in cu else bool(cu[khoa])
        if not ok and truoc is not False:
            _ghi(f"LỖI {ten_muc}: {cach_sua}", "canh_bao")
        elif ok and truoc is False:
            _ghi(f"{ten_muc}: đã bình thường lại")


# ---------------- Tổng hợp cho trang web ----------------
def _dong(nhom, ten, tt, ghi_chu=""):
    return {"nhom": nhom, "ten": ten, "tt": tt, "ghi_chu": ghi_chu}


def _ds_esp(ten, mqtt_ok):
    nhom = TEN_ESP[ten]
    with _khoa:
        online, bc, luc = _esp[ten]["online"], _esp[ten]["bc"], _esp[ten]["luc"]

    ds = []
    con_song = False
    if not mqtt_ok:
        ds.append(_dong(nhom, nhom, "chua_ro", "Pi chưa nối được MQTT broker nên chưa biết"))
    elif online is None and bc is None:
        ds.append(_dong(nhom, nhom, "chua_ro",
                        "Chưa nhận được báo cáo: ESP32 chưa bật, sai WiFi / MQTT_HOST, "
                        "hoặc chưa nạp code mới có tự kiểm tra"))
    elif online is False:
        ds.append(_dong(nhom, nhom, "loi", "MẤT KẾT NỐI – kiểm tra nguồn điện, WiFi, MQTT_HOST"))
    elif bc is not None and time.time() - luc > BAO_CAO_CU_GIAY:
        ds.append(_dong(nhom, nhom, "canh_bao",
                        f"Không nhận được báo cáo {int(time.time() - luc) // 60} phút – ESP32 có thể bị treo"))
    else:
        con_song = True
        tt, ghi_chu = "ok", "Đang kết nối"
        if bc:
            ly_do = bc.get("reset", "khac")
            phut = int(bc.get("uptime", 0)) // 60
            rssi = bc.get("rssi", 0)
            ghi_chu = f"Chạy được {phut} phút, sóng WiFi {rssi} dBm"
            if ly_do in LY_DO_XAU:
                tt, ghi_chu = "canh_bao", f"Lần khởi động gần nhất do {LY_DO_KHOI_DONG[ly_do]}. {ghi_chu}"
            elif rssi and rssi < -80:
                tt, ghi_chu = "canh_bao", f"{ghi_chu} – sóng yếu, đưa ESP32 lại gần bộ phát WiFi"
        ds.append(_dong(nhom, nhom, tt, ghi_chu))

    for khoa, ten_muc, cach_sua in MUC[ten]:
        if not bc or khoa not in bc:
            ds.append(_dong(nhom, ten_muc, "chua_ro", "Chưa có báo cáo"))
        elif not con_song:
            ds.append(_dong(nhom, ten_muc, "chua_ro",
                            "Lần cuối: " + ("bình thường" if bc[khoa] else "LỖI") + " (ESP32 đang mất liên lạc)"))
        elif bc[khoa]:
            ghi_chu = f"Chip phiên bản {bc.get('ver_' + khoa[6:], '?')}" if khoa.startswith("rc522") else ""
            ds.append(_dong(nhom, ten_muc, "ok", ghi_chu))
        else:
            ds.append(_dong(nhom, ten_muc, "loi", cach_sua))

    for ten_muc, ghi_chu in KHONG_DO_DUOC[ten]:
        ds.append(_dong(nhom, ten_muc, "khong_do", ghi_chu))
    return ds


def danh_sach(mqtt_ok):
    """Danh sách mọi thiết bị: [{nhom, ten, tt: ok|loi|canh_bao|chua_ro|khong_do, ghi_chu}]"""
    ds = [_dong("Raspberry Pi", "Kết nối MQTT broker", "ok" if mqtt_ok else "loi",
                "" if mqtt_ok else "Kiểm tra Mosquitto: sudo systemctl status mosquitto")]
    for cong, ten in (("vao", "Webcam cổng vào"), ("ra", "Webcam cổng ra")):
        tt, ghi_chu = CAM[camera.trang_thai(cong)]
        ds.append(_dong("Raspberry Pi", ten, tt, ghi_chu))
    tt, ghi_chu = OCR[ocr.trang_thai()]
    ds.append(_dong("Raspberry Pi", "Đọc biển số (EasyOCR)", tt, ghi_chu))
    ds += _ds_esp("cong", mqtt_ok)
    ds += _ds_esp("o_do", mqtt_ok)
    return ds


def o_cam_bien_loi():
    """Các ô (1..4) đang có cảm biến lỗi - để sơ đồ ô đỗ hiện 'Lỗi cảm biến'."""
    with _khoa:
        bc, online = _esp["o_do"]["bc"], _esp["o_do"]["online"]
    if not bc or online is False:
        return []
    return [i for i in range(1, 5) if f"sa{i}" in bc and not bc[f"sa{i}"]]


# ---------------- Theo dõi phía Pi (webcam, MQTT) ----------------
def _theo_doi(lay_mqtt_ok):
    truoc = {}
    while True:
        try:
            hien = {"mqtt": lay_mqtt_ok(), "vao": camera.trang_thai("vao"), "ra": camera.trang_thai("ra")}
            for khoa, gia_tri in hien.items():
                if khoa not in truoc or truoc[khoa] == gia_tri:
                    continue
                if khoa == "mqtt":
                    if gia_tri:
                        _ghi("Raspberry Pi: đã kết nối lại MQTT broker")
                    else:
                        _ghi("Raspberry Pi: MẤT KẾT NỐI MQTT broker – kiểm tra Mosquitto", "canh_bao")
                else:
                    ten = "Webcam cổng vào" if khoa == "vao" else "Webcam cổng ra"
                    if gia_tri == "loi":
                        _ghi(f"LỖI {ten}: {CAM['loi'][1]}", "canh_bao")
                    elif gia_tri == "ok" and truoc[khoa] == "loi":
                        _ghi(f"{ten}: đã có hình lại")
            truoc = hien
        except Exception:
            traceback.print_exc()
        time.sleep(5)


def bat_dau(lay_mqtt_ok):
    """Chạy luồng nền theo dõi webcam + MQTT (đợi 15 giây cho webcam mở xong)."""
    def chay():
        time.sleep(15)
        _theo_doi(lay_mqtt_ok)
    threading.Thread(target=chay, daemon=True).start()
