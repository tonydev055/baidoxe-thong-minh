"""
CHƯƠNG TRÌNH CHÍNH - BÃI ĐỖ XE THÔNG MINH (chạy trên Raspberry Pi 5)

- Nhận tin MQTT từ 2 ESP32 (trạng thái ô đỗ, quẹt thẻ ở cổng)
- Quẹt thẻ -> chụp webcam -> đọc biển số -> lưu SQLite -> gửi lệnh mở / từ chối
- Xe ra: so biển số lúc vào và lúc ra, tính tiền
- Trang web quản lý: http://<IP-cua-Pi>:5000

Chạy:  python app.py
"""
import math
import threading
import time
from datetime import datetime
from difflib import SequenceMatcher

import paho.mqtt.client as mqtt
from flask import Flask, Response, abort, jsonify, render_template, request, send_from_directory

import camera
import config
import db
import ocr

app = Flask(__name__)

trang_thai = {
    "o": {i: False for i in range(1, config.SO_O + 1)},   # True = có xe
    "cong": {"vao": "dong", "ra": "dong"},
    "mqtt": False,
}
khoa_cong = {"vao": threading.Lock(), "ra": threading.Lock()}
client = None


# ======================= TIỆN ÍCH =======================
def bay_gio():
    return datetime.now().replace(microsecond=0)


def dinh_dang(t):
    return t.isoformat(sep=" ")


def doc_gio(chuoi):
    return datetime.fromisoformat(chuoi)


def giong_nhau(a, b):
    a, b = ocr.chuan_hoa(a), ocr.chuan_hoa(b)
    if not a or not b:
        return False
    return a == b or SequenceMatcher(None, a, b).ratio() >= config.NGUONG_GIONG_BIEN


def tinh_tien(gio_vao, gio_ra, loai):
    if loai == "thang":
        return 0
    giay = max(0, (gio_ra - gio_vao).total_seconds())
    so_don_vi = max(1, math.ceil(giay / (config.DON_VI_PHUT * 60)))
    return config.GIA_LUOT_DAU + (so_don_vi - 1) * config.GIA_LUOT_TIEP


def tien_vn(so):
    return f"{so:,}".replace(",", ".") + "đ"


def gui_lenh(cong, lenh):
    if client is not None:
        client.publish(f"baidoxe/lenh/{cong}", lenh)


def so_cho_trong():
    co_xe = sum(1 for v in trang_thai["o"].values() if v)
    xe_trong_bai = len(db.ds_theo_trang_thai("trong_bai")) + len(db.ds_theo_trang_thai("cho_kiem_tra"))
    return max(0, config.SO_O - max(co_xe, xe_trong_bai))


def chup_va_doc(cong, uid):
    ten_anh, frame, that = camera.chup(cong)
    bien = ocr.doc_bien_so(frame, cong) if that else camera.bien_gia_lap(uid)
    return ten_anh, bien


# ======================= XỬ LÝ XE VÀO / RA =======================
def xu_ly_vao(uid):
    with khoa_cong["vao"]:
        if db.phien_hien_tai(uid):
            db.ghi_nhat_ky(f"Cổng vào: thẻ {uid} đang có xe trong bãi, từ chối", "canh_bao")
            gui_lenh("vao", "tuchoi")
            return
        if so_cho_trong() <= 0:
            db.ghi_nhat_ky(f"Cổng vào: bãi đã đầy, từ chối thẻ {uid}", "canh_bao")
            gui_lenh("vao", "tuchoi")
            return

        loai = "thang" if db.lay_the_thang(uid) else "luot"
        ten_anh, bien = chup_va_doc("vao", uid)
        db.tao_phien(uid, loai, bien, ten_anh, dinh_dang(bay_gio()))
        gui_lenh("vao", "mo")
        db.ghi_nhat_ky(f"Xe VÀO: thẻ {uid}, biển {bien or 'không đọc được'}"
                       f"{' (thẻ tháng)' if loai == 'thang' else ''}")


