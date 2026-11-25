/*
 * TEST RIÊNG 1 MODULE RC522
 * Chỉ cắm RC522 vào ESP32, tháo hết các thứ khác (servo, cảm biến, còi...).
 * Nên cắm thẳng bằng dây cái-cái, không qua breadboard.
 *
 *   RC522    ESP32
 *   SDA  ->  P5
 *   SCK  ->  P18
 *   MOSI ->  P23
 *   MISO ->  P19
 *   IRQ  ->  (bỏ trống)
 *   GND  ->  GND
 *   RST  ->  P22
 *   3.3V ->  3V3
 *
 * Mở Serial Monitor 115200, bấm EN. Sau đó quẹt thẻ.
 * Thư viện: MFRC522
 */
#include <SPI.h>
#include <MFRC522.h>

MFRC522 rfid(5, 22);   // SDA, RST

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===== TEST RIENG RC522 =====");

  SPI.begin();          // SCK 18, MISO 19, MOSI 23
  rfid.PCD_Init();
  delay(50);

  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("1) Version: 0x%02X  ", v);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("-> KHONG giao tiep duoc voi module");
    Serial.println("   Day da cam thang dung bang tren ma van loi => module hong hoac loi han.");
    return;
  }
  Serial.println("-> giao tiep OK");

  Serial.print("2) Tu kiem tra chip (self-test): ");
  bool ok = rfid.PCD_PerformSelfTest();
  Serial.println(ok ? "DAT" : "KHONG DAT (chip co the loi, hoac la ban nhai)");

  rfid.PCD_Init();      // self-test xong phải khởi động lại
  Serial.println("3) Moi quet the (dat sat mat module)...");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  Serial.print(">>> Doc duoc the, UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i) Serial.print(":");
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.print("   Loai: ");
  Serial.println(rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak)));
  rfid.PICC_HaltA();
}
