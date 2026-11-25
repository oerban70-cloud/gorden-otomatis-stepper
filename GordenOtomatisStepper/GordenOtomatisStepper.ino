/*
  Gorden Otomatis dengan Motor Stepper + Sensor Cahaya — ESP32
  ============================================================
  Cara kerja:
  Sensor cahaya LDR di jendela membaca terang langit lewat pembagi
  tegangan (LDR + resistor 10k). Saat terang melewati ambang buka,
  ESP32 memutar motor stepper 28BYJ-48 lewat driver ULN2003 sehingga
  gorden terbuka. Saat gelap melewati ambang tutup, motor memutar
  balik dan gorden tertutup. Dua ambang yang berbeda (histeresis)
  mencegah gorden buka-tutup berulang saat cahaya berfluktuasi,
  misalnya saat awan lewat.

  Perjalanan penuh gorden = 12288 langkah setengah (half-step),
  kira-kira 3 putaran poros motor 28BYJ-48. Kalibrasi: ubah
  LANGKAH_PERJALANAN di bawah sesuai panjang rel gordenmu — hitung
  dengan cara menggerakkan gorden dari tertutup ke terbuka sambil
  mencatat langkah di Serial Monitor.

  Wiring (lihat schematic.png):
    LDR + resistor 10k (pembagi tegangan) -> GPIO34 (ADC1)
    ULN2003 IN1 -> GPIO19, IN2 -> GPIO18, IN3 -> GPIO5, IN4 -> GPIO17
    ULN2003 OUT1..OUT4 -> kabel motor (biru, merah muda, kuning, oranye)
    ULN2003 COM -> 5V, ULN2003 GND -> GND
    Motor disuplai adaptor 5V 2A sendiri — JANGAN dari pin 5V ESP32.
    LED bawaan board (GPIO2) menyala selama motor bergerak.

  Video penjelasan (60 detik): link menyusul setelah upload
  Repo: https://github.com/oerban70-cloud/gorden-otomatis-stepper
*/

// ---------- Konfigurasi pin ----------
const uint8_t PIN_SENSOR_LDR = 34;  // node pembagi tegangan LDR -> GPIO34 (ADC1, input-only)
const uint8_t PIN_LED_GERAK  = 2;   // LED bawaan board, menyala saat motor bergerak
// Urutan pin mengikuti urutan kumparan motor (IN1..IN4 driver ULN2003)
const uint8_t PIN_STEPPER[4] = {19, 18, 5, 17};  // IN1, IN2, IN3, IN4

// ---------- Konfigurasi sensor cahaya ----------
// ADC ESP32 12-bit: 0 (gelap total) sampai 4095 (terang penuh).
// LDR ke 3V3 + resistor 10k ke GND: makin terang, makin besar nilai ADC.
const uint16_t AMBANG_BUKA_ADC   = 2500;  // di atas ini -> buka gorden
const uint16_t AMBANG_TUTUP_ADC  = 1200;  // di bawah ini -> tutup gorden
// (nilai di antara kedua ambang = zona mati, gorden diam di posisinya)
const uint8_t  JUMLAH_SAMPEL     = 10;    // rata-rata 10 bacaan per pengukuran
const uint16_t JEDA_SAMPEL_MS    = 10;    // jeda antar bacaan sampel

// ---------- Konfigurasi motor stepper 28BYJ-48 ----------
// Mode half-step: 4096 langkah per putaran poros. Perjalanan penuh
// gorden disetarakan dengan 3 putaran poros (kalibrasi di lapangan).
const uint16_t LANGKAH_PER_PUTARAN   = 4096;   // half-step per putaran
const uint16_t PUTARAN_PERJALANAN    = 3;      // putaran untuk buka penuh
const uint32_t LANGKAH_PERJALANAN    = 12288;  // = 4096 x 3 (hitung sekali saja)
const uint8_t  JEDA_ANTAR_LANGKAH_MS = 2;      // jeda tiap langkah (kecepatan motor)
const uint16_t LANGKAH_PER_GILIRAN   = 96;     // langkah per putaran loop() (tidak memblokir)

// Urutan half-step 8 fase untuk 28BYJ-48 (satu kumparan aktif bergantian
// dengan dua kumparan aktif — torsi lebih halus daripada full-step).
const uint8_t URUTAN_HALF_STEP[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1},
};
const uint8_t JUMLAH_FASE = 8;

// ---------- Konfigurasi waktu ----------
const unsigned long PERIODE_BACA_MS  = 500;    // baca LDR tiap 500 ms
const unsigned long BAUD_SERIAL      = 115200;

// ---------- State ----------
enum PosisiGorden : uint8_t {
  POSISI_TERTUTUP = 0,
  POSISI_TERBUKA  = 1,
  POSISI_TENGAH   = 2,  // sedang di antara keduanya (mis. setelah reset)
};

