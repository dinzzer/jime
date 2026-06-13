// JIME 変換エンジン実装
#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

#include <filesystem>
// Windows では非 ASCII パス対応のため UTF-8 → wide 変換して開く
#if defined(_WIN32)
#define JIME_PATH(p) std::filesystem::u8path(p)
#else
#define JIME_PATH(p) (p)
#endif

namespace jime {

// ============================================================== utf8 utils
static int Utf8Len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

std::vector<std::string> Utf8Split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        int len = Utf8Len((unsigned char)s[i]);
        if (i + len > s.size()) len = 1;
        out.emplace_back(s, i, len);
        i += len;
    }
    return out;
}

static uint32_t Utf8Cp(const std::string& cp) {
    if (cp.empty()) return 0;
    unsigned char c0 = cp[0];
    if (c0 < 0x80) return c0;
    if ((c0 >> 5) == 0x6 && cp.size() >= 2)
        return ((c0 & 0x1F) << 6) | (cp[1] & 0x3F);
    if ((c0 >> 4) == 0xE && cp.size() >= 3)
        return ((c0 & 0x0F) << 12) | ((cp[1] & 0x3F) << 6) | (cp[2] & 0x3F);
    if ((c0 >> 3) == 0x1E && cp.size() >= 4)
        return ((c0 & 0x07) << 18) | ((cp[1] & 0x3F) << 12) |
               ((cp[2] & 0x3F) << 6) | (cp[3] & 0x3F);
    return c0;
}

static std::string CpToUtf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

std::string HiraToKata(const std::string& hira) {
    std::string out;
    for (const auto& cps : Utf8Split(hira)) {
        uint32_t cp = Utf8Cp(cps);
        if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;
        out += CpToUtf8(cp);
    }
    return out;
}

// 全角カタカナ (U+30A1..U+30F4) → 半角カタカナ (濁点は分解)
static const char* const kHankakuKata[] = {
    "ｧ", "ｱ", "ｨ", "ｲ", "ｩ", "ｳ", "ｪ", "ｴ", "ｫ", "ｵ",          // ァ..オ
    "ｶ", "ｶﾞ", "ｷ", "ｷﾞ", "ｸ", "ｸﾞ", "ｹ", "ｹﾞ", "ｺ", "ｺﾞ",      // カ..ゴ
    "ｻ", "ｻﾞ", "ｼ", "ｼﾞ", "ｽ", "ｽﾞ", "ｾ", "ｾﾞ", "ｿ", "ｿﾞ",      // サ..ゾ
    "ﾀ", "ﾀﾞ", "ﾁ", "ﾁﾞ", "ｯ", "ﾂ", "ﾂﾞ", "ﾃ", "ﾃﾞ", "ﾄ", "ﾄﾞ",  // タ..ド
    "ﾅ", "ﾆ", "ﾇ", "ﾈ", "ﾉ",                                     // ナ..ノ
    "ﾊ", "ﾊﾞ", "ﾊﾟ", "ﾋ", "ﾋﾞ", "ﾋﾟ", "ﾌ", "ﾌﾞ", "ﾌﾟ",          // ハ..プ
    "ﾍ", "ﾍﾞ", "ﾍﾟ", "ﾎ", "ﾎﾞ", "ﾎﾟ",                            // ヘ..ポ
    "ﾏ", "ﾐ", "ﾑ", "ﾒ", "ﾓ",                                     // マ..モ
    "ｬ", "ﾔ", "ｭ", "ﾕ", "ｮ", "ﾖ",                                // ャ..ヨ
    "ﾗ", "ﾘ", "ﾙ", "ﾚ", "ﾛ",                                     // ラ..ロ
    "ﾜ", "ﾜ", "ｲ", "ｴ", "ｦ", "ﾝ", "ｳﾞ",                          // ヮ..ヴ
};

std::string HiraToHankakuKata(const std::string& hira) {
    std::string out;
    for (const auto& cps : Utf8Split(hira)) {
        uint32_t cp = Utf8Cp(cps);
        if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;  // ひらがな→カタカナ
        if (cp >= 0x30A1 && cp <= 0x30F4) {
            out += kHankakuKata[cp - 0x30A1];
        } else {
            switch (cp) {
                case 0x30FC: out += "\xEF\xBD\xB0"; break;  // ー→ｰ
                case 0x30FB: out += "\xEF\xBD\xA5"; break;  // ・→･
                case 0x3001: out += "\xEF\xBD\xA4"; break;  // 、→､
                case 0x3002: out += "\xEF\xBD\xA1"; break;  // 。→｡
                case 0x300C: out += "\xEF\xBD\xA2"; break;  // 「→｢
                case 0x300D: out += "\xEF\xBD\xA3"; break;  // 」→｣
                default: out += cps; break;
            }
        }
    }
    return out;
}

std::string AsciiToZenkaku(const std::string& ascii) {
    std::string out;
    for (const auto& cps : Utf8Split(ascii)) {
        uint32_t cp = Utf8Cp(cps);
        if (cp == ' ') out += CpToUtf8(0x3000);
        else if (cp >= 0x21 && cp <= 0x7E) out += CpToUtf8(cp + 0xFEE0);
        else out += cps;
    }
    return out;
}

bool IsPunctCp(const std::string& cp) {
    uint32_t c = Utf8Cp(cp);
    return c == 0x3001 /*、*/ || c == 0x3002 /*。*/ ||
           c == 0xFF01 /*！*/ || c == 0xFF1F /*？*/ ||
           c == '!' || c == '?';
}

