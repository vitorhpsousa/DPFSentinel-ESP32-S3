#!/usr/bin/env python3
"""Fills in unix_time for the rows of an S3 session CSV that were logged before
the clock was set, using the TIME marker in the matching raw log.

    python3 date_session.py session_33.csv raw_33.log > session_33_dated.csv

Row time = marker_unix - (marker_millis - row_millis)/1000. Rows that already
carry a unix_time are left alone. If there is no marker, the file is unchanged.
"""
import csv, sys

def main(csv_path, raw_path):
    marker = None
    for line in open(raw_path, errors="replace"):
        p = line.rstrip("\n").split("\t")
        if len(p) >= 4 and p[1] == "TIME":
            marker = (int(p[0]), float(p[3]), p[2])
            break
    rows = list(csv.reader(open(csv_path, newline="")))
    hdr = rows[0]
    ti = hdr.index("unix_time")
    out = csv.writer(sys.stdout, lineterminator="\n")
    out.writerow(hdr)
    filled = 0
    for r in rows[1:]:
        if marker and len(r) > ti and r[ti] == "":
            r[ti] = f"{marker[1] - (marker[0] - int(r[0])) / 1000.0:.3f}"
            filled += 1
        out.writerow(r)
    print(f"# {filled} rows dated from {marker[2] if marker else 'no'} marker", file=sys.stderr)

main(sys.argv[1], sys.argv[2])
