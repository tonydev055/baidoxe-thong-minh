/*
 * TEST PHẦN CỨNG ESP32 SỐ 2 (CỔNG VÀO / RA)
 * Không cần WiFi/MQTT. Mở Serial Monitor 115200.
 *
 * Bước 1: kiểm tra 2 đầu đọc RC522 (báo OK hoặc LỖI DÂY)
 * Bước 2: còi vào kêu 1 tiếng, còi ra kêu 2 tiếng
 * Bước 3: servo vào mở rồi đóng, sau đó servo ra mở rồi đóng
 * Bước 4: chạy liên tục:
 *   - Quẹt thẻ ở đầu đọc nào -> in mã thẻ, còi kêu, thanh chắn cổng đó mở 3 giây
 *   - In khoảng cách 2 cảm biến; < 8 cm báo có xe
 *
 * Gõ trong Serial Monitor:  s = test lại servo,  c = test lại còi,  r = test lại RC522
 *
 * LƯU Ý: servo lấy nguồn 5V từ adapter (thanh nguồn breadboard), GND adapter nối chung GND ESP32.
 * Thư viện: MFRC522, ESP32Servo
 */
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>

// RC522 dùng chung SPI: SCK 18, MISO 19, MOSI 23, RST 22. SDA (SS) riêng: vào 5, ra 4
MFRC522 theVao(5, 22);
MFRC522 theRa(4, 22);
Servo servoVao, servoRa;

struct Cong {
  const char* ten;
  MFRC522* the;
  Servo* servo;
  uint8_t trig, echo, servoPin, coi;
  unsigned long dongLuc;   // 0 = đang đóng
};

Cong cong[2] = {
  {"VAO", &theVao, &servoVao, 25, 34, 32, 27, 0},
  {"RA",  &theRa,  &servoRa,  26, 35, 33, 17, 0},
};

const int GOC_DONG = 0;
const int GOC_MO   = 90;
const float NGUONG_CM = 8.0;

// Bíp 2 kHz tạo bằng tay: kêu được cả còi chủ động lẫn còi thụ động
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
  if (t == 0) return -1;
  return t / 58.0;
}

void testRC522() {
  Serial.println("\n--- TEST RC522 ---");
  for (auto& c : cong) {
    byte v = c.the->PCD_ReadRegister(MFRC522::VersionReg);
    Serial.printf("Dau doc %s: version 0x%02X -> ", c.ten, v);
    if (v == 0x00 || v == 0xFF)
      Serial.println("LOI DAY (kiem tra SDA, SCK 18, MOSI 23, MISO 19, RST 22, 3V3, GND)");
    else
      Serial.println("OK");
  }
}

void testCoi() {
  Serial.println("\n--- TEST COI ---");
  Serial.println("Coi VAO (chan 27): 1 tieng");
  bip(cong[0].coi, 300);
  delay(500);
  Serial.println("Coi RA  (chan 17): 2 tieng");
  bip(cong[1].coi, 150);
  delay(150);
  bip(cong[1].coi, 150);
}

void testServo() {
  Serial.println("\n--- TEST SERVO ---");
  for (auto& c : cong) {
    Serial.printf("Servo %s (chan %d): MO\n", c.ten, c.servoPin);
    c.servo->write(GOC_MO);
    delay(1000);
    Serial.printf("Servo %s: DONG\n", c.ten);
    c.servo->write(GOC_DONG);
    delay(1000);
  }
}

String docUID(MFRC522* r) {
  String uid;
  for (byte i = 0; i < r->uid.size; i++) {
    if (i) uid += ":";
    if (r->uid.uidByte[i] < 0x10) uid += "0";
    uid += String(r->uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===== TEST ESP32 SO 2 (CONG) =====");

  SPI.begin();               // SCK 18, MISO 19, MOSI 23
  theVao.PCD_Init();
  theRa.PCD_Init();

  for (auto& c : cong) {
    pinMode(c.trig, OUTPUT);
    pinMode(c.echo, INPUT);
    pinMode(c.coi, OUTPUT);
    digitalWrite(c.coi, LOW);
    c.servo->setPeriodHertz(50);
    c.servo->attach(c.servoPin, 500, 2400);
    c.servo->write(GOC_DONG);
  }

  testRC522();
  testCoi();
  testServo();
  Serial.println("\n--- CHAY THU: quet the / dua tay truoc cam bien (s = servo, c = coi, r = RC522) ---");
}

unsigned long inLuc = 0;

void loop() {
  if (Serial.available()) {
    char k = Serial.read();
    if (k == 's' || k == 'S') testServo();
    if (k == 'c' || k == 'C') testCoi();
    if (k == 'r' || k == 'R') testRC522();
  }

  for (auto& c : cong) {
    // Quẹt thẻ -> mở cổng đó 3 giây
    if (c.the->PICC_IsNewCardPresent() && c.the->PICC_ReadCardSerial()) {
      Serial.printf(">>> Cong %s: the %s -> MO 3 giay\n", c.ten, docUID(c.the).c_str());
      c.the->PICC_HaltA();
      bip(c.coi, 100);
      c.servo->write(GOC_MO);
      c.dongLuc = millis() + 3000;
    }
    if (c.dongLuc && millis() > c.dongLuc) {
      c.servo->write(GOC_DONG);
      c.dongLuc = 0;
      Serial.printf(">>> Cong %s: DONG\n", c.ten);
    }
  }

  if (millis() - inLuc > 500) {
    inLuc = millis();
    for (auto& c : cong) {
      float d = doKhoangCach(c.trig, c.echo);
      if (d < 0) Serial.printf("Cam bien %s: ---- (khong tin hieu)   ", c.ten);
      else       Serial.printf("Cam bien %s: %5.1f cm %s   ", c.ten, d, d < NGUONG_CM ? "[CO XE]" : "       ");
      delay(30);
    }
    Serial.println();
  }
}