def xu_ly_ra(uid):
    with khoa_cong["ra"]:
        phien = db.phien_hien_tai(uid)
        if not phien:
            db.ghi_nhat_ky(f"Cổng ra: thẻ {uid} không có xe trong bãi, từ chối", "canh_bao")
            gui_lenh("ra", "tuchoi")
            return

        ten_anh, bien = chup_va_doc("ra", uid)
        gio_ra = bay_gio()

        if giong_nhau(phien["bien_vao"], bien):
            tien = tinh_tien(doc_gio(phien["gio_vao"]), gio_ra, phien["loai"])
            db.ket_thuc_phien(phien["id"], bien, ten_anh, dinh_dang(gio_ra), tien)
            gui_lenh("ra", "mo")
            db.ghi_nhat_ky(f"Xe RA: thẻ {uid}, biển {bien}, thu {tien_vn(tien)}")
        else:
            db.cho_kiem_tra(phien["id"], bien, ten_anh, dinh_dang(gio_ra))
            gui_lenh("ra", "tuchoi")
            db.ghi_nhat_ky(f"Cổng ra: biển KHÔNG KHỚP (vào {phien['bien_vao'] or '?'} / ra {bien or '?'}), "
                           f"chờ người trông xe kiểm tra", "canh_bao")


def chay_nen(ham, *args):
    threading.Thread(target=ham, args=args, daemon=True).start()


# ======================= MQTT =======================
def on_connect(cl, userdata, flags, reason_code, properties=None):
    trang_thai["mqtt"] = (reason_code == 0)
    if reason_code == 0:
        cl.subscribe([("baidoxe/o/+", 0), ("baidoxe/the/+", 0), ("baidoxe/cong/+", 0)])
        db.ghi_nhat_ky("Đã kết nối MQTT broker")


def on_disconnect(cl, userdata, flags, reason_code, properties=None):
    trang_thai["mqtt"] = False


def on_message(cl, userdata, msg):
    phan = msg.topic.split("/")
    noi_dung = msg.payload.decode(errors="ignore").strip()
    if len(phan) != 3:
        return
    _, loai, ten = phan

    if loai == "o" and ten.isdigit():
        so = int(ten)
        if so in trang_thai["o"]:
            trang_thai["o"][so] = (noi_dung == "1")
    elif loai == "the" and noi_dung:
        if ten == "vao":
            chay_nen(xu_ly_vao, noi_dung)
        elif ten == "ra":
            chay_nen(xu_ly_ra, noi_dung)
    elif loai == "cong" and ten in trang_thai["cong"]:
        trang_thai["cong"][ten] = noi_dung


def khoi_dong_mqtt():
    global client
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="pi-baidoxe")
    if config.MQTT_USER:
        client.username_pw_set(config.MQTT_USER, config.MQTT_PASS)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=10)
    client.connect_async(config.MQTT_HOST, config.MQTT_PORT, keepalive=30)
    client.loop_start()


