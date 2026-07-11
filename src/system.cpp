/*
  system.cpp
  ----------
  Implementasi gabungan seluruh modul non-inti aplikasi:
    - ConfigManager : penyimpanan, validasi, dan persistensi konfigurasi
    - WifiManager   : koneksi WiFi
    - OtaUpdater    : update firmware OTA dari URL
    - CliHandler    : tokenizer & dispatcher perintah Serial

  File ini TIDAK mengandung #ifdef ESP8266/ESP32 apapun -- semua perbedaan
  platform sudah diselesaikan oleh include/platform_compat.h (lihat macro
  FLASH_ATTR dan HTTP_UPDATER yang dipakai di bawah). Inilah yang membuat
  file ini bisa dipakai apa adanya di kedua platform.
*/

#include "system.h"
#include "platform_compat.h"

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <map>
#include <WiFiClientSecure.h>

// ============================================================================
// Skema & konstanta — dibangkitkan otomatis dari CONFIG_LIST (lihat system.h)
// ============================================================================

/// Bangkitkan array skema: { "nama", TIPE, "default" } untuk tiap baris CONFIG_LIST.
#define CONFIG_SCHEMA_ENTRY(name, type, def) { #name, type, def },
const ConfigSchemaEntry CONFIG_SCHEMA[] = {
  CONFIG_LIST(CONFIG_SCHEMA_ENTRY)
};
#undef CONFIG_SCHEMA_ENTRY
const int CONFIG_SCHEMA_COUNT = sizeof(CONFIG_SCHEMA) / sizeof(CONFIG_SCHEMA[0]);

/// Bangkitkan konstanta nama variabel, mis: CFG_KEY_interval = "interval".
#define CONFIG_KEY_DEF(name, type, def) const char* const CFG_KEY_##name = #name;
CONFIG_LIST(CONFIG_KEY_DEF)
#undef CONFIG_KEY_DEF

// ============================================================================
// Internal ConfigManager — tidak dideklarasikan di system.h (private ke file ini)
// ============================================================================

/**
 * @brief Penyimpanan konfigurasi yang sesungguhnya di memori.
 *
 * Sengaja dibuat `static` (linkage internal ke file ini) supaya modul lain
 * TIDAK BISA mengakses/mengubahnya langsung — semua akses harus lewat
 * fungsi-fungsi ConfigManager::* untuk menjaga validasi & konsistensi data.
 */
static std::map<String, ConfigEntry> configStore;

/// Path file penyimpanan konfigurasi permanen di LittleFS.
static const char* CONFIG_FILE = "/config.json";

/**
 * @brief Mengonversi enum ConfigType menjadi nama tipe dalam bentuk teks,
 *        untuk ditampilkan ke user (mis. di pesan error atau "config list").
 * @param t  Tipe yang ingin dikonversi
 * @return "bool" / "int" / "float" / "string"
 */
static String configTypeName(ConfigType t) {
  switch (t) {
    case CFG_BOOL:   return "bool";
    case CFG_INT:    return "int";
    case CFG_FLOAT:  return "float";
    default:         return "string";
  }
}

/**
 * @brief Mencari entri skema berdasarkan nama variabel.
 * @param name  Nama variabel yang dicari
 * @return Pointer ke entri di CONFIG_SCHEMA jika ditemukan, atau nullptr
 *         jika nama tidak terdaftar di skema (CONFIG_LIST).
 */
static const ConfigSchemaEntry* findSchemaEntry(const String &name) {
  for (int i = 0; i < CONFIG_SCHEMA_COUNT; i++) {
    if (name == CONFIG_SCHEMA[i].name) return &CONFIG_SCHEMA[i];
  }
  return nullptr;
}

/**
 * @brief Memeriksa apakah sebuah string adalah representasi bool yang valid.
 * @param raw  Teks input yang akan divalidasi
 * @return true jika raw (tanpa memandang huruf besar/kecil) sama dengan
 *         "true" atau "false"
 */
static bool isValidBool(const String &raw) {
  String lower = raw;
  lower.toLowerCase();
  return lower == "true" || lower == "false";
}

/**
 * @brief Memeriksa apakah sebuah string adalah bilangan bulat yang valid.
 *
 * Aturan: opsional tanda minus di depan, diikuti satu atau lebih digit.
 * String kosong atau hanya berisi "-" dianggap tidak valid.
 *
 * @param raw  Teks input yang akan divalidasi
 * @return true jika raw adalah representasi integer yang sah
 */
