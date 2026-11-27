"""
CHƯƠNG TRÌNH CHÍNH - BÃI ĐỖ XE THÔNG MINH (chạy trên Raspberry Pi 5)

- Nhận tin MQTT từ 2 ESP32 (trạng thái ô đỗ, quẹt thẻ ở cổng, xe đã đi qua cổng)
- Quẹt thẻ -> chụp webcam -> đọc biển số -> lưu SQLite -> gửi lệnh mở / từ chối
- Thanh chắn chỉ hạ khi xe đã đi qua cảm biến. Quá lâu xe chưa qua -> hỏi nhân viên trên web
- Xe ra: so biển số lúc vào và lúc ra, tính tiền
- Trang web quản lý: http://<IP-cua-Pi>:5000

Chạy:  python app.py
"""
import math
import sys
import threading
import time
import traceback
from datetime import datetime
from difflib import SequenceMatcher

import paho.mqtt.client as mqtt
from flask import Flask, Response, abort, jsonify, render_template, request, send_from_directory

import camera
import config
import db
import ocr
import thiet_bi

# In tiếng Việt ra console Windows / file log không bị lỗi UnicodeEncodeError
for _luong_in in (sys.stdout, sys.stderr):
    try:
        _luong_in.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass

app = Flask(__name__)

trang_thai = {
    "o": {i: False for i in range(1, config.SO_O + 1)},   # True = có xe
    "cong": {"vao": "dong", "ra": "dong"},
    "mqtt": False,
}
khoa_cong = {"vao": threading.Lock(), "ra": threading.Lock()}
client = None

# Lượt đang chờ xe đi qua cảm biến ở mỗi cổng (thanh chắn đang mở):
#   {"phien_id", "luc": lúc mở cổng, "hoi": đã hỏi nhân viên chưa, "da_xac_nhan": nhân viên bảo chờ tiếp}
cho_qua = {"vao": None, "ra": None}
khoa_cho_qua = threading.Lock()
TEN_CONG = {"vao": "Cổng vào", "ra": "Cổng ra"}


def gioi_han_cho(cong):
    return config.CHO_XAC_NHAN_VAO_GIAY if cong == "vao" else config.CHO_XAC_NHAN_RA_GIAY


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
    return f"{so or 0:,}".replace(",", ".") + "đ"


def gui_lenh(cong, lenh):
    if client is not None:
        client.publish(f"baidoxe/lenh/{cong}", lenh)


def so_cho_trong():
    co_xe = sum(1 for v in trang_thai["o"].values() if v)
    xe_trong_bai = len(db.ds_chua_ra())
    return max(0, config.SO_O - max(co_xe, xe_trong_bai))


def chup_va_doc(cong, uid):
    ten_anh, frame, that = camera.chup(cong)
    bien = ocr.doc_bien_so(frame, cong) if that else camera.bien_gia_lap(uid)
    return ten_anh, bien


def mo_cho_qua(cong, phien_id):
    """Mở thanh chắn và bắt đầu đếm giờ chờ xe đi qua cảm biến."""
    with khoa_cho_qua:
        cho_qua[cong] = {"phien_id": phien_id, "luc": time.time(), "hoi": False, "da_xac_nhan": False}
    gui_lenh(cong, "mo")


def bo_cho_qua(cong, phien_id=None):
    """Bỏ lượt đang chờ ở cổng (nếu phien_id khác lượt đang chờ thì không làm gì). Trả về lượt cũ."""
    with khoa_cho_qua:
        p = cho_qua[cong]
        if p is None or (phien_id is not None and p["phien_id"] != phien_id):
            return None
        cho_qua[cong] = None
        return p


# ======================= XỬ LÝ XE VÀO / RA =======================
def xu_ly_vao(uid):
    with khoa_cong["vao"]:
        if cho_qua["vao"] is not None:
            db.ghi_nhat_ky(f"Cổng vào: đang chờ xe trước đi qua, từ chối thẻ {uid}", "canh_bao")
            gui_lenh("vao", "tuchoi")
            return
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
        phien_id = db.tao_phien(uid, loai, bien, ten_anh, dinh_dang(bay_gio()))
        mo_cho_qua("vao", phien_id)
        db.ghi_nhat_ky(f"Cổng vào: nhận thẻ {uid}, biển {bien or 'không đọc được'}"
                       f"{' (thẻ tháng)' if loai == 'thang' else ''} – mở cổng, chờ xe đi qua")


