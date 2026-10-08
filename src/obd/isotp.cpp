#include "isotp.h"
#include <ctype.h>
#include <string.h>

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parses one text line into a CAN id (3 chars) + data bytes. Returns the data
// byte count, or -1 if the line isn't a frame.
static int parseLine(const char *s, size_t len, char id[4], uint8_t *data, size_t dataCap) {
    char clean[48];
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == ' ') continue;
        if (n >= sizeof(clean) - 1) return -1;
        clean[n++] = (char)toupper((unsigned char)s[i]);
    }
    if (n < 5 || ((n - 3) & 1)) return -1;
    for (size_t i = 0; i < n; i++) if (hexVal(clean[i]) < 0) return -1;
    memcpy(id, clean, 3);
    id[3] = 0;
    size_t bytes = (n - 3) / 2;
    if (bytes > dataCap) return -1;
    for (size_t i = 0; i < bytes; i++) {
        data[i] = (uint8_t)((hexVal(clean[3 + 2 * i]) << 4) | hexVal(clean[4 + 2 * i]));
    }
    return (int)bytes;
}

size_t isotpExtract(const char *text, const char *rxId, uint8_t *out, size_t cap) {
    bool gotFirst = false;
    size_t total = 0, got = 0, stored = 0;

    const char *p = text;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\r' && *e != '\n') e++;

        char id[4];
        uint8_t b[16];
        int n = parseLine(p, (size_t)(e - p), id, b, sizeof(b));
        p = (*e) ? e + 1 : e;
        if (n < 2 || strcmp(id, rxId) != 0) continue;

        uint8_t pci = b[0] >> 4;
        if (!gotFirst) {
            if (pci == 0) {
                size_t len = b[0] & 0x0F;
                if (len == 0 || (size_t)n < 1 + len) return 0;
                size_t c = len < cap ? len : cap;
                memcpy(out, b + 1, c);
                return c;
            }
            if (pci != 1) continue;
            total = ((size_t)(b[0] & 0x0F) << 8) | b[1];
            gotFirst = true;
            for (int i = 2; i < n; i++) {
                if (stored < cap) out[stored++] = b[i];
                got++;
            }
        } else if (pci == 2) {
            for (int i = 1; i < n; i++) {
                if (stored < cap) out[stored++] = b[i];
                got++;
            }
        }
        if (gotFirst && got >= total) return stored < total ? stored : total;
    }
    return 0;
}