static bool ReadFile(const std::string& path, std::vector<uint8_t>& buf) {
    std::ifstream f(JIME_PATH(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize size = f.tellg();
    f.seekg(0);
    buf.resize((size_t)size);
    return (bool)f.read((char*)buf.data(), size);
}

// ============================================================== Dictionary
bool Dictionary::Load(const std::string& path) {
    if (!ReadFile(path, buf_) || buf_.size() < 24) return false;
    if (memcmp(buf_.data(), "JIMEDIC1", 8) != 0) return false;
    const uint8_t* p = buf_.data() + 8;
    memcpy(&numReadings_, p, 4); p += 4;
    memcpy(&numEntries_, p, 4); p += 4;
    memcpy(&maxReadingChars_, p, 4); p += 4;
    memcpy(&posSize_, p, 4); p += 4;
    readingOffsets_ = (const uint32_t*)p; p += 4 * (size_t)(numReadings_ + 1);
    entryStart_ = (const uint32_t*)p;     p += 4 * (size_t)(numReadings_ + 1);
    entryRecs_ = p;                       p += 6 * (size_t)numEntries_;
    surfaceOffsets_ = (const uint32_t*)p; p += 4 * (size_t)(numEntries_ + 1);
    posFlags_ = p;                        p += posSize_;
    readingBlob_ = (const char*)p;        p += readingOffsets_[numReadings_];
    surfaceBlob_ = (const char*)p;
    return (const uint8_t*)surfaceBlob_ + surfaceOffsets_[numEntries_] <=
           buf_.data() + buf_.size();
}

bool Dictionary::Lookup(const char* reading, size_t len,
                        uint32_t& begin, uint32_t& end) const {
    uint32_t lo = 0, hi = numReadings_;
    while (lo < hi) {  // lower_bound
        uint32_t mid = (lo + hi) / 2;
        const char* r = readingBlob_ + readingOffsets_[mid];
        size_t rlen = readingOffsets_[mid + 1] - readingOffsets_[mid];
        int c = memcmp(r, reading, std::min(rlen, len));
        if (c < 0 || (c == 0 && rlen < len)) lo = mid + 1; else hi = mid;
    }
    if (lo >= numReadings_) return false;
    const char* r = readingBlob_ + readingOffsets_[lo];
    size_t rlen = readingOffsets_[lo + 1] - readingOffsets_[lo];
    if (rlen != len || memcmp(r, reading, len) != 0) return false;
    begin = entryStart_[lo];
    end = entryStart_[lo + 1];
    return true;
}

void Dictionary::Entry(uint32_t idx, uint16_t& lid, uint16_t& rid,
                       int16_t& cost) const {
    const uint8_t* p = entryRecs_ + 6 * (size_t)idx;
    memcpy(&lid, p, 2);
    memcpy(&rid, p + 2, 2);
    memcpy(&cost, p + 4, 2);
}

std::string Dictionary::Surface(uint32_t idx) const {
    return std::string(surfaceBlob_ + surfaceOffsets_[idx],
                       surfaceOffsets_[idx + 1] - surfaceOffsets_[idx]);
}

bool Dictionary::IsFunctional(uint16_t lid) const {
    return lid < posSize_ && posFlags_[lid] != 0;
}

// ============================================================== Connection
bool Connection::Load(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!ReadFile(path, buf) || buf.size() < 12) return false;
    if (memcmp(buf.data(), "JIMECON1", 8) != 0) return false;
    uint32_t size;
    memcpy(&size, buf.data() + 8, 4);
    if (buf.size() < 12 + 2 * (size_t)size * size) return false;
    mat_.resize((size_t)size * size);
    memcpy(mat_.data(), buf.data() + 12, 2 * mat_.size());
    size_ = size;
    return true;
}

// ============================================================== Rescorer
static uint32_t Fnv1a(const std::string& a, const std::string& b) {
    uint32_t h = 2166136261u;
    for (char c : a) { h ^= (uint8_t)c; h *= 16777619u; }
    h ^= 0xFF; h *= 16777619u;
    for (char c : b) { h ^= (uint8_t)c; h *= 16777619u; }
    return h;
}

bool Rescorer::Load(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!ReadFile(path, buf) || buf.size() < 12) return false;
    if (memcmp(buf.data(), "JIMENN01", 8) != 0) return false;
    uint32_t count;
    memcpy(&count, buf.data() + 8, 4);
    if (buf.size() < 12 + 6 * (size_t)count) return false;
    table_.resize(count);
    const uint8_t* p = buf.data() + 12;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&table_[i].first, p, 4);
        memcpy(&table_[i].second, p + 4, 2);
        p += 6;
    }
    return true;
}

int Rescorer::PairCost(const std::string& left, const std::string& right) const {
    if (table_.empty() || left.empty() || right.empty()) return 0;
    // 左語の末尾文字 + 右語の先頭文字
    auto lcps = Utf8Split(left);
    int rlen = Utf8Len((unsigned char)right[0]);
    uint32_t h = Fnv1a(lcps.back(), right.substr(0, rlen));
    auto it = std::lower_bound(table_.begin(), table_.end(),
                               std::make_pair(h, (int16_t)0),
                               [](const auto& a, const auto& b) { return a.first < b.first; });
    if (it != table_.end() && it->first == h) return it->second;
    return 0;
}

// ============================================================== Learning
void LearningStore::SetPath(const std::string& path) {
    path_ = path;
    mtime_ = -1;
    ReloadIfChanged();
}

bool LearningStore::ReloadIfChanged() {
    if (path_.empty()) return false;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(JIME_PATH(path_), ec);
    if (ec) return false;
    int64_t mt = (int64_t)t.time_since_epoch().count();
    if (mt == mtime_) return false;
    mtime_ = mt;
    byReading_.clear();
    std::ifstream f(JIME_PATH(path_), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t t1 = line.find('\t');
        size_t t2 = (t1 == std::string::npos) ? std::string::npos
                                              : line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        uint32_t count = (uint32_t)atoi(line.c_str() + t2 + 1);
        if (count == 0) continue;
        byReading_[line.substr(0, t1)].push_back(
            {line.substr(t1 + 1, t2 - t1 - 1), count});
    }
    return true;
}

void LearningStore::Save() const {
    if (path_.empty()) return;
    std::ofstream f(JIME_PATH(path_), std::ios::binary | std::ios::trunc);
    for (const auto& kv : byReading_)
        for (const auto& e : kv.second)
            f << kv.first << '\t' << e.first << '\t' << e.second << '\n';
}

