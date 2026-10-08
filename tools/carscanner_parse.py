#!/usr/bin/env python3
"""Parse a Car Scanner log.txt (raw ELM327 traffic, headers on, echo on) into
per-request lists of reassembled ISO-TP payloads, so raw bytes can be matched
against the decoded Car Scanner CSV to reverse-engineer PID byte offsets."""
import re
from collections import defaultdict

# request echo (incl. trailing expected-frame-count digit) -> (name, response id)
REQS = {
    "013E1": "013E", "010C1": "010C", "012C1": "012C", "01101": "0110",
    "01041": "0104", "01051": "0105", "010F1": "010F",
    "219480012": "21948001", "2103B": "2103", "211B1": "211B", "21013": "2101",
}
ECHOES = sorted(REQS, key=len, reverse=True)


def reassemble(hexstr):
    """hexstr = concatenated frames like 7E8 + 16 hex chars, repeated."""
    frames = []
    i = 0
    while i + 3 + 2 <= len(hexstr):
        if hexstr[i:i + 3] not in ("7E8", "7DC"):
            break
        frames.append(hexstr[i + 3:i + 19])
        i += 19
    if not frames:
        return None
    f0 = bytes.fromhex(frames[0].ljust(16, "0"))
    pci = f0[0] >> 4
    if pci == 0:
        n = f0[0] & 0x0F
        return f0[1:1 + n]
    if pci == 1:
        total = ((f0[0] & 0x0F) << 8) | f0[1]
        data = bytearray(f0[2:8])
        for fr in frames[1:]:
            b = bytes.fromhex(fr.ljust(16, "0"))
            if b[0] >> 4 == 2:
                data += b[1:8]
        return bytes(data[:total])
    return None


def parse(path):
    raw = open(path, errors="replace").read().replace("\r", "")
    raw = re.sub(r"\[NoFinishCharacter[^\]]*\]", "", raw)
    out = defaultdict(list)
    for seg in raw.split(">"):
        s = seg.strip().replace("\n", "").replace(" ", "")
        for e in ECHOES:
            if s.startswith(e):
                body = s[len(e):]
                if "NODATA" in body or "SEARCHING" in body or not body:
                    out[REQS[e]].append(None)
                else:
                    try:
                        out[REQS[e]].append(reassemble(body))
                    except ValueError:
                        out[REQS[e]].append(None)
                break
    return out


if __name__ == "__main__":
    import sys
    r = parse(sys.argv[1])
    for k, v in r.items():
        good = [x for x in v if x]
        print(k, len(v), "good", len(good), "len", sorted({len(x) for x in good})[:5], good[0].hex() if good else "")
