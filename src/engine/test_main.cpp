// JIME エンジン CLI テストハーネス
// 使い方:
//   jime_test <dataDir> <kana.txt> type "kyouhaiitenki desune."
//   jime_test <dataDir> <kana.txt> live "watasihakinougakkouheitta."
//   jime_test <dataDir> <kana.txt> repl
// "live" は1打鍵ごとの composition を出力し、再デコードで前の語が
// 切り替わる様子を確認できる。
#include "engine.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace jime;

static void PrintResult(const SessionResult& r, bool verbose) {
    if (!r.commitText.empty()) printf("[COMMIT] %s\n", r.commitText.c_str());
    if (verbose) {
        printf("  comp: ");
        size_t prev = 0;
        for (auto& sp : r.segmentSpans) {
            printf("|%s", r.composition.substr(sp.first, sp.second - sp.first).c_str());
            prev = sp.second;
        }
        (void)prev;
        printf("|\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <dataDir> <kana.csv> <type|live|repl> [text]\n",
                argv[0]);
        return 1;
    }
    Engine engine;
    if (!engine.Load(argv[1], argv[2])) {
        fprintf(stderr, "failed to load engine data\n");
        return 1;
    }
    std::string mode = argv[3];
    LiveSession session(engine);

    if (mode == "type" || mode == "live") {
        if (argc < 5) { fprintf(stderr, "missing text\n"); return 1; }
        std::string text = argv[4];
        std::string committed;
        for (char c : text) {
            SessionResult r = session.InputChar(c);
            committed += r.commitText;
            if (mode == "live") {
                printf("%c -> ", c);
                PrintResult(r, true);
            }
        }
        SessionResult r = session.CommitAll();
        committed += r.commitText;
        printf("RESULT: %s\n", committed.c_str());
        return 0;
    }

    if (mode == "repl") {
        // 行単位 REPL: 行を1文字ずつ流し、最後に確定結果を表示。
        // 特殊コマンド: ":q" 終了
        char buf[4096];
        std::string committed;
        while (fgets(buf, sizeof(buf), stdin)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (line == ":q") break;
            committed.clear();
            for (char c : line) {
                SessionResult r = session.InputChar(c);
                committed += r.commitText;
                printf("  %c -> %s", c, r.commitText.empty() ? "" : "[C]");
                printf("%s\n", r.composition.c_str());
            }
            SessionResult r = session.CommitAll();
            committed += r.commitText;
            printf("=> %s\n", committed.c_str());
        }
        return 0;
    }
    fprintf(stderr, "unknown mode\n");
    return 1;
}
