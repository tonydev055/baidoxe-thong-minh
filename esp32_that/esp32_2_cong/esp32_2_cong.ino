/*
 * ============================================================
 *  ESP32 SỐ 2 - CỔNG VÀO / CỔNG RA
 *  - 2 đầu đọc RFID RC522 (quẹt thẻ)
 *  - 2 cảm biến siêu âm đặt SAU thanh chắn (biết xe đã đi qua)
 *  - 2 servo SG90 làm thanh chắn
 *  - 2 còi báo
 *
 *  Luồng mỗi cổng:
 *   ĐÓNG --quẹt thẻ--> (chờ Pi duyệt biển số) --> MỞ
 *   MỞ --cảm biến thấy xe--> ĐANG QUA --hết xe 1 giây--> ĐÓNG
 *   MỞ quá 15 giây mà không có xe qua --> tự ĐÓNG
 *
 *  MÔ PHỎNG TRÊN WOKWI:
 *   - Bấm vào đầu đọc RFID để chọn và quẹt thẻ.
 *   - Bấm vào cảm biến HC-SR04, kéo khoảng cách xuống dưới 8 cm
 *     (= xe đang đi qua), rồi kéo lên lại (= xe đã qua).
 * ============================================================
 */
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ------------------ CẤU HÌNH ------------------
#define DUNG_MQTT    1   // 1: quẹt thẻ -> máy chủ đọc biển số rồi gửi lệnh mở / từ chối

// ESP32 chỉ bắt được WiFi 2.4 GHz (không bắt được mạng 5G)
const char* WIFI_SSID = "TonyDev";   // <-- sửa: tên WiFi
const char* WIFI_PASS = "12345678";     // <-- sửa: mật khẩu WiFi
const char* MQTT_HOST = "172.20.10.2";       // IP laptop đang chạy Mosquitto (dùng Pi thì đổi IP Pi)
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "";

// ------------------ CHÂN ------------------
// RC522 dùng chung SPI: SCK 18, MISO 19, MOSI 23, RST 22. SDA riêng: vào 5, ra 4
const uint8_t PIN_RST = 22;

// ------------------ THÔNG SỐ ------------------
const float NGUONG_CM = 8.0;                 // nhỏ hơn => có xe trước cảm biến
const int GOC_DONG = 0;
const int GOC_MO   = 90;
const unsigned long CHO_DUYET_MS = 25000;    // chờ Pi chụp ảnh + đọc biển số tối đa
const unsigned long MO_TOI_DA_MS = 15000;    // mở mà không có xe qua => tự đóng
const unsigned long CHO_DONG_MS  = 1000;     // xe qua hẳn rồi chờ bao lâu mới đóng

enum TrangThai { DONG, CHO_DUYET, MO, DANG_QUA };

MFRC522 theVao(5, PIN_RST);
MFRC522 theRa(4, PIN_RST);
Servo servoVao, servoRa;

struct Cong {
  const char* ten;     // "vao" / "ra"
  MFRC522* the;
  Servo* servo;
  uint8_t trig, echo, servoPin, coi;
  TrangThai tt;
  unsigned long moc;       // thời điểm vào trạng thái hiện tại
  unsigned long hetXeLuc;  // thời điểm cảm biến hết thấy xe
};

Cong cong[2] = {
  {"vao", &theVao, &servoVao, 25, 34, 32, 27, DONG, 0, 0},
  {"ra",  &theRa,  &servoRa,  26, 35, 33, 17, DONG, 0, 0},
};

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ------------------ TIỆN ÍCH ------------------
// Bíp 2 kHz tạo bằng tay (giống code test đã kêu được): hợp cả còi chủ động lẫn thụ động
void bip(uint8_t pin, unsigned int ms) {
  unsigned long batDau = millis();
  while (millis() - batDau < ms) {
    digitalWrite(pin, HIGH); delayMicroseconds(250);
    digitalWrite(pin, LOW);  delayMicroseconds(250);
  }
}

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

bool coXe(Cong& c) {
  return doKhoangCach(c.trig, c.echo) < NGUONG_CM;
}

