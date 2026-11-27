#!/usr/bin/env bash
# Bật mật khẩu cho Mosquitto (MQTT) trên Pi: không còn cho kết nối ẩn danh.
#
# Chạy:  sudo bash bat_mat_khau_mqtt.sh
#
# Sau đó điền CÙNG tài khoản / mật khẩu vào:
#   - pi_server/cau_hinh_rieng.py          (MQTT_USER, MQTT_PASS)
#   - esp32_that/esp32_1_o_do/bi_mat.h      (BI_MAT_MQTT_USER, BI_MAT_MQTT_PASS)
#   - esp32_that/esp32_2_cong/bi_mat.h
# rồi chạy lại app.py và nạp lại 2 ESP32.
set -e

if [ "$(id -u)" -ne 0 ]; then
  echo "Can chay bang sudo:  sudo bash bat_mat_khau_mqtt.sh"
  exit 1
fi

read -r -p "Ten tai khoan MQTT [baidoxe]: " TK
TK=${TK:-baidoxe}

FILE_MK=/etc/mosquitto/passwd
if [ -f "$FILE_MK" ]; then
  mosquitto_passwd "$FILE_MK" "$TK"       # thêm / đổi mật khẩu tài khoản này
else
  mosquitto_passwd -c "$FILE_MK" "$TK"    # tạo file mật khẩu mới
fi
chown mosquitto:mosquitto "$FILE_MK" 2>/dev/null || true
chmod 640 "$FILE_MK"

# Tắt các dòng listener 1883 / allow_anonymous / password_file cũ ở file cấu hình khác
# (để không trùng với file mới). Bản gốc được lưu thành .bak
for f in /etc/mosquitto/mosquitto.conf /etc/mosquitto/conf.d/*.conf; do
  [ -f "$f" ] || continue
  [ "$f" = /etc/mosquitto/conf.d/baidoxe.conf ] && continue
  if grep -Eq '^[[:space:]]*(listener[[:space:]]+1883|allow_anonymous|password_file)' "$f"; then
    sed -i.bak -E 's/^([[:space:]]*(listener[[:space:]]+1883|allow_anonymous|password_file).*)$/# \1   # tat boi bat_mat_khau_mqtt.sh/' "$f"
    echo "Da tat cau hinh cu trong $f (ban goc: $f.bak)"
  fi
done

cat > /etc/mosquitto/conf.d/baidoxe.conf <<EOF
# Bai do xe: bat buoc dang nhap
listener 1883
allow_anonymous false
password_file $FILE_MK
EOF

systemctl restart mosquitto
sleep 1
if systemctl is-active --quiet mosquitto; then
  echo
  echo "Da bat mat khau MQTT cho tai khoan: $TK"
  echo "Nho dien tai khoan + mat khau vao cau_hinh_rieng.py va bi_mat.h cua 2 ESP32."
else
  echo "Mosquitto KHONG chay duoc. Xem loi:  sudo journalctl -u mosquitto -n 30"
  echo "Muon quay lai nhu cu: xoa /etc/mosquitto/conf.d/baidoxe.conf, doi cac file .bak ve ten cu,"
  echo "roi chay  sudo systemctl restart mosquitto"
  exit 1
fi