# ======================= WEB =======================
def them_tam_tinh(phien):
    vao = doc_gio(phien["gio_vao"])
    phien["phut"] = int((bay_gio() - vao).total_seconds() // 60)
    phien["tam_tinh"] = tinh_tien(vao, bay_gio(), phien["loai"])
    return phien


@app.route("/")
def trang_chu():
    return render_template("index.html", so_o=config.SO_O)


@app.route("/api/state")
def api_state():
    luot, tien = db.thong_ke_hom_nay()
    return jsonify({
        "o": [{"so": k, "co_xe": v} for k, v in sorted(trang_thai["o"].items())],
        "tong": config.SO_O,
        "cho_trong": so_cho_trong(),
        "cong": trang_thai["cong"],
        "mqtt": trang_thai["mqtt"],
        "trong_bai": [them_tam_tinh(p) for p in db.ds_theo_trang_thai("trong_bai")],
        "cho_kiem_tra": db.ds_theo_trang_thai("cho_kiem_tra"),
        "lich_su": db.lich_su(50),
        "luot_hom_nay": luot,
        "doanh_thu_hom_nay": tien,
        "nhat_ky": db.nhat_ky_moi(20),
        "the_thang": db.ds_the_thang(),
        "gia": {"don_vi_phut": config.DON_VI_PHUT, "dau": config.GIA_LUOT_DAU, "tiep": config.GIA_LUOT_TIEP},
    })


@app.route("/video/<cong>")
def video(cong):
    """Xem webcam trực tiếp (MJPEG, khoảng 8 hình/giây)."""
    if cong not in ("vao", "ra") or camera.jpeg_truc_tiep(cong) is None:
        abort(404)

    def tao_luong():
        while True:
            jpg = camera.jpeg_truc_tiep(cong)
            if jpg:
                yield b"--khung\r\nContent-Type: image/jpeg\r\n\r\n" + jpg + b"\r\n"
            time.sleep(0.12)

    return Response(tao_luong(), mimetype="multipart/x-mixed-replace; boundary=khung")


@app.route("/anh/<path:ten>")
def xem_anh(ten):
    return send_from_directory(config.THU_MUC_ANH, ten)


@app.post("/api/mo_cong/<cong>")
def api_mo_cong(cong):
    if cong not in ("vao", "ra"):
        abort(404)
    gui_lenh(cong, "mo")
    db.ghi_nhat_ky(f"Mở cổng {cong} bằng tay từ trang web")
    return jsonify(ok=True)


@app.post("/api/cho_ra/<int:phien_id>")
def api_cho_ra(phien_id):
    phien = db.lay_phien(phien_id)
    if not phien or phien["trang_thai"] != "cho_kiem_tra":
        abort(404)
    tien = tinh_tien(doc_gio(phien["gio_vao"]), doc_gio(phien["gio_ra"]), phien["loai"])
    db.ket_thuc_phien(phien_id, phien["bien_ra"], phien["anh_ra"], phien["gio_ra"], tien)
    gui_lenh("ra", "mo")
    db.ghi_nhat_ky(f"Người trông xe cho thẻ {phien['uid']} ra, thu {tien_vn(tien)}")
    return jsonify(ok=True)


@app.post("/api/huy_ra/<int:phien_id>")
def api_huy_ra(phien_id):
    phien = db.lay_phien(phien_id)
    if not phien or phien["trang_thai"] != "cho_kiem_tra":
        abort(404)
    db.huy_kiem_tra(phien_id)
    db.ghi_nhat_ky(f"Không cho thẻ {phien['uid']} ra (biển không khớp)", "canh_bao")
    return jsonify(ok=True)


@app.post("/api/sua_bien/<int:phien_id>")
def api_sua_bien(phien_id):
    bien = ocr.chuan_hoa((request.get_json(silent=True) or {}).get("bien", ""))
    if not bien or not db.lay_phien(phien_id):
        abort(400)
    db.sua_bien_vao(phien_id, bien)
    db.ghi_nhat_ky(f"Sửa biển số lượt #{phien_id} thành {bien}")
    return jsonify(ok=True)


@app.post("/api/the")
def api_them_the():
    d = request.get_json(silent=True) or {}
    uid = (d.get("uid") or "").strip().upper()
    if not uid:
        abort(400)
    db.them_the_thang(uid, (d.get("ten") or "").strip(), ocr.chuan_hoa(d.get("bien_so")))
    db.ghi_nhat_ky(f"Thêm thẻ tháng {uid}")
    return jsonify(ok=True)


@app.delete("/api/the/<uid>")
def api_xoa_the(uid):
    db.xoa_the_thang(uid)
    db.ghi_nhat_ky(f"Xoá thẻ tháng {uid}")
    return jsonify(ok=True)


@app.post("/api/gia_lap/the")
def api_gia_lap_the():
    """Giả lập quẹt thẻ từ trang web (dùng khi chưa có ESP32)."""
    d = request.get_json(silent=True) or {}
    cong, uid = d.get("cong"), (d.get("uid") or "").strip().upper()
    if cong not in ("vao", "ra") or not uid:
        abort(400)
    chay_nen(xu_ly_vao if cong == "vao" else xu_ly_ra, uid)
    return jsonify(ok=True)


@app.post("/api/gia_lap/o/<int:so>")
def api_gia_lap_o(so):
    """Giả lập đổi trạng thái ô đỗ (dùng khi chưa có ESP32)."""
    if so not in trang_thai["o"]:
        abort(404)
    trang_thai["o"][so] = not trang_thai["o"][so]
    return jsonify(ok=True)


if __name__ == "__main__":
    khoi_dong_mqtt()
    camera.khoi_dong()
    ocr.nap_truoc()
    db.ghi_nhat_ky("Khởi động hệ thống")
    app.run(host="0.0.0.0", port=config.WEB_PORT, threaded=True)
