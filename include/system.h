#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>
#include <vector>

// ============================================================================
// TIPE DATA KONFIGURASI
// ============================================================================

/**
 * @brief Tipe nilai yang didukung untuk sebuah variabel konfigurasi.
 */
enum ConfigType {
  CFG_BOOL,   ///< true / false
  CFG_INT,    ///< bilangan bulat, mis. 1000, -5
  CFG_FLOAT,  ///< bilangan desimal, mis. 3.14, -0.5 (pakai titik, bukan koma)
  CFG_STRING  ///< teks bebas, mis. "ESP 8266"
};

/**
 * @brief Satu entri nilai konfigurasi yang tersimpan di memori.
 *
 * Nilai selalu disimpan sebagai String (representasi teks) apapun tipenya;
 * konversi ke tipe asli (bool/int/float) dilakukan saat diambil lewat
 * fungsi getConfigBool/getConfigInt/dst.
 */
struct ConfigEntry {
  ConfigType type;  ///< Tipe nilai sesuai skema (lihat CONFIG_LIST)
  String value;     ///< Nilai dalam bentuk String
};

// ============================================================================
// SKEMA VARIABEL KONFIGURASI — SATU-SATUNYA TEMPAT UNTUK MENAMBAH/UBAH VARIABEL
// ============================================================================
// Format tiap baris: X(nama_variabel, TIPE, "nilai_default")
//
// - Variabel yang TIDAK ada di daftar ini TIDAK BISA dibuat lewat CLI
//   (perintah "config set" akan menolaknya).
// - Untuk menambah variabel baru: tambah satu baris di sini, lalu build+upload.
// - TIPE harus salah satu dari: CFG_BOOL, CFG_INT, CFG_FLOAT, CFG_STRING
// - Berlaku SAMA di ESP8266 maupun ESP32, tidak perlu diubah per platform.
//
#define CONFIG_LIST(X) \
  X(aktif,          CFG_BOOL,   "false")   \
  X(interval,       CFG_INT,    "1000")    \
  X(suhu,           CFG_FLOAT,  "25.0")    \
  X(nama,           CFG_STRING, "ESP-Device") \
  X(wifi_ssid,      CFG_STRING, "")        \
  X(wifi_password,  CFG_STRING, "")
// ============================================================================

/**
 * @brief Satu baris skema: nama variabel, tipe, dan nilai default-nya.
 *
 * Array CONFIG_SCHEMA[] dibangkitkan otomatis dari makro CONFIG_LIST di atas
 * (lihat system.cpp), sehingga menambah satu baris di CONFIG_LIST otomatis
 * menambah satu entri di sini juga — tidak perlu mengubah kode lain.
 */
struct ConfigSchemaEntry {
  const char* name;          ///< Nama variabel, mis. "interval"
  ConfigType type;           ///< Tipe tetap variabel ini
  const char* defaultValue;  ///< Nilai default (dipakai saat first boot / reset)
};

extern const ConfigSchemaEntry CONFIG_SCHEMA[];  ///< Array skema, dibangkitkan dari CONFIG_LIST
extern const int CONFIG_SCHEMA_COUNT;            ///< Jumlah entri di CONFIG_SCHEMA[]

/**
 * @brief Konstanta nama variabel, dibangkitkan otomatis dari CONFIG_LIST.
 *
 * Contoh hasil: CFG_KEY_interval == "interval", CFG_KEY_wifi_ssid == "wifi_ssid".
 * Definisi sebenarnya ada di system.cpp.
 *
 * Pakai konstanta ini di kode (bukan string literal) supaya salah ketik
 * nama variabel gagal saat COMPILE, bukan diam-diam gagal saat runtime.
 * Contoh: ConfigManager::getConfigInt(CFG_KEY_interval)
 */
#define CONFIG_KEY_DECL(name, type, def) extern const char* const CFG_KEY_##name;
CONFIG_LIST(CONFIG_KEY_DECL)
#undef CONFIG_KEY_DECL

// ============================================================================
// ConfigManager — penyimpanan, validasi, dan persistensi konfigurasi
// ============================================================================
namespace ConfigManager {

  /**
   * @brief Mount filesystem LittleFS, dengan fallback otomatis mem-format
   *        jika mount pertama gagal (mis. chip baru / filesystem korup).
   *
   * Ditulis platform-agnostic (LittleFS.begin()/format() API-nya identik di
   * ESP8266 maupun ESP32), jadi main.cpp cukup memanggil ini satu kali di
   * setup() tanpa perlu tahu/peduli platform yang dipakai.
   *
   * @return true jika filesystem berhasil di-mount (baik langsung maupun
   *         setelah format ulang), false jika tetap gagal.
   */
  bool mountFilesystem();

