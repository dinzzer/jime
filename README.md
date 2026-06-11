# JIME — Mac風ライブ変換 Windows IME

macOS の「ライブ変換」のように、スペースキーを押さなくても入力した先から
自動で漢字かな交じり文に変換される Windows 用日本語 IME (TSF テキストサービス) です。

## 特徴

- **ライブ変換**: ローマ字入力した先から自動変換。確定操作は基本的に不要。
- **双方向文脈モデル**: 未確定バッファ全体を毎打鍵ごとにビタビ探索で再デコード
  するため、後ろの単語を入力すると前の単語の変換が自動で切り替わる
  (例: 「はしを」→`橋を` の後に「わたる」と続けると全体を再評価)。
- **変換待ちキュー**: 直近 **5文節** を未確定として保持。古い文節から自動確定。
  句読点 (、。!?) の入力でそこまで即確定。
- **辞書**: mozc OSS 辞書 (BSD ライセンス、約129万語) + 品詞接続コスト行列。
- **ハイブリッド補正フック**: `data/nn_model.bin` を置くと、語境界の文字ペアに
  対する学習済み補正コストが格子に加算される (任意。無くても動作)。

## 構成

```
jime/
├── CMakeLists.txt
├── kana.txt                  # ローマ字→かな変換表 (CSV)
├── data/
│   ├── system.dic            # バイナリ辞書 (生成物)
│   ├── connection.bin        # 品詞接続コスト (生成物)
│   └── nn_model.bin          # ハイブリッド補正 (任意)
├── src/
│   ├── engine/               # 変換エンジン (プラットフォーム非依存 C++17)
│   │   ├── engine.h / engine.cpp
│   │   └── test_main.cpp     # CLI テストハーネス
│   └── tsf/                  # TSF テキストサービス (Windows)
│       ├── TextService.h / TextService.cpp
│       ├── DllServer.cpp
│       └── jime.def
└── tools/
    ├── build_dictionary.py   # mozc 辞書 → バイナリ辞書
    └── train_nn.py           # 補正モデル学習 (任意)
```

## ビルド手順

前提: Visual Studio 2019/2022 (C++ デスクトップ開発ワークロード) + CMake。

```bat
:: x64 (必須)
cmake -B build64 -A x64
cmake --build build64 --config Release

:: x86 (32bit アプリでも使う場合)
cmake -B build32 -A Win32
cmake --build build32 --config Release
```

成果物: `build64\Release\jime.dll`, `jime_test.exe`

### 辞書データの生成 (済みの場合はスキップ)

`data/system.dic` と `data/connection.bin` が無い場合:

```bat
git clone --depth 1 --filter=blob:none --sparse https://github.com/google/mozc
cd mozc && git sparse-checkout set src/data/dictionary_oss && cd ..
python tools\build_dictionary.py mozc\src\data\dictionary_oss data
```

## インストール (要・管理者権限)

1. 配置 (例: `C:\Program Files\jime`):

   ```
   C:\Program Files\jime\
   ├── jime.dll
   ├── jime_config.exe       ← 設定コンソール (トレイメニューから起動される)
   └── data\
       ├── system.dic
       ├── connection.bin
       └── kana.txt          ← リポジトリ直下の kana.txt をコピー
   ```

   ※ `data` フォルダは **DLL と同じ場所** に置くこと (DLL が自分の場所から探す)。

2. 管理者のコマンドプロンプトで登録:

   ```bat
   regsvr32 "C:\Program Files\jime\jime.dll"
   ```

3. 「設定 → 時刻と言語 → 言語と地域 → 日本語 → 言語のオプション」に
   **JIME (ライブ変換)** が追加される。`Win + Space` で切り替えて使用。

アンインストール: `regsvr32 /u "C:\Program Files\jime\jime.dll"`

## キー操作

| キー | 動作 |
|---|---|
| a–z 等 | ローマ字入力 → 自動でライブ変換 |
| `、` `。` `!` `?` | そこまで自動確定 |
| Enter | 全文確定 |
| Esc | 未確定文字列を破棄 |
| Space / 変換 | フォーカス文節の候補ウィンドウを開く / 次候補 |
| ↑ / ↓ | 前候補 / 次候補 |
| ← / → | フォーカス文節の移動 |
| Backspace | 1文字削除 (再変換される) |
| F6 | ひらがなに変換 |
| F7 | 全角カタカナに変換 |
| F8 | 半角カタカナに変換 |
| F9 | 全角英数に変換 (再押下で 小文字→大文字→先頭大文字 を循環) |
| F10 | 半角英数に変換 (同上) |
| 半角/全角 | IME オン/オフ |
| 変換 | IME オン (オフ時) / 次候補 (入力中) ※設定で変更可 |
| 無変換 | IME オフ (入力中なら確定してオフ) ※設定で変更可 |
| Shift+Space | 半角スペース (通常 Space は全角スペース) |