static bool isValidInt(const String &raw) {
  int len = raw.length();
  if (len == 0) return false;
  int start = (raw[0] == '-') ? 1 : 0;
  if (start >= len) return false;
  for (int i = start; i < len; i++) {
    if (!isDigit(raw[i])) return false;
  }
  return true;
}

/**
 * @brief Memeriksa apakah sebuah string adalah bilangan desimal yang valid.
 *
 * Aturan: opsional tanda minus di depan, digit, dengan paling banyak SATU
 * tanda titik desimal (titik bersifat opsional — "5" tetap dianggap float
 * yang valid, sama seperti "5.0").
 *
 * @param raw  Teks input yang akan divalidasi
 * @return true jika raw adalah representasi float yang sah
 */
static bool isValidFloat(const String &raw) {
  int len = raw.length();
  if (len == 0) return false;
  int start = (raw[0] == '-') ? 1 : 0;
  if (start >= len) return false;
  bool dotFound = false;
  for (int i = start; i < len; i++) {
    if (raw[i] == '.') {
      if (dotFound) return false;
      dotFound = true;
    } else if (!isDigit(raw[i])) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Memvalidasi sebuah string sesuai tipe yang diharapkan.
 *
 * Dipanggil oleh ConfigManager::configSet() untuk memastikan nilai baru
 * cocok dengan tipe yang didefinisikan skema (CONFIG_LIST), sebelum
 * disimpan ke configStore.
 *
 * @param raw   Teks nilai yang akan divalidasi
 * @param type  Tipe yang diharapkan (dari skema)
 * @return true jika raw valid untuk tipe tersebut. Tipe CFG_STRING selalu
 *         menghasilkan true (string menerima teks apa saja).
 */
static bool isValidValueForType(const String &raw, ConfigType type) {
  switch (type) {
    case CFG_BOOL:  return isValidBool(raw);
    case CFG_INT:   return isValidInt(raw);
    case CFG_FLOAT: return isValidFloat(raw);
    default:        return true; // string menerima apa saja
  }
}

/**
 * @brief Memformat satu entri konfigurasi menjadi baris perintah
 *        "config set <nama> <nilai>" yang siap ditempel ulang ke terminal.
 *
 * Nilai bertipe CFG_STRING dibungkus tanda kutip ganda supaya aman jika
 * mengandung spasi saat ditempel ulang; tipe lain dicetak apa adanya.
 *
 * @param name   Nama variabel
 * @param entry  Entri konfigurasi (tipe + nilai) yang akan diformat
 * @return String berisi satu baris perintah "config set ..."
 */
static String formatConfigSetLine(const String &name, const ConfigEntry &entry) {
  String valuePart = entry.value;
  if (entry.type == CFG_STRING) {
    valuePart = "\"" + valuePart + "\"";
  }
  return "config set " + name + " " + valuePart;
}

// ============================================================================
// ConfigManager — implementasi
// ============================================================================
namespace ConfigManager {

bool mountFilesystem() {
  if (LittleFS.begin()) return true;

  // Mount gagal (mis. chip baru yang belum pernah diformat, atau
  // filesystem korup). Coba format lalu mount ulang -- API format()/begin()
  // sama persis di ESP8266 dan ESP32, jadi tidak perlu #ifdef di sini.
  Serial.println(F("Warning: mount LittleFS gagal, mencoba format..."));
  if (!LittleFS.format()) return false;
  return LittleFS.begin();
}

void initConfigDefaults() {
  for (int i = 0; i < CONFIG_SCHEMA_COUNT; i++) {
    String key = CONFIG_SCHEMA[i].name;
    if (configStore.find(key) == configStore.end()) {
      ConfigEntry entry;
      entry.type = CONFIG_SCHEMA[i].type;
      entry.value = CONFIG_SCHEMA[i].defaultValue;
      configStore[key] = entry;
    }
  }
}

// Jarang dipanggil (hanya saat perintah "config save") -> tetap di flash.
void FLASH_ATTR saveConfig() {
  DynamicJsonDocument doc(2048);

  for (auto const &kv : configStore) {
    JsonObject obj = doc.createNestedObject(kv.first);
    obj["type"] = (int)kv.second.type;
    obj["value"] = kv.second.value;
  }

  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println(F("Error: gagal membuka config.json untuk ditulis"));
    return;
  }
  serializeJson(doc, f);
  f.close();
}

// Hanya dipanggil sekali saat boot -> tetap di flash.
void FLASH_ATTR loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) return;

  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return;

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.println(F("Warning: config.json rusak, mengabaikan isi lama"));
    return;
  }

  for (JsonPair kv : doc.as<JsonObject>()) {
    ConfigEntry entry;
    entry.type = (ConfigType)(int)kv.value()["type"];
    entry.value = kv.value()["value"].as<String>();
    configStore[String(kv.key().c_str())] = entry;
  }
}

