// JIME 設定コンソール
// - 変換/無変換キーによる IME オン/オフの割り当て (HKCU\Software\JIME)
// - ユーザー辞書の登録/削除 (%APPDATA%\JIME\user_dict.tsv)
// 設定は即時保存され、IME 側は次の入力開始時に自動で反映する。
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

const wchar_t kRegKey[] = L"Software\\JIME";

// コントロール ID
enum {
    IDC_CHK_CONVERT = 100,
    IDC_CHK_NONCONVERT,
    IDC_CHK_HYBRID,
    IDC_EDIT_READING = 110,
    IDC_EDIT_WORD,
    IDC_BTN_ADD,
    IDC_LIST_DICT,
    IDC_BTN_DELETE,
};

struct DictEntry {
    std::wstring reading;
    std::wstring word;
};

HFONT g_font = nullptr;
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

// ---------------------------------------------------------------- window
HWND Create(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
            int x, int y, int w, int h, int id) {
    HWND hwnd = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w,
                              h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hwnd;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // --- 動作設定 ---
            Create(hwnd, L"BUTTON", L"動作設定", BS_GROUPBOX, 12, 10, 440, 110, 0);
            HWND c1 = Create(hwnd, L"BUTTON", L"変換キーで IME オン",
                             BS_AUTOCHECKBOX, 28, 34, 400, 22, IDC_CHK_CONVERT);
            HWND c2 = Create(hwnd, L"BUTTON", L"無変換キーで IME オフ",
                             BS_AUTOCHECKBOX, 28, 60, 400, 22,
                             IDC_CHK_NONCONVERT);
            HWND c3 = Create(hwnd, L"BUTTON",
                             L"ハイブリッド補正を使用 (data\\nn_model.bin が必要)",
                             BS_AUTOCHECKBOX, 28, 86, 400, 22, IDC_CHK_HYBRID);
            SendMessageW(c1, BM_SETCHECK,
                         ReadRegDword(L"ConvertKeyOn", 1) ? BST_CHECKED
                                                          : BST_UNCHECKED, 0);
            SendMessageW(c2, BM_SETCHECK,
                         ReadRegDword(L"NonConvertKeyOff", 1) ? BST_CHECKED
                                                              : BST_UNCHECKED, 0);
            SendMessageW(c3, BM_SETCHECK,
                         ReadRegDword(L"HybridRescoring", 1) ? BST_CHECKED
                                                             : BST_UNCHECKED, 0);
            // --- ユーザー辞書 ---
            Create(hwnd, L"BUTTON", L"ユーザー辞書", BS_GROUPBOX, 12, 130, 440,
                   300, 0);
            Create(hwnd, L"STATIC", L"よみ (ひらがな):", 0, 28, 156, 110, 20, 0);
            Create(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 140, 154, 130,
                   24, IDC_EDIT_READING);
            Create(hwnd, L"STATIC", L"単語:", 0, 280, 156, 40, 20, 0);
            Create(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 318, 154, 90,
                   24, IDC_EDIT_WORD);
            Create(hwnd, L"BUTTON", L"追加", BS_PUSHBUTTON, 414, 154, 32, 24,
                   IDC_BTN_ADD);
            Create(hwnd, L"LISTBOX", L"",
                   WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                   28, 188, 418, 200, IDC_LIST_DICT);
            Create(hwnd, L"BUTTON", L"選択した項目を削除", BS_PUSHBUTTON, 28,
                   394, 160, 26, IDC_BTN_DELETE);
            Create(hwnd, L"STATIC",
                   L"※ 設定と辞書は次の入力開始時に自動で反映されます", 0, 16,
                   438, 436, 20, 0);
            LoadDict();
            RefreshList(GetDlgItem(hwnd, IDC_LIST_DICT));
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_CHK_CONVERT:
                    WriteRegDword(L"ConvertKeyOn",
                                  IsDlgButtonChecked(hwnd, IDC_CHK_CONVERT) ==
                                          BST_CHECKED ? 1 : 0);
                    return 0;
                case IDC_CHK_NONCONVERT:
                    WriteRegDword(L"NonConvertKeyOff",
                                  IsDlgButtonChecked(hwnd, IDC_CHK_NONCONVERT) ==
                                          BST_CHECKED ? 1 : 0);
                    return 0;
                case IDC_CHK_HYBRID:
                    WriteRegDword(L"HybridRescoring",
                                  IsDlgButtonChecked(hwnd, IDC_CHK_HYBRID) ==
                                          BST_CHECKED ? 1 : 0);
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
                        MessageBoxW(hwnd,
                                    L"よみはひらがなで入力してください。",
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
                         L"Meiryo UI");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"JimeConfig";
    RegisterClassW(&wc);

    RECT rc = {0, 0, 466, 496};
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
    return 0;
}
