// Thông tin WiFi / MQTT riêng của mạch này. File bi_mat.h KHÔNG đưa lên GitHub,
// git pull không ghi đè.
//
// Cách dùng: chép file này thành bi_mat.h (cùng thư mục), sửa giá trị, rồi nạp lại ESP32.
//   cp bi_mat.vi_du.h bi_mat.h && nano bi_mat.h
//
// Dòng nào không cần đổi thì xoá đi (sẽ dùng giá trị mặc định trong file .ino).

#define BI_MAT_WIFI_SSID "TonyDev"
#define BI_MAT_WIFI_PASS "12345678"
#define BI_MAT_MQTT_HOST "172.20.10.2"   // IP của Pi trong mạng WiFi (hostname -I)

// Sau khi chạy  sudo bash bat_mat_khau_mqtt.sh  trên Pi (giống MQTT_USER / MQTT_PASS trong cau_hinh_rieng.py):
// #define BI_MAT_MQTT_USER "baidoxe"
// #define BI_MAT_MQTT_PASS "mat-khau-mqtt"
