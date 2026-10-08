#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

// BLE GATT connection to an ELM327-compatible adapter that exposes a UART
// bridge (a notify characteristic for replies, a writable one for commands).
// Plain-text framing as on the serial targets: commands end in \r, replies
// end in '>'.
class ElmClient {
public:
    bool begin(uint32_t scanTimeoutMs = 10000);
    bool connected();
    void close();

    // Full init: reset, echo/linefeeds off, headers ON, spaces off,
    // ISO 15765-4 CAN 11/500, 200 ms reply timeout. Also forgets the
    // selected header so the next setHeader() always sends ATSH.
    bool initAdapter();
    bool setHeader(const char *header);

    // Sends a command and returns the raw reply up to (excluding) '>' with
    // its internal '\r' frame separators intact.
    String sendCommand(const String &cmd, uint32_t timeoutMs = 3000);
    // Request with the expected-frame-count digit appended.
    String queryRaw(const char *requestHex, const char *frames, uint32_t timeoutMs = 3000);

    static void scanAndPrint(uint32_t durationMs);
    bool dumpServices();

private:
    NimBLEClient *client_ = nullptr;
    NimBLERemoteCharacteristic *txChar_ = nullptr;
    NimBLERemoteCharacteristic *rxChar_ = nullptr;
    String currentHeader_;

    bool discoverAndSubscribe();
    static void notifyCallback(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify);

    // Filled from NimBLE's notify task, drained from the main loop. Fixed
    // buffer so nothing allocates inside the critical section.
    static portMUX_TYPE bufMux_;
    static char rxBuf_[2048];
    static volatile size_t rxLen_;
    static ElmClient *activeInstance_;
};
