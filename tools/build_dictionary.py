# -*- coding: utf-8 -*-
"""
build_dictionary.py — mozc OSS 辞書から JIME 用バイナリ辞書を生成する。

入力: mozc の src/data/dictionary_oss ディレクトリ
  - dictionary00.txt .. dictionary09.txt : 読み\tlid\trid\tcost\t表層 (UTF-8)
  - connection_single_column.txt         : 1行目=品詞数N、以降 N*N 行のコスト
  - id.def                               : "id 品詞名" (機能語判定に使用)

出力:
  - system.dic     : 読みでソートされた辞書 (バイナリ、リトルエンディアン)
  - connection.bin : int16 接続コスト行列

system.dic レイアウト:
  char[8]  magic "JIMEDIC1"
  u32      numReadings
  u32      numEntries
  u32      maxReadingChars (コードポイント数)
  u32      posSize
  u32[numReadings+1]  readingOffsets (readingBlob 内オフセット)
  u32[numReadings+1]  entryStart     (読み i のエントリ範囲)
  {u16 lid, u16 rid, s16 cost}[numEntries]
  u32[numEntries+1]   surfaceOffsets (surfaceBlob 内オフセット)
  u8[posSize]         functionalLid  (1=付属語: 助詞/助動詞/接尾/非自立)
  u8[]     readingBlob (UTF-8 連結)
  u8[]     surfaceBlob (UTF-8 連結)

connection.bin レイアウト:
  char[8]  magic "JIMECON1"
  u32      size
  s16[size*size]  cost  (index = rid_left * size + lid_right)

使い方:
  python build_dictionary.py <dictionary_oss_dir> <output_dir> [--max-cost N]
"""
import sys
import os
import glob
import struct
import array


def build_pos_flags(id_def_path):
    flags = {}
    with open(id_def_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            num, name = line.split(" ", 1)
            i = int(num)
            functional = (
                name.startswith("助詞")
                or name.startswith("助動詞")
                or ",接尾" in name
                or ",非自立" in name
            )
            flags[i] = 1 if functional else 0
    size = max(flags) + 1
    out = bytearray(size)
    for k, v in flags.items():
        out[k] = v
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    max_cost = None
    if "--max-cost" in sys.argv:
        max_cost = int(sys.argv[sys.argv.index("--max-cost") + 1])
    os.makedirs(dst, exist_ok=True)

    # ---- 接続行列 ----
    con_path = os.path.join(src, "connection_single_column.txt")
    print("reading", con_path)
    with open(con_path, encoding="utf-8") as f:
        size = int(f.readline())
        mat = array.array("h")
        for line in f:
            v = int(line)
            if v > 32767:
                v = 32767
            elif v < -32768:
                v = -32768
            mat.append(v)
    assert len(mat) == size * size, (len(mat), size)
    with open(os.path.join(dst, "connection.bin"), "wb") as f:
        f.write(b"JIMECON1")
        f.write(struct.pack("<I", size))
        if sys.byteorder == "big":
            mat.byteswap()
        mat.tofile(f)
    print("connection.bin:", size, "x", size)

    # ---- 品詞フラグ ----
    pos_flags = build_pos_flags(os.path.join(src, "id.def"))
    pos_flags = pos_flags.ljust(size, b"\x00")[:size]

    # ---- 辞書 ----
    entries = {}  # reading -> list[(cost, lid, rid, surface)]
    n_in = 0
    for path in sorted(glob.glob(os.path.join(src, "dictionary*.txt"))):
        print("reading", path)
        with open(path, encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 5:
                    continue
                reading, lid, rid, cost, surface = (
                    parts[0], int(parts[1]), int(parts[2]), int(parts[3]), parts[4])
                if max_cost is not None and cost > max_cost:
                    continue
                if not reading or not surface:
                    continue
                n_in += 1
                entries.setdefault(reading, []).append((cost, lid, rid, surface))

    readings = sorted(entries, key=lambda r: r.encode("utf-8"))
    num_readings = len(readings)
    max_chars = max(len(r) for r in readings)

    reading_offsets = array.array("I", [0])
    entry_start = array.array("I", [0])
    entry_recs = bytearray()
    surface_offsets = array.array("I", [0])
    reading_blob = bytearray()
    surface_blob = bytearray()
    num_entries = 0
    for r in readings:
        reading_blob += r.encode("utf-8")
        reading_offsets.append(len(reading_blob))
        for cost, lid, rid, surface in sorted(entries[r]):
            entry_recs += struct.pack("<HHh", lid, rid, cost)
            sb = surface.encode("utf-8")
            surface_blob += sb
            surface_offsets.append(len(surface_blob))
            num_entries += 1
        entry_start.append(num_entries)

    out_path = os.path.join(dst, "system.dic")
    with open(out_path, "wb") as f:
        f.write(b"JIMEDIC1")
        f.write(struct.pack("<IIII", num_readings, num_entries, max_chars, size))
        if sys.byteorder == "big":
            reading_offsets.byteswap(); entry_start.byteswap(); surface_offsets.byteswap()
        reading_offsets.tofile(f)
        entry_start.tofile(f)
        f.write(entry_recs)
        surface_offsets.tofile(f)
        f.write(pos_flags)
        f.write(reading_blob)
        f.write(surface_blob)
    print("system.dic: %d readings, %d entries (from %d lines), max reading %d chars"
          % (num_readings, num_entries, n_in, max_chars))
    print("size: %.1f MB" % (os.path.getsize(out_path) / 1e6))


if __name__ == "__main__":
    main()