bool docThe(MFRC522* r, String& uid) {
  if (!r->PICC_IsNewCardPresent() || !r->PICC_ReadCardSerial()) return false;
  uid = "";
  for (byte i = 0; i < r->uid.size; i++) {
    if (i) uid += ":";
    if (r->uid.uidByte[i] < 0x10) uid += "0";
    uid += String(r->uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  r->PICC_HaltA();
  return true;
}

void guiMQTT(const char* loai, const char* ten, const char* noiDung, bool retain) {
#if DUNG_MQTT
  if (!mqtt.connected()) return;
  char topic[32];
  snprintf(topic, sizeof(topic), "baidoxe/%s/%s", loai, ten);
  mqtt.publish(topic, noiDung, retain);
#endif
}

// ------------------ ĐIỀU KHIỂN CỔNG ------------------
void datTrangThai(Cong& c, TrangThai tt) {
  c.tt = tt;
  c.moc = millis();
  c.hetXeLuc = 0;
}

void moCong(Cong& c) {
  c.servo->write(GOC_MO);
  datTrangThai(c, MO);
  Serial.printf("[%s] MO thanh chan\n", c.ten);
  guiMQTT("cong", c.ten, "mo", true);
}

void dongCong(Cong& c) {
  c.servo->write(GOC_DONG);
  datTrangThai(c, DONG);
  Serial.printf("[%s] DONG thanh chan\n", c.ten);
  guiMQTT("cong", c.ten, "dong", true);
}

void xuLyCong(Cong& c) {
  switch (c.tt) {
    case DONG: {
      String uid;
      if (docThe(c.the, uid)) {
        Serial.printf("[%s] Quet the %s\n", c.ten, uid.c_str());
        bip(c.coi, 250);                              // 1 tiếng = đã nhận thẻ, đang chờ Pi
#if DUNG_MQTT
        guiMQTT("the", c.ten, uid.c_str(), false);   // Pi chụp ảnh, đọc biển, trả lời
        datTrangThai(c, CHO_DUYET);
#else
        moCong(c);                                    // mô phỏng: tự cho qua
#endif
      }
      break;
    }
    case CHO_DUYET:
      if (millis() - c.moc > CHO_DUYET_MS) {
        Serial.printf("[%s] Pi khong tra loi, huy luot\n", c.ten);
        bip(c.coi, 600);
        datTrangThai(c, DONG);
      }
      break;

    case MO:
      if (coXe(c)) {
        Serial.printf("[%s] Xe dang di qua\n", c.ten);
        datTrangThai(c, DANG_QUA);
      } else if (millis() - c.moc > MO_TOI_DA_MS) {
        Serial.printf("[%s] Khong co xe qua, tu dong\n", c.ten);
        dongCong(c);
      }
      break;

    case DANG_QUA:
      if (coXe(c)) {
        c.hetXeLuc = 0;                       // xe vẫn còn dưới cảm biến: giữ mở
      } else if (c.hetXeLuc == 0) {
        c.hetXeLuc = millis();                // vừa hết thấy xe: bắt đầu đếm
      } else if (millis() - c.hetXeLuc > CHO_DONG_MS) {
        Serial.printf("[%s] Xe da qua han\n", c.ten);
        dongCong(c);
      }
      break;
  }
}

// ------------------ MQTT ------------------
#if DUNG_MQTT
// Pi gửi "mo" hoặc "tuchoi" vào baidoxe/lenh/vao hoặc baidoxe/lenh/ra
void nhanLenh(char* topic, byte* payload, unsigned int len) {
  String lenh;
  for (unsigned int i = 0; i < len; i++) lenh += (char)payload[i];
  for (auto& c : cong) {
    if (String("baidoxe/lenh/") + c.ten != topic) continue;
    if (lenh == "mo") {
      // Mở khi đang chờ Pi duyệt, hoặc khi người trông xe bấm mở từ trang web
      if (c.tt == CHO_DUYET || c.tt == DONG) {
        bip(c.coi, 120); delay(80); bip(c.coi, 120);   // 2 tiếng ngắn = được qua
        moCong(c);
      }
    } else if (lenh == "tuchoi" && c.tt == CHO_DUYET) {
      Serial.printf("[%s] Pi tu choi (bien so khong khop)\n", c.ten);
      bip(c.coi, 600);
      datTrangThai(c, DONG);
    }
  }
}

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
              ? mqtt.connect("esp32-cong", MQTT_USER, MQTT_PASS)
              : mqtt.connect("esp32-cong");
  if (ok) {
    Serial.println("MQTT: da ket noi");
    mqtt.subscribe("baidoxe/lenh/+");
  } else {
    Serial.printf("MQTT: loi ket noi (%d), thu lai sau 3s\n", mqtt.state());
  }
}
#endif

// ------------------ SETUP & LOOP ------------------
void setup() {
  Serial.begin(115200);

  SPI.begin();          // SCK 18, MISO 19, MOSI 23
  theVao.PCD_Init();
  theRa.PCD_Init();

  for (auto& c : cong) {
    pinMode(c.trig, OUTPUT);
    pinMode(c.echo, INPUT);
    pinMode(c.coi, OUTPUT);
    c.servo->setPeriodHertz(50);
    c.servo->attach(c.servoPin, 500, 2400);
    c.servo->write(GOC_DONG);
  }
  Serial.println("ESP32 CONG: san sang - moi quet the");

#if DUNG_MQTT
  WiFi.mode(WIFI_STA);
  quetWifi();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(nhanLenh);
#endif
}

void loop() {
#if DUNG_MQTT
  giuKetNoi();
#endif
  for (auto& c : cong) {
    xuLyCong(c);
    delay(20);
  }
}
