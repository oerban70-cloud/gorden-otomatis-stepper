# Gorden Otomatis — ESP32 + Motor Stepper 28BYJ-48 + Sensor Cahaya LDR

Matahari terbit, gordenmu buka sendiri tanpa bangun? Alat kecil ini yang
melakukannya: sensor cahaya LDR di jendela membaca terang langit, dan saat
cahaya sudah cukup terang, ESP32 memutar motor stepper 28BYJ-48 lewat driver
ULN2003 sehingga gorden terbuka. Saat senja tiba dan ruangan gelap, motor
memutar balik dan gorden tertutup sendiri. Dua ambang yang berbeda
(histeresis) mencegah gorden buka-tutup berulang saat cahaya berfluktuasi,
misalnya saat awan lewat.

Video penjelasan (60 detik):
https://www.youtube.com/shorts/SpqPcDPu5Ko

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Motor stepper 28BYJ-48 5V | Menggerakkan rel gorden (4096 langkah half-step per putaran) |
| Driver ULN2003 | 4 channel Darlington, penggerak kumparan stepper |
| Sensor cahaya LDR + resistor 10k | Pembagi tegangan, membaca terang langit |
| Adaptor 5V 2A | Catu daya khusus motor — JANGAN dari pin 5V ESP32 |
| Rel gorden + tali/kait | Digerakkan poros motor |

## Wiring

| Dari | Ke |
|---|---|
| LDR OUT (node pembagi tegangan) | ESP32 GPIO34 (ADC1) |
| LDR VCC | ESP32 3V3 |
| ULN2003 IN1 | ESP32 GPIO19 |
| ULN2003 IN2 | ESP32 GPIO18 |
| ULN2003 IN3 | ESP32 GPIO5 |
| ULN2003 IN4 | ESP32 GPIO17 |
| ULN2003 OUT1 | Kabel motor biru |
| ULN2003 OUT2 | Kabel motor pink |
| ULN2003 OUT3 | Kabel motor kuning |
| ULN2003 OUT4 | Kabel motor oranye |
| ULN2003 COM | 5V (adaptor) |
| ESP32 VIN | 5V (adaptor) |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

LDR dan resistor 10k membentuk pembagi tegangan: makin terang cahaya, makin
besar nilai ADC yang dibaca ESP32 di GPIO34 (0–4095). ESP32 membaca cahaya
tiap 500 ms dan mengambil rata-rata 10 sampel supaya angkanya stabil. Kalau
nilai ADC melewati 2500, target gorden jadi terbuka penuh; kalau turun di
bawah 1200, target jadi tertutup penuh. Di antara kedua ambang itu ada zona
mati — gorden diam di posisinya, jadi awan yang lewat sebentar tidak membuat
gorden bergerak.

Motor digerakkan dengan urutan half-step 8 fase, 2 ms per langkah, dan gerakan
dibagi kecil-kecil (96 langkah per putaran loop) supaya pembacaan sensor tetap
jalan selama motor bergerak. LED bawaan board (GPIO2) menyala selama motor
bergerak dan semua kumparan dimatikan saat gorden sampai target, supaya motor
dingin dan hemat daya.

```
perjalanan penuh = 12288 langkah half-step = 3 putaran poros motor
```

## Kalibrasi

Ukur perjalanan gordenmu sekali saja, lalu ubah `LANGKAH_PERJALANAN` di
sketch. Caranya: jalankan alat sambil memantau Serial Monitor — angka
`langkah` yang tampil saat gorden sampai terbuka penuh adalah nilai yang
dipakai. Kalau rel gordenmu lebih panjang atau lebih pendek dari 3 putaran
poros, ganti angkanya. Ambang cahaya juga bisa diubah: `AMBANG_BUKA_ADC`
(2500) dan `AMBANG_TUTUP_ADC` (1200) — naikkan ambang buka kalau gorden
terbuka terlalu pagi, turunkan ambang tutup kalau gorden tertutup terlalu
sore.

Contoh angka: cahaya pagi yang cerah biasanya membaca ADC di atas 3000,
ruangan gelap di bawah 500 — jadi ambang 2500 dan 1200 duduk nyaman di
tengah-tengah.

## Konfigurasi di sketch

| Konstanta | Nilai bawaan | Arti |
|---|---|---|
| `PIN_SENSOR_LDR` | 34 | Node pembagi tegangan LDR (ADC1) |
| `PIN_STEPPER` | 19, 18, 5, 17 | IN1–IN4 driver ULN2003 |
| `AMBANG_BUKA_ADC` | 2500 | Di atas ini gorden dibuka |
| `AMBANG_TUTUP_ADC` | 1200 | Di bawah ini gorden ditutup |
| `LANGKAH_PERJALANAN` | 12288 | Langkah half-step buka penuh (3 putaran) |
| `JEDA_ANTAR_LANGKAH_MS` | 2 | Jeda tiap langkah motor |
| `PERIODE_BACA_MS` | 500 | Baca LDR tiap 500 ms |
| `JUMLAH_SAMPEL` | 10 | Rata-rata 10 bacaan ADC |

## Compile (terverifikasi)

```
arduino-cli 1.6.0, ESP32 core 4.0.0, board esp32:esp32:esp32
Sketch uses 300237 bytes (22%) of program storage space. Maximum is 1310720 bytes.
Global variables use 23320 bytes (7%) of dynamic memory. Maximum is 327680 bytes.
```

Tidak ada library eksternal yang dibutuhkan — sketch hanya memakai fungsi
bawaan Arduino.
