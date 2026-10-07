#!/usr/bin/env python3
"""Merge a table of lighting cues into a set list, matching songs by title.

Until the cue editor exists, this is how 267 cues get into setlist.json
without anybody typing them. It is also the right tool afterwards for a band
that plans a show in a spreadsheet, which is how this one was planned.

The table is tab- or comma-separated, one row per song:

    title <TAB> program <TAB> bars <TAB> end_bar

where `bars` is the "next cue" bars separated by spaces or commas. Lines
starting with # are ignored. For example:

    Mr Brightside	3	1 17 33 41 57 73 81 97	138

Songs already in the set list keep everything else about them - their stems,
their alignment, their tempo. Only the program change and the cue list are
written, and only for songs the table names.

    tools/add-cues.py <setlist.json> <table.tsv> [--next-note 38] [--end-note 37]
                      [--channel 1] [--dry-run]
"""
import json, sys, argparse, pathlib, shutil


def parse_table(path):
    rows = []
    for n, line in enumerate(pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in (line.split("\t") if "\t" in line else line.split(","))]
        if len(parts) < 4:
            print(f"  line {n}: expected 4 fields, got {len(parts)} - skipped")
            continue
        title, program, bars, end = parts[0], parts[1], parts[2], parts[3]
        try:
            rows.append({
                "title": title,
                "program": int(program),
                "bars": [int(b) for b in bars.replace(",", " ").split()],
                "end": int(end),
            })
        except ValueError:
            print(f"  line {n}: could not read numbers - skipped")
    return rows


def norm(s):
    """Loose title match: case, punctuation and spacing vary between a
    spreadsheet and a set list, and nobody should have to make them agree."""
    return "".join(c for c in s.lower() if c.isalnum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("setlist")
    ap.add_argument("table")
    ap.add_argument("--next-note", type=int, default=38)
    ap.add_argument("--end-note", type=int, default=37)
    ap.add_argument("--channel", type=int, default=1)
    ap.add_argument("--velocity", type=int, default=127)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    sl = json.loads(pathlib.Path(a.setlist).read_text(encoding="utf-8"))
    rows = parse_table(a.table)
    by_title = {norm(r["title"]): r for r in rows}

    sl["lighting"] = {"channel": a.channel, "next_note": a.next_note,
                      "end_note": a.end_note}
    if a.velocity != 127:
        sl["lighting"]["velocity"] = a.velocity

    matched, missing_in_set = 0, []
    used = set()
    for song in sl.get("songs", []):
        key = norm(song.get("title", ""))
        row = by_title.get(key)
        if not row:
            missing_in_set.append(song.get("title", "(untitled)"))
            continue
        used.add(key)

        cues = [{"bar": b} for b in row["bars"]]
        if row["end"]:
            cues.append({"bar": row["end"], "note": a.end_note, "desc": "end"})
        song["midi_program"] = row["program"]
        song["light_cues"] = cues
        matched += 1

    print(f"{matched} song(s) given cues, "
          f"{sum(len(s.get('light_cues', [])) for s in sl.get('songs', []))} cues in total")
    if missing_in_set:
        print(f"\nin the set list but not in the table ({len(missing_in_set)}):")
        for t in missing_in_set:
            print(f"  {t}")
    unused = [r["title"] for r in rows if norm(r["title"]) not in used]
    if unused:
        print(f"\nin the table but not in the set list ({len(unused)}):")
        for t in unused:
            print(f"  {t}")

    if a.dry_run:
        print("\n--dry-run: nothing written")
        return 0

    # A backup, because this rewrites a file somebody spent hours building.
    backup = a.setlist + ".before-cues"
    shutil.copy2(a.setlist, backup)
    pathlib.Path(a.setlist).write_text(
        json.dumps(sl, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"\nwritten. previous version kept at {backup}")
    print("open the set list in BackingTrackLive and save a song to tidy the "
          "formatting.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
