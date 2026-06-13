// JIME 設定コンソール (モダン UI)
// - 動作: 変換/無変換キー、ハイブリッド補正、候補選択学習
// - 入力: 記号の全角/半角、LaTeX 風記号入力 (\sigma 等) のモード
// - 表示: 候補フォント / 下線スタイル / 変換フラッシュ /
//          候補ウィンドウの配色・背景画像
// - ユーザー辞書の登録/削除 (%APPDATA%\JIME\user_dict.tsv)
// 設定は即時保存され、IME 側は次の入力開始時に自動で反映する。
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
// 視覚スタイル (comctl32 v6) を有効化
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

const wchar_t kRegKey[] = L"Software\\JIME";
const COLORREF kAccent = RGB(0, 103, 192);
const COLORREF kText = RGB(32, 32, 32);
const COLORREF kGray = RGB(120, 120, 120);

// コントロール ID
enum {
    IDC_CHK_CONVERT = 100,
    IDC_CHK_NONCONVERT,
    IDC_CHK_HYBRID,
    IDC_CHK_LEARN,
    IDC_CHK_ZENKIGOU = 110,
    IDC_CMB_LATEX,
    IDC_LBL_FONT = 120,
    IDC_BTN_FONT,
    IDC_CMB_UNDERLINE,
    IDC_CHK_FLASH,
    IDC_BTN_FLASHCOLOR,
    IDC_SW_FLASH,
    IDC_BTN_CANDBG,
    IDC_SW_CANDBG,
    IDC_BTN_CANDTX,
    IDC_SW_CANDTX,
    IDC_BTN_CANDSEL,
    IDC_SW_CANDSEL,
    IDC_LBL_IMG,
    IDC_BTN_IMG,
    IDC_BTN_IMGCLR,
    IDC_EDIT_READING = 140,
    IDC_EDIT_WORD,
    IDC_BTN_ADD,
    IDC_LIST_DICT,
    IDC_BTN_DELETE,
};

struct DictEntry {
    std::wstring reading;
    std::wstring word;
};

struct Swatch {
    int swatchId;
    int buttonId;
    const wchar_t* regName;
    COLORREF def;
    COLORREF cur = 0;
    HBRUSH brush = nullptr;
};

Swatch g_swatches[] = {
    {IDC_SW_FLASH, IDC_BTN_FLASHCOLOR, L"FlashColor", RGB(255, 221, 120)},
    {IDC_SW_CANDBG, IDC_BTN_CANDBG, L"CandBgColor", RGB(255, 255, 255)},
    {IDC_SW_CANDTX, IDC_BTN_CANDTX, L"CandTextColor", RGB(32, 32, 32)},
    {IDC_SW_CANDSEL, IDC_BTN_CANDSEL, L"CandSelColor", RGB(0, 103, 192)},
};

HFONT g_font = nullptr;
HFONT g_headerFont = nullptr;
HBRUSH g_white = nullptr;
std::set<HWND> g_headers;
std::vector<DictEntry> g_dict;

// ---------------------------------------------------------------- helpers
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr,
                        nullptr);
    return s;
}

DWORD ReadRegDword(const wchar_t* name, DWORD def) {
    DWORD val = def, size = sizeof(val);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_DWORD,
                     nullptr, &val, &size) != ERROR_SUCCESS)
        return def;
    return val;
}

void WriteRegDword(const wchar_t* name, DWORD val) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey,
                        nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

std::wstring ReadRegString(const wchar_t* name, const std::wstring& def) {
    wchar_t buf[512];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_SZ, nullptr,
                     buf, &size) != ERROR_SUCCESS)
        return def;
    return buf;
}

void WriteRegString(const wchar_t* name, const std::wstring& val) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey,
                        nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE*)val.c_str(),
                       (DWORD)(val.size() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

std::wstring UserDictPath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    std::wstring dir = std::wstring(buf) + L"\\JIME";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\user_dict.tsv";
}

