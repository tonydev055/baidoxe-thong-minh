/*
 * KIỂM TRA DÂY KHÔNG CẦN RÚT (ESP32 SỐ 2 - CỔNG)
 * Giữ nguyên mạch, chỉ nạp code này. Mở Serial Monitor 115200, bấm EN, chờ ~20 giây.
 *
 * Phần 1 - Còi: xem chân P27 (còi vào) và P17 (còi ra) có đang nối tới còi không.
 * Phần 2 - RC522: thử mọi trường hợp 1 hoặc 2 dây SDA/SCK/MOSI/MISO cắm lệch
 *          sang chân khác của ESP32. Tìm thấy thì báo dây nào đang ở chân nào.
 *          Kiểm tra cho cả module VÀO (SDA P5) và module RA (SDA P4).
 * Không cần thư viện.
 */
#include <SPI.h>

const uint8_t RA_VAO[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
const uint8_t CHI_VAO[] = {34, 35, 36, 39};          // chân chỉ đọc được (chỉ có thể là MISO)
const int SO_RV = sizeof(RA_VAO);
const uint8_t KHONG_GIU_CAO[] = {32, 33, 27, 17};    // servo, còi: để mức thấp cho yên

const char* TEN[4] = {"SDA ", "SCK ", "MOSI", "MISO"};

bool giuCao(uint8_t p) {
  for (uint8_t x : KHONG_GIU_CAO) if (x == p) return false;
  return true;
}

void chuanBi() {
  for (int i = 0; i < SO_RV; i++) {
    pinMode(RA_VAO[i], OUTPUT);
    digitalWrite(RA_VAO[i], giuCao(RA_VAO[i]) ? HIGH : LOW);   // RST dù ở chân nào cũng được giữ cao
  }
}

bool versionTot(byte v) { return v == 0x91 || v == 0x92 || v == 0xB2 || v == 0x88 || v == 0x12; }

byte docVersion(const int p[4]) {   // p = {sda, sck, mosi, miso}
  SPI.end();
  chuanBi();
  SPI.begin(p[1], p[3], p[2], -1);
  pinMode(p[0], OUTPUT);
  digitalWrite(p[0], HIGH);
  delay(2);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(p[0], LOW);
  SPI.transfer(0x80 | (0x37 << 1));
  byte v = SPI.transfer(0x00);
  digitalWrite(p[0], HIGH);
  SPI.endTransaction();
  return v;
}

// danh sách chân có thể thử cho từng vai trò
int dsChan(int vaiTro, uint8_t* out) {
  int n = 0;
  for (int i = 0; i < SO_RV; i++) out[n++] = RA_VAO[i];
  if (vaiTro == 3) for (uint8_t x : CHI_VAO) out[n++] = x;
  return n;
}

bool trung(const int p[4]) {
  for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) if (p[a] == p[b]) return true;
  return false;
}

void baoTimThay(const int p[4], const int dung[4], byte v) {
  Serial.printf("  TIM THAY (version 0x%02X). Module dang noi:\n", v);
  for (int r = 0; r < 4; r++) {
    if (p[r] == dung[r]) Serial.printf("    %s -> P%-2d  (dung)\n", TEN[r], p[r]);
    else                 Serial.printf("    %s -> P%-2d  <-- SAI, phai chuyen sang P%d\n", TEN[r], p[r], dung[r]);
  }
}

bool doModule(const char* ten, int sdaDung) {
  const int dung[4] = {sdaDung, 18, 23, 19};
  Serial.printf("\n--- RC522 %s (SDA dung la P%d) ---\n", ten, sdaDung);

  byte v = docVersion(dung);
  if (versionTot(v)) { Serial.printf("  OK, day dung het (version 0x%02X)\n", v); return true; }
  Serial.printf("  Cam dung bang ma version 0x%02X -> dang do cac truong hop lech day...\n", v);

  uint8_t c1[30], c2[30];
  int p[4];

  // 1 dây lệch
  for (int r = 0; r < 4; r++) {
    int n = dsChan(r, c1);
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < 4; k++) p[k] = dung[k];
      p[r] = c1[i];
      if (p[r] == dung[r] || trung(p)) continue;
      v = docVersion(p);
      if (versionTot(v)) { baoTimThay(p, dung, v); return true; }
    }
  }
  // 2 dây lệch
  for (int r1 = 0; r1 < 4; r1++)
    for (int r2 = r1 + 1; r2 < 4; r2++) {
      int n1 = dsChan(r1, c1), n2 = dsChan(r2, c2);
      for (int i = 0; i < n1; i++)
        for (int j = 0; j < n2; j++) {
          for (int k = 0; k < 4; k++) p[k] = dung[k];
          p[r1] = c1[i]; p[r2] = c2[j];
          if (p[r1] == dung[r1] || p[r2] == dung[r2] || trung(p)) continue;
          v = docVersion(p);
          if (versionTot(v)) { baoTimThay(p, dung, v); return true; }
        }
    }

  Serial.println("  KHONG tim thay. Khong phai do cam lech chan, ma la:");
  Serial.println("   - Day long / dut ngam (hay gap o cho noi duc-duc voi cai-cai)");
  Serial.println("   - RST khong noi P22, hoac 3.3V/GND chap chon");
  Serial.println("   - Loi han chan hoac module hong (hoac chua cam module nay)");
  return false;
}

void kiemTraCoi() {
  Serial.println("\n--- COI ---");
  for (int i = 0; i < SO_RV; i++) pinMode(RA_VAO[i], INPUT_PULLUP);
  delay(50);
  bool tai27 = digitalRead(27) == LOW, tai17 = digitalRead(17) == LOW;
  Serial.printf("  P27 (coi vao): %s\n", tai27 ? "thay co coi noi vao" : "KHONG thay gi noi vao");
  Serial.printf("  P17 (coi ra) : %s\n", tai17 ? "thay co coi noi vao" : "KHONG thay gi noi vao");
  if (tai17 && !tai27)
    Serial.println("  => Coi ra noi dung, coi VAO thi day S khong toi P27: day long, cam lech chan, hoac thieu nguon/GND cho coi vao.");
  else if (tai27 && !tai17)
    Serial.println("  => Coi vao noi dung, coi RA thi day S khong toi P17.");
  else if (tai27 && tai17)
    Serial.println("  => Ca 2 chan deu thay coi. Neu coi vao van khong keu: coi vao hong hoac thieu nguon (+) cho coi vao.");
  else
    Serial.println("  => Loai coi nay khong do duoc bang cach nay (khong ket luan duoc).");
  for (int i = 0; i < SO_RV; i++) pinMode(RA_VAO[i], INPUT);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===== KIEM TRA DAY (KHONG CAN RUT) =====");
  kiemTraCoi();
  doModule("VAO", 5);
  doModule("RA ", 4);
  SPI.end();
  chuanBi();
  Serial.println("\nXong. Bam EN de chay lai.");
}

void loop() {}
