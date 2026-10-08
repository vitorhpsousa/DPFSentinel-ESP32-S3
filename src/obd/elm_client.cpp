#include "elm_client.h"
#include "../config.h"

portMUX_TYPE ElmClient::bufMux_ = portMUX_INITIALIZER_UNLOCKED;
char ElmClient::rxBuf_[2048];
volatile size_t ElmClient::rxLen_ = 0;
ElmClient *ElmClient::activeInstance_ = nullptr;

void ElmClient::notifyCallback(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
    portENTER_CRITICAL(&bufMux_);
    size_t room = sizeof(rxBuf_) - 1 - rxLen_;
    size_t n = len < room ? len : room;
    memcpy(rxBuf_ + rxLen_, data, n);
    rxLen_ += n;
    rxBuf_[rxLen_] = 0;
    portEXIT_CRITICAL(&bufMux_);
}

static void ensureBleInit() {
    static bool done = false;
    if (!done) { NimBLEDevice::init("ESP32-S3-OBD"); done = true; }
}

static bool containsNoCase(const String &hay, const char *needle) {
    String h = hay, n = String(needle);
    h.toUpperCase();
    n.toUpperCase();
    return h.indexOf(n) >= 0;
}

static bool nameLooksLikeAdapter(const String &name) {
    if (strlen(BLE_NAME_HINT) > 0) return containsNoCase(name, BLE_NAME_HINT);
    static const char *const hints[] = {"OBD", "ELM", "VLINK", "V-LINK", "VEEPEAK", "IOS-", "VGATE", "KONNWEI", "KW9", "KW8", "KW-", "OBDLINK", "MX+", "LELink", "CX"};
    for (const char *h : hints) if (containsNoCase(name, h)) return true;
    return false;
}

void ElmClient::scanAndPrint(uint32_t durationMs) {
    ensureBleInit();
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    uint32_t sec = durationMs / 1000 ? durationMs / 1000 : 1;
    Serial.printf("Scanning for %lus...\n", (unsigned long)sec);
    NimBLEScanResults results = scan->start(sec, false);
    Serial.printf("Found %d device(s):\n", results.getCount());
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice dev = results.getDevice(i);
        Serial.printf("  %s  name=\"%s\"  rssi=%d%s\n", dev.getAddress().toString().c_str(),
                      dev.getName().c_str(), dev.getRSSI(),
                      nameLooksLikeAdapter(String(dev.getName().c_str())) ? "   <-- looks like an OBD adapter" : "");
        for (int s = 0; dev.haveServiceUUID() && s < dev.getServiceUUIDCount(); s++)
            Serial.printf("      service: %s\n", dev.getServiceUUID(s).toString().c_str());
    }
    scan->clearResults();
}