void LearningStore::Record(const std::string& reading,
                           const std::string& surface) {
    ReloadIfChanged();  // 他プロセスの更新を取り込んでから加算
    auto& vec = byReading_[reading];
    bool found = false;
    for (auto& e : vec) {
        if (e.first == surface) {
            e.second++;
            found = true;
            break;
        }
    }
    if (!found) vec.push_back({surface, 1});
    Save();
    std::error_code ec;
    auto t = std::filesystem::last_write_time(JIME_PATH(path_), ec);
    if (!ec) mtime_ = (int64_t)t.time_since_epoch().count();
}

const std::vector<std::pair<std::string, uint32_t>>* LearningStore::Find(
    const std::string& reading) const {
    auto it = byReading_.find(reading);
    return (it == byReading_.end()) ? nullptr : &it->second;
}

int LearningStore::Bonus(uint32_t count) {
    if (count == 0) return 0;
    // 文節分解 (起き+て vs 掟、口+が vs 甲賀 等) との競合にも勝てるよう
    // 強めに設定。明示選択した語は次回からデフォルトになる (MS-IME 同等)。
    double b = 2000.0 + 600.0 * std::log2((double)count);
    if (b > 3500.0) b = 3500.0;
    return -(int)b;
}

// ============================================================== Romaji
// CSV 1行をパース (ダブルクォート対応)
static std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inq) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; }
                else inq = false;
            } else cur += c;
        } else if (c == '"') inq = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool RomajiConverter::Load(const std::string& path) {
    std::ifstream f(JIME_PATH(path), std::ios::binary);
    if (!f) return false;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {  // BOM / ヘッダ行スキップ
            first = false;
            if (line.find("Column") != std::string::npos) continue;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
        }
        if (line.empty()) continue;
        auto cols = ParseCsvLine(line);
        if (cols.size() < 2 || cols[0].empty()) continue;
        const std::string& output = cols[0];
        for (size_t i = 1; i < cols.size(); i++) {
            const std::string& key = cols[i];
            if (key.empty()) continue;
            // ASCII 恒等マッピング (例 "n"→"n") は除外: 未一致文字は元々
            // 素通しされるため冗長で、n→ん などの変換規則を阻害する。
            if (key == output && key.size() == 1 &&
                (unsigned char)key[0] < 0x80)
                continue;
            if (table_.find(key) == table_.end()) {  // 先勝ち
                table_[key] = output;
                maxKeyLen_ = std::max(maxKeyLen_, key.size());
            }
            // 逆引き (かな→ローマ字)。純粋なかな出力のみ・先頭キー優先。
            if (kanaToRomaji_.find(output) == kanaToRomaji_.end()) {
                bool pure = true;
                for (char ch : output)
                    if ((unsigned char)ch < 0x80) { pure = false; break; }
                if (pure) {
                    kanaToRomaji_[output] = key;
                    maxKanaKeyCp_ =
                        std::max(maxKanaKeyCp_, Utf8Split(output).size());
                }
            }
        }
    }
    // 待機判定用: 全キーの真の接頭辞集合
    for (const auto& kv : table_) {
        const std::string& k = kv.first;
        for (size_t l = 1; l < k.size(); l++)
            prefixes_.insert(k.substr(0, l));
    }
    return !table_.empty();
}

static bool AllAscii(const std::string& s) {
    for (char c : s) if ((unsigned char)c >= 0x80) return false;
    return true;
}

void RomajiConverter::Drain(std::string& pending, std::string& out,
                            bool flush) const {
    while (!pending.empty()) {
        // さらに長いキーに伸びる可能性があれば待機
        if (!flush && prefixes_.count(pending)) return;
        bool matched = false;
        size_t maxLen = std::min(maxKeyLen_, pending.size());
        for (size_t len = maxLen; len >= 1; len--) {
            auto it = table_.find(pending.substr(0, len));
            if (it == table_.end()) continue;
            const std::string& val = it->second;
            if (AllAscii(val)) {  // ASCII→ASCII は素通し (無限ループ防止)
                out += val;
                pending.erase(0, len);
            } else {
                // 出力中の ASCII は未処理として先頭に戻す (例: "んb" → ん + b)
                std::string back;
                for (const auto& cp : Utf8Split(val)) {
                    if (cp.size() == 1 && (unsigned char)cp[0] < 0x80) back += cp;
                    else out += cp;
                }
                pending.erase(0, len);
                pending = back + pending;
            }
            matched = true;
            break;
        }
        if (matched) continue;
        char c = pending[0];
        // 単独 n + 子音 → ん
        if ((c == 'n' || c == 'N') && pending.size() >= 2) {
            char d = (char)tolower((unsigned char)pending[1]);
            if (isalpha((unsigned char)d) && d != 'a' && d != 'i' && d != 'u' &&
                d != 'e' && d != 'o' && d != 'y' && d != 'n') {
                out += "\xE3\x82\x93";  // ん
                pending.erase(0, 1);
                continue;
            }
        }
        if (flush && (pending == "n" || pending == "N")) {
            out += "\xE3\x82\x93";
            pending.clear();
            return;
        }
        out += c;  // 変換不能文字は素通し
        pending.erase(0, 1);
    }
}

void RomajiConverter::Feed(char c, std::string& pending,
                           std::string& kanaOut) const {
    pending += c;
    Drain(pending, kanaOut, false);
}

std::string RomajiConverter::Flush(std::string& pending) const {
    std::string out;
    Drain(pending, out, true);
    return out;
}

std::string RomajiConverter::ToRomaji(const std::string& kana) const {
    std::vector<std::string> cps = Utf8Split(kana);
    std::string out;
    size_t i = 0;
    while (i < cps.size()) {
        bool matched = false;
        size_t maxLen = std::min(maxKanaKeyCp_, cps.size() - i);
        for (size_t len = maxLen; len >= 1; len--) {
            std::string key;
            for (size_t j = 0; j < len; j++) key += cps[i + j];
            auto it = kanaToRomaji_.find(key);
            if (it != kanaToRomaji_.end()) {
                out += it->second;
                i += len;
                matched = true;
                break;
            }
        }
        if (!matched) {
            out += cps[i];
            i++;
        }
    }
    return out;
}

