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
 *  TỰ KIỂM TRA THIẾT BỊ (gửi lên Pi qua baidoxe/tb/cong, hiện ở mục "Tình trạng thiết bị"):
 *   - RC522: đọc thanh ghi phiên bản, đọc không được => lỗi dây / mất nguồn 3.3V
 *   - Siêu âm: chân ECHO không phản hồi => lỗi dây / mất nguồn 5V
 *   - ESP32 khởi động lại vì sụt nguồn => thường do servo kéo dòng
 *   - Servo, còi: KHÔNG tự đo được (không có tín hiệu phản hồi) -> xem HUONG_DAN_LOI.md
 *  Lúc bật nguồn mỗi cổng kêu: 1 tiếng ngắn = đầu đọc thẻ OK, 3 tiếng = đầu đọc thẻ lỗi.
 *  Không kêu tiếng nào => còi hỏng / lỏng dây.
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
#include <esp_system.h>

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
const unsigned long KIEM_TRA_MS  = 5000;     // bao lâu tự kiểm tra thiết bị 1 lần
const unsigned long BAO_CAO_MS   = 30000;    // không có gì đổi vẫn gửi báo cáo định kỳ
const uint8_t SO_LAN_LOI_SA = 3;             // siêu âm không phản hồi liên tiếp bấy nhiêu lần => lỗi

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
  bool rc522Ok;            // kết quả tự kiểm tra
  byte rc522Ver;
  uint8_t saLoiLienTiep;   // số lần siêu âm không phản hồi liên tiếp
};

Cong cong[2] = {
  {"vao", &theVao, &servoVao, 25, 34, 32, 27, DONG, 0, 0, false, 0, 0},
  {"ra",  &theRa,  &servoRa,  26, 35, 33, 17, DONG, 0, 0, false, 0, 0},
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

// Đo khoảng cách (cm).
//   999 = cảm biến chạy nhưng không có vật cản (không có tiếng dội)
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

// Đo + ghi nhận lỗi cảm biến. Cảm biến lỗi thì coi như không có xe.
float doCong(Cong& c) {
  float d = doKhoangCach(c.trig, c.echo);
  if (d < 0) {
    if (c.saLoiLienTiep < 255) c.saLoiLienTiep++;
  } else {
    c.saLoiLienTiep = 0;
  }
  return d;
}

bool coXe(Cong& c) {
  float d = doCong(c);
  return d >= 0 && d < NGUONG_CM;
}

bool saOk(Cong& c) {
  return c.saLoiLienTiep < SO_LAN_LOI_SA;
}

// RC522 trả về phiên bản chip (0x91, 0x92, bản nhái 0x88/0x12/0xB2...). 0x00 hoặc 0xFF = không đọc được
bool kiemTraRC522(Cong& c) {
  c.rc522Ver = c.the->PCD_ReadRegister(MFRC522::VersionReg);
  bool ok = c.rc522Ver != 0x00 && c.rc522Ver != 0xFF;
  if (!ok) {
    c.the->PCD_Init();                         // thử khởi động lại (vd vừa cắm lại dây)
    c.rc522Ver = c.the->PCD_ReadRegister(MFRC522::VersionReg);
    ok = c.rc522Ver != 0x00 && c.rc522Ver != 0xFF;
  }
  c.rc522Ok = ok;
  return ok;
}

const char* lyDoKhoiDong() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "bat_nguon";
    case ESP_RST_BROWNOUT: return "sut_nguon";      // nguồn yếu, thường do servo kéo dòng
    case ESP_RST_PANIC:    return "loi_chuong_trinh";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "bi_treo";
    case ESP_RST_SW:       return "khoi_dong_lai";
    case ESP_RST_EXT:      return "nut_reset";
    default:               return "khac";
  }
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