bool ElmClient::begin(uint32_t scanTimeoutMs) {
    activeInstance_ = this;
    ensureBleInit();

    // A flaky adapter (this one connected 45 times in 992 attempts from a
    // phone) is reconnected by address rather than rescanned each time; the
    // cache is dropped after two failures in a row so a changed adapter is
    // rediscovered.
    static NimBLEAddress cachedAddr;
    static bool haveCached = false;
    static uint8_t cacheFails = 0;

    NimBLEAddress target;
    bool have = false;
    if (strlen(BLE_TARGET_ADDRESS) > 0) {
        target = NimBLEAddress(BLE_TARGET_ADDRESS);
        have = true;
    } else if (haveCached) {
        target = cachedAddr;
        have = true;
        Serial.printf("Reconnecting to %s...\n", target.toString().c_str());
    } else {
        NimBLEScan *scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);
        uint32_t sec = scanTimeoutMs / 1000 ? scanTimeoutMs / 1000 : 1;
        Serial.println("Scanning for a BLE OBD adapter...");
        NimBLEScanResults results = scan->start(sec, false);
        // Several adapters may be in range (e.g. a Konnwei and an OBDLink):
        // take the strongest signal, i.e. the one nearest the board.
        int bestRssi = -1000;
        for (int i = 0; i < results.getCount(); i++) {
            NimBLEAdvertisedDevice dev = results.getDevice(i);
            if (!nameLooksLikeAdapter(String(dev.getName().c_str()))) continue;
            Serial.printf("  candidate: %s (\"%s\") rssi=%d\n", dev.getAddress().toString().c_str(), dev.getName().c_str(), dev.getRSSI());
            if (dev.getRSSI() > bestRssi) {
                bestRssi = dev.getRSSI();
                target = dev.getAddress();
                have = true;
            }
        }
        if (have) Serial.printf("Using adapter %s\n", target.toString().c_str());
        else {
            Serial.println("No adapter-like names seen. Named devices in range:");
            for (int i = 0; i < results.getCount(); i++) {
                NimBLEAdvertisedDevice dev = results.getDevice(i);
                if (dev.getName().length()) Serial.printf("    %s \"%s\"\n", dev.getAddress().toString().c_str(), dev.getName().c_str());
            }
        }
        scan->clearResults();
    }
    if (!have) {
        Serial.println("No BLE adapter found — is it plugged into the OBD port with ignition on, and "
                       "nothing else (phone/Car Scanner) connected to it? Try BLE_SCAN_ONLY.");
        return false;
    }

    if (client_) { NimBLEDevice::deleteClient(client_); client_ = nullptr; }
    client_ = NimBLEDevice::createClient();
    client_->setConnectTimeout(8);
    if (!client_->connect(target)) {
        Serial.println("BLE connect() failed");
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        if (haveCached && ++cacheFails >= 2) { haveCached = false; cacheFails = 0; }
        return false;
    }
    if (!haveCached) { cachedAddr = target; haveCached = true; }
    cacheFails = 0;
    return discoverAndSubscribe();
}

static bool isGenericService(const String &u) {
    return u == "0x1800" || u == "0x1801" || u == "0x180a" || u == "0x180A";
}

bool ElmClient::discoverAndSubscribe() {
    txChar_ = rxChar_ = nullptr;

    if (strlen(BLE_SERVICE_UUID) > 0) {
        NimBLERemoteService *svc = client_->getService(BLE_SERVICE_UUID);
        if (!svc) { Serial.printf("Service %s not found on the adapter\n", BLE_SERVICE_UUID); return false; }
        txChar_ = svc->getCharacteristic(BLE_CHAR_TX_UUID);
        rxChar_ = svc->getCharacteristic(BLE_CHAR_RX_UUID);
    } else {
        // Auto-detect: prefer Nordic UART, otherwise the first non-generic
        // service that has a notify characteristic and a writable one.
        static const char *NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
        std::vector<NimBLERemoteService *> *services = client_->getServices(true);
        NimBLERemoteService *pick = nullptr;
        for (auto *svc : *services) {
            if (String(svc->getUUID().toString().c_str()) == NUS) { pick = svc; break; }
        }
        for (int pass = 0; pass < 2 && !(txChar_ && rxChar_); pass++) {
            for (auto *svc : *services) {
                if (pass == 0 && pick && svc != pick) continue;
                if (isGenericService(String(svc->getUUID().toString().c_str()))) continue;
                NimBLERemoteCharacteristic *rx = nullptr, *tx = nullptr;
                // Enumerate ONCE and reuse: getCharacteristics(true) frees and
                // rebuilds the characteristic objects, so a pointer taken before a
                // second refresh would dangle (that crashed subscribe() at boot).
                std::vector<NimBLERemoteCharacteristic *> *chars = svc->getCharacteristics(true);
                for (auto *chr : *chars) {
                    if (!rx && chr->canNotify()) rx = chr;
                }
                for (auto *chr : *chars) {
                    if (chr != rx && (chr->canWrite() || chr->canWriteNoResponse())) { tx = chr; break; }
                }
                if (!tx && rx && (rx->canWrite() || rx->canWriteNoResponse())) tx = rx;  // HM-10 style single characteristic
                if (rx && tx) { rxChar_ = rx; txChar_ = tx; break; }
            }
        }
    }

    if (!txChar_ || !rxChar_) {
        Serial.println("Could not find a notify + write characteristic pair — run BLE_DUMP_SERVICES.");
        return false;
    }
    Serial.printf("Using service %s\n  TX (write)  %s\n  RX (notify) %s\n",
                  txChar_->getRemoteService()->getUUID().toString().c_str(),
                  txChar_->getUUID().toString().c_str(), rxChar_->getUUID().toString().c_str());
    if (!rxChar_->canNotify() || !rxChar_->subscribe(true, notifyCallback)) {
        Serial.println("Failed to subscribe to RX notifications");
        return false;
    }
    return true;
}

