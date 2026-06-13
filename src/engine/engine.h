// JIME — Mac風ライブ変換 IME エンジン (プラットフォーム非依存)
// 双方向文脈: 未確定バッファ全体を毎打鍵ビタビ再デコードするため、
// 後続の入力により既出語の変換結果が自動的に切り替わる。
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace jime {

// ---------------------------------------------------------------- Dictionary
// build_dictionary.py が生成する system.dic を読み込む。
class Dictionary {
public:
    bool Load(const std::string& path);
    bool Loaded() const { return !buf_.empty(); }
    // 読み(UTF-8)完全一致 → エントリ範囲 [begin, end)
    bool Lookup(const char* reading, size_t len, uint32_t& begin, uint32_t& end) const;
    void Entry(uint32_t idx, uint16_t& lid, uint16_t& rid, int16_t& cost) const;
    std::string Surface(uint32_t idx) const;
    bool IsFunctional(uint16_t lid) const;   // 助詞/助動詞/接尾/非自立 (文節付属語)
    uint32_t MaxReadingChars() const { return maxReadingChars_; }
private:
    std::vector<uint8_t> buf_;
    uint32_t numReadings_ = 0, numEntries_ = 0, maxReadingChars_ = 0, posSize_ = 0;
    const uint32_t* readingOffsets_ = nullptr;  // [numReadings+1]
    const uint32_t* entryStart_ = nullptr;      // [numReadings+1]
    const uint8_t*  entryRecs_ = nullptr;       // 6byte * numEntries
    const uint32_t* surfaceOffsets_ = nullptr;  // [numEntries+1]
    const uint8_t*  posFlags_ = nullptr;        // [posSize]
    const char*     readingBlob_ = nullptr;
    const char*     surfaceBlob_ = nullptr;
};

// ---------------------------------------------------------------- Connection
class Connection {
public:
    bool Load(const std::string& path);
    bool Loaded() const { return size_ != 0; }
    int Cost(uint16_t ridLeft, uint16_t lidRight) const {
        return mat_[(size_t)ridLeft * size_ + lidRight];
    }
private:
    std::vector<int16_t> mat_;
    uint32_t size_ = 0;
};

// ---------------------------------------------------------------- Rescorer
// ハイブリッド用フック。data/nn_model.bin が存在すれば読み込まれ、
// 隣接語境界の文字ペアに対する補正コストを格子に加算する。
// 形式: "JIMENN01" + u32 count + {u32 hash; s16 cost}[count] (hash昇順)
class Rescorer {
public:
    bool Load(const std::string& path);          // 失敗時は無効のまま (補正0)
    bool Loaded() const { return !table_.empty(); }
    int PairCost(const std::string& leftSurface, const std::string& rightSurface) const;
private:
    std::vector<std::pair<uint32_t, int16_t>> table_;
};

// ---------------------------------------------------------------- Learning
// 候補選択の学習。Space で選んだ候補が確定されるたびに (読み, 表層) を
// カウントし、以後の変換で語コストにボーナスを与える。
// 保存形式: TSV "読み\t表層\t回数"。プロセス間は mtime で再読込。
class LearningStore {
public:
    void SetPath(const std::string& path);
    bool ReloadIfChanged();
    void Record(const std::string& reading, const std::string& surface);
    // 該当読みの学習エントリ (無ければ nullptr)
    const std::vector<std::pair<std::string, uint32_t>>* Find(
        const std::string& reading) const;
    // 回数 → コストボーナス (負値)。1回=-400、対数飽和で最大 -1500。
    static int Bonus(uint32_t count);
private:
    void Save() const;
    std::unordered_map<std::string,
                       std::vector<std::pair<std::string, uint32_t>>> byReading_;
    std::string path_;
    int64_t mtime_ = -1;
};

// ---------------------------------------------------------------- Romaji
// kana.txt (CSV: 出力,キー1,キー2,...) による最長一致ローマ字→かな変換。
// 出力中の ASCII 文字は未処理入力として先頭に戻される (例: "nnb"→"んb")。
class RomajiConverter {
public:
    bool Load(const std::string& path);
    bool Loaded() const { return !table_.empty(); }
    // pending に1文字追加し、確定したかなを out に追記する。
    // pending には未確定ローマ字が残る (例: "ky")。
    void Feed(char c, std::string& pending, std::string& kanaOut) const;
    // 入力終了時のフラッシュ ("n" 単独 → ん、その他は素通し)
    std::string Flush(std::string& pending) const;
    // かな → ローマ字 (F9/F10 用。各かなの先頭キー表記を使用)
    std::string ToRomaji(const std::string& kana) const;
private:
    void Drain(std::string& pending, std::string& kanaOut, bool flush) const;
    std::unordered_map<std::string, std::string> table_;
    std::unordered_set<std::string> prefixes_;
    size_t maxKeyLen_ = 1;
    std::unordered_map<std::string, std::string> kanaToRomaji_;
    size_t maxKanaKeyCp_ = 1;  // 逆引きキーの最大コードポイント数
};

// ---------------------------------------------------------------- Lattice
struct Node {
    int begin = 0, end = 0;        // かな配列上の [begin, end)
    std::string surface;
    std::string reading;
    uint16_t lid = 0, rid = 0;
    int16_t wcost = 0;
    bool functional = false;
    bool punct = false;
    bool learned = false;          // 学習ボーナス適用済み (補正減衰に使用)
    // viterbi
    int64_t total = 0;
    int prev = -1;
};

struct Segment {
    int begin = 0, end = 0;        // かな配列上の範囲
    std::string surface;           // 表示文字列 (UTF-8)
    std::string reading;           // ひらがな読み
    uint16_t lid = 0, rid = 0;
    bool endsWithPunct = false;
    // 文節内構造 (候補列挙用): [begin,contentEnd)=自立語、以降=付属語
    int contentEnd = 0;
    std::string tailSurface;       // 付属語部分の表示
    uint16_t tailLid = 0;          // 付属語先頭の lid
};

struct Pin {                        // 手動候補選択の固定
    int begin = 0, end = 0;
    std::string surface;
    uint16_t lid = 0, rid = 0;
    // 確定時に学習へ記録する内容 (自立語部分のみ)
    std::string learnReading;
    std::string learnSurface;
};

struct Candidate {
    std::string surface;
    uint16_t lid = 0, rid = 0;
    int64_t score = 0;          // 並び順 (小さいほど上位)
    bool wholeSegment = false;  // 文節全体読みの単語 (付属語を連結しない)
};

// ---------------------------------------------------------------- Engine
class Engine {
public:
    // dataDir に system.dic / connection.bin / kana.txt (/ nn_model.bin 任意)
    bool Load(const std::string& dataDir, const std::string& kanaCsvPath);
    bool Loaded() const { return dict_.Loaded() && conn_.Loaded() && romaji_.Loaded(); }

    // ユーザー辞書 (TSV: 読み\t単語)。ファイル更新時刻が変わると再読込。
    bool LoadUserDict(const std::string& path);
    bool ReloadUserDictIfChanged();

    // ハイブリッド補正 (nn_model.bin) の有効/無効
    void SetRescorerEnabled(bool on) { rescorerEnabled_ = on; }
    bool RescorerActive() const { return rescorerEnabled_ && rescorer_.Loaded(); }

    // LaTeX 風記号 (\sigma 等)。data/latex_symbols.json から読み込み。
    const std::string* LatexLookup(const std::string& name) const {
        auto it = latexMap_.find(name);
        return (it == latexMap_.end()) ? nullptr : &it->second;
    }
    bool LatexAvailable() const { return !latexMap_.empty(); }

    // 候補選択学習
    void SetLearningPath(const std::string& path) { learning_.SetPath(path); }
    bool ReloadLearningIfChanged() { return learning_.ReloadIfChanged(); }
    void SetLearningEnabled(bool on) { learningEnabled_ = on; }
    void RecordSelection(const std::string& reading, const std::string& surface) {
        if (learningEnabled_ && !reading.empty() && !surface.empty())
            learning_.Record(reading, surface);
    }

    // かな列 (UTF-8 codepoint 単位) を文節列にデコード。pins は固定制約。
    std::vector<Segment> Decode(const std::vector<std::string>& kana,
                                const std::vector<Pin>& pins) const;
    // span [begin,end) の候補列挙 (文脈コスト込みで昇順)。
    std::vector<Candidate> Candidates(const std::vector<std::string>& kana,
                                      int begin, int end,
                                      uint16_t prevRid, uint16_t nextLid,
                                      int maxCount = 32) const;
    const RomajiConverter& Romaji() const { return romaji_; }
private:
    void BuildNodes(const std::vector<std::string>& kana,
                    const std::vector<Pin>& pins,
                    std::vector<std::vector<Node>>& nodesByBegin) const;
    Dictionary dict_;
    Connection conn_;
    Rescorer rescorer_;
    RomajiConverter romaji_;
    std::unordered_map<std::string, std::vector<std::string>> userDict_;
    std::string userDictPath_;
    int64_t userDictMtime_ = -1;
    bool rescorerEnabled_ = true;
    LearningStore learning_;
    bool learningEnabled_ = true;
    std::unordered_map<std::string, std::string> latexMap_;
};

// ---------------------------------------------------------------- LiveSession
// ライブ変換セッション。1コンポジション = 1セッション状態。
struct SessionResult {
    std::string commitText;            // 今回の操作で確定したテキスト (UTF-8)
    std::string composition;           // 未確定表示文字列 (UTF-8)
    // composition 内の文節区切り (UTF-8 バイトオフセット)。フォーカス表示用。
    std::vector<std::pair<int, int>> segmentSpans;
    int focusedSegment = -1;           // -1 = 自動 (フォーカス表示なし)
    bool consumed = true;
};

class LiveSession {
public:
    explicit LiveSession(Engine& engine) : engine_(engine) {}

    static constexpr int kMaxPendingSegments = 5;  // 変換待ちキュー長 (文節)

    SessionResult InputChar(char c);       // 印字可能 ASCII
    SessionResult Backspace();
    SessionResult CommitAll();             // Enter
    SessionResult Clear();                 // Escape
    SessionResult MoveFocus(int delta);    // ←/→ で文節移動
    SessionResult NextCandidate(int delta);// Space/↑↓ で候補切替
    // F6=ひらがな F7=全角カナ F8=半角カナ F9=全角英数 F10=半角英数
    // F9/F10 は再押下で 小文字→大文字→先頭大文字 を循環
    SessionResult FunctionKey(int n);

    // LaTeX 風記号入力 (\ で開始)
    SessionResult ConvertLatexToken();       // Space/Tab での明示変換
    bool LatexActive() const { return latexActive_; }
    // 0=変換しない (\ は文字として入力) / 1=自動変換 (非英字で確定) /
    // 2=ショートカット変換 (Space/Tab のみ)
    void SetLatexMode(int mode) { latexMode_ = mode; }
    // 記号の全角入力 ([→「 ]→」 !→！ 等)
    void SetFullWidthSymbols(bool on) { fullWidthSymbols_ = on; }
    bool Composing() const {
        return !kana_.empty() || !pendingRomaji_.empty() || latexActive_;
    }
    const std::vector<std::string>& CandidateList() const { return candidates_; }
    int CandidateIndex() const { return candIndex_; }
    bool CandidateOpen() const { return candOpen_; }
    void CloseCandidates() { candOpen_ = false; candidates_.clear(); candIndex_ = -1; }

private:
    SessionResult Refresh(std::string commitPrefix = std::string());
    void AutoCommit(std::string& out);     // 5文節超過/句読点 確定処理
    void FlushPendingToKana();             // 未確定ローマ字をかな確定 (n→ん等)
    void ResetFocus();
    std::string KanaString(int begin, int end) const;

    Engine& engine_;
    std::vector<std::string> kana_;        // 確定かな (codepoint 毎)
    std::string pendingRomaji_;
    std::vector<Pin> pins_;
    std::vector<Segment> segments_;        // 直近デコード結果
    int focus_ = -1;
    bool candOpen_ = false;
    std::vector<Candidate> candidatesFull_;
    std::vector<std::string> candidates_;
    int candIndex_ = -1;
    // 候補オープン時の文節構造 (ピンで再デコードされても元のスパンを保持)
    int candSegBegin_ = 0, candSegEnd_ = 0, candContentEnd_ = 0;
    uint16_t candTailRid_ = 0;
    std::string candTailSurface_;
    int fnMode_ = 0;        // 0=通常 / 6..10=ファンクション変換表示中
    int fnCase_ = 0;        // F9/F10 の大小文字循環
    std::string fnText_;
    bool latexActive_ = false;
    std::string latexToken_;
    int latexMode_ = 1;
    bool fullWidthSymbols_ = true;
    void FinishLatex(bool convert);
};

// ---------------------------------------------------------------- utf8 utils
std::vector<std::string> Utf8Split(const std::string& s);
std::string HiraToKata(const std::string& hira);
std::string HiraToHankakuKata(const std::string& hira);  // F8 用
std::string AsciiToZenkaku(const std::string& ascii);    // F9 用
bool IsPunctCp(const std::string& cp);

}  // namespace jime
