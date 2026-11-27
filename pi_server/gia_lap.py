"""
Giả lập 2 ESP32 gửi tin MQTT (dùng khi chưa lắp mạch).

Ví dụ:
  python gia_lap.py o 1 1            # ô 1 có xe
  python gia_lap.py o 1 0            # ô 1 trống
  python gia_lap.py the vao A1:B2:C3:D4
  python gia_lap.py the ra  A1:B2:C3:D4
  python gia_lap.py qua vao          # xe đã đi qua cảm biến cổng vào (thanh chắn hạ)
  python gia_lap.py qua ra
  python gia_lap.py nghe             # in mọi lệnh Pi gửi xuống cổng (baidoxe/lenh/#)

Thêm --host <IP> nếu chạy trên máy khác Pi.
Tài khoản MQTT lấy từ config.py / cau_hinh_rieng.py (thêm --user / --pass để ghi đè).
"""
import argparse
import time

import paho.mqtt.client as mqtt

import config

p = argparse.ArgumentParser(description="Giả lập ESP32 bãi đỗ xe")
p.add_argument("--host", default="localhost")
p.add_argument("--user", default=config.MQTT_USER)
p.add_argument("--pass", dest="mat_khau", default=config.MQTT_PASS)
p.add_argument("lenh", choices=["o", "the", "qua", "nghe"])
p.add_argument("thamso", nargs="*")
a = p.parse_args()

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"gia-lap-{int(time.time())}")
if a.user:
    c.username_pw_set(a.user, a.mat_khau)
c.connect(a.host, 1883, 30)

if a.lenh == "o":
    so, gia_tri = a.thamso
    c.publish(f"baidoxe/o/{so}", gia_tri, retain=True).wait_for_publish()
    print(f"Đã gửi: ô {so} = {'có xe' if gia_tri == '1' else 'trống'}")
elif a.lenh == "the":
    cong, uid = a.thamso
    c.publish(f"baidoxe/the/{cong}", uid).wait_for_publish()
    print(f"Đã gửi: quẹt thẻ {uid} ở cổng {cong}")
elif a.lenh == "qua":
    cong = a.thamso[0]
    c.publish(f"baidoxe/qua/{cong}", "1").wait_for_publish()
    print(f"Đã gửi: xe đi qua cổng {cong}")
else:
    c.on_message = lambda cl, u, m: print(f"{m.topic} -> {m.payload.decode()}")
    c.subscribe("baidoxe/lenh/#")
    print("Đang nghe lệnh từ Pi (Ctrl+C để thoát)...")
    c.loop_forever()
c.disconnect()