def cho_ra(phien, bien_ra, anh_ra, gio_ra, ly_do):
    """Cho xe ra: tính tiền, mở cổng, chờ xe đi qua cảm biến mới kết thúc lượt."""
    tien = tinh_tien(doc_gio(phien["gio_vao"]), doc_gio(gio_ra), phien["loai"])
    db.bat_dau_ra(phien["id"], bien_ra, anh_ra, gio_ra, tien)
    mo_cho_qua("ra", phien["id"])
    db.ghi_nhat_ky(f"Cổng ra: {ly_do} thẻ {phien['uid']}, thu {tien_vn(tien)} – mở cổng, chờ xe đi qua")


def xu_ly_ra(uid):
    with khoa_cong["ra"]:
        if cho_qua["ra"] is not None:
            db.ghi_nhat_ky(f"Cổng ra: đang chờ xe trước đi qua, từ chối thẻ {uid}", "canh_bao")
            gui_lenh("ra", "tuchoi")
            return
        phien = db.phien_hien_tai(uid)
        if not phien or phien["trang_thai"] == "dang_vao":
            db.ghi_nhat_ky(f"Cổng ra: thẻ {uid} không có xe trong bãi, từ chối", "canh_bao")
            gui_lenh("ra", "tuchoi")
            return

        ten_anh, bien = chup_va_doc("ra", uid)
        gio_ra = dinh_dang(bay_gio())

        if giong_nhau(phien["bien_vao"], bien):
            cho_ra(phien, bien, ten_anh, gio_ra, f"biển {bien} khớp,")
        else:
            db.cho_kiem_tra(phien["id"], bien, ten_anh, gio_ra)
            gui_lenh("ra", "tuchoi")
            db.ghi_nhat_ky(f"Cổng ra: biển KHÔNG KHỚP (vào {phien['bien_vao'] or '?'} / ra {bien or '?'}), "
                           f"chờ người trông xe kiểm tra", "canh_bao")


def xu_ly_the(cong, uid):
    """Xử lý quẹt thẻ. Có lỗi bất ngờ vẫn trả lời 'tuchoi' để cổng không phải chờ hết 25 giây."""
    try:
        (xu_ly_vao if cong == "vao" else xu_ly_ra)(uid)
    except Exception as e:
        traceback.print_exc()
        gui_lenh(cong, "tuchoi")
        try:
            db.ghi_nhat_ky(f"Lỗi xử lý thẻ {uid} ở cổng {cong}: {e}", "canh_bao")
        except Exception:
            pass


def xu_ly_qua(cong):
    """ESP32 báo: xe đã đi qua cảm biến và thanh chắn đã hạ."""
    p = bo_cho_qua(cong)
    if p is None:
        db.ghi_nhat_ky(f"{TEN_CONG[cong]}: có xe đi qua (cổng mở tay, không có lượt chờ)")
        return
    phien = db.lay_phien(p["phien_id"])
    if not phien:
        return
    if cong == "vao":
        db.da_vao(phien["id"])
        db.ghi_nhat_ky(f"Xe VÀO bãi: thẻ {phien['uid']}, biển {phien['bien_vao'] or 'không đọc được'}")
    else:
        db.da_ra(phien["id"])
        db.ghi_nhat_ky(f"Xe RA: thẻ {phien['uid']}, biển {phien['bien_ra'] or '?'}, thu {tien_vn(phien['so_tien'])}")


def theo_doi_cho_qua():
    """Mở cổng quá CHO_XAC_NHAN_*_GIAY mà xe chưa qua cảm biến -> hỏi nhân viên trên web."""
    while True:
        try:
            for cong in ("vao", "ra"):
                with khoa_cho_qua:
                    p = cho_qua[cong]
                    hoi_ngay = (p is not None and not p["hoi"] and not p["da_xac_nhan"]
                                and time.time() - p["luc"] >= gioi_han_cho(cong))
                    if hoi_ngay:
                        p["hoi"] = True
                if hoi_ngay:
                    phien = db.lay_phien(p["phien_id"]) or {}
                    viec = "vào" if cong == "vao" else "ra"
                    db.ghi_nhat_ky(f"{TEN_CONG[cong]}: thẻ {phien.get('uid', '?')} quẹt được {gioi_han_cho(cong)} "
                                   f"giây nhưng xe chưa {viec} – chờ nhân viên xác nhận", "canh_bao")
        except Exception:
            traceback.print_exc()
        time.sleep(1)