// Báo cáo tình trạng thiết bị lên Pi (retain: Pi khởi động sau vẫn nhận được)
void guiBaoCao() {
  if (!mqtt.connected()) return;
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"rc522_vao\":%d,\"rc522_ra\":%d,\"sa_vao\":%d,\"sa_ra\":%d,"
           "\"ver_vao\":\"0x%02X\",\"ver_ra\":\"0x%02X\",\"reset\":\"%s\",\"uptime\":%lu,\"rssi\":%d}",
           cong[0].rc522Ok, cong[1].rc522Ok, saOk(cong[0]), saOk(cong[1]),
           cong[0].rc522Ver, cong[1].rc522Ver, lyDoKhoiDong(), millis() / 1000, (int)WiFi.RSSI());
  mqtt.publish("baidoxe/tb/cong", buf, true);
}

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
  // "Di chúc" (LWT): ESP32 mất điện / rớt mạng thì broker tự báo baidoxe/tb/cong/online = 0
  bool ok = strlen(MQTT_USER)
              ? mqtt.connect("esp32-cong", MQTT_USER, MQTT_PASS, "baidoxe/tb/cong/online", 0, true, "0")
              : mqtt.connect("esp32-cong", "baidoxe/tb/cong/online", 0, true, "0");
  if (ok) {
    Serial.println("MQTT: da ket noi");
    mqtt.publish("baidoxe/tb/cong/online", "1", true);
    guiBaoCao();
    mqtt.subscribe("baidoxe/lenh/+");
    for (auto& c : cong)   // báo lại trạng thái cổng hiện tại cho trang web
      guiMQTT("cong", c.ten, (c.tt == MO || c.tt == DANG_QUA) ? "mo" : "dong", true);
  } else {
    Serial.printf("MQTT: loi ket noi (%d), thu lai sau 3s\n", mqtt.state());
  }
}
#endif

// ------------------ TỰ KIỂM TRA THIẾT BỊ ------------------
void inKetQuaKiemTra() {
  for (auto& c : cong) {
    Serial.printf("[%s] RC522: %s (phien ban 0x%02X) | Sieu am: %s\n", c.ten,
                  c.rc522Ok ? "OK" : "LOI - kiem tra day SDA/SCK/MOSI/MISO/RST va nguon 3.3V",
                  c.rc522Ver,
                  saOk(c) ? "OK" : "LOI - ECHO khong phan hoi, kiem tra day TRIG/ECHO va nguon 5V");
  }
}

// Kiểm tra định kỳ. Có gì thay đổi thì in ra Serial và báo ngay lên Pi.
void kiemTraThietBi(bool batBuoc) {
  static unsigned long lanKiemTra = 0, lanBaoCao = 0;
  if (!batBuoc && millis() - lanKiemTra < KIEM_TRA_MS) return;
  lanKiemTra = millis();

  bool thayDoi = false;
  for (auto& c : cong) {
    bool cuRc = c.rc522Ok, cuSa = saOk(c);
    kiemTraRC522(c);
    if (c.tt == DONG || c.tt == CHO_DUYET) doCong(c);   // lúc mở cổng thì xuLyCong đã đo rồi
    thayDoi |= (cuRc != c.rc522Ok) || (cuSa != saOk(c));
  }
  if (thayDoi || batBuoc) inKetQuaKiemTra();
#if DUNG_MQTT
  if (thayDoi || batBuoc || millis() - lanBaoCao > BAO_CAO_MS) {
    lanBaoCao = millis();
    guiBaoCao();
  }
#endif
}

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

  Serial.printf("Ly do khoi dong: %s\n", lyDoKhoiDong());
  if (esp_reset_reason() == ESP_RST_BROWNOUT)
    Serial.println("CANH BAO: ESP32 vua khoi dong lai vi SUT NGUON - cap nguon 5V rieng cho servo, noi chung GND");

  // Tự kiểm tra lúc bật: mỗi cổng kêu 1 tiếng = đầu đọc thẻ OK, 3 tiếng = lỗi
  for (auto& c : cong) {
    for (uint8_t i = 0; i < SO_LAN_LOI_SA; i++) { doCong(c); delay(30); }
    if (kiemTraRC522(c)) {
      bip(c.coi, 80);
    } else {
      for (uint8_t i = 0; i < 3; i++) { bip(c.coi, 80); delay(120); }
    }
    delay(300);
  }
  inKetQuaKiemTra();
  Serial.println("ESP32 CONG: san sang - moi quet the");

#if DUNG_MQTT
  WiFi.mode(WIFI_STA);
  quetWifi();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
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
  kiemTraThietBi(false);
}
