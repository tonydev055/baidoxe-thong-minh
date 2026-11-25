/*
 * THEO DÕI 2 MODULE RC522 LIÊN TỤC
 * Mỗi giây in version của cả 2 module. Trong lúc chạy, lắc nhẹ / ấn từng dây:
 * dây nào làm số nhảy giữa OK và LOI => dây đó tiếp xúc kém.
 *
 *   RC522 VÀO: SDA -> P5      RC522 RA: SDA -> P4
 *   Dùng chung: SCK -> P18, MOSI -> P23, MISO -> P19, RST -> P22, 3.3V -> 3V3, GND -> GND
 *
 * Quẹt thẻ vào module nào cũng sẽ in mã thẻ.
 * Thư viện: MFRC522
 */
#include <SPI.h>
#include <MFRC522.h>

MFRC522 theVao(5, 22);
MFRC522 theRa(4, 22);

struct DauDoc { const char* ten; MFRC522* r; uint8_t sda; bool ok; };
DauDoc dauDoc[2] = { {"VAO (SDA P5)", &theVao, 5, false}, {"RA  (SDA P4)", &theRa, 4, false} };

bool versionTot(byte v) { return v == 0x91 || v == 0x92 || v == 0xB2 || v == 0x88 || v == 0x12; }

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===== THEO DOI 2 MODULE RC522 =====");
  pinMode(5, OUTPUT); digitalWrite(5, HIGH);   // bỏ chọn cả 2 trước khi khởi động
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  SPI.begin();          // SCK 18, MISO 19, MOSI 23
  for (auto& d : dauDoc) d.r->PCD_Init();
  Serial.println("Lac nhe tung day trong luc xem. Quet the de thu doc.\n");
}

unsigned long inLuc = 0;

void loop() {
  if (millis() - inLuc > 1000) {
    inLuc = millis();
    for (auto& d : dauDoc) {
      byte v = d.r->PCD_ReadRegister(MFRC522::VersionReg);
      Serial.printf("%s: 0x%02X %-6s   ", d.ten, v, versionTot(v) ? "OK" : "LOI");
      bool tot = versionTot(v);
      if (tot && !d.ok) d.r->PCD_Init();    // vừa kết nối lại -> khởi động lại module để đọc thẻ được
      d.ok = tot;
    }
    Serial.println();
  }

  for (auto& d : dauDoc) {
    if (d.r->PICC_IsNewCardPresent() && d.r->PICC_ReadCardSerial()) {
      Serial.printf(">>> %s doc duoc the: ", d.ten);
      for (byte i = 0; i < d.r->uid.size; i++) {
        if (i) Serial.print(":");
        Serial.printf("%02X", d.r->uid.uidByte[i]);
      }
      Serial.println();
      d.r->PICC_HaltA();
    }
  }
}