def khoi_phuc_cho_qua():
    """Pi khởi động lại giữa chừng: lấy lại các lượt đang chờ xe qua từ cơ sở dữ liệu."""
    for p in reversed(db.ds_chua_ra()):          # cũ -> mới, lượt mới nhất của mỗi cổng thắng
        cong = {"dang_vao": "vao", "dang_ra": "ra"}.get(p["trang_thai"])
        if cong:
            with khoa_cho_qua:
                cho_qua[cong] = {"phien_id": p["id"], "luc": time.time(), "hoi": False, "da_xac_nhan": False}


def chay_nen(ham, *args):
    threading.Thread(target=ham, args=args, daemon=True).start()


def xoa_luot(phien, ly_do):
    """Xoá hẳn 1 lượt (kèm ảnh). Lượt đang chờ xe qua cổng thì đóng cổng luôn."""
    for cong in ("vao", "ra"):
        if bo_cho_qua(cong, phien["id"]):
            gui_lenh(cong, "dong")
    db.xoa_phien(phien["id"])
    camera.xoa_anh(phien.get("anh_vao"), phien.get("anh_ra"))
    db.ghi_nhat_ky(f"{ly_do}: xoá lượt #{phien['id']} (thẻ {phien['uid']}, "
                   f"biển {phien['bien_vao'] or 'không đọc được'})", "canh_bao")


def don_anh_cu_dinh_ky():
    """Mỗi 6 giờ xoá ảnh cũ hơn GIU_ANH_NGAY ngày (giữ lại ảnh của xe còn trong bãi)."""
    while True:
        try:
            giu = {f for p in db.ds_chua_ra() for f in (p["anh_vao"], p["anh_ra"]) if f}
            so = camera.xoa_anh_cu(config.GIU_ANH_NGAY, giu)
            if so:
                db.ghi_nhat_ky(f"Đã xoá {so} ảnh cũ hơn {config.GIU_ANH_NGAY} ngày")
        except Exception:
            traceback.print_exc()
        time.sleep(6 * 3600)


# ======================= MQTT =======================
def on_connect(cl, userdata, flags, reason_code, properties=None):
    trang_thai["mqtt"] = (reason_code == 0)
    if reason_code == 0:
        cl.subscribe([("baidoxe/o/+", 0), ("baidoxe/the/+", 0), ("baidoxe/cong/+", 0),
                      ("baidoxe/qua/+", 0), ("baidoxe/tb/#", 0)])
        db.ghi_nhat_ky("Đã kết nối MQTT broker")


def on_disconnect(cl, userdata, flags, reason_code, properties=None):
    trang_thai["mqtt"] = False


def on_message(cl, userdata, msg):
    phan = msg.topic.split("/")
    noi_dung = msg.payload.decode(errors="ignore").strip()
    if len(phan) in (3, 4) and phan[1] == "tb":          # báo cáo tình trạng thiết bị từ ESP32
        thiet_bi.nhan(phan[2], phan[3] if len(phan) == 4 else "", noi_dung)
        return
    if len(phan) != 3:
        return
    _, loai, ten = phan

    if loai == "o" and ten.isdigit():
        so = int(ten)
        if so in trang_thai["o"]:
            trang_thai["o"][so] = (noi_dung == "1")
    elif loai == "the" and noi_dung and ten in ("vao", "ra"):
        chay_nen(xu_ly_the, ten, noi_dung.upper())
    elif loai == "qua" and ten in ("vao", "ra"):
        chay_nen(xu_ly_qua, ten)
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
    phien["tam_tinh"] = phien["so_tien"] if phien["trang_thai"] == "dang_ra" else tinh_tien(vao, bay_gio(), phien["loai"])
    return phien