// ============================================================== Engine
// ---- 最小 JSON パーサ (フラットな {"key":"value",...} のみ対応) ----
static void JsonSkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r'))
        i++;
}

static bool JsonString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        char c = s[i];
        if (c == '\\') {
            i++;
            if (i >= s.size()) return false;
            char e = s[i];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': case 'f': break;
                case 'u': {
                    if (i + 4 >= s.size()) return false;
                    unsigned v = (unsigned)strtoul(s.substr(i + 1, 4).c_str(),
                                                   nullptr, 16);
                    i += 4;
                    // サロゲートペア
                    if (v >= 0xD800 && v < 0xDC00 && i + 6 < s.size() &&
                        s[i + 1] == '\\' && s[i + 2] == 'u') {
                        unsigned lo = (unsigned)strtoul(
                            s.substr(i + 3, 4).c_str(), nullptr, 16);
                        if (lo >= 0xDC00 && lo < 0xE000) {
                            v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                            i += 6;
                        }
                    }
                    out += CpToUtf8(v);
                    break;
                }
                default: return false;
            }
            i++;
        } else {
            out += c;
            i++;
        }
    }
    if (i >= s.size()) return false;
    i++;
    return true;
}

static bool ParseFlatJsonObject(
    const std::string& text,
    std::unordered_map<std::string, std::string>& out) {
    size_t i = 0;
    JsonSkipWs(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    i++;
    JsonSkipWs(text, i);
    if (i < text.size() && text[i] == '}') return true;
    while (i < text.size()) {
        JsonSkipWs(text, i);
        std::string k, v;
        if (!JsonString(text, i, k)) return false;
        JsonSkipWs(text, i);
        if (i >= text.size() || text[i] != ':') return false;
        i++;
        JsonSkipWs(text, i);
        if (!JsonString(text, i, v)) return false;
        out[k] = v;
        JsonSkipWs(text, i);
        if (i < text.size() && text[i] == ',') { i++; continue; }
        if (i < text.size() && text[i] == '}') return true;
        return false;
    }
    return false;
}

bool Engine::Load(const std::string& dataDir, const std::string& kanaCsvPath) {
    if (!dict_.Load(dataDir + "/system.dic")) return false;
    if (!conn_.Load(dataDir + "/connection.bin")) return false;
    if (!romaji_.Load(kanaCsvPath)) return false;
    rescorer_.Load(dataDir + "/nn_model.bin");  // 任意 (失敗可)
    // LaTeX 記号表 (任意)
    std::vector<uint8_t> buf;
    if (ReadFile(dataDir + "/latex_symbols.json", buf)) {
        std::string text((const char*)buf.data(), buf.size());
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF)
            text = text.substr(3);  // BOM
        ParseFlatJsonObject(text, latexMap_);
    }
    return true;
}

bool Engine::LoadUserDict(const std::string& path) {
    userDictPath_ = path;
    userDictMtime_ = -1;
    return ReloadUserDictIfChanged();
}

bool Engine::ReloadUserDictIfChanged() {
    if (userDictPath_.empty()) return false;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(JIME_PATH(userDictPath_), ec);
    if (ec) {
        if (!userDict_.empty()) userDict_.clear();
        return false;
    }
    int64_t mt = (int64_t)t.time_since_epoch().count();
    if (mt == userDictMtime_) return false;
    userDictMtime_ = mt;
    userDict_.clear();
    std::ifstream f(JIME_PATH(userDictPath_), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size())
            continue;
        userDict_[line.substr(0, tab)].push_back(line.substr(tab + 1));
    }
    return true;
}

static const uint16_t kGenericNounId = 1851;  // 名詞,一般 (mozc id.def)
static const int16_t kUnknownCost = 12000;
static const int kMaxEntriesPerReading = 48;
static const int kMaxWordLen = 16;
static const int64_t kInf = std::numeric_limits<int64_t>::max() / 4;

