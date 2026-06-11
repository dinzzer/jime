# -*- coding: utf-8 -*-
"""
make_corpus_knp.py — KNP 形式コーパスから生テキスト (1行1文) を抽出する。

京大黒橋研 (ku-nlp) 公開の KNP 形式アノテーションコーパスに対応:
  - KWDLC                    https://github.com/ku-nlp/KWDLC
  - WikipediaAnnotatedCorpus https://github.com/ku-nlp/WikipediaAnnotatedCorpus
  - 京大テキストコーパス (KyotoCorpus) ※毎日新聞CD-ROMから原文を復元済みの場合のみ
    (リポジトリ配布の dat/num は表層文字列を含まないため、そのままでは使えない)

形態素行の先頭フィールド (表層) を連結し、EOS で1文として出力する。

使い方:
  python make_corpus_knp.py corpus.txt <KNPコーパスdir> [<dir2> ...]
出力した corpus.txt は train_nn.py の入力に使える:
  python train_nn.py corpus.txt data/nn_model.bin --counts
"""
import sys
import os


def extract_file(path, out):
    n = 0
    sent = []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):       # S-ID コメント行
                continue
            if line.startswith("*") or line.startswith("+"):  # 文節/基本句
                continue
            if line == "EOS":
                if sent:
                    out.write("".join(sent) + "\n")
                    n += 1
                sent = []
                continue
            surface = line.split(" ", 1)[0]
            if surface and surface != "*":
                sent.append(surface)
    if sent:
        out.write("".join(sent) + "\n")
        n += 1
    return n


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    out_path = sys.argv[1]
    total = 0
    files = 0
    with open(out_path, "w", encoding="utf-8") as out:
        for root_dir in sys.argv[2:]:
            for dirpath, _, names in os.walk(root_dir):
                for name in sorted(names):
                    if not name.endswith(".knp"):
                        continue
                    total += extract_file(os.path.join(dirpath, name), out)
                    files += 1
    print(f"{out_path}: {total} sentences from {files} files")


if __name__ == "__main__":
    main()
