#include "session_store.h"
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include "../config.h"
#include "../obd/pid_registry.h"

// 4-bit when the board wires D1-D3, else 1-bit. Never formats the card.
static bool mountSd() {
#if SDMMC_D1_PIN >= 0
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN, SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN);
    if (SD_MMC.begin("/sdcard", false, false)) return true;
    SD_MMC.end();
#endif
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);
    return SD_MMC.begin("/sdcard", true, false);
}

bool SessionStore::begin(uint32_t bootNumber) {
    boot_ = bootNumber;
#if USE_SD
    if (mountSd()) { fs_ = &SD_MMC; backend_ = "SD"; }
#endif
    if (!fs_) {
        if (!LittleFS.begin(true)) return false;
        fs_ = &LittleFS;
        backend_ = "flash";
    }
    // Everything lives under dir_ so a shared card (e.g. one that also holds
    // another device's boot files) is never touched outside /obd.
    if (fs_ != &LittleFS) { dir_ = "/obd"; fs_->mkdir(dir_); }
    String name = dir_ + "/session_" + String(boot_) + ".csv";
    csv_ = fs_->open(name, FILE_WRITE);
    if (!csv_) return false;
    String header = "millis,unix_time";
    for (size_t i = 0; i < COLUMN_COUNT; i++) { header += ','; header += columnName(i); }
    csv_.println(header);
    csv_.flush();
    ok_ = true;
    return true;
}

// Internal flash is small, so when it runs low the oldest session/raw files
// (never the current boot's) are deleted to make room, instead of logging just
// stopping. The SD card isn't touched by this.
static void freeFlash(uint32_t currentBoot) {
    for (int guard = 0; guard < 40 && LittleFS.totalBytes() - LittleFS.usedBytes() < 200000; guard++) {
        File root = LittleFS.open("/");
        String oldest;
        long oldestN = 1L << 30;
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            String nm = f.name();
            nm = nm.substring(nm.lastIndexOf('/') + 1);
            int us = nm.indexOf('_'), dot = nm.lastIndexOf('.');
            if (us < 0 || dot < us) continue;
            long n = nm.substring(us + 1, dot).toInt();
            if (n < (long)currentBoot && n < oldestN) { oldestN = n; oldest = nm; }
        }
        if (oldest.length() == 0) break;
        LittleFS.remove("/" + oldest);
    }
}

bool SessionStore::spaceLow() {
    if (fs_ == &LittleFS) {
        if (LittleFS.totalBytes() - LittleFS.usedBytes() < 60000) freeFlash(boot_);
        return LittleFS.totalBytes() - LittleFS.usedBytes() < 30000;
    }
    return false;
}

void SessionStore::writeRow(uint32_t ms, double unixSec, const float *values, size_t count) {
    if (!ok_ || spaceLow()) return;
    String row = String(ms) + ',' + (isnan(unixSec) ? String("") : String(unixSec, 3));
    for (size_t i = 0; i < count; i++) {
        row += ',';
        if (!isnan(values[i])) row += String(values[i], 3);
    }
    size_t n = csv_.println(row);
    csv_.flush();
    noteWrite(n > 0);
}

// A card that stops accepting writes (contact glitch, vibration, a FAT error) used to fail silently:
// the board kept running and printing rows but nothing reached the file. Count failures and, after
// three in a row, try to remount the card and reopen the session files; if that fails, log to flash.
void SessionStore::noteWrite(bool good) {
    if (good) { failStreak_ = 0; return; }
    failTotal_++;
    if (++failStreak_ >= 3) recover();
}

