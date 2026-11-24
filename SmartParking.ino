/*
 * ============================================================
 *  MÔ HÌNH BÃI ĐỖ XE THU NHỎ
 *  Sử dụng cảm biến siêu âm để phát hiện vị trí trống
 *  và quản lý chỗ đỗ xe thông minh
 * ============================================================
 *  Phần cứng:
 *   - Arduino Uno R3
 *   - 4 x HC-SR04 (mỗi ô đỗ 1 cảm biến)
 *   - 4 x LED xanh + 4 x LED đỏ (+ 8 điện trở 220Ω)
 *       xanh sáng = ô trống, đỏ sáng = ô có xe
 *   - 1 x LCD 16x2 có module I2C
 *
 *  Thư viện: "LiquidCrystal I2C" (Frank de Brabander)
 * ============================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define SO_O 4

// Chân cảm biến của từng ô
const uint8_t TRIG[SO_O] = {2, 4, 6, 8};
const uint8_t ECHO[SO_O] = {3, 5, 7, 9};

// Chân LED của từng ô
const uint8_t LED_XANH[SO_O] = {10, 11, 12, 13};
const uint8_t LED_DO[SO_O]   = {A0, A1, A2, A3};
// LCD I2C: SDA = A4, SCL = A5

// Ngưỡng: khoảng cách nhỏ hơn mức này => ô có xe (chỉnh theo mô hình)
const float NGUONG = 6.0;          // cm
const uint8_t SO_LAN_XAC_NHAN = 3; // chống nhiễu

LiquidCrystal_I2C lcd(0x27, 16, 2); // đổi thành 0x3F nếu LCD không hiện chữ

bool coXe[SO_O];
uint8_t dem[SO_O] = {0};

// Đo khoảng cách (cm), không có tín hiệu dội về thì trả về 999
float doKhoangCach(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long t = pulseIn(echo, HIGH, 25000UL);
  if (t == 0) return 999.0;
  return t * 0.0343 / 2.0;
}

void capNhatLED(uint8_t i) {
  digitalWrite(LED_XANH[i], coXe[i] ? LOW : HIGH);
  digitalWrite(LED_DO[i],   coXe[i] ? HIGH : LOW);
}

void guiTrangThai() {
  // Gửi lên máy tính qua Serial, dạng: TRONG:2/4 O1:1 O2:0 O3:0 O4:1
  uint8_t trong = 0;
  for (uint8_t i = 0; i < SO_O; i++) if (!coXe[i]) trong++;
  Serial.print(F("TRONG:")); Serial.print(trong); Serial.print('/'); Serial.print(SO_O);
  for (uint8_t i = 0; i < SO_O; i++) {
    Serial.print(F(" O")); Serial.print(i + 1); Serial.print(':'); Serial.print(coXe[i] ? 1 : 0);
  }
  Serial.println();
}

void hienThiLCD() {
  uint8_t trong = 0;
  for (uint8_t i = 0; i < SO_O; i++) if (!coXe[i]) trong++;

  lcd.setCursor(0, 0);
  if (trong == 0) {
    lcd.print(F("  BAI DA DAY!   "));
  } else {
    lcd.print(F("CON TRONG: "));
    lcd.print(trong); lcd.print('/'); lcd.print(SO_O);
    lcd.print(F("   "));
  }

  // Dòng 2: X = có xe, O = trống
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < SO_O; i++) {
    lcd.print(i + 1); lcd.print(':'); lcd.print(coXe[i] ? 'X' : 'O');
    if (i < SO_O - 1) lcd.print(' ');
  }
  lcd.print(F("  "));
}

void setup() {
  Serial.begin(9600);
  for (uint8_t i = 0; i < SO_O; i++) {
    pinMode(TRIG[i], OUTPUT);
    pinMode(ECHO[i], INPUT);
    pinMode(LED_XANH[i], OUTPUT);
    pinMode(LED_DO[i], OUTPUT);
  }

  lcd.init();
  lcd.backlight();
  lcd.print(F("BAI DO XE THONG"));
  lcd.setCursor(0, 1);
  lcd.print(F("MINH"));
  delay(1500);
  lcd.clear();

  // Đọc trạng thái ban đầu
  for (uint8_t i = 0; i < SO_O; i++) {
    coXe[i] = doKhoangCach(TRIG[i], ECHO[i]) < NGUONG;
    capNhatLED(i);
    delay(30);
  }
  hienThiLCD();
  guiTrangThai();
}

void loop() {
  bool thayDoi = false;

  for (uint8_t i = 0; i < SO_O; i++) {
    float d = doKhoangCach(TRIG[i], ECHO[i]);
    bool doc = d < NGUONG;

    if (doc != coXe[i]) {
      if (++dem[i] >= SO_LAN_XAC_NHAN) {   // đọc giống nhau 3 lần mới đổi
        coXe[i] = doc;
        dem[i] = 0;
        capNhatLED(i);
        thayDoi = true;
      }
    } else {
      dem[i] = 0;
    }
    delay(30); // tránh nhiễu sóng giữa các cảm biến
  }

  if (thayDoi) {
    hienThiLCD();
    guiTrangThai();
  }
}