// Aksi manual/destruktif, jarang dipanggil -> tetap di flash.
void FLASH_ATTR resetConfig() {
  configStore.clear();
  initConfigDefaults();
  if (LittleFS.exists(CONFIG_FILE)) {
    LittleFS.remove(CONFIG_FILE);
  }
  Serial.println(F("OK: semua konfigurasi telah direset ke nilai default (config.json dihapus)"));
}

void configSet(const String &name, const String &rawValue, bool silent) {
  const ConfigSchemaEntry* schema = findSchemaEntry(name);

  if (schema == nullptr) {
    Serial.println("Error: variabel '" + name + "' tidak dikenal.");
    Serial.println(F("       Variabel baru tidak bisa ditambahkan lewat CLI."));
    Serial.println(F("       Ketik 'config list' untuk melihat variabel yang tersedia."));
    return;
  }

  if (!isValidValueForType(rawValue, schema->type)) {
    Serial.println("Error: nilai '" + rawValue + "' tidak valid untuk '" + name +
                    "' (harus bertipe " + configTypeName(schema->type) + ")");
    return;
  }

  ConfigEntry entry;
  entry.type = schema->type; // tipe SELALU mengikuti skema, bukan tebakan
  entry.value = rawValue;
  configStore[name] = entry;
  // Catatan: tidak langsung saveConfig() di sini. Jalankan "config save"
  // untuk menulis perubahan ke flash.

  if (!silent) {
    Serial.println("OK: " + name + " = " + entry.value + " (" + configTypeName(entry.type) +
                    ") [belum disimpan, jalankan 'config save']");
  }
}

String configGetFormatted(const String &name) {
  if (findSchemaEntry(name) == nullptr) {
    return "Error: variabel '" + name + "' tidak dikenal (ketik 'config list')";
  }
  auto it = configStore.find(name);
  if (it == configStore.end()) {
    return "Error: konfigurasi '" + name + "' tidak ditemukan";
  }
  return formatConfigSetLine(name, it->second);
}

void configGetAll() {
  // Tampilkan dalam urutan skema (CONFIG_LIST), bukan urutan internal map,
  // supaya urutan output konsisten setiap kali dipanggil.
  for (int i = 0; i < CONFIG_SCHEMA_COUNT; i++) {
    String key = CONFIG_SCHEMA[i].name;
    auto it = configStore.find(key);

    ConfigEntry entry;
    if (it != configStore.end()) {
      entry = it->second;
    } else {
      entry.type = CONFIG_SCHEMA[i].type;
      entry.value = CONFIG_SCHEMA[i].defaultValue;
    }
    Serial.println(formatConfigSetLine(key, entry));
  }
}

// Hanya untuk referensi manual pengguna -> tetap di flash.
void FLASH_ATTR configListSchema() {
  Serial.println(F("Variabel konfigurasi yang tersedia:"));
  for (int i = 0; i < CONFIG_SCHEMA_COUNT; i++) {
    Serial.println("  " + String(CONFIG_SCHEMA[i].name) + "  (" +
                    configTypeName(CONFIG_SCHEMA[i].type) + ", default: \"" +
                    String(CONFIG_SCHEMA[i].defaultValue) + "\")");
  }
}

bool getConfigBool(const String &name, bool defaultVal) {
  auto it = configStore.find(name);
  if (it == configStore.end()) return defaultVal;
  String v = it->second.value;
  v.toLowerCase();
  return v == "true";
}

long getConfigInt(const String &name, long defaultVal) {
  auto it = configStore.find(name);
  if (it == configStore.end()) return defaultVal;
  return it->second.value.toInt();
}

float getConfigFloat(const String &name, float defaultVal) {
  auto it = configStore.find(name);
  if (it == configStore.end()) return defaultVal;
  return it->second.value.toFloat();
}

String getConfigString(const String &name, const String &defaultVal) {
  auto it = configStore.find(name);
  if (it == configStore.end()) return defaultVal;
  return it->second.value;
}

} // namespace ConfigManager

