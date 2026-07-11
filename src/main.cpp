/*
  main.cpp — ESP8266 / ESP32 CLI Config Template
  -----------------------------------------------
  File ini SENGAJA dibuat minim — hanya berisi setup(), loop(), dan
  pembacaan input Serial (termasuk penanganan backspace). Seluruh logika
  detail ada di system.h / system.cpp.

  FILE INI TIDAK PERLU DIEDIT saat berpindah antara ESP8266 dan ESP32.
  Cukup pilih environment yang sesuai di platformio.ini (atau lewat panel
  PlatformIO di VSCode), build, dan upload:

      pio run -e esp8266 -t upload
      pio run -e esp32   -t upload

  Perbedaan API antar chip (WiFi, HTTP OTA, dst) sudah ditangani oleh
  include/platform_compat.h, dipakai secara internal oleh system.cpp.

  Perintah CLI yang didukung (Serial Monitor, baud 115200, line ending
  "Newline") — ketik "help" setelah upload untuk melihatnya langsung, atau
  lihat README.md / dokumentasi Doxygen di system.h.
*/

#include <Arduino.h>
#include "system.h"
#include "platform_compat.h" // hanya untuk PLATFORM_NAME di boot banner

// ============================================================================
// NAMA PROYEK — ganti nilai ini sesuai proyek Anda. Ini SATU-SATUNYA tempat
// yang perlu diedit saat template ini dipakai untuk proyek baru; tidak
// mempengaruhi logika CLI/config/WiFi/OTA sama sekali, hanya tampil di
// boot banner Serial Monitor.
// ============================================================================
#define NAMA_PROYEK "Little Snowman"

String inputBuffer = "";

void printPrompt() {
  Serial.print("> ");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.print(F("=== "));
  Serial.print(NAMA_PROYEK);
  Serial.println(F(" ==="));
  Serial.print(F("Platform : "));
  Serial.println(PLATFORM_NAME);

  if (!ConfigManager::mountFilesystem()) {
    Serial.println(F("Error: LittleFS gagal di-mount maupun diformat"));
  } else {
    ConfigManager::loadConfig();
  }

  // Pastikan semua variabel di skema punya nilai (mengisi yang belum ada
  // dengan default), misalnya saat first boot atau setelah menambah
  // variabel baru di CONFIG_LIST (system.h).
  ConfigManager::initConfigDefaults();

  Serial.println(F("Ketik 'help' untuk daftar perintah"));

  // Auto-connect WiFi jika kredensial sebelumnya sudah tersimpan (tidak kosong)
  String savedSsid = ConfigManager::getConfigString(CFG_KEY_wifi_ssid);
  if (savedSsid.length() > 0) {
    WifiManager::connect(savedSsid, ConfigManager::getConfigString(CFG_KEY_wifi_password));
  }

  printPrompt();
  pinMode(2, OUTPUT);
}

unsigned long last_blink = 0;

void loop() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        Serial.println();
        CliHandler::handleCommand(inputBuffer);
        inputBuffer = "";
        printPrompt();
      }

    } else if (c == 8 || c == 127) {
      // Backspace (8) atau Delete (127) dari terminal
      if (inputBuffer.length() > 0) {
        inputBuffer.remove(inputBuffer.length() - 1);
        Serial.print("\b \b"); // mundur, timpa spasi, mundur lagi
      }

    } else if (c == 27) {
      // Awal escape sequence (mis. tombol panah). Buang sisa sequence-nya
      // supaya tidak ikut masuk buffer.
      delay(2);
      while (Serial.available()) Serial.read();

    } else if (c >= 32 && c < 127) {
      // Hanya terima karakter yang bisa dicetak
      inputBuffer += c;
      Serial.print(c); // echo karakter yang diketik
    }
    // karakter kontrol lain diabaikan
  }
  // -- logika aplikasi lain bisa jalan di sini, contoh: --
  // if (ConfigManager::getConfigBool(CFG_KEY_aktif)) { ... }
  // blink led
  if(millis() - last_blink >=  ConfigManager::getConfigInt(CFG_KEY_interval)){
    digitalWrite(2,!digitalRead(2));
    last_blink = millis();
  }
}