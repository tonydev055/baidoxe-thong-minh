/*
 * TEST PHẦN CỨNG ESP32 SỐ 1 (Ô ĐỖ)
 * Không cần WiFi/MQTT. Mở Serial Monitor 115200.
 *
 * Bước 1: quét I2C -> báo có thấy màn OLED không (địa chỉ 0x3C)
 * Bước 2: lần lượt sáng đèn XANH rồi ĐỎ của ô 1 -> 4 (nhìn xem đèn nào không sáng)
 * Bước 3: đo liên tục 4 cảm biến, in ra Serial + OLED.
 *         Đưa tay/xe lại gần cảm biến < 6 cm -> ô đó chuyển ĐỎ.
 *
 * Gõ "l" trong Serial Monitor để test lại đèn.
 *
 * Thư viện: Adafruit SSD1306, Adafruit GFX Library
 */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const uint8_t TRIG[4]     = {25, 26, 27, 14};
const uint8_t ECHO[4]     = {34, 35, 36, 39};   // 36 = SVP, 39 = SVN
const uint8_t LED_XANH[4] = {19, 5, 16, 2};
const uint8_t LED_DO[4]   = {18, 17, 4, 15};
const float NGUONG_CM = 6.0;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool coOled = false;

float doKhoangCach(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long t = pulseIn(echo, HIGH, 30000UL);
  if (t == 0) return -1;            // không có tín hiệu
  return t / 58.0;
}

void tatHet() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_XANH[i], LOW);
    digitalWrite(LED_DO[i], LOW);
  }
}

void testDen() {
  Serial.println("\n--- TEST DEN ---");
  tatHet();
  for (int i = 0; i < 4; i++) {
    Serial.printf("O %d: XANH (chan %d)\n", i + 1, LED_XANH[i]);
    digitalWrite(LED_XANH[i], HIGH); delay(700); digitalWrite(LED_XANH[i], LOW);
    Serial.printf("O %d: DO   (chan %d)\n", i + 1, LED_DO[i]);
    digitalWrite(LED_DO[i], HIGH);   delay(700); digitalWrite(LED_DO[i], LOW);
  }
  Serial.println("Tat ca cung sang 1 giay...");
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_XANH[i], HIGH);
    digitalWrite(LED_DO[i], HIGH);
  }
  delay(1000);
  tatHet();
}

void testOled() {
  Serial.println("\n--- QUET I2C (SDA 21, SCL 22) ---");
  int dem = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Thay thiet bi o dia chi 0x%02X\n", a);
      dem++;
    }
  }
  if (dem == 0) Serial.println("KHONG thay thiet bi nao -> kiem tra day OLED (VCC 3V3, GND, SDA 21, SCL 22)");

  coOled = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Serial.println(coOled ? "OLED: OK" : "OLED: LOI");
  if (coOled) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2);
    oled.setCursor(10, 20);
    oled.print("OLED OK");
    oled.display();
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===== TEST ESP32 SO 1 (O DO) =====");
  for (int i = 0; i < 4; i++) {
    pinMode(TRIG[i], OUTPUT);
    pinMode(ECHO[i], INPUT);
    pinMode(LED_XANH[i], OUTPUT);
    pinMode(LED_DO[i], OUTPUT);
  }
  Wire.begin(21, 22);
  testOled();
  testDen();
  Serial.println("\n--- DO CAM BIEN (go 'l' de test lai den) ---");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'l' || c == 'L') testDen();
  }

  float d[4];
  for (int i = 0; i < 4; i++) {
    d[i] = doKhoangCach(TRIG[i], ECHO[i]);
    bool coXe = d[i] > 0 && d[i] < NGUONG_CM;
    digitalWrite(LED_XANH[i], coXe ? LOW : HIGH);
    digitalWrite(LED_DO[i], coXe ? HIGH : LOW);
    delay(30);                                   // tránh dội sóng giữa các cảm biến
  }

  for (int i = 0; i < 4; i++) {
    if (d[i] < 0) Serial.printf("O%d: ---- (khong tin hieu)  ", i + 1);
    else          Serial.printf("O%d: %5.1f cm %s  ", i + 1, d[i], d[i] < NGUONG_CM ? "[XE]" : "    ");
  }
  Serial.println();

  if (coOled) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("TEST CAM BIEN");
    for (int i = 0; i < 4; i++) {
      oled.setCursor(0, 14 + i * 12);
      if (d[i] < 0) oled.printf("O%d: ---- loi day", i + 1);
      else          oled.printf("O%d: %5.1f cm %s", i + 1, d[i], d[i] < NGUONG_CM ? "XE" : "");
    }
    oled.display();
  }
  delay(300);
}
