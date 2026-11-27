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
 *   MỞ --cảm biến thấy xe--> ĐANG QUA --hết xe 1 giây--> ĐÓNG + báo Pi (baidoxe/qua/<cổng>)
 *   MỞ mà xe chưa qua: KHÔNG tự đóng. Pi hỏi nhân viên trên web, xe không đi nữa
 *   thì Pi gửi lệnh "dong" để hạ thanh chắn
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
#define DUNG_MQTT    0   // 0: quẹt thẻ là tự mở (mô phỏng). 1: chờ Pi đọc biển số rồi gửi lệnh mở
#define COI_THU_DONG 1   // 1: còi thụ động (Wokwi). 0: module còi chủ động 3 chân (mạch thật)

const char* WIFI_SSID = "Wokwi-GUEST";   // mạch thật: đổi thành tên WiFi
const char* WIFI_PASS = "";
const char* MQTT_HOST = "192.168.1.50";  // IP của Raspberry Pi
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
const unsigned long CHO_DUYET_MS = 10000;    // chờ Pi trả lời tối đa
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
void bip(uint8_t pin, unsigned int ms) {
#if COI_THU_DONG
  tone(pin, 2000, ms);
#else
  digitalWrite(pin, HIGH);
  delay(ms);
  digitalWrite(pin, LOW);
#endif
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

// Trả về true nếu đã gửi được (false: chưa kết nối MQTT hoặc gửi lỗi)
bool guiMQTT(const char* loai, const char* ten, const char* noiDung, bool retain) {
#if DUNG_MQTT
  if (!mqtt.connected()) return false;
  char topic[32];
  snprintf(topic, sizeof(topic), "baidoxe/%s/%s", loai, ten);
  return mqtt.publish(topic, noiDung, retain);
#else
  return false;
#endif
}

// Xe đã đi qua cổng: báo Pi để kết thúc lượt. Đang mất MQTT thì nhớ lại, kết nối lại sẽ gửi.
bool chuaBaoQua[2] = {false, false};

void guiXeQuaConNo() {
  for (int i = 0; i < 2; i++) {
    if (chuaBaoQua[i] && guiMQTT("qua", cong[i].ten, "1", false)) chuaBaoQua[i] = false;
  }
}

void baoXeQua(Cong& c) {
  chuaBaoQua[&c - cong] = true;
  guiXeQuaConNo();
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
        bip(c.coi, 100);
#if DUNG_MQTT
        if (guiMQTT("the", c.ten, uid.c_str(), false)) {   // Pi chụp ảnh, đọc biển, trả lời
          datTrangThai(c, CHO_DUYET);
        } else {
          // Chưa nối được máy chủ: báo ngay thay vì bắt xe chờ hết CHO_DUYET_MS
          Serial.printf("[%s] Chua ket noi MQTT, khong gui duoc the\n", c.ten);
          bip(c.coi, 600);
        }
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
      // Không tự đóng: chờ xe đi qua cảm biến, hoặc lệnh "dong" từ Pi
      if (coXe(c)) {
        Serial.printf("[%s] Xe dang di qua\n", c.ten);
        datTrangThai(c, DANG_QUA);
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
        baoXeQua(c);
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
      if (c.tt == CHO_DUYET || c.tt == DONG) moCong(c);
    } else if (lenh == "dong") {
      // Nhân viên xác nhận xe không đi qua nữa (hoặc đóng tay từ web).
      // Chỉ hạ khi chưa có xe dưới cảm biến (trạng thái MỞ), tránh hạ trúng xe.
      if (c.tt == MO) {
        Serial.printf("[%s] Pi yeu cau dong cong\n", c.ten);
        dongCong(c);
      }
    } else if (lenh == "tuchoi" && c.tt == CHO_DUYET) {
      Serial.printf("[%s] Pi tu choi (bien so khong khop)\n", c.ten);
      bip(c.coi, 600);
      datTrangThai(c, DONG);
    }
  }
}

unsigned long lanThuCuoi = 0;
void giuKetNoi() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lanThuCuoi < 3000) return;
  lanThuCuoi = millis();
  bool ok = strlen(MQTT_USER)
              ? mqtt.connect("esp32-cong", MQTT_USER, MQTT_PASS)
              : mqtt.connect("esp32-cong");
  if (ok) {
    Serial.println("MQTT: da ket noi");
    mqtt.subscribe("baidoxe/lenh/+");
    for (auto& c : cong)   // báo lại trạng thái cổng hiện tại cho trang web
      guiMQTT("cong", c.ten, (c.tt == MO || c.tt == DANG_QUA) ? "mo" : "dong", true);
    guiXeQuaConNo();
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
