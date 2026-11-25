/*
 * ============================================================
 *  ESP32 SỐ 1 - CÁC Ô ĐỖ XE
 *  - 4 cảm biến siêu âm HC-SR04 phát hiện ô có xe / trống
 *  - LED xanh (trống) / đỏ (có xe) cho từng ô
 *  - OLED 128x64 hiển thị số chỗ trống
 *  - (tuỳ chọn) gửi trạng thái lên Raspberry Pi qua MQTT
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

// ------------------ CẤU HÌNH ------------------
#define DUNG_MQTT 1          // 1: gửi trạng thái lên máy chủ (laptop hoặc Pi)

// ESP32 chỉ bắt được WiFi 2.4 GHz (không bắt được mạng 5G)
const char* WIFI_SSID = "TonyDev";           // <-- sửa: tên WiFi
const char* WIFI_PASS = "12345678";     // <-- sửa: mật khẩu WiFi
const char* MQTT_HOST = "172.20.10.2";     // IP laptop đang chạy Mosquitto (dùng Pi thì đổi IP Pi)
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";              // để trống nếu broker cho phép ẩn danh
const char* MQTT_PASS = "";

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

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool coXe[SO_O] = {false};
uint8_t dem[SO_O] = {0};

// Đo khoảng cách (cm). Không có tín hiệu dội về => 999
float doKhoangCach(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long t = pulseIn(echo, HIGH, 30000UL);
  if (t == 0) return 999;
  return t / 58.0;
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

void veOLED() {
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
    oled.print(coXe[i] ? ":X" : ":O");
  }
  oled.display();
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

#if DUNG_MQTT
unsigned long lanThuCuoi = 0;
unsigned long lanBaoWifi = 0;
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
    if (millis() - lanBaoWifi > 5000) {
      lanBaoWifi = millis();
      int s = WiFi.status();
      Serial.printf("WiFi: chua ket noi (ma %d) %s\n", s,
                    s == WL_NO_SSID_AVAIL ? "- khong thay mang" :
                    s == WL_CONNECT_FAILED ? "- sai mat khau?" : "- dang thu...");
    }
    return;
  }
  if (!daBaoIP) {
    daBaoIP = true;
    Serial.printf("WiFi: da ket noi, IP ESP32 = %s, may chu = %s\n", WiFi.localIP().toString().c_str(), MQTT_HOST);
  }
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lanThuCuoi < 3000) return;
  lanThuCuoi = millis();
  bool ok = strlen(MQTT_USER)
              ? mqtt.connect("esp32-o-do", MQTT_USER, MQTT_PASS)
              : mqtt.connect("esp32-o-do");
  if (ok) {
    Serial.println("MQTT: da ket noi");
    guiTrangThai();
  } else {
    Serial.printf("MQTT: loi ket noi (%d), thu lai sau 3s\n", mqtt.state());
  }
}
#endif

// ------------------ SETUP & LOOP ------------------
void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < SO_O; i++) {
    pinMode(TRIG[i], OUTPUT);
    pinMode(ECHO[i], INPUT);
    pinMode(LED_XANH[i], OUTPUT);
    pinMode(LED_DO[i], OUTPUT);
  }

  Wire.begin(21, 22);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Khong tim thay OLED (kiem tra day SDA/SCL)");
  }

  // Đọc trạng thái ban đầu
  for (uint8_t i = 0; i < SO_O; i++) {
    coXe[i] = doKhoangCach(TRIG[i], ECHO[i]) < NGUONG_CM;
    capNhatLED(i);
    delay(20);
  }
  veOLED();
  Serial.println("ESP32 O DO: san sang");

#if DUNG_MQTT
  WiFi.mode(WIFI_STA);
  quetWifi();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
#endif
}

void loop() {
#if DUNG_MQTT
  giuKetNoi();
#endif

  bool thayDoi = false;
  for (uint8_t i = 0; i < SO_O; i++) {
    float d = doKhoangCach(TRIG[i], ECHO[i]);
    bool doc = d < NGUONG_CM;
    if (doc != coXe[i]) {
      if (++dem[i] >= SO_LAN_XAC_NHAN) {   // đọc giống nhau 3 lần mới đổi
        coXe[i] = doc;
        dem[i] = 0;
        capNhatLED(i);
        thayDoi = true;
        Serial.printf("O %u: %s (%.1f cm)\n", i + 1, coXe[i] ? "CO XE" : "TRONG", d);
      }
    } else {
      dem[i] = 0;
    }
    delay(20);   // tránh nhiễu sóng giữa các cảm biến
  }

  if (thayDoi) {
    veOLED();
    guiTrangThai();
  }
}
