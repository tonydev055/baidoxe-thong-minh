/*
 * ============================================================
 *  ESP32 SỐ 1 - CÁC Ô ĐỖ XE
 *  - 4 cảm biến siêu âm HC-SR04 phát hiện ô có xe / trống
 *  - LED xanh (trống) / đỏ (có xe) cho từng ô
 *  - OLED 128x64 hiển thị số chỗ trống
 *  - (tuỳ chọn) gửi trạng thái lên Raspberry Pi qua MQTT
 *
 *  TỰ KIỂM TRA THIẾT BỊ (gửi lên Pi qua baidoxe/tb/o_do, hiện ở mục "Tình trạng thiết bị"):
 *   - Từng cảm biến siêu âm: chân ECHO không phản hồi => lỗi dây / mất nguồn 5V
 *     (ô có cảm biến lỗi: LED xanh và đỏ nháy luân phiên, OLED hiện "?")
 *   - OLED: không trả lời trên I2C => lỗi dây SDA/SCL / mất nguồn
 *   - ESP32 khởi động lại vì sụt nguồn
 *   - LED: KHÔNG tự đo được -> xem HUONG_DAN_LOI.md
 *
 *  MÔ PHỎNG TRÊN WOKWI: bấm vào cảm biến HC-SR04 khi đang chạy,
 *  kéo thanh trượt khoảng cách xuống dưới 6 cm = có xe trong ô.
 * ============================================================
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>

// ------------------ CẤU HÌNH ------------------
#define DUNG_MQTT 1          // 1: gửi trạng thái lên máy chủ (laptop hoặc Pi)

// Thông tin WiFi / MQTT: giá trị mặc định ở dưới. Muốn đổi mà KHÔNG bị git pull ghi đè:
// tạo file bi_mat.h cạnh file .ino này (chép từ bi_mat.vi_du.h). File đó không đưa lên GitHub.
#if __has_include("bi_mat.h")
#include "bi_mat.h"
#endif
#ifndef BI_MAT_WIFI_SSID
#define BI_MAT_WIFI_SSID "TonyDev"        // ESP32 chỉ bắt được WiFi 2.4 GHz (không bắt được mạng 5G)
#endif
#ifndef BI_MAT_WIFI_PASS
#define BI_MAT_WIFI_PASS "12345678"
#endif
#ifndef BI_MAT_MQTT_HOST
#define BI_MAT_MQTT_HOST "172.20.10.2"    // IP của Pi (máy chạy Mosquitto) trong mạng WiFi
#endif
#ifndef BI_MAT_MQTT_USER
#define BI_MAT_MQTT_USER ""               // để trống nếu Mosquitto cho phép ẩn danh
#endif
#ifndef BI_MAT_MQTT_PASS
#define BI_MAT_MQTT_PASS ""
#endif
const char* WIFI_SSID = BI_MAT_WIFI_SSID;
const char* WIFI_PASS = BI_MAT_WIFI_PASS;
const char* MQTT_HOST = BI_MAT_MQTT_HOST;
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = BI_MAT_MQTT_USER;
const char* MQTT_PASS = BI_MAT_MQTT_PASS;

// ------------------ CHÂN ------------------
const uint8_t SO_O = 4;
const uint8_t TRIG[SO_O]     = {25, 26, 27, 14};
const uint8_t ECHO[SO_O]     = {34, 35, 36, 39};   // 36 = VP, 39 = VN
const uint8_t LED_XANH[SO_O] = {19, 5, 16, 2};     // ô1..ô4 (16 = RX2)
const uint8_t LED_DO[SO_O]   = {18, 17, 4, 15};    // ô1..ô4 (17 = TX2)
// OLED: SDA = 21, SCL = 22

// ------------------ THÔNG SỐ ------------------
const float   NGUONG_CM = 6.0;        // nhỏ hơn => ô có xe
const uint8_t SO_LAN_XAC_NHAN = 3;    // chống nhiễu
const uint8_t SO_LAN_XAC_NHAN_MAT_VONG = 15;  // đang có xe mà mất tiếng dội (999): phải liên tục
                                              // bấy nhiêu lần (~4 giây) mới tính là ô trống
const uint8_t SO_LAN_LOI_SA = 5;      // không phản hồi liên tiếp bấy nhiêu lần => cảm biến lỗi
const uint8_t DIA_CHI_OLED = 0x3C;
const unsigned long KIEM_TRA_MS = 5000;   // bao lâu kiểm tra OLED 1 lần
const unsigned long BAO_CAO_MS  = 30000;  // không có gì đổi vẫn gửi báo cáo định kỳ

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool coXe[SO_O] = {false};
uint8_t dem[SO_O] = {0};
uint8_t saLoi[SO_O] = {0};            // số lần không phản hồi liên tiếp của từng cảm biến
bool oledOk = false;

// Đo khoảng cách (cm).
//   999 = cảm biến chạy nhưng không có vật cản
//   -1  = cảm biến KHÔNG phản hồi (chân ECHO không lên mức cao) => lỏng dây / mất nguồn
float doKhoangCach(uint8_t trig, uint8_t echo) {
  unsigned long t0 = micros();
  while (digitalRead(echo) == HIGH) {          // chờ lần đo trước kết thúc hẳn
    if (micros() - t0 > 60000UL) return 999;
  }
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  t0 = micros();
  while (digitalRead(echo) == LOW) {           // HC-SR04 luôn kéo ECHO lên cao sau khi phát
    if (micros() - t0 > 10000UL) return -1;
  }
  unsigned long t1 = micros();
  while (digitalRead(echo) == HIGH) {
    if (micros() - t1 > 30000UL) return 999;
  }
  return (micros() - t1) / 58.0;
}

bool saOk(uint8_t i) {
  return saLoi[i] < SO_LAN_LOI_SA;
}

// OLED có trả lời trên I2C không
bool kiemTraOLED() {
  Wire.beginTransmission(DIA_CHI_OLED);
  return Wire.endTransmission() == 0;
}

const char* lyDoKhoiDong() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "bat_nguon";
    case ESP_RST_BROWNOUT: return "sut_nguon";
    case ESP_RST_PANIC:    return "loi_chuong_trinh";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "bi_treo";
    case ESP_RST_SW:       return "khoi_dong_lai";
    case ESP_RST_EXT:      return "nut_reset";
    default:               return "khac";
  }
}

uint8_t demTrong() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < SO_O; i++) if (!coXe[i]) n++;
  return n;
}

void capNhatLED(uint8_t i) {
  digitalWrite(LED_XANH[i], coXe[i] ? LOW : HIGH);
  digitalWrite(LED_DO[i],   coXe[i] ? HIGH : LOW);
}

// Ô có cảm biến lỗi: LED xanh và đỏ nháy luân phiên để nhìn là biết ô nào hỏng
void nhayLEDLoi() {
  bool pha = (millis() / 400) % 2;
  for (uint8_t i = 0; i < SO_O; i++) {
    if (saOk(i)) continue;
    digitalWrite(LED_XANH[i], pha ? HIGH : LOW);
    digitalWrite(LED_DO[i],   pha ? LOW : HIGH);
  }
}

void veOLED() {
  if (!oledOk) return;
  uint8_t trong = demTrong();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(4, 4);
  oled.print("BAI DO XE THONG MINH");

  oled.setTextSize(2);
  oled.setCursor(4, 24);
  if (trong == 0) {
    oled.print("HET CHO!");
  } else {
    oled.print("TRONG ");
    oled.print(trong);
    oled.print("/");
    oled.print(SO_O);
  }

  oled.setTextSize(1);
  for (uint8_t i = 0; i < SO_O; i++) {
    oled.setCursor(4 + i * 31, 52);
    oled.print(i + 1);
    oled.print(!saOk(i) ? ":?" : coXe[i] ? ":X" : ":O");
  }
  oled.display();
}

void inKetQuaKiemTra() {
  for (uint8_t i = 0; i < SO_O; i++)
    Serial.printf("O %u - sieu am: %s\n", i + 1,
                  saOk(i) ? "OK" : "LOI - ECHO khong phan hoi, kiem tra day TRIG/ECHO va nguon 5V");
  Serial.printf("OLED: %s\n", oledOk ? "OK" : "LOI - khong thay o dia chi 0x3C, kiem tra day SDA(21)/SCL(22)/nguon");
}

// ------------------ MQTT ------------------
void guiTrangThai() {
#if DUNG_MQTT
  if (!mqtt.connected()) return;   // kết nối lại sẽ gửi toàn bộ trạng thái
  char topic[24];
  for (uint8_t i = 0; i < SO_O; i++) {
    snprintf(topic, sizeof(topic), "baidoxe/o/%u", i + 1);
    mqtt.publish(topic, coXe[i] ? "1" : "0", true);   // retain: trạng thái
  }
  char so[4];
  snprintf(so, sizeof(so), "%u", demTrong());
  mqtt.publish("baidoxe/trong", so, true);
#endif
}

// Báo cáo tình trạng thiết bị lên Pi (retain: Pi khởi động sau vẫn nhận được)
void guiBaoCao() {
#if DUNG_MQTT
  if (!mqtt.connected()) return;
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"sa1\":%d,\"sa2\":%d,\"sa3\":%d,\"sa4\":%d,\"oled\":%d,\"reset\":\"%s\",\"uptime\":%lu,\"rssi\":%d}",
           saOk(0), saOk(1), saOk(2), saOk(3), oledOk, lyDoKhoiDong(), millis() / 1000, (int)WiFi.RSSI());
  mqtt.publish("baidoxe/tb/o_do", buf, true);
#endif
}

#if DUNG_MQTT
unsigned long lanThuCuoi = 0;
unsigned long lanBaoWifi = 0;
unsigned long matWifiLuc = 0;   // lúc bắt đầu mất WiFi (0 = đang có WiFi)
bool daBaoIP = false;

// Liệt kê các mạng WiFi ESP32 nhìn thấy (chỉ thấy mạng 2.4 GHz)
void quetWifi() {
  Serial.println("Dang quet WiFi...");
  int n = WiFi.scanNetworks();
  bool thay = false;
  for (int i = 0; i < n; i++) {
    bool la = WiFi.SSID(i) == WIFI_SSID;
    thay |= la;
    Serial.printf("  %-28s  song %d dBm %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), la ? "<-- mang can ket noi" : "");
  }
  if (!thay) Serial.printf("KHONG thay mang \"%s\": sai ten, hoac dang phat 5GHz (iPhone: bat 'Toi da hoa tinh tuong thich')\n", WIFI_SSID);
}

void giuKetNoi() {
  if (WiFi.status() != WL_CONNECTED) {
    daBaoIP = false;
    // Mạng chập chờn: ESP32 đôi khi không tự vào lại WiFi -> cứ 15 giây chủ động kết nối lại
    if (matWifiLuc == 0) matWifiLuc = millis();
    if (millis() - matWifiLuc > 15000) {
      Serial.println("WiFi: mat ket noi qua 15 giay, thu ket noi lai");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      matWifiLuc = millis();
    }
    if (millis() - lanBaoWifi > 5000) {
      lanBaoWifi = millis();
      int s = WiFi.status();
      Serial.printf("WiFi: chua ket noi (ma %d) %s\n", s,
                    s == WL_NO_SSID_AVAIL ? "- khong thay mang" :
                    s == WL_CONNECT_FAILED ? "- sai mat khau?" : "- dang thu...");
    }
    return;
  }
  matWifiLuc = 0;
  if (!daBaoIP) {
    daBaoIP = true;
    Serial.printf("WiFi: da ket noi, IP ESP32 = %s, may chu = %s\n", WiFi.localIP().toString().c_str(), MQTT_HOST);
  }
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lanThuCuoi < 3000) return;
  lanThuCuoi = millis();
  // "Di chúc" (LWT): ESP32 mất điện / rớt mạng thì broker tự báo baidoxe/tb/o_do/online = 0
  bool ok = strlen(MQTT_USER)
              ? mqtt.connect("esp32-o-do", MQTT_USER, MQTT_PASS, "baidoxe/tb/o_do/online", 0, true, "0")
              : mqtt.connect("esp32-o-do", "baidoxe/tb/o_do/online", 0, true, "0");
  if (ok) {
    Serial.println("MQTT: da ket noi");
    mqtt.publish("baidoxe/tb/o_do/online", "1", true);
    guiTrangThai();
    guiBaoCao();
  } else {
    Serial.printf("MQTT: loi ket noi (%d), thu lai sau 3s\n", mqtt.state());
  }
}
#endif

// Kiểm tra OLED định kỳ (cắm lại dây thì tự bật lại) + gửi báo cáo định kỳ
void kiemTraDinhKy() {
  static unsigned long lanKiemTra = 0, lanBaoCao = 0;
  if (millis() - lanKiemTra < KIEM_TRA_MS) return;
  lanKiemTra = millis();

  bool co = kiemTraOLED();
  bool thayDoi = false;
  if (co && !oledOk) {
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, DIA_CHI_OLED);
    veOLED();
    thayDoi = true;
  } else if (!co && oledOk) {
    oledOk = false;
    thayDoi = true;
  }
  if (thayDoi) inKetQuaKiemTra();
  if (thayDoi || millis() - lanBaoCao > BAO_CAO_MS) {
    lanBaoCao = millis();
    guiBaoCao();
  }
}

// ------------------ SETUP & LOOP ------------------
void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < SO_O; i++) {
    pinMode(TRIG[i], OUTPUT);
    pinMode(ECHO[i], INPUT);
    pinMode(LED_XANH[i], OUTPUT);
    pinMode(LED_DO[i], OUTPUT);
  }

  Serial.printf("Ly do khoi dong: %s\n", lyDoKhoiDong());
  if (esp_reset_reason() == ESP_RST_BROWNOUT)
    Serial.println("CANH BAO: ESP32 vua khoi dong lai vi SUT NGUON - kiem tra nguon cap");

  Wire.begin(21, 22);
  oledOk = kiemTraOLED() && oled.begin(SSD1306_SWITCHCAPVCC, DIA_CHI_OLED);

  // Đọc trạng thái ban đầu + tự kiểm tra từng cảm biến
  for (uint8_t i = 0; i < SO_O; i++) {
    float d = 999;
    for (uint8_t lan = 0; lan < SO_LAN_LOI_SA; lan++) {
      d = doKhoangCach(TRIG[i], ECHO[i]);
      if (d >= 0) { saLoi[i] = 0; break; }
      saLoi[i]++;
      delay(20);
    }
    coXe[i] = d >= 0 && d < NGUONG_CM;
    capNhatLED(i);
    delay(20);
  }
  veOLED();
  inKetQuaKiemTra();
  Serial.println("ESP32 O DO: san sang");

#if DUNG_MQTT
  WiFi.mode(WIFI_STA);
  quetWifi();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
#endif
}

void loop() {
#if DUNG_MQTT
  giuKetNoi();
#endif

  bool thayDoi = false, doiTinhTrang = false;
  for (uint8_t i = 0; i < SO_O; i++) {
    float d = doKhoangCach(TRIG[i], ECHO[i]);
    bool cuOk = saOk(i);

    if (d < 0) {                           // cảm biến không phản hồi: giữ nguyên trạng thái ô
      if (saLoi[i] < 255) saLoi[i]++;
      dem[i] = 0;
    } else {
      saLoi[i] = 0;
      bool doc = d < NGUONG_CM;
      // Mất tiếng dội (999) khi ô đang có xe: có thể do thân xe làm lệch sóng chứ chưa chắc xe đã đi
      // -> phải mất liên tục lâu hơn mới tính là trống (đo được khoảng cách thật thì 3 lần là đủ)
      uint8_t canDem = (d >= 999 && coXe[i]) ? SO_LAN_XAC_NHAN_MAT_VONG : SO_LAN_XAC_NHAN;
      if (doc != coXe[i]) {
        if (++dem[i] >= canDem) {            // đọc giống nhau đủ số lần mới đổi
          coXe[i] = doc;
          dem[i] = 0;
          thayDoi = true;
          Serial.printf("O %u: %s (%.1f cm)\n", i + 1, coXe[i] ? "CO XE" : "TRONG", d);
        }
      } else {
        dem[i] = 0;
      }
    }

    if (cuOk != saOk(i)) {
      doiTinhTrang = true;
      Serial.printf("O %u - sieu am: %s\n", i + 1, saOk(i) ? "da binh thuong lai" : "LOI - khong phan hoi");
    }
    if (saOk(i) && (thayDoi || cuOk != saOk(i))) capNhatLED(i);
    delay(20);   // tránh nhiễu sóng giữa các cảm biến
  }
  nhayLEDLoi();

  if (thayDoi || doiTinhTrang) veOLED();
  if (thayDoi) guiTrangThai();
  if (doiTinhTrang) guiBaoCao();
  kiemTraDinhKy();
}