uint32_t langkahSaatIni   = 0;      // 0 = tertutup penuh, LANGKAH_PERJALANAN = terbuka penuh
uint32_t langkahTarget    = 0;      // target gerakan saat ini
uint8_t  faseSaatIni      = 0;      // fase half-step aktif (0..7)
PosisiGorden posisiTerkini = POSISI_TERTUTUP;
unsigned long waktuBacaTerakhirMs = 0;

void setup() {
  Serial.begin(BAUD_SERIAL);
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(PIN_STEPPER[i], OUTPUT);
    digitalWrite(PIN_STEPPER[i], LOW);
  }
  pinMode(PIN_LED_GERAK, OUTPUT);
  digitalWrite(PIN_LED_GERAK, LOW);
  analogReadResolution(12);  // ADC 12-bit (0..4095)

  Serial.println("Gorden otomatis siap.");
  Serial.println("Cahaya di atas 2500 -> buka, di bawah 1200 -> tutup.");
}

// Rata-rata beberapa bacaan ADC supaya angka cahaya stabil.
uint16_t bacaCahayaADC() {
  uint32_t jumlah = 0;
  for (uint8_t i = 0; i < JUMLAH_SAMPEL; i++) {
    jumlah += analogRead(PIN_SENSOR_LDR);
    delay(JEDA_SAMPEL_MS);
  }
  return (uint16_t)(jumlah / JUMLAH_SAMPEL);
}

// Tentukan target gorden dari cahaya, dengan histeresis:
// hanya pindah target kalau melewati ambang — di zona mati diam saja.
void tentukanTarget(uint16_t cahayaADC) {
  if (cahayaADC >= AMBANG_BUKA_ADC && posisiTerkini != POSISI_TERBUKA) {
    langkahTarget = LANGKAH_PERJALANAN;
    posisiTerkini = POSISI_TERBUKA;
    Serial.print("Terang (");
    Serial.print(cahayaADC);
    Serial.println(") -> buka gorden.");
  } else if (cahayaADC <= AMBANG_TUTUP_ADC && posisiTerkini != POSISI_TERTUTUP) {
    langkahTarget = 0;
    posisiTerkini = POSISI_TERTUTUP;
    Serial.print("Gelap (");
    Serial.print(cahayaADC);
    Serial.println(") -> tutup gorden.");
  }
}

// Satu langkah half-step ke arah maju (true) atau mundur (false).
void gerakSatuLangkah(bool maju) {
  if (maju) {
    faseSaatIni = (faseSaatIni + 1) % JUMLAH_FASE;
    langkahSaatIni++;
  } else {
    faseSaatIni = (faseSaatIni + JUMLAH_FASE - 1) % JUMLAH_FASE;
    langkahSaatIni--;
  }
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(PIN_STEPPER[i], URUTAN_HALF_STEP[faseSaatIni][i]);
  }
  delay(JEDA_ANTAR_LANGKAH_MS);
}

// Gerakkan motor paling banyak LANGKAH_PER_GILIRAN langkah menuju target.
// Dipanggil tiap loop() supaya pembacaan sensor tetap jalan selama gerak.
void gerakMenujuTarget() {
  uint16_t langkahDigerakkan = 0;
  while (langkahSaatIni != langkahTarget && langkahDigerakkan < LANGKAH_PER_GILIRAN) {
    gerakSatuLangkah(langkahSaatIni < langkahTarget);
    langkahDigerakkan++;
  }
  bool sedangBergerak = (langkahSaatIni != langkahTarget) || (langkahDigerakkan > 0);
  digitalWrite(PIN_LED_GERAK, sedangBergerak ? HIGH : LOW);
  if (langkahDigerakkan > 0 && langkahSaatIni == langkahTarget) {
    matikanKumparan();
    Serial.print("Gorden sampai target, langkah: ");
    Serial.println(langkahSaatIni);
  }
}

// Matikan semua kumparan supaya motor dingin dan hemat daya saat diam.
void matikanKumparan() {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(PIN_STEPPER[i], LOW);
  }
  digitalWrite(PIN_LED_GERAK, LOW);
}

void loop() {
  unsigned long sekarangMs = millis();
  if (sekarangMs - waktuBacaTerakhirMs >= PERIODE_BACA_MS) {
    waktuBacaTerakhirMs = sekarangMs;
    uint16_t cahayaADC = bacaCahayaADC();
    Serial.print("Cahaya ADC: ");
    Serial.print(cahayaADC);
    Serial.print(", langkah: ");
    Serial.println(langkahSaatIni);
    tentukanTarget(cahayaADC);
  }
  gerakMenujuTarget();
}
