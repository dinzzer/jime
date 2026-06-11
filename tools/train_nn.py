# -*- coding: utf-8 -*-
"""
train_nn.py — ハイブリッド補正モデル (nn_model.bin) の学習 (オプション)

JIME は辞書 + 品詞 bigram 格子をベースに、語境界の文字ペアに対する
補正コストテーブル (nn_model.bin) があれば格子に加算する。
このスクリプトは UTF-8 の日本語コーパス (1行1文) から、小さな
embedding モデルを学習して文字ペアの自然さを推定し、補正コスト
テーブルとして書き出す。numpy のみで動作する (依存を増やさないため
2層の小規模ネットを numpy で直接学習する)。

コーパスが無い場合でも、--counts モードなら出現頻度ベースの
統計テーブルとして即生成できる (推奨。学習不要で効果が近い)。

使い方:
  python train_nn.py corpus.txt data/nn_model.bin            # NN 学習
  python train_nn.py corpus.txt data/nn_model.bin --counts   # 頻度ベース

出力形式 (リトルエンディアン):
  char[8] "JIMENN01"
  u32     count
  {u32 hash; s16 cost}[count]   hash 昇順
hash は FNV-1a (左語末尾文字 UTF-8 + 0xFF + 右語先頭文字 UTF-8)。
コストは格子の接続コストに加算される (負 = その接続を優遇)。
"""
import sys
import struct
import math
from collections import Counter


def fnv1a_pair(a: str, b: str) -> int:
    h = 2166136261
    for byte in a.encode("utf-8"):
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    h = ((h ^ 0xFF) * 16777619) & 0xFFFFFFFF
    for byte in b.encode("utf-8"):
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def collect_pairs(corpus_path):
    pairs = Counter()
    total = 0
    with open(corpus_path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            for i in range(len(line) - 1):
                a, b = line[i], line[i + 1]
                if ord(a) < 0x3000 or ord(b) < 0x3000:
                    continue  # 和文文字ペアのみ
                pairs[(a, b)] += 1
                total += 1
    return pairs, total


def counts_to_costs(pairs, total, scale=500.0, max_entries=200000):
    """PMI 風: 高頻度ペアに負のコスト (優遇)、計算は対数比。"""
    left = Counter()
    right = Counter()
    for (a, b), c in pairs.items():
        left[a] += c
        right[b] += c
    out = {}
    for (a, b), c in pairs.most_common(max_entries):
        if c < 3:
            break
        pmi = math.log(c * total / (left[a] * right[b]) + 1e-12)
        cost = int(-pmi * scale)
        cost = max(-4000, min(4000, cost))
        if cost != 0:
            out[fnv1a_pair(a, b)] = cost
    return out


def train_embeddings(pairs, total, dim=16, epochs=3, lr=0.05):
    """小型双方向モデル: 文字 embedding の内積でペア自然さを推定。"""
    import numpy as np
    chars = sorted({c for p in pairs for c in p})
    idx = {c: i for i, c in enumerate(chars)}
    rng = np.random.default_rng(0)
    L = rng.normal(0, 0.1, (len(chars), dim))   # 左文字 (前方文脈)
    R = rng.normal(0, 0.1, (len(chars), dim))   # 右文字 (後方文脈)
    items = list(pairs.items())
    n = len(chars)
    for ep in range(epochs):
        loss_sum = 0.0
        for (a, b), c in items:
            ia, ib = idx[a], idx[b]
            w = math.log(1 + c)
            # 正例: スコアを上げる / 負例: ランダム文字
            for sign, jb in ((1.0, ib), (-1.0, rng.integers(0, n))):
                s = float(L[ia] @ R[jb])
                p = 1.0 / (1.0 + math.exp(-s))
                g = w * (p - (1.0 if sign > 0 else 0.0))
                loss_sum += abs(g)
                gL = g * R[jb]
                gR = g * L[ia]
                L[ia] -= lr * gL
                R[jb] -= lr * gR
        print(f"epoch {ep}: |grad|={loss_sum / max(1, len(items)):.4f}")
    out = {}
    for (a, b), c in pairs.items():
        if c < 3:
            continue
        s = float(L[idx[a]] @ R[idx[b]])
        cost = int(-s * 800)
        cost = max(-4000, min(4000, cost))
        if cost != 0:
            out[fnv1a_pair(a, b)] = cost
    return out


def write_model(table, path):
    items = sorted(table.items())
    with open(path, "wb") as f:
        f.write(b"JIMENN01")
        f.write(struct.pack("<I", len(items)))
        for h, c in items:
            f.write(struct.pack("<Ih", h, c))
    print(f"{path}: {len(items)} entries")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    corpus, out = sys.argv[1], sys.argv[2]
    use_counts = "--counts" in sys.argv
    pairs, total = collect_pairs(corpus)
    print(f"pairs: {len(pairs)} (total {total})")
    if use_counts:
        table = counts_to_costs(pairs, total)
    else:
        table = train_embeddings(pairs, total)
    write_model(table, out)


if __name__ == "__main__":
    main()