def tinh_trang_cho_qua():
    """Lượt đang chờ xe qua ở mỗi cổng, cho trang web."""
    kq = {}
    for cong in ("vao", "ra"):
        with khoa_cho_qua:
            p = dict(cho_qua[cong]) if cho_qua[cong] else None
        if p is None:
            kq[cong] = None
            continue
        phien = db.lay_phien(p["phien_id"]) or {}
        kq[cong] = {"phien_id": p["phien_id"], "giay": int(time.time() - p["luc"]), "hoi": p["hoi"],
                    "da_xac_nhan": p["da_xac_nhan"], "gioi_han": gioi_han_cho(cong),
                    "uid": phien.get("uid"), "bien": phien.get("bien_vao" if cong == "vao" else "bien_ra"),
                    "anh": phien.get("anh_vao" if cong == "vao" else "anh_ra"),
                    "so_tien": phien.get("so_tien")}
    return kq


@app.route("/")
def trang_chu():
    return render_template("index.html", so_o=config.SO_O)


@app.route("/api/state")
def api_state():
    luot, tien = db.thong_ke_hom_nay()
    chua_ra = db.ds_chua_ra()
    return jsonify({
        "o": [{"so": k, "co_xe": v} for k, v in sorted(trang_thai["o"].items())],
        "tong": config.SO_O,
        "cho_trong": so_cho_trong(),
        "cong": trang_thai["cong"],
        "mqtt": trang_thai["mqtt"],
        "trong_bai": [them_tam_tinh(p) for p in chua_ra if p["trang_thai"] != "cho_kiem_tra"],
        "cho_kiem_tra": [p for p in chua_ra if p["trang_thai"] == "cho_kiem_tra"],
        "cho_qua": tinh_trang_cho_qua(),
        "lich_su": db.lich_su(50),
        "luot_hom_nay": luot,
        "doanh_thu_hom_nay": tien,
        "nhat_ky": db.nhat_ky_moi(20),
        "the_thang": db.ds_the_thang(),
        "gia": {"don_vi_phut": config.DON_VI_PHUT, "dau": config.GIA_LUOT_DAU, "tiep": config.GIA_LUOT_TIEP},
        "vua_vao": db.phien_vua_vao(),
        "thiet_bi": thiet_bi.danh_sach(trang_thai["mqtt"]),
        "o_loi": thiet_bi.o_cam_bien_loi(),
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


@app.post("/api/dong_cong/<cong>")
def api_dong_cong(cong):
    """Hạ thanh chắn bằng tay (vd mở nhầm). Lượt đang chờ xe qua (nếu có) vẫn giữ để nhân viên quyết định."""
    if cong not in ("vao", "ra"):
        abort(404)
    gui_lenh(cong, "dong")
    db.ghi_nhat_ky(f"Đóng cổng {cong} bằng tay từ trang web")
    return jsonify(ok=True)


# ---------- Nhân viên xác nhận khi xe quẹt thẻ xong mà chưa đi qua ----------
@app.post("/api/cho_qua/<cong>/huy")
def api_cho_qua_huy(cong):
    """
    Cổng vào: xe không vào nữa -> xoá lượt (xoá thẻ khỏi bãi), hạ thanh chắn.
    Cổng ra : xe không ra nữa  -> hạ thanh chắn, xe VẪN trong bãi như cũ.
    """
    if cong not in ("vao", "ra"):
        abort(404)
    p = bo_cho_qua(cong)
    if p is None:
        return jsonify(ok=False, loi="Không có xe nào đang chờ ở cổng này"), 404
    gui_lenh(cong, "dong")
    phien = db.lay_phien(p["phien_id"])
    if phien:
        if cong == "vao":
            db.xoa_phien(phien["id"])
            camera.xoa_anh(phien.get("anh_vao"))
            db.ghi_nhat_ky(f"Nhân viên xác nhận: thẻ {phien['uid']} KHÔNG vào – đóng cổng, xoá thẻ khỏi bãi",
                           "canh_bao")
        else:
            db.ve_trong_bai(phien["id"])
            camera.xoa_anh(phien.get("anh_ra"))
            db.ghi_nhat_ky(f"Nhân viên xác nhận: thẻ {phien['uid']} KHÔNG ra – đóng cổng, xe vẫn trong bãi",
                           "canh_bao")
    return jsonify(ok=True)


@app.post("/api/cho_qua/<cong>/cho_tiep")
def api_cho_qua_cho_tiep(cong):
    """Xe vẫn vào / vẫn ra: giữ thanh chắn mở, chờ xe đi qua cảm biến như bình thường."""
    if cong not in ("vao", "ra"):
        abort(404)
    with khoa_cho_qua:
        p = cho_qua[cong]
        if p is not None:
            p["da_xac_nhan"], p["hoi"] = True, False
    if p is None:
        return jsonify(ok=False, loi="Không có xe nào đang chờ ở cổng này"), 404
    phien = db.lay_phien(p["phien_id"]) or {}
    viec = "vẫn vào" if cong == "vao" else "vẫn ra"
    db.ghi_nhat_ky(f"Nhân viên xác nhận: thẻ {phien.get('uid', '?')} {viec} – giữ cổng mở, chờ xe đi qua")
    return jsonify(ok=True)


@app.post("/api/cho_ra/<int:phien_id>")
def api_cho_ra(phien_id):
    phien = db.lay_phien(phien_id)
    if not phien or phien["trang_thai"] != "cho_kiem_tra":
        abort(404)
    if cho_qua["ra"] is not None:
        return jsonify(ok=False, loi="Cổng ra đang chờ xe khác đi qua"), 409
    cho_ra(phien, phien["bien_ra"], phien["anh_ra"], phien["gio_ra"], "người trông xe cho")
    return jsonify(ok=True)


@app.post("/api/huy_ra/<int:phien_id>")
def api_huy_ra(phien_id):
    phien = db.lay_phien(phien_id)
    if not phien or phien["trang_thai"] != "cho_kiem_tra":
        abort(404)
    db.ve_trong_bai(phien_id)
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


# ---------- Xoá lượt xe (test / dọn bãi) ----------
@app.delete("/api/phien/<int:phien_id>")
def api_xoa_phien(phien_id):
    """Xoá hẳn 1 lượt xe (kèm ảnh). Xoá xong có thể quẹt lại đúng thẻ đó."""
    phien = db.lay_phien(phien_id)
    if not phien:
        abort(404)
    xoa_luot(phien, "Xoá bằng tay từ trang web")
    return jsonify(ok=True)


@app.post("/api/xoa_xe_vua_vao")
def api_xoa_xe_vua_vao():
    """Xoá lượt xe vào gần nhất còn trong bãi -> quẹt lại thẻ đó."""
    phien = db.phien_vua_vao()
    if not phien:
        return jsonify(ok=False, loi="Không có xe nào trong bãi"), 404
    xoa_luot(phien, "Xoá xe vừa vào")
    return jsonify(ok=True, uid=phien["uid"])


@app.post("/api/xoa_tat_ca_trong_bai")
def api_xoa_tat_ca_trong_bai():
    """Xoá mọi lượt chưa ra (dọn sạch sau khi test). Lịch sử xe đã ra giữ nguyên."""
    ds = db.ds_chua_ra()
    for p in ds:
        xoa_luot(p, "Dọn bãi sau khi test")
    return jsonify(ok=True, so_luot=len(ds))


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
    chay_nen(xu_ly_the, cong, uid)
    return jsonify(ok=True)


@app.post("/api/gia_lap/qua/<cong>")
def api_gia_lap_qua(cong):
    """Giả lập xe đi qua cảm biến cổng (dùng khi chưa có ESP32)."""
    if cong not in ("vao", "ra"):
        abort(404)
    xu_ly_qua(cong)
    return jsonify(ok=True)


@app.post("/api/gia_lap/o/<int:so>")
def api_gia_lap_o(so):
    """Giả lập đổi trạng thái ô đỗ (dùng khi chưa có ESP32)."""
    if so not in trang_thai["o"]:
        abort(404)
    trang_thai["o"][so] = not trang_thai["o"][so]
    return jsonify(ok=True)


if __name__ == "__main__":
    khoi_phuc_cho_qua()
    khoi_dong_mqtt()
    camera.khoi_dong()
    ocr.nap_truoc()
    chay_nen(don_anh_cu_dinh_ky)
    chay_nen(theo_doi_cho_qua)
    thiet_bi.bat_dau(lambda: trang_thai["mqtt"])
    db.ghi_nhat_ky("Khởi động hệ thống")
    app.run(host="0.0.0.0", port=config.WEB_PORT, threaded=True)
