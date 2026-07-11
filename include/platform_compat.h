#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

/*
  platform_compat.h
  ------------------
  Lapisan kompatibilitas supaya kode aplikasi (system.h / system.cpp /
  main.cpp) bisa dipakai APA ADANYA di ESP8266 maupun ESP32, TANPA perlu
  diedit sama sekali saat berpindah platform.

  Cara kerja: PlatformIO otomatis mendefinisikan macro ESP8266 atau ESP32
  sesuai [env:...] yang aktif (lihat platformio.ini). File ini mendeteksi
  macro tersebut lalu:
    1. Meng-include header WiFi/HTTP yang benar untuk platform itu.
    2. Menyediakan nama SERAGAM untuk hal-hal yang API-nya sedikit berbeda
       antar kedua platform, supaya kode di atasnya tidak perlu tahu/peduli
       sedang berjalan di platform mana.

  Jika suatu saat menambah platform baru (mis. ESP32-S3, RP2040-W, dst),
  cukup tambah satu blok #elif di sini — bagian lain project tidak perlu
  disentuh.
*/

#if defined(ESP8266)

  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266httpUpdate.h>

  /// Nama platform untuk ditampilkan di boot banner / log.
  #define PLATFORM_NAME   "ESP8266"

  /// Atribut compiler untuk menandai fungsi "tidak time-critical, cukup di
  /// flash" (di ESP8266 ini sudah perilaku default; atribut ini hanya
  /// penegasan/dokumentasi, lihat catatan di system.cpp).
  #define FLASH_ATTR      ICACHE_FLASH_ATTR

  /// Objek OTA updater. Nama kelas/objeknya beda antar platform
  /// (ESPhttpUpdate vs httpUpdate) walau API method-nya sama persis.
  #define HTTP_UPDATER    ESPhttpUpdate

#elif defined(ESP32)

  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>

  #define PLATFORM_NAME   "ESP32"

  // ESP32 tidak punya padanan langsung ICACHE_FLASH_ATTR (arsitekturnya
  // beda -- eksekusi kode sudah diatur toolchain/linker, bukan atribut per
  // fungsi). Didefinisikan kosong supaya baris kode yang sama tetap
  // ter-compile di kedua platform tanpa #ifdef berulang di system.cpp.
  #define FLASH_ATTR

  #define HTTP_UPDATER    httpUpdate

#else
  #error "platform_compat.h: platform tidak didukung. Pakai environment 'esp8266' atau 'esp32' di platformio.ini."
#endif

#endif // PLATFORM_COMPAT_H