  /**
   * @brief Mengisi configStore dengan nilai default untuk setiap variabel
   *        skema yang BELUM punya nilai di memori.
   *
   * Dipanggil saat boot (setelah loadConfig()) dan saat resetConfig(), supaya
   * semua variabel di CONFIG_LIST selalu punya nilai valid di memori — baik
   * saat first boot (config.json belum ada) maupun setelah developer
   * menambah variabel baru ke skema.
   */
  void initConfigDefaults();

  /**
   * @brief Menulis seluruh isi configStore saat ini ke file /config.json di
   *        LittleFS, menjadikannya permanen (bertahan setelah reboot).
   *
   * Hanya dipanggil secara manual lewat perintah CLI "config save" — dengan
   * kata lain, "config set" dan "wifi connect" TIDAK memanggil fungsi ini
   * secara otomatis.
   */
  void saveConfig();

  /**
   * @brief Membaca /config.json dari LittleFS (jika ada) dan memuatnya ke
   *        configStore di memori.
   *
   * Dipanggil sekali saat setup(). Jika file tidak ada atau isinya rusak
   * (gagal parse JSON), fungsi ini akan diam saja / mencetak peringatan dan
   * configStore tetap kosong — nantinya diisi default oleh initConfigDefaults().
   */
  void loadConfig();

  /**
   * @brief Reset SEMUA konfigurasi kembali ke nilai default skema, dan
   *        menghapus file /config.json dari LittleFS.
   *
   * Ini aksi destruktif yang berlaku SEKETIKA (tidak menunggu "config save"),
   * dipicu oleh perintah CLI "config set default".
   */
  void resetConfig();

  /**
   * @brief Mengubah nilai satu variabel konfigurasi di memori (RAM).
   *
   * Alur validasi:
   *   1. Cari nama variabel di CONFIG_SCHEMA. Jika tidak ada -> ditolak
   *      (variabel baru tidak bisa dibuat lewat CLI).
   *   2. Validasi rawValue sesuai tipe yang didefinisikan skema (bukan
   *      ditebak dari input). Jika tidak valid -> ditolak.
   *   3. Simpan ke configStore. TIDAK langsung ditulis ke flash — jalankan
   *      "config save" untuk itu.
   *
   * @param name      Nama variabel (harus terdaftar di CONFIG_LIST)
   * @param rawValue  Nilai baru dalam bentuk teks
   * @param silent    Jika true, tidak mencetak pesan "OK: ..." saat berhasil
   *                  (dipakai WifiManager supaya tidak dobel pesan saat
   *                  menyimpan ssid & password sekaligus). Pesan error tetap
   *                  selalu dicetak apapun nilai silent.
   */
  void configSet(const String &name, const String &rawValue, bool silent = false);

  /**
   * @brief Mengambil satu nilai konfigurasi, diformat sebagai baris
   *        "config set <nama> <nilai>" yang siap ditempel ulang ke terminal.
   *
   * @param name  Nama variabel yang ingin dibaca
   * @return String berisi baris "config set ..." (nilai string dibungkus
   *         tanda kutip), atau pesan "Error: ..." jika nama tidak dikenal.
   */
  String configGetFormatted(const String &name);

  /**
   * @brief Mencetak SEMUA variabel konfigurasi ke Serial, masing-masing
   *        dalam format "config set <nama> <nilai>".
   *
   * Karena formatnya sama dengan perintah "config set", seluruh output bisa
   * disalin langsung dan ditempel ke terminal lain sebagai backup/restore.
   * Urutan cetak mengikuti urutan di CONFIG_LIST (bukan urutan internal map).
   */
  void configGetAll();

  /**
   * @brief Mencetak daftar variabel yang TERSEDIA di skema (nama, tipe,
   *        nilai default) — bukan nilai yang sedang aktif saat ini.
   *
   * Berguna untuk melihat variabel apa saja yang boleh di-set lewat CLI
   * tanpa perlu membuka kode sumber.
   */
  void configListSchema();

  /**
   * @brief Mengambil nilai konfigurasi bertipe bool.
   * @param name        Nama variabel (idealnya pakai konstanta CFG_KEY_...)
   * @param defaultVal  Nilai yang dikembalikan jika variabel tidak ditemukan
   * @return Nilai bool dari konfigurasi, atau defaultVal jika tidak ada
   */
  bool getConfigBool(const String &name, bool defaultVal = false);

  /**
   * @brief Mengambil nilai konfigurasi bertipe integer.
   * @param name        Nama variabel (idealnya pakai konstanta CFG_KEY_...)
   * @param defaultVal  Nilai yang dikembalikan jika variabel tidak ditemukan
   * @return Nilai long dari konfigurasi, atau defaultVal jika tidak ada
   */
  long getConfigInt(const String &name, long defaultVal = 0);

