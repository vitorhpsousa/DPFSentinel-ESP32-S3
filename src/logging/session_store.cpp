#include "session_store.h"
#include <esp_heap_caps.h>
#include <errno.h>
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include "../config.h"
#include "../obd/pid_registry.h"

static SemaphoreHandle_t lockHandle() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
}
StoreLock::StoreLock() { xSemaphoreTakeRecursive(lockHandle(), portMAX_DELAY); }
StoreLock::~StoreLock() { xSemaphoreGiveRecursive(lockHandle()); }

// 4-bit when the board wires D1-D3, else 1-bit. Never formats the card. 10 open
// files: csv + raw + a directory listing + a Telegram send can overlap.
static const uint8_t SD_MAX_OPEN_FILES = 10;
static bool mountSd() {
#if SDMMC_D1_PIN >= 0
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN, SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN);
    if (SD_MMC.begin("/sdcard", false, false, BOARD_MAX_SDMMC_FREQ, SD_MAX_OPEN_FILES)) return true;
    SD_MMC.end();
#endif
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);
    return SD_MMC.begin("/sdcard", true, false, BOARD_MAX_SDMMC_FREQ, SD_MAX_OPEN_FILES);
}

// Opens session_<boot>.csv; a new file gets the header. Append mode keeps rows
// already written when reopening after a remount.
bool SessionStore::openSession(bool append) {
    String name = dir_ + "/session_" + String(boot_) + ".csv";
    csv_ = fs_->open(name, append ? FILE_APPEND : FILE_WRITE);
    if (!csv_) return false;
    // A new file always gets the header. (File::size() is unreliable for a just-created file on this
    // framework, so it is only trusted for an existing file reopened in append mode.)
    if (!append || csv_.size() == 0) {
        String header = "millis,unix_time";
        for (size_t i = 0; i < COLUMN_COUNT; i++) { header += ','; header += columnName(i); }
        csv_.println(header);
        csv_.flush();
    }
    return true;
}

void SessionStore::openRaw() {
    raw_ = fs_->open(dir_ + "/raw_" + String(boot_) + ".log", FILE_APPEND);

}

static bool useFlash(fs::FS *&fs, const char *&backend, String &dir) {
    if (!LittleFS.begin(true)) return false;
    fs = &LittleFS; backend = "flash"; dir = "";
    return true;
}

