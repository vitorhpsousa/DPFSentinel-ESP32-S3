#pragma once
#include <stddef.h>
#include <stdint.h>

// Reassembles an ISO-TP (CAN) payload from ELM327/STN text with headers on
// (ATH1) and spaces off (ATS0): frames separated by '\r', each line being a
// 3-char CAN id followed by up to 8 data bytes in hex, e.g.
//   7E8034104A2                     single frame -> 41 04 A2
//   7E8104B6103FFFFFFFF\r7E821...   first frame + consecutive frames
// Only frames from rxId are used. Copies the payload (starting at the service
// id, 0x41.. / 0x61..) into out and returns its length, or 0 if the reply is
// missing, from another ECU, or incomplete. Pure C++: no Arduino types, so it
// is unit-tested natively against Car Scanner's raw log.
size_t isotpExtract(const char *text, const char *rxId, uint8_t *out, size_t cap);