// ============================================================================
// WifiManager — implementasi
// ============================================================================
namespace WifiManager {

// Manual (dipanggil dari CLI atau sekali saat boot) -> tetap di flash.
bool FLASH_ATTR connect(const String &ssid, const String &password, unsigned long timeoutMs) {
  Serial.println("Menghubungkan ke WiFi: " + ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("OK: terhubung ke '" + ssid + "'");
    Serial.println("IP Address : " + WiFi.localIP().toString());

    // Simpan kredensial lewat ConfigManager (silent=true supaya tidak dobel
    // pesan "OK: ..." untuk tiap variabel). Masih di RAM saja sampai
    // "config save" dijalankan.
    ConfigManager::configSet(CFG_KEY_wifi_ssid, ssid, true);
    ConfigManager::configSet(CFG_KEY_wifi_password, password, true);
    Serial.println(F("(kredensial WiFi belum disimpan, jalankan 'config save' agar permanen)"));
    return true;
  }

  Serial.println("Error: gagal terhubung ke '" + ssid + "' (timeout)");
  return false;
}

// Manual (dipanggil dari CLI) -> tetap di flash.
void FLASH_ATTR status() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("Status     : terhubung"));
    Serial.println("SSID       : " + WiFi.SSID());
    Serial.println("IP Address : " + WiFi.localIP().toString());
    Serial.println("RSSI       : " + String(WiFi.RSSI()) + " dBm");
    Serial.println("MAC        : " + WiFi.macAddress());
  } else {
    Serial.println(F("Status     : tidak terhubung"));
  }
}

// Manual (dipanggil dari CLI) -> tetap di flash.
void FLASH_ATTR disconnect() {
  WiFi.disconnect();
  Serial.println(F("OK: WiFi diputus"));
}

void handleCommand(const std::vector<String> &args) {
  if (args.size() < 2) {
    Serial.println(F("Error: format -> wifi connect|status|disconnect ..."));
    return;
  }
  String sub = args[1];
  sub.toLowerCase();

  if (sub == "connect") {
    if (args.size() < 4) {
      Serial.println(F("Error: format -> wifi connect <ssid> <password>"));
      return;
    }
    connect(args[2], args[3]);

  } else if (sub == "status") {
    status();

  } else if (sub == "disconnect") {
    disconnect();

  } else {
    Serial.println("Error: subcommand wifi tidak dikenal: " + sub);
  }
}

} // namespace WifiManager

// ============================================================================
// OtaUpdater — implementasi
// ============================================================================
namespace OtaUpdater {

// Aksi manual, jarang dipanggil, tidak time-critical -> tetap di flash.
void FLASH_ATTR update(const String &url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Error: WiFi belum terhubung. Jalankan 'wifi connect <ssid> <pass>' dulu."));
    return;
  }
  if (url.length() == 0) {
    Serial.println(F("Error: format -> update <url_firmware>"));
    return;
  }

  Serial.println(F("Memulai update firmware dari:"));
  Serial.println("  " + url);
  Serial.println(F("JANGAN matikan daya selama proses berlangsung..."));
  Serial.flush();

  HTTP_UPDATER.rebootOnUpdate(true); // otomatis restart kalau sukses

  t_httpUpdate_return ret;

  if (url.startsWith("https://")) {
    WiFiClientSecure secureClient;
    // Catatan keamanan: setInsecure() melewati verifikasi sertifikat server
    // HTTPS. Cocok untuk contoh/skala kecil. Untuk produksi, sebaiknya pakai
    // secureClient.setFingerprint(...) atau setTrustAnchors(...) agar server
    // yang dituju benar-benar tervalidasi. API-nya sama di ESP8266 & ESP32.
    secureClient.setInsecure();
    ret = HTTP_UPDATER.update(secureClient, url);
  } else if (url.startsWith("http://")) {
    WiFiClient plainClient;
    ret = HTTP_UPDATER.update(plainClient, url);
  } else {
    Serial.println(F("Error: URL harus diawali http:// atau https://"));
    return;
  }

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.println("Update GAGAL (" + String(HTTP_UPDATER.getLastError()) + "): " +
                      HTTP_UPDATER.getLastErrorString());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(F("Tidak ada firmware baru dari server tersebut."));
      break;
    case HTTP_UPDATE_OK:
      // Baris ini biasanya tidak sempat tercetak karena board sudah restart
      // otomatis (rebootOnUpdate(true)) begitu update sukses.
      Serial.println(F("Update berhasil, merestart..."));
      break;
  }
}

} // namespace OtaUpdater

// ============================================================================
// CliHandler — implementasi
// ============================================================================

/**
 * @brief Memecah satu baris teks menjadi daftar token (kata), dengan
 *        dukungan tanda kutip ganda untuk token yang mengandung spasi.
 *
 * Contoh: `config set nama "ESP 8266"` menghasilkan token:
 *   ["config", "set", "nama", "ESP 8266"]
 * (tanda kutipnya sendiri tidak ikut jadi bagian token).
 *
 * @param input  Baris teks mentah yang akan ditokenisasi
 * @return Vector berisi token-token hasil pemisahan
 */