bool SessionStore::begin(uint32_t bootNumber) {
    StoreLock lock;
    boot_ = bootNumber;
#if USE_SD
    if (mountSd()) {
        fs_ = &SD_MMC; backend_ = "SD";
        // Everything lives under dir_ so a shared card (e.g. one that also holds
        // another device's boot files) is never touched outside /obd.
        dir_ = "/obd";
        fs_->mkdir(dir_);
        // A card that mounts but won't take a file (read-only, damaged FAT) falls back to flash.
        if (openSession(false)) { ok_ = true; return true; }
        Serial.println("STORAGE: SD mounted but the session file won't open — using internal flash");
        SD_MMC.end();
        fs_ = nullptr;
    }
#endif
    if (!useFlash(fs_, backend_, dir_)) return false;
    if (!openSession(false)) return false;
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

// LittleFS usedBytes() walks the whole filesystem, so it's checked every 30 s, not every row.
bool SessionStore::spaceLow() {
    if (fs_ != &LittleFS) return false;
    static uint32_t lastCheck = 0;
    static bool low = false;
    if (lastCheck == 0 || millis() - lastCheck > 30000) {
        lastCheck = millis() | 1;
        if (LittleFS.totalBytes() - LittleFS.usedBytes() < 60000) freeFlash(boot_);
        low = LittleFS.totalBytes() - LittleFS.usedBytes() < 30000;
    }
    return low;
}

void SessionStore::writeRow(uint32_t ms, double unixSec, const float *values, size_t count) {
    StoreLock lock;
    if (!ok_ || spaceLow()) return;
    String row = String(ms) + ',' + (isnan(unixSec) ? String("") : String(unixSec, 3));
    for (size_t i = 0; i < count; i++) {
        row += ',';
        if (!isnan(values[i])) row += String(values[i], 3);
    }
    if (!csv_) { noteWrite(false); return; }
    // Failure = the write call refused the data. (A file-size check was tried and reverted: on this
    // framework File::size() can be garbage for a just-created file, which made it remount in a loop.)
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
    StoreLock lock;   // other tasks are kept out of the filesystem while it is remounted
    if (lastRecoverMs_ != 0 && millis() - lastRecoverMs_ < 10000) return;   // at most one attempt per 10 s
    lastRecoverMs_ = millis() | 1;
    failStreak_ = 0;
    Serial.printf("STORAGE: writes failing (%lu so far) — remounting | DMA free %u largest %u | internal free %u | PSRAM free %u\n",
                  (unsigned long)failTotal_, (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (csv_) csv_.close();
    if (raw_) raw_.close();
#if USE_SD
    if (fs_ == &SD_MMC) {
        SD_MMC.end();
        delay(50);
        if (mountSd() && openSession(true)) {
            openRaw();
            remounts_++;
            Serial.println("STORAGE: SD remounted, logging continues");
            return;
        }
        Serial.println("STORAGE: SD unusable — falling back to internal flash");
        SD_MMC.end();   // may still be mounted if only the file reopen failed
        // No flash either: stay ok_ with no open file, so writeRow keeps retrying every 10 s.
        if (!useFlash(fs_, backend_, dir_)) return;
    }
#endif
    // Flash (first choice, or after the SD gave up): reopen both files in append mode.
    if (openSession(true)) openRaw();   // else: retried from writeRow via recover() in 10 s
}

void SessionStore::queueRaw(uint32_t ms, const char *header, const char *request, const String &reply) {
    if (!ok_ || rawQueue_.length() > 3000) return;
    String r = reply;
    r.replace("\r", "|");
    r.replace("\n", "|");
    rawQueue_ += String(ms) + '\t' + header + '\t' + request + '\t' + r + '\n';
}

void SessionStore::flushRaw() {
    StoreLock lock;
    if (!ok_ || rawQueue_.length() == 0) return;
    if (spaceLow()) { rawQueue_ = ""; return; }
    if (!raw_) openRaw();
    if (raw_) {
        size_t n = raw_.print(rawQueue_);
        raw_.flush();
        noteWrite(n > 0);
    }
    rawQueue_ = "";
}

void SessionStore::dumpAll(Print &out) {
    StoreLock lock;
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
    StoreLock lock;
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
    StoreLock lock;
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

size_t SessionStore::fileSize(const String &name) {
    StoreLock lock;
    if (!fs_) return 0;
    File f = fs_->open(dir_ + "/" + name, FILE_READ);
    size_t n = f ? f.size() : 0;
    if (f) f.close();
    return n;
}

size_t SessionStore::readChunk(const String &name, size_t offset, uint8_t *buf, size_t len) {
    StoreLock lock;
    if (!fs_) return 0;
    File f = fs_->open(dir_ + "/" + name, FILE_READ);
    if (!f) return 0;
    size_t n = f.seek(offset) ? f.read(buf, len) : 0;
    f.close();
    return n;
}

uint64_t SessionStore::cardBytes() const {
    StoreLock lock;
#if USE_SD
    if (fs_ == &SD_MMC) return SD_MMC.cardSize();
#endif
    return 0;
}

uint64_t SessionStore::freeBytes() const {
    StoreLock lock;
    if (fs_ == &LittleFS) return LittleFS.totalBytes() - LittleFS.usedBytes();
#if USE_SD
    if (fs_ == &SD_MMC) return SD_MMC.totalBytes() - SD_MMC.usedBytes();
#endif
    return 0;
}
