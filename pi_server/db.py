"""Lưu trữ dữ liệu bằng SQLite."""
import sqlite3
import threading
from datetime import datetime

import config

_khoa = threading.Lock()
_conn = sqlite3.connect(config.DB_FILE, check_same_thread=False)
_conn.row_factory = sqlite3.Row

SCHEMA = """
CREATE TABLE IF NOT EXISTS the_thang (
    uid      TEXT PRIMARY KEY,
    ten      TEXT,
    bien_so  TEXT,
    tao_luc  TEXT
);
CREATE TABLE IF NOT EXISTS phien (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    uid         TEXT NOT NULL,
    loai        TEXT NOT NULL,          -- 'luot' hoặc 'thang'
    bien_vao    TEXT,
    anh_vao     TEXT,
    gio_vao     TEXT NOT NULL,
    bien_ra     TEXT,
    anh_ra      TEXT,
    gio_ra      TEXT,
    so_tien     INTEGER,
    trang_thai  TEXT NOT NULL           -- 'trong_bai' | 'cho_kiem_tra' | 'da_ra'
);
CREATE TABLE IF NOT EXISTS nhat_ky (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    thoi_gian  TEXT NOT NULL,
    muc        TEXT NOT NULL,           -- 'info' | 'canh_bao'
    noi_dung   TEXT NOT NULL
);
"""

with _khoa:
    _conn.executescript(SCHEMA)
    _conn.commit()


def _now():
    return datetime.now().replace(microsecond=0).isoformat(sep=" ")


def _query(sql, args=(), one=False):
    with _khoa:
        cur = _conn.execute(sql, args)
        rows = [dict(r) for r in cur.fetchall()]
    return (rows[0] if rows else None) if one else rows


def _exec(sql, args=()):
    with _khoa:
        cur = _conn.execute(sql, args)
        _conn.commit()
        return cur.lastrowid


# ---------------- Nhật ký ----------------
def ghi_nhat_ky(noi_dung, muc="info"):
    _exec("INSERT INTO nhat_ky (thoi_gian, muc, noi_dung) VALUES (?, ?, ?)", (_now(), muc, noi_dung))
    print(f"[{muc}] {noi_dung}", flush=True)


def nhat_ky_moi(n=20):
    return _query("SELECT * FROM nhat_ky ORDER BY id DESC LIMIT ?", (n,))


# ---------------- Thẻ tháng ----------------
def lay_the_thang(uid):
    return _query("SELECT * FROM the_thang WHERE uid = ?", (uid,), one=True)


def ds_the_thang():
    return _query("SELECT * FROM the_thang ORDER BY tao_luc DESC")


def them_the_thang(uid, ten, bien_so):
    _exec("INSERT OR REPLACE INTO the_thang (uid, ten, bien_so, tao_luc) VALUES (?, ?, ?, ?)",
          (uid, ten, bien_so, _now()))


def xoa_the_thang(uid):
    _exec("DELETE FROM the_thang WHERE uid = ?", (uid,))


# ---------------- Phiên gửi xe ----------------
def phien_hien_tai(uid):
    """Phiên chưa kết thúc của thẻ (xe đang trong bãi hoặc đang chờ kiểm tra)."""
    return _query("SELECT * FROM phien WHERE uid = ? AND trang_thai IN ('trong_bai', 'cho_kiem_tra') "
                  "ORDER BY id DESC LIMIT 1", (uid,), one=True)


def lay_phien(phien_id):
    return _query("SELECT * FROM phien WHERE id = ?", (phien_id,), one=True)


def tao_phien(uid, loai, bien_vao, anh_vao, gio_vao):
    return _exec("INSERT INTO phien (uid, loai, bien_vao, anh_vao, gio_vao, trang_thai) "
                 "VALUES (?, ?, ?, ?, ?, 'trong_bai')", (uid, loai, bien_vao, anh_vao, gio_vao))


def ket_thuc_phien(phien_id, bien_ra, anh_ra, gio_ra, so_tien):
    _exec("UPDATE phien SET bien_ra = ?, anh_ra = ?, gio_ra = ?, so_tien = ?, trang_thai = 'da_ra' "
          "WHERE id = ?", (bien_ra, anh_ra, gio_ra, so_tien, phien_id))


def cho_kiem_tra(phien_id, bien_ra, anh_ra, gio_ra):
    _exec("UPDATE phien SET bien_ra = ?, anh_ra = ?, gio_ra = ?, trang_thai = 'cho_kiem_tra' "
          "WHERE id = ?", (bien_ra, anh_ra, gio_ra, phien_id))


def huy_kiem_tra(phien_id):
    _exec("UPDATE phien SET bien_ra = NULL, anh_ra = NULL, gio_ra = NULL, trang_thai = 'trong_bai' "
          "WHERE id = ?", (phien_id,))


def sua_bien_vao(phien_id, bien):
    _exec("UPDATE phien SET bien_vao = ? WHERE id = ?", (bien, phien_id))


def xoa_phien(phien_id):
    """Xoá hẳn 1 lượt gửi xe (dùng khi test: xoá xe vừa vào để quẹt lại thẻ đó)."""
    _exec("DELETE FROM phien WHERE id = ?", (phien_id,))


def phien_vua_vao():
    """Lượt xe vào gần nhất còn trong bãi (hoặc đang chờ kiểm tra)."""
    return _query("SELECT * FROM phien WHERE trang_thai IN ('trong_bai', 'cho_kiem_tra') "
                  "ORDER BY id DESC LIMIT 1", one=True)


def ds_chua_ra():
    """Tất cả lượt chưa ra (trong bãi + chờ kiểm tra)."""
    return _query("SELECT * FROM phien WHERE trang_thai IN ('trong_bai', 'cho_kiem_tra') ORDER BY id DESC")


def ds_theo_trang_thai(trang_thai):
    return _query("SELECT * FROM phien WHERE trang_thai = ? ORDER BY id DESC", (trang_thai,))


def lich_su(n=50):
    return _query("SELECT * FROM phien WHERE trang_thai = 'da_ra' ORDER BY gio_ra DESC, id DESC LIMIT ?", (n,))


def thong_ke_hom_nay():
    hom_nay = datetime.now().strftime("%Y-%m-%d")
    r = _query("SELECT COUNT(*) AS luot, COALESCE(SUM(so_tien), 0) AS tien FROM phien "
               "WHERE trang_thai = 'da_ra' AND substr(gio_ra, 1, 10) = ?", (hom_nay,), one=True)
    return r["luot"], r["tien"]