void LoadDict() {
    g_dict.clear();
    std::wstring path = UserDictPath();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(h, nullptr);
    std::string buf(size, 0);
    DWORD read = 0;
    ReadFile(h, &buf[0], size, &read, nullptr);
    CloseHandle(h);
    buf.resize(read);
    size_t pos = 0;
    while (pos < buf.size()) {
        size_t nl = buf.find('\n', pos);
        if (nl == std::string::npos) nl = buf.size();
        std::string line = buf.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF)
            line = line.substr(3);
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size())
            continue;
        g_dict.push_back({Utf8ToWide(line.substr(0, tab)),
                          Utf8ToWide(line.substr(tab + 1))});
    }
}

void SaveDict() {
    std::wstring path = UserDictPath();
    std::string out;
    for (const auto& e : g_dict)
        out += WideToUtf8(e.reading) + "\t" + WideToUtf8(e.word) + "\n";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(h);
}

void RefreshList(HWND list) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& e : g_dict) {
        std::wstring item = e.reading + L"  →  " + e.word;
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

std::wstring GetEditText(HWND edit) {
    wchar_t buf[256];
    GetWindowTextW(edit, buf, 256);
    return buf;
}

bool IsHiragana(const std::wstring& s) {
    if (s.empty()) return false;
    for (wchar_t c : s)
        if (!((c >= 0x3041 && c <= 0x3096) || c == 0x30FC /*ー*/)) return false;
    return true;
}

void LoadSwatches() {
    for (auto& sw : g_swatches) {
        sw.cur = (COLORREF)ReadRegDword(sw.regName, sw.def);
        if (sw.brush) DeleteObject(sw.brush);
        sw.brush = CreateSolidBrush(sw.cur);
    }
}

Swatch* SwatchByButton(int buttonId) {
    for (auto& sw : g_swatches)
        if (sw.buttonId == buttonId) return &sw;
    return nullptr;
}

Swatch* SwatchById(int swatchId) {
    for (auto& sw : g_swatches)
        if (sw.swatchId == swatchId) return &sw;
    return nullptr;
}

void UpdateFontLabel(HWND hwnd) {
    std::wstring name = ReadRegString(L"CandidateFontName", L"Meiryo UI");
    DWORD size = ReadRegDword(L"CandidateFontSize", 15);
    wchar_t text[96];
    wsprintfW(text, L"%s, %upx", name.c_str(), size);
    SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_FONT), text);
}

void UpdateImageLabel(HWND hwnd) {
    std::wstring path = ReadRegString(L"CandBgImage", L"");
    if (path.empty()) {
        SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_IMG), L"(なし)");
    } else {
        size_t pos = path.find_last_of(L"\\/");
        SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_IMG),
                       pos == std::wstring::npos ? path.c_str()
                                                 : path.c_str() + pos + 1);
    }
}

// ---------------------------------------------------------------- window
HWND Create(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
            int x, int y, int w, int h, int id, HFONT font = nullptr) {
    HWND hwnd = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w,
                              h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)(font ? font : g_font), TRUE);
    return hwnd;
}

HWND Header(HWND parent, const wchar_t* text, int y) {
    HWND h = Create(parent, L"STATIC", text, 0, 20, y, 430, 24, 0, g_headerFont);
    g_headers.insert(h);
    return h;
}