候補を選ぶとその文節は「ピン」され、続きを入力しても選択が維持されます。
Ctrl / Alt との組み合わせ (Ctrl+C 等のショートカット) は IME が奪わず、アプリにそのまま渡ります。

## タスクトレイ インジケーター

JIME 使用中はタスクバーの通知領域に **あ** (ひらがな) / **A** (英数) の
インジケーターが表示されます (Microsoft IME 相当)。右クリック (左クリックでも可) で
メニューが開き、「JIME の設定...」から設定コンソールを起動できます。
半角/全角・変換/無変換・Win+Space による切替も自動でアイコンに反映されます。

## 設定コンソール (jime_config.exe)

トレイメニューまたは直接起動で以下を設定できます (Microsoft IME の設定画面に相当):

- **動作設定**:
  - 「変換キーで IME オン」「無変換キーで IME オフ」の有効/無効
  - 「ハイブリッド補正を使用」— `data\nn_model.bin` による語境界補正の ON/OFF
  (いずれも HKCU\Software\JIME に保存)
- **ユーザー辞書**: よみ (ひらがな) と単語の登録/削除
  (`%APPDATA%\JIME\user_dict.tsv` に保存。登録語は変換候補の先頭に出る)

設定・辞書は IME 側が次の入力開始時に自動で再読込するため、再起動は不要です。
辞書ファイルは単純な TSV (`よみ→タブ→単語`) なので、テキストエディタでの一括編集も可能です。

## エンジン単体テスト (Windows / Linux 共通)

```bat
jime_test data kana.txt type "kyouhaiitenkidesune."
:: → 今日はいい天気ですね。
jime_test data kana.txt live "hashiwowataru"
:: 1打鍵ごとの再デコード過程を表示
jime_test data kana.txt repl
```

## ハイブリッド補正モデル (任意)

UTF-8 コーパス (1行1文) から語境界補正テーブルを作成し、変換精度を底上げできます:

```bat
:: 頻度 (PMI) ベース — 高速で効果的 (推奨)
python tools\train_nn.py corpus.txt data\nn_model.bin --counts
:: 小型 embedding モデル学習 (numpy 必要)
python tools\train_nn.py corpus.txt data\nn_model.bin
```

生成した `nn_model.bin` を DLL 横の `data\` に置くと次回から自動で読み込まれます。

### 同梱モデルについて

`data/nn_model.bin` は京大黒橋研公開の KNP 形式コーパス
[KWDLC](https://github.com/ku-nlp/KWDLC) +
[WikipediaAnnotatedCorpus](https://github.com/ku-nlp/WikipediaAnnotatedCorpus)
(計 約32,000文) から生成済み (頻度ベース)。再生成手順:

```bat
git clone --depth 1 https://github.com/ku-nlp/KWDLC
git clone --depth 1 https://github.com/ku-nlp/WikipediaAnnotatedCorpus
python tools\make_corpus_knp.py corpus.txt KWDLC\knp WikipediaAnnotatedCorpus\knp
python tools\train_nn.py corpus.txt data\nn_model.bin --counts
```

効果例: 「きのうがっこうにいった」が 機能学校→**昨日学校** に、
「はしをわたる」が わたる→**渡る** に改善。


## 既知の制限

- 変換品質は mozc OSS 辞書の生コスト準拠。同音語の意味的選択
  (橋/箸、行った/言った 等) は文脈によっては期待と異なることがある。
  → 候補選択 (Space) か nn_model.bin で改善可能。
- キー→文字変換は JIS 配列基準。US 配列では一部記号がずれる。
- 数字キーでの候補直接選択は未実装 (↑↓/Space で選択)。
- セキュアデスクトップ (UAC 画面等) では動作しない (仕様)。
- 32bit アプリで使うには x86 版 DLL も登録が必要。

## ライセンス

- 本体: 自由に利用・改変可。
- 辞書データ: mozc プロジェクト由来 (BSD-3-Clause、`data/LICENSE.mozc.txt` 参照)。