void Engine::BuildNodes(const std::vector<std::string>& kana,
                        const std::vector<Pin>& pins,
                        std::vector<std::vector<Node>>& byBegin) const {
    const int n = (int)kana.size();
    byBegin.assign(n, {});
    // ピン領域: 内部から出入りする境界マタギを禁止
    auto crossesPin = [&pins](int b, int e) {
        for (const auto& p : pins) {
            // [b,e) が [p.begin,p.end) と部分的に重なる (完全一致以外) は不可
            bool overlap = b < p.end && p.begin < e;
            if (overlap && !(b == p.begin && e == p.end)) return true;
            if (overlap && b == p.begin && e == p.end) return true;  // ピン自身に置換
        }
        return false;
    };
    auto pinned = [&pins](int b, int e) -> const Pin* {
        for (const auto& p : pins)
            if (p.begin == b && p.end == e) return &p;
        return nullptr;
    };

    // 部分文字列の累積バイト列を作って辞書引き
    for (int b = 0; b < n; b++) {
        std::string key;
        int maxLen = std::min({(int)dict_.MaxReadingChars(), kMaxWordLen, n - b});
        for (int l = 1; l <= maxLen; l++) {
            key += kana[b + l - 1];
            if (crossesPin(b, b + l)) {
                const Pin* p = pinned(b, b + l);
                if (p) {  // ピンノードのみ
                    Node nd;
                    nd.begin = b; nd.end = b + l;
                    nd.surface = p->surface;
                    nd.reading = key;
                    nd.lid = p->lid; nd.rid = p->rid;
                    nd.wcost = -8000;  // 強制選択
                    nd.functional = false;
                    byBegin[b].push_back(std::move(nd));
                }
                continue;
            }
            // ユーザー辞書 (低コストで優遇)
            auto uit = userDict_.find(key);
            if (uit != userDict_.end()) {
                for (const auto& surf : uit->second) {
                    Node nd;
                    nd.begin = b; nd.end = b + l;
                    nd.surface = surf;
                    nd.reading = key;
                    nd.lid = nd.rid = kGenericNounId;
                    nd.wcost = 500;
                    byBegin[b].push_back(std::move(nd));
                }
            }
            // 学習エントリ (この読みで過去に選択された表層)
            const auto* learned = learning_.Find(key);
            std::vector<bool> learnedCovered;
            if (learned) learnedCovered.assign(learned->size(), false);

            uint32_t eb, ee;
            int minCost = 32767;  // 同一読みの最安コスト (エントリはコスト昇順)
            if (dict_.Lookup(key.data(), key.size(), eb, ee)) {
                {
                    uint16_t l0, r0; int16_t c0;
                    dict_.Entry(eb, l0, r0, c0);
                    minCost = c0;
                }
                uint32_t count = std::min<uint32_t>(ee - eb, kMaxEntriesPerReading);
                for (uint32_t i = 0; i < count; i++) {
                    Node nd;
                    nd.begin = b; nd.end = b + l;
                    dict_.Entry(eb + i, nd.lid, nd.rid, nd.wcost);
                    nd.surface = dict_.Surface(eb + i);
                    nd.reading = key;
                    nd.punct = (l == 1 && IsPunctCp(kana[b]));
                    nd.functional = dict_.IsFunctional(nd.lid) || nd.punct;
                    if (learned) {
                        for (size_t li = 0; li < learned->size(); li++) {
                            if ((*learned)[li].first == nd.surface) {
                                // 同一読みの最安語より確実に安くする
                                // (明示選択は即デフォルト化。回数で深さ増加)
                                uint32_t cnt = (*learned)[li].second;
                                int v = std::min(
                                    nd.wcost + LearningStore::Bonus(cnt),
                                    minCost - 500 -
                                        50 * (int)std::min(cnt, 10u));
                                nd.wcost = (int16_t)std::max(v, -30000);
                                nd.learned = true;
                                learnedCovered[li] = true;
                                break;
                            }
                        }
                    }
                    byBegin[b].push_back(std::move(nd));
                }
            }
            // 辞書に無い学習表層 (カタカナ形等) は合成ノードで追加
            if (learned) {
                for (size_t li = 0; li < learned->size(); li++) {
                    if (learnedCovered[li]) continue;
                    Node nd;
                    nd.begin = b; nd.end = b + l;
                    nd.surface = (*learned)[li].first;
                    nd.reading = key;
                    nd.lid = nd.rid = kGenericNounId;
                    int base = std::min(
                        4000 + LearningStore::Bonus((*learned)[li].second),
                        minCost - 500);
                    nd.wcost = (int16_t)std::max(base, -30000);
                    nd.learned = true;
                    byBegin[b].push_back(std::move(nd));
                }
            }
            if (l == 1) {
                // フォールバック (接続保証): 1文字をそのまま出力
                Node nd;
                nd.begin = b; nd.end = b + 1;
                nd.surface = kana[b];
                nd.reading = kana[b];
                nd.lid = nd.rid = kGenericNounId;
                nd.punct = IsPunctCp(kana[b]);
                nd.wcost = nd.punct ? 0 : kUnknownCost;
                nd.functional = nd.punct;  // 句読点は直前文節に付属
                byBegin[b].push_back(std::move(nd));
            }
        }
    }
}

std::vector<Segment> Engine::Decode(const std::vector<std::string>& kana,
                                    const std::vector<Pin>& pins) const {
    const int n = (int)kana.size();
    std::vector<Segment> result;
    if (n == 0) return result;

    std::vector<std::vector<Node>> byBegin;
    BuildNodes(kana, pins, byBegin);

    // フラット化 + 終端リスト
    std::vector<Node> all;
    for (auto& v : byBegin)
        for (auto& nd : v) all.push_back(std::move(nd));
    std::vector<std::vector<int>> byEnd(n + 1);
    std::vector<std::vector<int>> byBeginIdx(n);
    for (int i = 0; i < (int)all.size(); i++) {
        byEnd[all[i].end].push_back(i);
        byBeginIdx[all[i].begin].push_back(i);
    }

    // ビタビ (BOS rid=0, EOS lid=0)
    const bool useNN = RescorerActive();
    for (int pos = 0; pos < n; pos++) {
        for (int idx : byBeginIdx[pos]) {
            Node& nd = all[idx];
            int64_t best = kInf;
            int bestPrev = -1;
            if (pos == 0) {
                best = conn_.Cost(0, nd.lid);
            } else {
                for (int pi : byEnd[pos]) {
                    const Node& pv = all[pi];
                    if (pv.total >= kInf) continue;
                    int64_t c = pv.total + conn_.Cost(pv.rid, nd.lid);
                    if (useNN) {
                        int rc = rescorer_.PairCost(pv.surface, nd.surface);
                        // 学習語に接する境界は補正を減衰させ、
                        // コーパス文脈がユーザーの明示選択を打ち消さないようにする
                        if (pv.learned || nd.learned) rc /= 2;
                        c += rc;
                    }
                    if (c < best) { best = c; bestPrev = pi; }
                }
            }
            nd.total = (best >= kInf) ? kInf : best + nd.wcost;
            nd.prev = bestPrev;
        }
    }
    int64_t best = kInf;
    int bestIdx = -1;
    for (int pi : byEnd[n]) {
        const Node& pv = all[pi];
        if (pv.total >= kInf) continue;
        int64_t c = pv.total + conn_.Cost(pv.rid, 0);
        if (c < best) { best = c; bestIdx = pi; }
    }
    if (bestIdx < 0) return result;

    // パス復元
    std::vector<int> path;
    for (int i = bestIdx; i >= 0; i = all[i].prev) path.push_back(i);
    std::reverse(path.begin(), path.end());

    // 文節グルーピング: 自立語で開始、付属語/句読点は直前に付属
    for (int i = 0; i < (int)path.size(); i++) {
        const Node& nd = all[path[i]];
        bool startNew = result.empty() || !nd.functional ||
                        result.back().endsWithPunct;
        if (startNew) {
            Segment s;
            s.begin = nd.begin;
            s.lid = nd.lid;
            s.contentEnd = nd.end;
            result.push_back(std::move(s));
        } else {
            Segment& t = result.back();
            if (t.tailSurface.empty()) t.tailLid = nd.lid;
            t.tailSurface += nd.surface;
        }
        Segment& s = result.back();
        s.end = nd.end;
        s.surface += nd.surface;
        s.reading += nd.reading;
        s.rid = nd.rid;
        if (nd.punct) s.endsWithPunct = true;
    }
    return result;
}

