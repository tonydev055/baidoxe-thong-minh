/*
 * DÒ DÂY RC522 - tìm dây tín hiệu bị cắm nhầm
 * Mở Serial Monitor 115200, bấm EN để chạy lại.
 *
 * Code thử mọi cách gán 4 vai trò SDA / SCK / MOSI / MISO
 * lên các chân 5, 4, 18, 19, 23, 22 và đọc thanh ghi version của RC522.
 * Cách nào đọc ra 0x91 / 0x92 / 0xB2 => đó là cách bạn đang cắm thật.
 * Không cần thư viện MFRC522.
 */
#include <SPI.h>

const int CHAN[] = {5, 4, 18, 19, 23, 22};
const int SO_CHAN = sizeof(CHAN) / sizeof(CHAN[0]);
const int DUNG_SDA = 5, DUNG_SCK = 18, DUNG_MOSI = 23, DUNG_MISO = 19;

bool versionTot(byte v) { return v == 0x91 || v == 0x92 || v == 0xB2 || v == 0x88 || v == 0x12; }

void tatCaCao() {
  for (int i = 0; i < SO_CHAN; i++) { pinMode(CHAN[i], OUTPUT); digitalWrite(CHAN[i], HIGH); }
}

byte docVersion(int sda, int sck, int mosi, int miso) {
  SPI.end();
  tatCaCao();                       // giữ RST (22) và các chân khác ở mức cao
  SPI.begin(sck, miso, mosi, -1);
  pinMode(sda, OUTPUT);
  digitalWrite(sda, HIGH);
  delay(2);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(sda, LOW);
  SPI.transfer(0x80 | (0x37 << 1));  // đọc VersionReg
  byte v = SPI.transfer(0x00);
  digitalWrite(sda, HIGH);
  SPI.endTransaction();
  return v;
}

void inSoSanh(const char* ten, int thucTe, int dung) {
  Serial.printf("  %-5s dang cam vao P%-2d", ten, thucTe);
  if (thucTe == dung) Serial.println("  (dung)");
  else                Serial.printf("  <-- SAI, phai la P%d\n", dung);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===== DO DAY RC522 =====");

  byte vChuan = docVersion(DUNG_SDA, DUNG_SCK, DUNG_MOSI, DUNG_MISO);
  Serial.printf("Cach cam dung (SDA 5, SCK 18, MOSI 23, MISO 19): version 0x%02X\n", vChuan);
  if (versionTot(vChuan)) {
    Serial.println("=> DAY DUNG HET, module OK. Nap lai test_esp32_2 de quet the.");
    return;
  }

  Serial.println("Dang thu cac cach dao day...");
  int tim = 0;
  for (int a = 0; a < SO_CHAN; a++)
    for (int b = 0; b < SO_CHAN; b++)
      for (int c = 0; c < SO_CHAN; c++)
        for (int d = 0; d < SO_CHAN; d++) {
          if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
          int sda = CHAN[a], sck = CHAN[b], mosi = CHAN[c], miso = CHAN[d];
          byte v = docVersion(sda, sck, mosi, miso);
          if (versionTot(v)) {
            tim++;
            Serial.printf("\nTIM THAY (version 0x%02X). Ban dang cam:\n", v);
            inSoSanh("SDA",  sda,  DUNG_SDA);
            inSoSanh("SCK",  sck,  DUNG_SCK);
            inSoSanh("MOSI", mosi, DUNG_MOSI);
            inSoSanh("MISO", miso, DUNG_MISO);
          }
        }

  if (tim == 0) {
    Serial.println("\nKHONG TIM THAY cach nao. Kiem tra:");
    Serial.println(" - Day long / tiep xuc kem (thu cam thang khong qua breadboard)");
    Serial.println(" - RST phai noi P22, 3.3V noi 3V3, GND noi GND");
    Serial.println(" - Co the module hong");
  }
  Serial.println("\nXong. Bam EN de chay lai.");
}

void loop() {}