  /**
   * @brief Mengambil nilai konfigurasi bertipe float.
   * @param name        Nama variabel (idealnya pakai konstanta CFG_KEY_...)
   * @param defaultVal  Nilai yang dikembalikan jika variabel tidak ditemukan
   * @return Nilai float dari konfigurasi, atau defaultVal jika tidak ada
   */
  float getConfigFloat(const String &name, float defaultVal = 0.0f);

  /**
   * @brief Mengambil nilai konfigurasi bertipe string.
   * @param name        Nama variabel (idealnya pakai konstanta CFG_KEY_...)
   * @param defaultVal  Nilai yang dikembalikan jika variabel tidak ditemukan
   * @return Nilai String dari konfigurasi, atau defaultVal jika tidak ada
   */
  String getConfigString(const String &name, const String &defaultVal = "");

} // namespace ConfigManager

// ============================================================================
// WifiManager — koneksi WiFi (implementasi memakai API yang sama di kedua
// platform lewat platform_compat.h, lihat system.cpp)
// ============================================================================
namespace WifiManager {

  /**
   * @brief Menghubungkan chip ke jaringan WiFi dan menunggu hingga
   *        terhubung atau timeout.
   *
   * Jika berhasil, kredensial (ssid & password) otomatis disimpan lewat
   * ConfigManager::configSet() ke variabel wifi_ssid / wifi_password —
   * masih di RAM saja, perlu "config save" agar permanen dan bisa
   * auto-connect di boot berikutnya.
   *
   * @param ssid        Nama jaringan WiFi
   * @param password    Kata sandi jaringan WiFi
   * @param timeoutMs   Batas waktu tunggu koneksi dalam milidetik (default 15000)
   * @return true jika berhasil terhubung, false jika timeout/gagal
   */
  bool connect(const String &ssid, const String &password, unsigned long timeoutMs = 15000);

  /**
   * @brief Mencetak status koneksi WiFi saat ini ke Serial: status
   *        (terhubung/tidak), SSID, IP address, RSSI, dan MAC address.
   */
  void status();

  /**
   * @brief Memutuskan koneksi WiFi saat ini (WiFi.disconnect()).
   *
   * Tidak menghapus kredensial yang sudah tersimpan di konfigurasi — hanya
   * memutus koneksi aktif saat ini.
   */
  void disconnect();

  /**
   * @brief Dispatcher untuk subcommand "wifi ...", dipanggil dari
   *        CliHandler::handleCommand().
   *
   * Subcommand yang didukung: connect <ssid> <pass>, status, disconnect.
   *
   * @param args  Token hasil tokenize dari baris perintah lengkap, dengan
   *              args[0] == "wifi"
   */
  void handleCommand(const std::vector<String> &args);

} // namespace WifiManager

// ============================================================================
// OtaUpdater — update firmware over-the-air (memakai HTTP_UPDATER dari
// platform_compat.h, sehingga kode sama untuk ESP8266 maupun ESP32)
// ============================================================================
namespace OtaUpdater {

  /**
   * @brief Mengunduh dan memasang firmware baru dari sebuah URL (OTA update).
   *
   * Syarat: WiFi harus sudah terhubung (lihat WifiManager::connect()).
   * Mendukung URL http:// maupun https:// (untuk https, verifikasi
   * sertifikat DILEWATI lewat setInsecure() — cocok untuk skala kecil/contoh,
   * bukan untuk produksi yang butuh validasi server ketat).
   *
   * Jika update berhasil, chip otomatis restart menjalankan firmware baru
   * (rebootOnUpdate(true)), sehingga baris log "Update berhasil" biasanya
   * tidak sempat terlihat di Serial Monitor.
   *
   * @param url  Alamat file firmware .bin, harus diawali http:// atau https://
   */
  void update(const String &url);

} // namespace OtaUpdater

// ============================================================================
// CliHandler — parsing & dispatch perintah dari Serial
// ============================================================================
namespace CliHandler {

  /**
   * @brief Titik masuk utama pemrosesan perintah: menerima satu baris teks
   *        dari Serial, memecahnya jadi token, lalu menjalankan aksi yang
   *        sesuai (config/wifi/update/reboot/help).
   *
   * Alur:
   *   1. Trim spasi di awal/akhir baris; baris kosong diabaikan.
   *   2. Tokenize (tokenizer internal mendukung tanda kutip untuk string
   *      berspasi, mis. config set nama "ESP 8266").
   *   3. Cocokkan token pertama (nama perintah) dan panggil handler yang
   *      sesuai, atau cetak pesan error jika tidak dikenal.
   *
   * @param line  Satu baris perintah mentah dari input Serial
   */
  void handleCommand(String line);

} // namespace CliHandler

#endif // SYSTEM_H