std::vector<Candidate> Engine::Candidates(const std::vector<std::string>& kana,
                                          int begin, int end,
                                          uint16_t prevRid, uint16_t nextLid,
                                          int maxCount) const {
    std::vector<std::pair<int64_t, Candidate>> scored;
    std::string key;
    for (int i = begin; i < end; i++) key += kana[i];

    // ユーザー辞書エントリを最優先で先頭に
    auto uit = userDict_.find(key);
    if (uit != userDict_.end()) {
        int64_t rank = -1000000;
        for (const auto& surf : uit->second)
            scored.push_back({rank++, {surf, kGenericNounId, kGenericNounId}});
    }
    const auto* learned = learning_.Find(key);
    std::vector<bool> learnedCovered;
    if (learned) learnedCovered.assign(learned->size(), false);

    uint32_t eb, ee;
    if (dict_.Lookup(key.data(), key.size(), eb, ee)) {
        for (uint32_t i = eb; i < ee; i++) {
            Candidate c;
            int16_t wcost;
            dict_.Entry(i, c.lid, c.rid, wcost);
            c.surface = dict_.Surface(i);
            int64_t score = (int64_t)wcost + conn_.Cost(prevRid, c.lid) +
                            conn_.Cost(c.rid, nextLid);
            if (learned) {
                for (size_t li = 0; li < learned->size(); li++) {
                    if ((*learned)[li].first == c.surface) {
                        // ユーザー辞書 (-1000000) の次に、回数順で上位固定
                        score = -800000 - (int64_t)(*learned)[li].second;
                        learnedCovered[li] = true;
                        break;
                    }
                }
            }
            scored.push_back({score, std::move(c)});
        }
    }
    // 辞書外の学習表層も学習回数順で上位に
    if (learned) {
        for (size_t li = 0; li < learned->size(); li++) {
            if (learnedCovered[li]) continue;
            scored.push_back(
                {-800000 - (int64_t)(*learned)[li].second,
                 {(*learned)[li].first, kGenericNounId, kGenericNounId}});
        }
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Candidate> out;
    std::unordered_set<std::string> seen;
    for (auto& sc : scored) {
        if (seen.insert(sc.second.surface).second) {
            sc.second.score = sc.first;
            out.push_back(std::move(sc.second));
            if ((int)out.size() >= maxCount - 2) break;
        }
    }
    // ひらがな / カタカナ (常に末尾側)
    if (!seen.count(key)) {
        Candidate c{key, kGenericNounId, kGenericNounId};
        c.score = 900000;
        out.push_back(std::move(c));
    }
    std::string kata = HiraToKata(key);
    if (kata != key && !seen.count(kata)) {
        Candidate c{kata, kGenericNounId, kGenericNounId};
        c.score = 900001;
        out.push_back(std::move(c));
    }
    return out;
}

// ============================================================== LiveSession
void LiveSession::ResetFocus() {
    focus_ = -1;
    CloseCandidates();
    fnMode_ = 0;
    fnText_.clear();
}

void LiveSession::FlushPendingToKana() {
    if (pendingRomaji_.empty()) return;
    std::string flushed = engine_.Romaji().Flush(pendingRomaji_);
    for (auto& cp : Utf8Split(flushed)) kana_.push_back(cp);
    segments_ = engine_.Decode(kana_, pins_);
}

std::string LiveSession::KanaString(int begin, int end) const {
    std::string s;
    for (int i = begin; i < end && i < (int)kana_.size(); i++) s += kana_[i];
    return s;
}

void LiveSession::AutoCommit(std::string& out) {
    if (segments_.empty()) return;
    int cutSeg = 0;
    // 句読点で確定 (最後の句読点文節まで)
    for (int i = 0; i < (int)segments_.size(); i++)
        if (segments_[i].endsWithPunct) cutSeg = i + 1;
    // 変換待ちキュー長超過
    int remain = (int)segments_.size() - cutSeg;
    if (remain > kMaxPendingSegments)
        cutSeg = (int)segments_.size() - kMaxPendingSegments;
    if (cutSeg <= 0) return;

    int cutKana = segments_[cutSeg - 1].end;
    for (int i = 0; i < cutSeg; i++) out += segments_[i].surface;
    kana_.erase(kana_.begin(), kana_.begin() + cutKana);
    // ピンの平行移動 / 破棄 (確定されたピンは学習に記録)
    std::vector<Pin> kept;
    for (auto& p : pins_) {
        if (p.begin >= cutKana) {
            p.begin -= cutKana;
            p.end -= cutKana;
            kept.push_back(p);
        } else if (p.end <= cutKana) {
            engine_.RecordSelection(p.learnReading, p.learnSurface);
        }
    }
    pins_.swap(kept);
    segments_ = engine_.Decode(kana_, pins_);
}

SessionResult LiveSession::Refresh(std::string commitPrefix) {
    SessionResult r;
    r.commitText = std::move(commitPrefix);
    segments_ = engine_.Decode(kana_, pins_);
    AutoCommit(r.commitText);

    // 表示用: 末尾の未確定ローマ字も可能な限りかな化して格子に含める
    // (例: pending "n" → ん、"nn" → ん。かな化できない "ky" 等はそのまま)
    // 内部状態 (pendingRomaji_) は変更しないため、続きの入力は正しく解釈される。
    const std::vector<Segment>* disp = &segments_;
    std::vector<Segment> preview;
    if (!pendingRomaji_.empty()) {
        std::string copy = pendingRomaji_;
        std::string flushed = engine_.Romaji().Flush(copy);
        if (!flushed.empty()) {
            std::vector<std::string> dispKana = kana_;
            for (auto& cp : Utf8Split(flushed)) dispKana.push_back(cp);
            preview = engine_.Decode(dispKana, pins_);
            disp = &preview;
        }
    }
    for (int i = 0; i < (int)disp->size(); i++) {
        int from = (int)r.composition.size();
        r.composition += (*disp)[i].surface;
        r.segmentSpans.push_back({from, (int)r.composition.size()});
    }
    if (latexActive_) {  // LaTeX トークンは生表示 (\sig...)
        int from = (int)r.composition.size();
        r.composition += "\\" + latexToken_;
        r.segmentSpans.push_back({from, (int)r.composition.size()});
    }
    if (focus_ >= (int)segments_.size()) focus_ = (int)segments_.size() - 1;
    r.focusedSegment = (focus_ < (int)disp->size()) ? focus_ : -1;
    return r;
}

// 記号の全角化 ([→「 ]→」、その他 ASCII 記号→全角形)。英数字は対象外。
static std::string SymbolsToZenkaku(const std::string& s) {
    std::string out;
    for (auto& cp : Utf8Split(s)) {
        if (cp.size() == 1) {
            unsigned char c = (unsigned char)cp[0];
            if (c == '[') { out += "\xE3\x80\x8C"; continue; }  // 「
            if (c == ']') { out += "\xE3\x80\x8D"; continue; }  // 」
            if (c > 0x20 && c < 0x7F && !isalnum(c)) {
                out += CpToUtf8(c + 0xFEE0);
                continue;
            }
        }
        out += cp;
    }
    return out;
}

void LiveSession::FinishLatex(bool convert) {
    latexActive_ = false;
    const std::string* sym =
        (convert && !latexToken_.empty()) ? engine_.LatexLookup(latexToken_)
                                          : nullptr;
    std::string text = sym ? *sym : ("\\" + latexToken_);  // 不一致はリテラル
    for (auto& cp : Utf8Split(text)) kana_.push_back(cp);
    latexToken_.clear();
}

SessionResult LiveSession::ConvertLatexToken() {
    if (!latexActive_) { SessionResult r; r.consumed = false; return r; }
    FinishLatex(true);
    return Refresh();
}

SessionResult LiveSession::InputChar(char c) {
    if (latexActive_) {
        if (isalpha((unsigned char)c)) {
            latexToken_ += c;
            return Refresh();
        }
        // 非英字: mode 1=自動変換してから通常入力, mode 2=トークン中断(変換しない)
        FinishLatex(latexMode_ == 1);
        // fall through to normal char processing
    }
    // \ で LaTeX モード開始
    if (c == '\\' && latexMode_ != 0 && engine_.LatexAvailable()) {
        latexActive_ = true;
        latexToken_.clear();
        return Refresh();
    }
    ResetFocus();
    std::string kanaOut;
    engine_.Romaji().Feed(c, pendingRomaji_, kanaOut);
    if (fullWidthSymbols_) kanaOut = SymbolsToZenkaku(kanaOut);
    for (auto& cp : Utf8Split(kanaOut)) kana_.push_back(cp);
    return Refresh();
}

SessionResult LiveSession::Backspace() {
    ResetFocus();
    if (latexActive_) {
        if (!latexToken_.empty()) {
            latexToken_.pop_back();
            return Refresh();
        }
        latexActive_ = false;  // \ だけ残っていた状態 → LaTeX モード解除
        return Refresh();
    }
    SessionResult r;
    if (!pendingRomaji_.empty()) {
        pendingRomaji_.pop_back();
    } else if (!kana_.empty()) {
        kana_.pop_back();
        std::vector<Pin> kept;
        for (auto& p : pins_)
            if (p.end <= (int)kana_.size()) kept.push_back(p);
        pins_.swap(kept);
    } else {
        r.consumed = false;
        return r;
    }
    return Refresh();
}

SessionResult LiveSession::CommitAll() {
    if (fnMode_ != 0) {  // ファンクション変換表示中はその文字列を確定
        SessionResult r;
        r.commitText = fnText_;
        kana_.clear();
        pendingRomaji_.clear();
        pins_.clear();
        segments_.clear();
        latexActive_ = false;
        latexToken_.clear();
        ResetFocus();
        return r;
    }
    // LaTeX トークンが残っていれば確定 (mode 0 ならリテラル、それ以外は変換)
    if (latexActive_) FinishLatex(latexMode_ != 0);
    std::string flushed = engine_.Romaji().Flush(pendingRomaji_);
    for (auto& cp : Utf8Split(flushed)) kana_.push_back(cp);
    segments_ = engine_.Decode(kana_, pins_);
    SessionResult r;
    for (auto& s : segments_) r.commitText += s.surface;
    // 残っているピンを全て学習記録
    for (auto& p : pins_)
        engine_.RecordSelection(p.learnReading, p.learnSurface);
    kana_.clear();
    pendingRomaji_.clear();
    pins_.clear();
    segments_.clear();
    latexActive_ = false;
    latexToken_.clear();
    ResetFocus();
    return r;
}

SessionResult LiveSession::Clear() {
    kana_.clear();
    pendingRomaji_.clear();
    pins_.clear();
    segments_.clear();
    latexActive_ = false;
    latexToken_.clear();
    ResetFocus();
    return SessionResult();
}

SessionResult LiveSession::MoveFocus(int delta) {
    CloseCandidates();
    fnMode_ = 0;
    FlushPendingToKana();
    if (segments_.empty()) { SessionResult r; r.consumed = false; return r; }
    if (focus_ < 0) focus_ = (int)segments_.size() - 1;
    focus_ = std::max(0, std::min((int)segments_.size() - 1, focus_ + delta));
    return Refresh();
}

SessionResult LiveSession::NextCandidate(int delta) {
    fnMode_ = 0;
    if (!candOpen_) FlushPendingToKana();
    if (segments_.empty()) { SessionResult r; r.consumed = false; return r; }
    if (focus_ < 0) focus_ = (int)segments_.size() - 1;

    if (!candOpen_) {
        // 初回オープン: 文節構造を保存 (再デコード後も元のスパンを参照するため)
        const Segment& seg = segments_[focus_];
        candSegBegin_   = seg.begin;
        candSegEnd_     = seg.end;
        candContentEnd_ = (seg.contentEnd > seg.begin && seg.contentEnd <= seg.end)
                              ? seg.contentEnd : seg.end;
        candTailRid_    = seg.rid;
        candTailSurface_ = seg.tailSurface;

        uint16_t prevRid = (focus_ > 0) ? segments_[focus_ - 1].rid : 0;
        bool hasTail = candContentEnd_ < candSegEnd_;
        uint16_t nextLid = hasTail ? seg.tailLid
                           : (focus_ + 1 < (int)segments_.size())
                               ? segments_[focus_ + 1].lid : 0;

        // (A) 自立語スパン候補 (こう → 甲、公、...)
        candidatesFull_ =
            engine_.Candidates(kana_, candSegBegin_, candContentEnd_, prevRid, nextLid);

        // (B) 文節全体スパン候補 (こうが → 甲賀、おきて → 掟 等)
        if (hasTail) {
            auto whole =
                engine_.Candidates(kana_, candSegBegin_, candSegEnd_, prevRid, 0);
            for (auto& cc : whole) {
                bool dup = false;
                for (const auto& ex : candidatesFull_)
                    if (ex.surface == cc.surface) { dup = true; break; }
                if (!dup) {
                    cc.score += 800;  // 非学習の全文節候補はやや下位に
                    cc.wholeSegment = true;
                    candidatesFull_.push_back(std::move(cc));
                }
            }
            std::stable_sort(candidatesFull_.begin(), candidatesFull_.end(),
                             [](const Candidate& a, const Candidate& b) {
                                 return a.score < b.score;
                             });
        }

        candidates_.clear();
        for (const auto& cc : candidatesFull_)
            candidates_.push_back(cc.wholeSegment ? cc.surface
                                                  : cc.surface + candTailSurface_);
        // 初回オープン時の開始位置を決定:
        //   現在の表示が候補リスト先頭 [0] と一致する場合 → candIndex_=0 のまま
        //   → +delta で [1](2番目候補) へ。
        //   一致しない (学習済み全文節語等が先頭に割り込んでいる場合) → candIndex_=-1
        //   → +delta で [0](1番目候補) へ。いずれの場合も1回目のスペースで
        //   「現在表示の次に最も優先度の高い候補」を選ぶ。
        if (!candidates_.empty() && candidates_[0] == seg.surface)
            candIndex_ = 0;   // 現在表示 = 先頭 → 1つ先へ進む
        else
            candIndex_ = -1;  // 現在表示 ≠ 先頭 → 先頭候補から始める
        candOpen_ = true;
    }

    if (candidatesFull_.empty()) { SessionResult r; r.consumed = false; return r; }
    candIndex_ = (candIndex_ + delta + (int)candidatesFull_.size()) %
                 (int)candidatesFull_.size();

    // 選択をピンとして固定し再デコード
    const Candidate& c = candidatesFull_[candIndex_];
    std::vector<Pin> kept;
    for (auto& p : pins_)
        if (p.end <= candSegBegin_ || p.begin >= candSegEnd_) kept.push_back(p);
    Pin pin;
    pin.begin = candSegBegin_;
    pin.end   = candSegEnd_;
    if (c.wholeSegment) {
        pin.surface      = c.surface;
        pin.lid          = c.lid;
        pin.rid          = c.rid;
        pin.learnReading = KanaString(candSegBegin_, candSegEnd_);
        pin.learnSurface = c.surface;
    } else {
        pin.surface      = c.surface + candTailSurface_;
        pin.lid          = c.lid;
        pin.rid          = candTailRid_;
        pin.learnReading = KanaString(candSegBegin_, candContentEnd_);
        pin.learnSurface = c.surface;
    }
    kept.push_back(pin);
    pins_.swap(kept);
    return Refresh();
}

SessionResult LiveSession::FunctionKey(int n) {
    if (n < 6 || n > 10) { SessionResult r; r.consumed = false; return r; }
    CloseCandidates();
    focus_ = -1;
    FlushPendingToKana();
    if (kana_.empty()) { SessionResult r; r.consumed = false; return r; }
    std::string reading = KanaString(0, (int)kana_.size());

    if (fnMode_ == n) fnCase_++; else fnCase_ = 0;
    fnMode_ = n;
    switch (n) {
        case 6:  fnText_ = reading; break;                    // ひらがな
        case 7:  fnText_ = HiraToKata(reading); break;        // 全角カタカナ
        case 8:  fnText_ = HiraToHankakuKata(reading); break; // 半角カタカナ
        case 9:                                               // 全角英数
        case 10: {                                            // 半角英数
            std::string ro = engine_.Romaji().ToRomaji(reading);
            int mode = fnCase_ % 3;
            std::string out;
            bool first = true;
            for (char ch : ro) {
                char cv = ch;
                if (isalpha((unsigned char)ch)) {
                    if (mode == 1) cv = (char)toupper((unsigned char)ch);
                    else if (mode == 2)
                        cv = first ? (char)toupper((unsigned char)ch)
                                   : (char)tolower((unsigned char)ch);
                    else cv = (char)tolower((unsigned char)ch);
                    first = false;
                }
                out += cv;
            }
            fnText_ = (n == 9) ? AsciiToZenkaku(out) : out;
            break;
        }
    }
    SessionResult r;
    r.composition = fnText_;
    r.segmentSpans.push_back({0, (int)fnText_.size()});
    r.focusedSegment = -1;
    return r;
}

}  // namespace jime
