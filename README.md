# ESP8266 / ESP32 CLI Config Template

Template/framework dasar untuk proyek ESP8266 maupun ESP32 dengan:
- **CLI konfigurasi** lewat Serial (skema variabel tetap, validasi tipe, backup/restore)
- **Manajer WiFi** (connect, status, disconnect, auto-connect saat boot)
- **OTA update firmware** dari URL (http/https)
- **Persistensi** ke LittleFS (bertahan setelah restart)
- **Satu basis kode untuk dua platform** (ESP8266 & ESP32) — pilih lewat environment PlatformIO, tanpa edit kode.

Dirancang sebagai titik awal (starting point) untuk proyek-proyek berikutnya: tambahkan logika aplikasi Anda sendiri di `src/main.cpp` dan/atau modul baru, sementara CLI, config, WiFi, dan OTA sudah siap pakai.

---

## 1. Yang Perlu Disiapkan

### Software
1. **[Visual Studio Code](https://code.visualstudio.com/)**
2. Extension **[PlatformIO IDE](https://platformio.org/install/ide?install=vscode)** (cari "PlatformIO IDE" di tab Extensions VSCode, lalu Install)
3. **Driver USB-to-Serial**, tergantung chip yang dipakai board Anda:
   - **CP2102 / CP2104** → [driver Silicon Labs CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
   - **CH340 / CH341** → driver CH34x (banyak board NodeMCU/klon memakai ini)
   - Board dengan USB native (beberapa ESP32-S2/S3/C3) umumnya tidak perlu driver tambahan.

### Hardware
- Board **ESP8266** (mis. NodeMCU v2/v3, Wemos D1 Mini), **atau**
- Board **ESP32** (mis. ESP32 DevKit v1 / DOIT ESP32), **atau keduanya** jika ingin coba dua platform.
- Kabel USB yang mendukung transfer data (bukan kabel charge-only).

Tidak perlu menyiapkan apa pun secara khusus untuk LittleFS — filesystem akan otomatis dibuat/diformat sendiri oleh firmware saat pertama kali boot (lihat `ConfigManager::mountFilesystem()`).

---

## 2. Membuka Project

1. Buka VSCode.
2. Klik ikon PlatformIO di sidebar kiri (ikon alien/semut) → **Open** → **Open Project** → pilih folder project ini.
3. Tunggu PlatformIO selesai mengunduh platform & library yang dibutuhkan (`espressif8266`, `espressif32`, `ArduinoJson`) — proses ini hanya terjadi sekali di awal, butuh koneksi internet.

---

## 3. Struktur Folder

```
.
├── platformio.ini          <- konfigurasi build, dua environment: esp8266 & esp32
├── README.md                <- file ini
├── include/
│   ├── platform_compat.h    <- lapisan kompatibilitas ESP8266/ESP32 (jarang perlu diedit)
│   └── system.h             <- skema konfigurasi + deklarasi semua fungsi (Doxygen)
└── src/
    ├── main.cpp              <- setup()/loop(), TITIK AWAL untuk logika aplikasi Anda
    └── system.cpp            <- implementasi ConfigManager, WifiManager, OtaUpdater, CliHandler
```

---

## 4. Build & Upload

Project ini punya **dua environment**: `esp8266` dan `esp32`. Pilih sesuai board yang sedang dipakai.

### Lewat panel PlatformIO (GUI)
1. Klik ikon PlatformIO di sidebar → **PROJECT TASKS**.
2. Pilih environment (`esp8266` atau `esp32`) dari daftar.
3. Klik **Upload** (ikon panah kanan) untuk compile + flash ke board.
4. Klik **Monitor** untuk membuka Serial Monitor.

### Lewat terminal
```bash
# Build & upload ke ESP8266
pio run -e esp8266 -t upload

# Build & upload ke ESP32
pio run -e esp32 -t upload

# Buka serial monitor (baud rate sudah diset 115200 di platformio.ini)
pio device monitor
```

> **Tidak perlu mengedit satu baris kode pun** untuk berpindah antara ESP8266 dan ESP32 — cukup ganti `-e esp8266` menjadi `-e esp32` (atau pilih environment yang berbeda di panel PlatformIO). Semua perbedaan library/API ditangani otomatis oleh `include/platform_compat.h`.

### Board berbeda dari default?
`platformio.ini` memakai board default `nodemcuv2` (ESP8266) dan `esp32dev` (ESP32). Kalau board Anda berbeda (mis. Wemos D1 Mini, ESP32-S3, dll), cukup ubah baris `board = ...` di `platformio.ini` — cari ID board yang sesuai di:
- ESP8266: https://docs.platformio.org/en/latest/boards/index.html#espressif-8266
- ESP32: https://docs.platformio.org/en/latest/boards/index.html#espressif-32

---

## 5. Memakai CLI

Setelah upload, buka Serial Monitor (baud **115200**, line ending **Newline**), lalu ketik `help`:

| Perintah | Fungsi |
|---|---|
| `config set <nama> <nilai>` | Set nilai variabel konfigurasi (hanya untuk nama yang ada di skema) |
| `config get <nama>` | Tampilkan satu variabel, format `config set ...` (siap ditempel ulang) |
| `config get all` | Tampilkan semua variabel — bisa dipakai sebagai **backup** |
| `config list` | Tampilkan variabel yang **tersedia** (skema: nama, tipe, default) |
| `config save` | Simpan konfigurasi saat ini ke flash (LittleFS) |
| `config set default` | **Reset** semua konfigurasi ke nilai default |
| `wifi connect <ssid> <pass>` | Hubungkan ke WiFi (kredensial tersimpan di RAM, perlu `config save` agar permanen) |
| `wifi status` | Tampilkan status koneksi WiFi saat ini |
| `wifi disconnect` | Putuskan koneksi WiFi |
| `update <url_firmware>` | Update firmware OTA dari URL `.bin` (http/https) |
| `reboot` | Restart perangkat |
| `help` | Tampilkan bantuan |

**Contoh alur pemakaian:**
```
wifi connect RumahWiFi rahasia123
config set interval 500
config set nama "Sensor Ruang Tamu"
config save
config get all
reboot
```

**Backup & restore konfigurasi:** jalankan `config get all`, salin semua baris output, lalu tempel ke Serial Monitor perangkat lain (atau perangkat yang sama setelah reset) — diikuti `config save` agar permanen.

⚠️ `config get all` menampilkan `wifi_password` dalam bentuk teks biasa. Jangan sembarangan membagikan output ini.

---

## 6. Mengakses & Menulis Variabel Konfigurasi dari Kode

Selain lewat CLI, variabel konfigurasi juga bisa dibaca/ditulis langsung dari kode (`src/main.cpp` atau modul lain) lewat namespace `ConfigManager`.

### Membaca (Get)

Pakai `ConfigManager::getConfigXxx()`, dengan nama variabel memakai konstanta `CFG_KEY_<nama>` (bukan string literal) supaya salah ketik ketahuan saat **compile**, bukan diam-diam gagal saat runtime.

```cpp
#include "system.h"

void loop() {
  bool aktif      = ConfigManager::getConfigBool(CFG_KEY_aktif);
  long interval   = ConfigManager::getConfigInt(CFG_KEY_interval);
  float suhu      = ConfigManager::getConfigFloat(CFG_KEY_suhu);
  String nama     = ConfigManager::getConfigString(CFG_KEY_nama);

  if (aktif) {
    // ... logika Anda, pakai interval/suhu/nama di sini
  }
}
```

Kalau variabel belum pernah di-`set`, fungsi ini mengembalikan **nilai default** dari skema (`CONFIG_LIST` di `system.h`) — selalu aman dipanggil, tidak perlu cek exists dulu.

### Menulis (Set)

Pakai `ConfigManager::configSet()`. Nilainya **selalu dalam bentuk teks (String)** apapun tipe aslinya:

```cpp
ConfigManager::configSet(CFG_KEY_interval, "2000");     // int
ConfigManager::configSet(CFG_KEY_aktif, "true");         // bool
ConfigManager::configSet(CFG_KEY_suhu, "27.5");           // float
ConfigManager::configSet(CFG_KEY_nama, "Sensor Baru");   // string

// simpan permanen ke flash (LittleFS) -- WAJIB kalau ingin bertahan setelah reboot
ConfigManager::saveConfig();
```

| Hal | Penjelasan |
|---|---|
| Validasi otomatis | `configSet` menolak nilai yang tidak sesuai tipe skema (mis. `configSet(CFG_KEY_interval, "abc")` gagal & mencetak pesan error) |
| Hanya variabel terdaftar | Nama yang tidak ada di `CONFIG_LIST` akan ditolak — tidak bisa membuat variabel baru secara dinamis dari kode |
| Tidak otomatis permanen | `configSet()` hanya mengubah nilai di **RAM**. Kalau ingin nilainya bertahan setelah `reboot`, panggil `ConfigManager::saveConfig()` setelahnya |
| Parameter `silent` | `configSet(name, value, true)` — argumen ketiga `true` membungkam pesan `"OK: ..."` yang biasanya tercetak ke Serial (dipakai internal oleh `WifiManager` supaya tidak dobel pesan) |

### Contoh: baca sensor, update konfigurasi otomatis dari kode

```cpp
#include "system.h"

void loop() {
  float suhuTerukur = bacaSensorSuhu(); // fungsi Anda sendiri

  // update nilai konfigurasi "suhu" dengan hasil pembacaan sensor (silent)
  ConfigManager::configSet(CFG_KEY_suhu, String(suhuTerukur), true);

  // cek ambang batas dari konfigurasi
  float ambangBatas = ConfigManager::getConfigFloat(CFG_KEY_suhu);
  if (suhuTerukur > ambangBatas) {
    // ... aksi Anda
  }

  delay(ConfigManager::getConfigInt(CFG_KEY_interval));
}
```

> `saveConfig()` **sengaja tidak dipanggil** tiap `loop()` di contoh ini supaya tidak menulis ke flash terus-menerus (flash punya batas siklus tulis-hapus). Cukup dipanggil sesekali — lewat perintah CLI `config save`, atau dari kode hanya saat nilainya benar-benar perlu permanen (mis. sekali saat `setup()`, atau saat event tertentu).

---

## 7. Menjadikan Ini Basis Proyek Baru

### Mengganti nama proyek
Edit nilai `NAMA_PROYEK` di bagian atas `src/main.cpp`:
```cpp
#define NAMA_PROYEK "CLI Config Template"
```
Nilai ini hanya tampil di boot banner Serial Monitor saat perangkat menyala — tidak memengaruhi logika CLI/config/WiFi/OTA sama sekali.

### Menambah variabel konfigurasi
Edit **satu tempat saja**: makro `CONFIG_LIST` di `include/system.h`.
```cpp
#define CONFIG_LIST(X) \
  X(aktif,          CFG_BOOL,   "false")   \
  X(interval,       CFG_INT,    "1000")    \
  X(suhu,           CFG_FLOAT,  "25.0")    \
  X(nama,           CFG_STRING, "ESP-Device") \
  X(wifi_ssid,      CFG_STRING, "")        \
  X(wifi_password,  CFG_STRING, "")        \
  X(led_pin,        CFG_INT,    "2")       // <- tambahan baru
```
Setelah build ulang, variabel baru otomatis:
- Bisa di-set lewat `config set led_pin 4`
- Muncul di `config list` dan `config get all`
- Punya konstanta `CFG_KEY_led_pin` yang aman-typo untuk dipakai di kode:
  ```cpp
  long ledPin = ConfigManager::getConfigInt(CFG_KEY_led_pin);
  ```

### Menambah logika aplikasi
Tulis kode Anda langsung di `loop()` / `setup()` pada `src/main.cpp`, atau — untuk project yang lebih besar — buat modul baru mengikuti pola yang sudah ada di `system.h`/`system.cpp` (namespace, fungsi terdokumentasi Doxygen, fungsi manual/jarang dipakai ditandai `FLASH_ATTR`).

PlatformIO otomatis meng-compile **semua** file `.cpp` di dalam `src/` dan meng-include semua header di `include/` — jadi Anda bebas memecah modul baru jadi file terpisah tanpa perlu mengubah `platformio.ini`.

### Menambah dukungan platform lain
Kalau suatu saat perlu mendukung chip lain (mis. ESP32-C3, RP2040), tambahkan:
1. Environment baru di `platformio.ini`
2. Blok `#elif defined(...)` baru di `include/platform_compat.h`

Bagian lain project (`system.cpp`, `main.cpp`) tidak perlu disentuh, selama chip barunya menyediakan API WiFi/HTTPUpdate yang mirip (semua chip berbasis Arduino core umumnya begitu).

---

## 8. Catatan Teknis

- **`FLASH_ATTR`**: di ESP8266 fungsi memang sudah otomatis dieksekusi dari flash secara default; makro ini di ESP8266 memetakan ke `ICACHE_FLASH_ATTR` (penegasan/dokumentasi), dan kosong di ESP32 (tidak ada padanannya di arsitektur itu). Bukan optimasi RAM yang signifikan — sekadar penanda "fungsi ini manual/jarang dipanggil, tidak time-critical".
- **HTTPS OTA** memakai `setInsecure()` (tidak memverifikasi sertifikat server). Cukup untuk skala kecil/pengembangan; untuk produksi sebaiknya pakai `setFingerprint()` / `setTrustAnchors()`.
- **Ukuran flash & partisi**: ESP8266 dan ESP32 punya skema partisi berbeda. Kalau nanti butuh ruang LittleFS lebih besar (banyak file/data), sesuaikan lewat `board_build.filesystem_size` (ESP8266) atau custom partition table (ESP32) di `platformio.ini` — di luar cakupan template dasar ini.
- **`config set` dan `wifi connect`** hanya mengubah nilai di RAM. Jalankan `config save` supaya perubahan permanen dan bertahan setelah `reboot`.

---

## 9. Lisensi

Silakan gunakan, modifikasi, dan sebarkan template ini secara bebas sebagai basis proyek Anda.