void SessionStore::recover() {
    if (millis() - lastRecoverMs_ < 10000) return;   // at most one attempt per 10 s
    lastRecoverMs_ = millis();
    failStreak_ = 0;
    Serial.printf("STORAGE: writes failing (%lu so far) — remounting\n", (unsigned long)failTotal_);
    if (csv_) csv_.close();
    if (raw_) raw_.close();
#if USE_SD
    if (fs_ == &SD_MMC) {
        SD_MMC.end();
        delay(50);
        bool up = mountSd();
        if (up) {
            String base = dir_ + "/session_" + String(boot_) + ".csv";
            csv_ = fs_->open(base, FILE_APPEND);
            raw_ = fs_->open(dir_ + "/raw_" + String(boot_) + ".log", FILE_APPEND);
            if (csv_) { remounts_++; Serial.println("STORAGE: SD remounted, logging continues"); return; }
        }
        Serial.println("STORAGE: SD unusable — falling back to internal flash");
        if (LittleFS.begin(true)) {
            fs_ = &LittleFS; backend_ = "flash"; dir_ = "";
            String header = "millis,unix_time";
            for (size_t i = 0; i < COLUMN_COUNT; i++) { header += ','; header += columnName(i); }
            csv_ = fs_->open("/session_" + String(boot_) + ".csv", FILE_WRITE);
            if (csv_) { csv_.println(header); csv_.flush(); } else ok_ = false;
        } else ok_ = false;
    }
#endif
}

void SessionStore::queueRaw(uint32_t ms, const char *header, const char *request, const String &reply) {
    if (!ok_ || rawQueue_.length() > 3000) return;
    String r = reply;
    r.replace("\r", "|");
    r.replace("\n", "|");
    rawQueue_ += String(ms) + '\t' + header + '\t' + request + '\t' + r + '\n';
}

void SessionStore::flushRaw() {
    if (!ok_ || rawQueue_.length() == 0) return;
    if (spaceLow()) { rawQueue_ = ""; return; }
    if (!raw_) raw_ = fs_->open(dir_ + "/raw_" + String(boot_) + ".log", FILE_WRITE);
    if (raw_) { size_t n = raw_.print(rawQueue_); raw_.flush(); noteWrite(n > 0); }
    rawQueue_ = "";
}

void SessionStore::dumpAll(Print &out) {
    if (!fs_) return;
    flushRaw();
    File root = fs_->open(dir_.length() ? dir_ : String("/"));
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        out.printf("===== BEGIN %s (%u bytes) =====\n", f.name(), (unsigned)f.size());
        while (f.available()) out.write(f.read());
        out.printf("\n===== END %s =====\n", f.name());
    }
}

void SessionStore::eraseAll() {
    if (!fs_) return;
    if (csv_) csv_.close();
    if (raw_) raw_.close();
    File root = fs_->open(dir_.length() ? dir_ : String("/"));
    String names[64];
    int n = 0;
    for (File f = root.openNextFile(); f && n < 64; f = root.openNextFile()) {
        String nm = f.name();
        names[n++] = nm.startsWith("/") ? nm : dir_ + "/" + nm;
    }
    for (int i = 0; i < n; i++) fs_->remove(names[i]);
    ok_ = false;  // files are gone; reboot to start a fresh session
}

void SessionStore::tailSession(Print &out, uint32_t boot, int rows) {
    if (!fs_) return;
    File f = fs_->open(dir_ + "/session_" + String(boot) + ".csv", FILE_READ);
    if (!f) { out.printf("session_%lu.csv not found\n", (unsigned long)boot); return; }
    String header = f.readStringUntil('\n');
    size_t size = f.size();
    size_t window = 60000;
    f.seek(size > window ? size - window : 0);
    String body = f.readString();
    f.close();
    int idx = body.length();
    for (int n = 0; n <= rows && idx > 0; n++) idx = body.lastIndexOf('\n', idx - 1);
    out.println("--- TAIL session_" + String(boot) + ".csv ---");
    out.println(header);
    out.print(idx >= 0 ? body.substring(idx + 1) : body);
    out.println("--- END TAIL ---");
}

uint64_t SessionStore::freeBytes() const {
    if (fs_ == &LittleFS) return LittleFS.totalBytes() - LittleFS.usedBytes();
#if USE_SD
    if (fs_ == &SD_MMC) return SD_MMC.totalBytes() - SD_MMC.usedBytes();
#endif
    return 0;
}