bool ElmClient::dumpServices() {
    if (!connected()) { Serial.println("dumpServices: not connected"); return false; }
    for (auto *svc : *client_->getServices(true)) {
        Serial.printf("Service: %s\n", svc->getUUID().toString().c_str());
        for (auto *chr : *svc->getCharacteristics(true)) {
            Serial.printf("  Char: %s  props=%s%s%s%s\n", chr->getUUID().toString().c_str(),
                          chr->canRead() ? "R" : "", chr->canWrite() ? "W" : "",
                          chr->canWriteNoResponse() ? "w" : "", chr->canNotify() ? "N" : "");
        }
    }
    Serial.println("--- service dump complete ---");
    return true;
}

bool ElmClient::connected() { return client_ && client_->isConnected() && txChar_ && rxChar_; }

void ElmClient::close() {
    if (client_) {
        if (client_->isConnected()) client_->disconnect();
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
    }
    txChar_ = rxChar_ = nullptr;
}

String ElmClient::sendCommand(const String &cmd, uint32_t timeoutMs) {
    if (!connected()) return "";
    portENTER_CRITICAL(&bufMux_);
    rxLen_ = 0;
    rxBuf_[0] = 0;
    portEXIT_CRITICAL(&bufMux_);

    String out = cmd + "\r";
    bool withResponse = !txChar_->canWriteNoResponse();
    if (!txChar_->writeValue((const uint8_t *)out.c_str(), out.length(), withResponse)) return "";

    uint32_t start = millis();
    for (;;) {
        bool done = false;
        String snap;
        portENTER_CRITICAL(&bufMux_);
        const char *gt = strchr(rxBuf_, '>');
        done = gt != nullptr;
        size_t n = done ? (size_t)(gt - rxBuf_) : rxLen_;
        char tmp[sizeof(rxBuf_)];
        memcpy(tmp, rxBuf_, n);
        tmp[n] = 0;
        portEXIT_CRITICAL(&bufMux_);
        if (done || millis() - start >= timeoutMs) {
            snap = String(tmp);
            snap.trim();
            return snap;
        }
        delay(2);
    }
}

bool ElmClient::initAdapter() {
    currentHeader_ = "";
    sendCommand("ATZ", 3000);
    // ATH1: headers on, so multi-frame replies arrive as raw CAN frames that
    // obd/isotp.cpp reassembles — exactly how Car Scanner talks to this car.
    // ATSP6 = ISO 15765-4 CAN 11-bit/500k (auto-search takes ~10 s).
    static const char *const steps[] = {"ATE0", "ATL0", "ATH1", "ATS0", "ATSP6", "ATST32"};
    for (const char *s : steps) {
        if (sendCommand(s).length() == 0) return false;
    }
    return true;
}

bool ElmClient::setHeader(const char *header) {
    if (currentHeader_ == header) return true;
    if (sendCommand(String("ATSH") + header).indexOf("OK") < 0) return false;
    currentHeader_ = header;
    return true;
}

String ElmClient::queryRaw(const char *requestHex, const char *frames, uint32_t timeoutMs) {
    return sendCommand(String(requestHex) + frames, timeoutMs);
}