static std::vector<String> tokenize(const String &input) {
  std::vector<String> tokens;
  int i = 0;
  int n = input.length();

  while (i < n) {
    while (i < n && input[i] == ' ') i++;
    if (i >= n) break;

    String token = "";
    if (input[i] == '"') {
      i++; // lewati kutip pembuka
      while (i < n && input[i] != '"') {
        token += input[i];
        i++;
      }
      if (i < n) i++; // lewati kutip penutup
    } else {
      while (i < n && input[i] != ' ') {
        token += input[i];
        i++;
      }
    }
    tokens.push_back(token);
  }
  return tokens;
}

/**
 * @brief Mencetak daftar seluruh perintah CLI beserta contoh penggunaannya.
 *
 * Dipanggil hanya lewat perintah manual "help" -> tetap di flash
 * (teksnya panjang, sayang kalau makan RAM tanpa perlu).
 */
static void FLASH_ATTR printHelp() {
  Serial.println(F("Daftar perintah:"));
  Serial.println(F("  config set <nama> <nilai>   - set konfigurasi (bool/int/float/string)"));
  Serial.println(F("                                 contoh:"));
  Serial.println(F("                                   config set aktif true"));
  Serial.println(F("                                   config set interval 1000"));
  Serial.println(F("                                   config set suhu 3.14"));
  Serial.println(F("                                   config set nama \"ESP 8266\""));
  Serial.println(F("  config get <nama>           - tampilkan satu konfigurasi (format: config set ...)"));
  Serial.println(F("  config get all              - tampilkan semua konfigurasi (untuk backup)"));
  Serial.println(F("  config list                 - tampilkan variabel yang TERSEDIA (skema)"));
  Serial.println(F("  config save                 - simpan konfigurasi saat ini ke flash"));
  Serial.println(F("  config set default          - reset SEMUA konfigurasi"));
  Serial.println(F("  wifi connect <ssid> <pass>  - hubungkan ke WiFi"));
  Serial.println(F("  wifi status                 - status koneksi WiFi saat ini"));
  Serial.println(F("  wifi disconnect             - putuskan koneksi WiFi"));
  Serial.println(F("  update <url_firmware>       - update firmware OTA dari URL (.bin)"));
  Serial.println(F("  reboot                      - restart perangkat"));
  Serial.println(F("  help                        - tampilkan bantuan ini"));
  Serial.println();
  Serial.println(F("Catatan: 'config set' dan 'wifi connect' hanya mengubah nilai di"));
  Serial.println(F("memori. Jalankan 'config save' agar perubahan tersimpan permanen."));
}

namespace CliHandler {

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  std::vector<String> args = tokenize(line);
  if (args.empty()) return;

  String cmd = args[0];
  cmd.toLowerCase();

  if (cmd == "help") {
    printHelp();

  } else if (cmd == "config") {
    if (args.size() < 2) {
      Serial.println(F("Error: format -> config set|get|save|list ..."));
      return;
    }
    String sub = args[1];
    sub.toLowerCase();

    if (sub == "set") {
      if (args.size() == 3 && args[2] == "default") {
        // "config set default" -> reset semua konfigurasi
        ConfigManager::resetConfig();
        return;
      }
      if (args.size() < 4) {
        Serial.println(F("Error: format -> config set <nama> <nilai>"));
        return;
      }
      ConfigManager::configSet(args[2], args[3]);

    } else if (sub == "get") {
      if (args.size() < 3) {
        Serial.println(F("Error: format -> config get <nama>  atau  config get all"));
        return;
      }
      if (args[2] == "all") {
        ConfigManager::configGetAll();
      } else {
        Serial.println(ConfigManager::configGetFormatted(args[2]));
      }

    } else if (sub == "save") {
      ConfigManager::saveConfig();
      Serial.println(F("OK: konfigurasi disimpan ke config.json"));

    } else if (sub == "list") {
      ConfigManager::configListSchema();

    } else {
      Serial.println("Error: subcommand config tidak dikenal: " + sub);
    }

  } else if (cmd == "wifi") {
    WifiManager::handleCommand(args);

  } else if (cmd == "update") {
    if (args.size() < 2) {
      Serial.println(F("Error: format -> update <url_firmware>"));
      return;
    }
    OtaUpdater::update(args[1]);

  } else if (cmd == "reboot") {
    Serial.println(F("Rebooting..."));
    Serial.flush();
    delay(200);
    ESP.restart();

  } else {
    Serial.println("Error: perintah tidak dikenal: '" + cmd + "' (ketik 'help')");
  }
}

} // namespace CliHandler