void Separator(HWND parent, int y) {
    Create(parent, L"STATIC", L"", SS_ETCHEDHORZ, 20, y, 440, 1, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            LoadSwatches();
            // ===== 動作設定 =====
            Header(hwnd, L"動作設定", 14);
            Create(hwnd, L"BUTTON", L"変換キーで IME オン", BS_AUTOCHECKBOX, 32,
                   42, 410, 22, IDC_CHK_CONVERT);
            Create(hwnd, L"BUTTON", L"無変換キーで IME オフ", BS_AUTOCHECKBOX,
                   32, 66, 410, 22, IDC_CHK_NONCONVERT);
            Create(hwnd, L"BUTTON",
                   L"ハイブリッド補正を使用 (data\\nn_model.bin)",
                   BS_AUTOCHECKBOX, 32, 90, 410, 22, IDC_CHK_HYBRID);
            Create(hwnd, L"BUTTON", L"候補選択を学習して変換の優先順位に反映",
                   BS_AUTOCHECKBOX, 32, 114, 410, 22, IDC_CHK_LEARN);
            CheckDlgButton(hwnd, IDC_CHK_CONVERT,
                           ReadRegDword(L"ConvertKeyOn", 1) ? BST_CHECKED
                                                            : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHK_NONCONVERT,
                           ReadRegDword(L"NonConvertKeyOff", 1) ? BST_CHECKED
                                                                : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHK_HYBRID,
                           ReadRegDword(L"HybridRescoring", 1) ? BST_CHECKED
                                                               : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHK_LEARN,
                           ReadRegDword(L"Learning", 1) ? BST_CHECKED
                                                        : BST_UNCHECKED);
            Separator(hwnd, 144);

            // ===== 入力 =====
            Header(hwnd, L"入力", 154);
            Create(hwnd, L"BUTTON", L"記号を全角で入力 ([ ] は 「 」 になる)",
                   BS_AUTOCHECKBOX, 32, 182, 410, 22, IDC_CHK_ZENKIGOU);
            CheckDlgButton(hwnd, IDC_CHK_ZENKIGOU,
                           ReadRegDword(L"FullWidthSymbols", 1) ? BST_CHECKED
                                                                : BST_UNCHECKED);
            Create(hwnd, L"STATIC", L"LaTeX 記号入力 (\\sigma → σ):", 0, 32,
                   212, 184, 20, 0);
            HWND cmbL = Create(hwnd, L"COMBOBOX", L"",
                               CBS_DROPDOWNLIST | WS_VSCROLL, 222, 208, 220,
                               200, IDC_CMB_LATEX);
            const wchar_t* lmodes[] = {L"変換しない", L"自動で変換",
                                       L"Space / Tab で変換"};
            for (auto* s : lmodes)
                SendMessageW(cmbL, CB_ADDSTRING, 0, (LPARAM)s);
            SendMessageW(cmbL, CB_SETCURSEL, ReadRegDword(L"LatexMode", 1), 0);
            Separator(hwnd, 242);

            // ===== 表示 =====
            Header(hwnd, L"表示", 252);
            Create(hwnd, L"STATIC", L"候補ウィンドウのフォント:", 0, 32, 284,
                   170, 20, 0);
            Create(hwnd, L"STATIC", L"", 0, 204, 284, 150, 20, IDC_LBL_FONT);
            Create(hwnd, L"BUTTON", L"変更...", BS_PUSHBUTTON, 364, 280, 78, 26,
                   IDC_BTN_FONT);
            Create(hwnd, L"STATIC", L"変換中の下線:", 0, 32, 316, 110, 20, 0);
            HWND cmb = Create(hwnd, L"COMBOBOX", L"",
                              CBS_DROPDOWNLIST | WS_VSCROLL, 204, 312, 158, 200,
                              IDC_CMB_UNDERLINE);
            const wchar_t* styles[] = {L"点線", L"実線", L"破線",
                                       L"波線 (スクイグル)", L"太線"};
            for (auto* s : styles)
                SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)s);
            SendMessageW(cmb, CB_SETCURSEL, ReadRegDword(L"UnderlineStyle", 0),
                         0);
            Create(hwnd, L"BUTTON", L"漢字に変換された時に背景色をフラッシュ",
                   BS_AUTOCHECKBOX, 32, 348, 300, 22, IDC_CHK_FLASH);
            CheckDlgButton(hwnd, IDC_CHK_FLASH,
                           ReadRegDword(L"FlashEnabled", 1) ? BST_CHECKED
                                                            : BST_UNCHECKED);
            Create(hwnd, L"STATIC", L"", WS_BORDER, 336, 348, 22, 22,
                   IDC_SW_FLASH);
            Create(hwnd, L"BUTTON", L"色...", BS_PUSHBUTTON, 364, 346, 78, 26,
                   IDC_BTN_FLASHCOLOR);
            Create(hwnd, L"STATIC", L"候補ウィンドウの色:", 0, 32, 384, 130, 20,
                   0);
            Create(hwnd, L"BUTTON", L"背景", BS_PUSHBUTTON, 168, 380, 52, 24,
                   IDC_BTN_CANDBG);
            Create(hwnd, L"STATIC", L"", WS_BORDER, 224, 382, 20, 20,
                   IDC_SW_CANDBG);
            Create(hwnd, L"BUTTON", L"文字", BS_PUSHBUTTON, 258, 380, 52, 24,
                   IDC_BTN_CANDTX);
            Create(hwnd, L"STATIC", L"", WS_BORDER, 314, 382, 20, 20,
                   IDC_SW_CANDTX);
            Create(hwnd, L"BUTTON", L"選択", BS_PUSHBUTTON, 348, 380, 52, 24,
                   IDC_BTN_CANDSEL);
            Create(hwnd, L"STATIC", L"", WS_BORDER, 404, 382, 20, 20,
                   IDC_SW_CANDSEL);
            Create(hwnd, L"STATIC", L"候補の背景画像:", 0, 32, 416, 104, 20, 0);
            Create(hwnd, L"STATIC", L"", 0, 140, 416, 188, 20, IDC_LBL_IMG);
            Create(hwnd, L"BUTTON", L"選択...", BS_PUSHBUTTON, 334, 412, 56, 24,
                   IDC_BTN_IMG);
            Create(hwnd, L"BUTTON", L"クリア", BS_PUSHBUTTON, 394, 412, 50, 24,
                   IDC_BTN_IMGCLR);
            UpdateFontLabel(hwnd);
            UpdateImageLabel(hwnd);
            Separator(hwnd, 448);

            // ===== ユーザー辞書 =====
            Header(hwnd, L"ユーザー辞書", 458);
            Create(hwnd, L"STATIC", L"よみ:", 0, 32, 492, 40, 20, 0);
            Create(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 74, 488, 140,
                   26, IDC_EDIT_READING);
            Create(hwnd, L"STATIC", L"単語:", 0, 228, 492, 40, 20, 0);
            Create(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 270, 488, 92,
                   26, IDC_EDIT_WORD);
            Create(hwnd, L"BUTTON", L"追加", BS_PUSHBUTTON, 370, 488, 72, 26,
                   IDC_BTN_ADD);
            Create(hwnd, L"LISTBOX", L"",
                   WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                   32, 522, 410, 148, IDC_LIST_DICT);
            Create(hwnd, L"BUTTON", L"選択した項目を削除", BS_PUSHBUTTON, 32,
                   678, 160, 28, IDC_BTN_DELETE);
            Create(hwnd, L"STATIC",
                   L"設定と辞書は次の入力開始時に自動で反映されます", 0, 200,
                   684, 250, 20, 0);
            LoadDict();
            RefreshList(GetDlgItem(hwnd, IDC_LIST_DICT));
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND ctl = (HWND)lParam;
            int id = GetDlgCtrlID(ctl);
            Swatch* sw = SwatchById(id);
            if (sw) return (LRESULT)sw->brush;
            SetBkMode(hdc, TRANSPARENT);
            if (g_headers.count(ctl)) SetTextColor(hdc, kAccent);
            else if (id == IDC_LBL_FONT || id == IDC_LBL_IMG)
                SetTextColor(hdc, kGray);
            else SetTextColor(hdc, kText);
            return (LRESULT)g_white;
        }
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kText);
            return (LRESULT)g_white;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_CHK_CONVERT:
                    WriteRegDword(L"ConvertKeyOn",
                                  IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
                    return 0;
                case IDC_CHK_NONCONVERT:
                    WriteRegDword(L"NonConvertKeyOff",
                                  IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
                    return 0;
                case IDC_CHK_HYBRID:
                    WriteRegDword(L"HybridRescoring",
                                  IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
                    return 0;
                case IDC_CHK_LEARN:
                    WriteRegDword(L"Learning",
                                  IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
                    return 0;
                case IDC_CHK_ZENKIGOU:
                    WriteRegDword(L"FullWidthSymbols",
                                  IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
                    return 0;
                case IDC_CHK_FLASH:
                    WriteRegDword(L"FlashEnabled",
                                  IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
                    return 0;
                case IDC_CMB_LATEX:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL,
                                                    0, 0);
                        if (sel >= 0) WriteRegDword(L"LatexMode", sel);
                    }
                    return 0;
                case IDC_CMB_UNDERLINE:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL,
                                                    0, 0);
                        if (sel >= 0) WriteRegDword(L"UnderlineStyle", sel);
                    }
                    return 0;
                case IDC_BTN_FONT: {
                    LOGFONTW lf = {};
                    std::wstring cur =
                        ReadRegString(L"CandidateFontName", L"Meiryo UI");
                    lstrcpynW(lf.lfFaceName, cur.c_str(), LF_FACESIZE);
                    lf.lfHeight = -(LONG)ReadRegDword(L"CandidateFontSize", 15);
                    lf.lfCharSet = DEFAULT_CHARSET;
                    CHOOSEFONTW cf = {sizeof(cf)};
                    cf.hwndOwner = hwnd;
                    cf.lpLogFont = &lf;
                    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT |
                               CF_NOVERTFONTS;
                    if (ChooseFontW(&cf)) {
                        int px = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
                        if (px < 10) px = 10;
                        if (px > 48) px = 48;
                        WriteRegString(L"CandidateFontName", lf.lfFaceName);
                        WriteRegDword(L"CandidateFontSize", (DWORD)px);
                        UpdateFontLabel(hwnd);
                    }
                    return 0;
                }
                case IDC_BTN_FLASHCOLOR:
                case IDC_BTN_CANDBG:
                case IDC_BTN_CANDTX:
                case IDC_BTN_CANDSEL: {
                    Swatch* sw = SwatchByButton(id);
                    if (!sw) return 0;
                    static COLORREF custom[16] = {};
                    CHOOSECOLORW cc = {sizeof(cc)};
                    cc.hwndOwner = hwnd;
                    cc.rgbResult = sw->cur;
                    cc.lpCustColors = custom;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        sw->cur = cc.rgbResult;
                        WriteRegDword(sw->regName, (DWORD)sw->cur);
                        if (sw->brush) DeleteObject(sw->brush);
                        sw->brush = CreateSolidBrush(sw->cur);
                        InvalidateRect(GetDlgItem(hwnd, sw->swatchId), nullptr,
                                       TRUE);
                    }
                    return 0;
                }
                case IDC_BTN_IMG: {
                    wchar_t path[MAX_PATH] = L"";
                    OPENFILENAMEW ofn = {sizeof(ofn)};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = path;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter =
                        L"画像 (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0"
                        L"*.png;*.jpg;*.jpeg;*.bmp;*.gif\0すべて (*.*)\0*.*\0";
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        WriteRegString(L"CandBgImage", path);
                        UpdateImageLabel(hwnd);
                    }
                    return 0;
                }
                case IDC_BTN_IMGCLR:
                    WriteRegString(L"CandBgImage", L"");
                    UpdateImageLabel(hwnd);
                    return 0;
                case IDC_BTN_ADD: {
                    std::wstring reading =
                        GetEditText(GetDlgItem(hwnd, IDC_EDIT_READING));
                    std::wstring word =
                        GetEditText(GetDlgItem(hwnd, IDC_EDIT_WORD));
                    if (reading.empty() || word.empty()) {
                        MessageBoxW(hwnd, L"よみと単語を入力してください。",
                                    L"JIME", MB_ICONINFORMATION);
                        return 0;
                    }
                    if (!IsHiragana(reading)) {
                        MessageBoxW(hwnd, L"よみはひらがなで入力してください。",
                                    L"JIME", MB_ICONINFORMATION);
                        return 0;
                    }
                    g_dict.push_back({reading, word});
                    SaveDict();
                    RefreshList(GetDlgItem(hwnd, IDC_LIST_DICT));
                    SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_READING), L"");
                    SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_WORD), L"");
                    return 0;
                }
                case IDC_BTN_DELETE: {
                    HWND list = GetDlgItem(hwnd, IDC_LIST_DICT);
                    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < (int)g_dict.size()) {
                        g_dict.erase(g_dict.begin() + sel);
                        SaveDict();
                        RefreshList(list);
                    }
                    return 0;
                }
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                         L"Yu Gothic UI");
    g_headerFont = CreateFontW(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Yu Gothic UI");
    g_white = CreateSolidBrush(RGB(255, 255, 255));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_white;
    wc.lpszClassName = L"JimeConfig";
    RegisterClassW(&wc);

    RECT rc = {0, 0, 474, 716};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                              WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowW(
        L"JimeConfig", L"JIME 設定",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(g_font);
    DeleteObject(g_headerFont);
    DeleteObject(g_white);
    for (auto& sw : g_swatches)
        if (sw.brush) DeleteObject(sw.brush);
    return 0;
}
