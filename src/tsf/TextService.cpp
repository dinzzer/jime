// JIME — TSF テキストサービス実装
#include "TextService.h"

#include <functional>
#include <olectl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUNDSMALL
#define DWMWCP_ROUNDSMALL 3
#endif

// ---------------------------------------------------------------- GUIDs
const CLSID CLSID_JimeTextService =
    {0x8A3F0E2D, 0x5B71, 0x4C46, {0x9D, 0x9B, 0x2E, 0x1A, 0x7C, 0x64, 0xF0, 0xB3}};
const GUID GUID_JimeProfile =
    {0xB6A4F1D2, 0x8C3E, 0x49B7, {0xA5, 0xD0, 0x3F, 0x2E, 0x1C, 0x9B, 0x8A, 0x74}};
const GUID GUID_JimeDisplayAttributeInput =
    {0xC2D8E5A1, 0x4F6B, 0x4E29, {0xB0, 0xC3, 0x7A, 0x91, 0x8D, 0x5E, 0x2F, 0x46}};
const GUID GUID_JimeDisplayAttributeFocused =
    {0xD4E9F6B2, 0x5A7C, 0x4F3A, {0x81, 0xD4, 0x8B, 0x02, 0x9E, 0x6F, 0x3A, 0x57}};
// {E5FA07C3-6B8D, ...} 変換フラッシュ (背景色)
static const GUID GUID_JimeDisplayAttributeFlash =
    {0xE5FA07C3, 0x6B8D, 0x4A2B, {0x92, 0xE5, 0x9C, 0x13, 0xAF, 0x70, 0x4B, 0x68}};

// 表示設定 (ReloadSettings で更新。表示属性プロバイダーが参照)
static LONG g_underlineStyle = 0;            // 0=点線 1=実線 2=破線 3=波線 4=太線
static COLORREF g_flashColor = RGB(255, 221, 120);

// ---------------------------------------------------------------- utils
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr,
                        nullptr);
    return s;
}

int Utf8OffsetToWide(const std::string& s, int byteOffset) {
    if (byteOffset <= 0) return 0;
    if (byteOffset > (int)s.size()) byteOffset = (int)s.size();
    return MultiByteToWideChar(CP_UTF8, 0, s.data(), byteOffset, nullptr, 0);
}

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? p : p.substr(0, pos);
}

// ---------------------------------------------------------------- EditSession
class CEditSession : public ITfEditSession {
public:
    using Fn = std::function<HRESULT(TfEditCookie)>;
    explicit CEditSession(Fn fn) : fn_(std::move(fn)) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = (ITfEditSession*)this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return fn_(ec); }
private:
    virtual ~CEditSession() {}
    Fn fn_;
    ULONG ref_ = 1;
};

// ---------------------------------------------------------------- DisplayAttributeInfo
class CDisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    CDisplayAttributeInfo(const GUID& guid, const wchar_t* desc,
                          const TF_DISPLAYATTRIBUTE& da)
        : guid_(guid), desc_(desc), da_(da) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
            *ppv = (ITfDisplayAttributeInfo*)this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP GetGUID(GUID* pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = guid_;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR* pbstr) override {
        if (!pbstr) return E_INVALIDARG;
        *pbstr = SysAllocString(desc_.c_str());
        return *pbstr ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override {
        if (!pda) return E_INVALIDARG;
        *pda = da_;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP Reset() override { return S_OK; }
private:
    virtual ~CDisplayAttributeInfo() {}
    GUID guid_;
    std::wstring desc_;
    TF_DISPLAYATTRIBUTE da_;
    ULONG ref_ = 1;
};

static TF_DISPLAYATTRIBUTE DaInput() {
    TF_DISPLAYATTRIBUTE da = {{TF_CT_NONE, 0}, {TF_CT_NONE, 0}, TF_LS_DOT,
                              FALSE,           {TF_CT_NONE, 0}, TF_ATTR_INPUT};
    switch (g_underlineStyle) {
        case 1: da.lsStyle = TF_LS_SOLID; break;
        case 2: da.lsStyle = TF_LS_DASH; break;
        case 3: da.lsStyle = TF_LS_SQUIGGLE; break;  // 波線
        case 4: da.lsStyle = TF_LS_SOLID; da.fBoldLine = TRUE; break;
        default: da.lsStyle = TF_LS_DOT; break;
    }
    return da;
}
static TF_DISPLAYATTRIBUTE DaFocused() {
    TF_DISPLAYATTRIBUTE da = {{TF_CT_NONE, 0}, {TF_CT_NONE, 0}, TF_LS_SOLID,
                              TRUE,            {TF_CT_NONE, 0},
                              TF_ATTR_TARGET_CONVERTED};
    return da;
}
static TF_DISPLAYATTRIBUTE DaFlash() {
    TF_DISPLAYATTRIBUTE da = DaInput();
    da.crBk.type = TF_CT_COLORREF;
    da.crBk.cr = g_flashColor;
    da.bAttr = TF_ATTR_CONVERTED;
    return da;
}

class CEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
            *ppv = (IEnumTfDisplayAttributeInfo*)this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override {
        if (!ppEnum) return E_INVALIDARG;
        auto* e = new CEnumDisplayAttributeInfo();
        e->index_ = index_;
        *ppEnum = e;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo,
                      ULONG* pcFetched) override {
        ULONG fetched = 0;
        while (fetched < ulCount && index_ < 3) {
            if (index_ == 0)
                rgInfo[fetched] = new CDisplayAttributeInfo(
                    GUID_JimeDisplayAttributeInput, L"JIME Input", DaInput());
            else if (index_ == 1)
                rgInfo[fetched] = new CDisplayAttributeInfo(
                    GUID_JimeDisplayAttributeFocused, L"JIME Focused",
                    DaFocused());
            else
                rgInfo[fetched] = new CDisplayAttributeInfo(
                    GUID_JimeDisplayAttributeFlash, L"JIME Flash", DaFlash());
            fetched++;
            index_++;
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == ulCount) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG ulCount) override {
        index_ += (int)ulCount;
        return S_OK;
    }
private:
    virtual ~CEnumDisplayAttributeInfo() {}
    int index_ = 0;
    ULONG ref_ = 1;
};

// ---------------------------------------------------------------- CandidateWindow
static const wchar_t kCandClass[] = L"JimeCandidateWindow";
static const int kCandMaxVisible = 9;
static const int kCandWidth = 240;
static const int kCandPad = 6;       // ウィンドウ内余白

static ULONG_PTR g_gdiplusToken = 0;
static void EnsureGdiplus() {
    if (!g_gdiplusToken) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    }
}

CCandidateWindow::CCandidateWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = kCandClass;
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);  // 二重登録は失敗するだけなので無視
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kCandClass, L"",
        WS_POPUP, 0, 0, kCandWidth, 100, nullptr, nullptr, g_hInst, this);
    if (hwnd_) {
        // Windows 11: ネイティブ角丸。失敗時 (Win10) は SetWindowRgn で代替
        int pref = DWMWCP_ROUNDSMALL;
        dwmRounded_ = SUCCEEDED(DwmSetWindowAttribute(
            hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref)));
    }
}

CCandidateWindow::~CCandidateWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (image_) delete image_;
}

void CCandidateWindow::SetTheme(COLORREF bg, COLORREF text, COLORREF sel,
                                const std::wstring& imagePath) {
    bgColor_ = bg;
    textColor_ = text;
    selColor_ = sel;
    if (imagePath != imagePath_) {
        imagePath_ = imagePath;
        if (image_) {
            delete image_;
            image_ = nullptr;
        }
        if (!imagePath_.empty()) {
            EnsureGdiplus();
            Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(imagePath_.c_str());
            if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) image_ = bmp;
            else delete bmp;
        }
    }
    if (hwnd_ && IsWindowVisible(hwnd_)) InvalidateRect(hwnd_, nullptr, TRUE);
}

bool CCandidateWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void CCandidateWindow::SetFont(const std::wstring& name, int sizePx) {
    if (!name.empty()) fontName_ = name;
    if (sizePx >= 10 && sizePx <= 48) fontSize_ = sizePx;
}

int CCandidateWindow::LineHeight() const { return fontSize_ + 12; }

void CCandidateWindow::Show(const std::vector<std::wstring>& items, int sel,
                            POINT pt) {
    if (!hwnd_) return;
    items_ = items;
    sel_ = sel;
    scrollTop_ = 0;
    UpdateSelection(sel);
    int visible = (int)items_.size();
    if (visible > kCandMaxVisible) visible = kCandMaxVisible;
    int h = visible * LineHeight() + kCandPad * 2;
    int w = kCandWidth;
    // 画面外にはみ出す場合は上に出す
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    int x = pt.x, y = pt.y + 4;
    if (y + h > mi.rcWork.bottom) y = pt.y - h - 28;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!dwmRounded_)
        SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, 12, 12), TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CCandidateWindow::UpdateSelection(int sel) {
    sel_ = sel;
    if (sel_ < scrollTop_) scrollTop_ = sel_;
    if (sel_ >= scrollTop_ + kCandMaxVisible)
        scrollTop_ = sel_ - kCandMaxVisible + 1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void CCandidateWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void CCandidateWindow::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HBRUSH bgBrush = CreateSolidBrush(bgColor_);
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);
    if (image_) {
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(image_, Gdiplus::Rect(0, 0, rc.right, rc.bottom));
    }
    const int lineH = LineHeight();
    HFONT font = CreateFontW(-fontSize_, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, fontName_.c_str());
    HFONT numFont = CreateFontW(-(fontSize_ - 3), 0, 0, 0, FW_NORMAL, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    int y = kCandPad;
    for (int i = scrollTop_;
         i < (int)items_.size() && i < scrollTop_ + kCandMaxVisible; i++) {
        RECT line = {kCandPad, y, rc.right - kCandPad, y + lineH};
        if (i == sel_) {
            // 角丸のアクセント色ハイライト
            HBRUSH selBrush = CreateSolidBrush(selColor_);
            HPEN selPen = CreatePen(PS_SOLID, 1, selColor_);
            HGDIOBJ ob = SelectObject(hdc, selBrush);
            HGDIOBJ op = SelectObject(hdc, selPen);
            RoundRect(hdc, line.left, line.top + 1, line.right, line.bottom - 1,
                      8, 8);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(selBrush);
            DeleteObject(selPen);
        }
        // 番号 (小さめ・薄め)
        wchar_t num[8];
        wsprintfW(num, L"%d", (i % 9) + 1);
        RECT nr = line;
        nr.left += 8;
        SelectObject(hdc, numFont);
        SetTextColor(hdc, i == sel_ ? RGB(200, 222, 245) : RGB(150, 150, 150));
        DrawTextW(hdc, num, -1, &nr, DT_SINGLELINE | DT_VCENTER);
        // 候補本体
        RECT tr = line;
        tr.left += 26;
        SelectObject(hdc, font);
        SetTextColor(hdc, i == sel_ ? RGB(255, 255, 255) : textColor_);
        DrawTextW(hdc, items_[i].c_str(), (int)items_[i].size(), &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        y += lineH;
    }
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteObject(numFont);
}

LRESULT CALLBACK CCandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam) {
    CCandidateWindow* self;
    if (msg == WM_NCCREATE) {
        self = (CCandidateWindow*)((CREATESTRUCT*)lParam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    self = (CCandidateWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (self) self->Paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- トレイインジケーター
// Microsoft IME の あ/A インジケーターに相当。プロセス間で重複しないよう
// 名前付きミューテックスで単一所有とし、状態変更は FindWindow + PostMessage
// でアイコン所有プロセスへ通知する。右クリックメニューから設定アプリを起動。
namespace tray {

const wchar_t kWndClass[] = L"JimeTrayWnd";
const UINT kCallbackMsg = WM_APP + 1;
const UINT kStateMsg = WM_APP + 2;  // wParam: 1=ひらがな(オン) 0=英数(オフ)

HANDLE g_mutex = nullptr;
HWND g_wnd = nullptr;
HICON g_icons[2] = {};
bool g_on = true;
UINT g_taskbarCreated = 0;

HICON MakeModeIcon(bool on) {
    int s = GetSystemMetrics(SM_CXSMICON);
    if (s <= 0) s = 16;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, s, s);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT rc = {0, 0, s, s};
    HBRUSH bg = CreateSolidBrush(on ? RGB(0, 103, 192) : RGB(64, 64, 64));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HFONT font = CreateFontW(-(s - 3), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Meiryo UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, on ? L"あ" : L"A", 1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    // 全面不透明マスク
    HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
    HDC mdc = CreateCompatibleDC(screen);
    HGDIOBJ om = SelectObject(mdc, mask);
    PatBlt(mdc, 0, 0, s, s, BLACKNESS);
    SelectObject(mdc, om);
    DeleteDC(mdc);
    ICONINFO ii = {TRUE, 0, 0, mask, bmp};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}

void AddIcon() {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = g_wnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kCallbackMsg;
    nid.hIcon = g_icons[g_on ? 1 : 0];
    lstrcpynW(nid.szTip,
              g_on ? L"JIME ― ひらがな"
                   : L"JIME ― 英数",
              ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void SetState(bool on) {
    g_on = on;
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = g_wnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = g_icons[on ? 1 : 0];
    lstrcpynW(nid.szTip,
              on ? L"JIME ― ひらがな"
                 : L"JIME ― 英数",
              ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
                g_on ? L"現在: ひらがな (あ)"
                     : L"現在: 英数 (A)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, L"JIME の設定(&S)...");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);  // メニューを閉じられるようにする定石
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == 1) {
        std::wstring exe = GetModuleDir() + L"\\jime_config.exe";
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kCallbackMsg) {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU ||
            lParam == WM_LBUTTONUP)
            ShowMenu(hwnd);
        return 0;
    }
    if (msg == kStateMsg) {
        SetState(wParam != 0);
        return 0;
    }
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        AddIcon();  // エクスプローラー再起動時に再登録
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Create() {
    if (g_wnd) return;
    HANDLE m = CreateMutexW(nullptr, TRUE, L"Local\\JimeTrayIndicator");
    if (!m) return;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        return;  // 別プロセスが所有中
    }
    g_mutex = m;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kWndClass;
    RegisterClassW(&wc);
    g_wnd = CreateWindowW(kWndClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr,
                          g_hInst, nullptr);
    if (!g_wnd) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return;
    }
    g_icons[0] = MakeModeIcon(false);
    g_icons[1] = MakeModeIcon(true);
    AddIcon();
}

void Destroy() {
    if (!g_wnd) return;
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = g_wnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(g_wnd);
    g_wnd = nullptr;
    for (HICON& ic : g_icons) {
        if (ic) DestroyIcon(ic);
        ic = nullptr;
    }
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
}

// どのプロセスからでも所有者へ状態を通知
void NotifyState(bool on) {
    if (g_wnd) {
        SetState(on);
        return;
    }
    HWND w = FindWindowW(kWndClass, nullptr);
    if (w) PostMessageW(w, kStateMsg, on ? 1 : 0, 0);
}

}  // namespace tray

// ---------------------------------------------------------------- CTextService
CTextService::CTextService() { DllAddRef(); }

CTextService::~CTextService() {
    if (composition_) {
        composition_->Release();
        composition_ = nullptr;
    }
    if (compositionContext_) {
        compositionContext_->Release();
        compositionContext_ = nullptr;
    }
    if (flashWnd_) {
        DestroyWindow(flashWnd_);
        flashWnd_ = nullptr;
    }
    DllRelease();
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
        *ppv = (ITfTextInputProcessor*)this;
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
        *ppv = (ITfTextInputProcessorEx*)this;
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
        *ppv = (ITfKeyEventSink*)this;
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
        *ppv = (ITfCompositionSink*)this;
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
        *ppv = (ITfDisplayAttributeProvider*)this;
    if (*ppv) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) CTextService::Release() {
    LONG r = InterlockedDecrement(&refCount_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid,
                                      DWORD flags) {
    threadMgr_ = ptim;
    threadMgr_->AddRef();
    clientId_ = tid;
    activateFlags_ = flags;

    // キーイベントシンク
    ITfKeystrokeMgr* keyMgr = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                             (void**)&keyMgr))) {
        keyMgr->AdviseKeyEventSink(clientId_, (ITfKeyEventSink*)this, TRUE);
        keyMgr->Release();
    }
    // 表示属性アトム
    ITfCategoryMgr* catMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                   (void**)&catMgr))) {
        catMgr->RegisterGUID(GUID_JimeDisplayAttributeInput, &attrInput_);
        catMgr->RegisterGUID(GUID_JimeDisplayAttributeFocused, &attrFocused_);
        catMgr->RegisterGUID(GUID_JimeDisplayAttributeFlash, &attrFlash_);
        catMgr->Release();
    }
    ReloadSettings();
    tray::Create();  // タスクトレイ インジケーター (既に他プロセス所有なら何もしない)
    SetImeOn(true);  // 選択された時点で日本語入力を有効化
    return S_OK;
}

STDMETHODIMP CTextService::Deactivate() {
    if (session_ && session_->Composing()) session_->Clear();
    HideCandidates();
    ReleaseCompositionContext();
    if (flashWnd_) {
        DestroyWindow(flashWnd_);
        flashWnd_ = nullptr;
    }
    tray::Destroy();
    if (threadMgr_) {
        ITfKeystrokeMgr* keyMgr = nullptr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                                 (void**)&keyMgr))) {
            keyMgr->UnadviseKeyEventSink(clientId_);
            keyMgr->Release();
        }
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

// ---------------------------------------------------------------- engine
// ---------------------------------------------------------------- フラッシュ用隠しウィンドウ
static const wchar_t kFlashWndClass[] = L"JimeFlashWnd";

static LRESULT CALLBACK FlashWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
    if (msg == WM_TIMER) {
        CTextService* svc =
            (CTextService*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (svc) svc->OnFlashTimer();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static std::wstring UserDictPath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    return std::wstring(buf) + L"\\JIME\\user_dict.tsv";
}

bool CTextService::EnsureEngine() {
    if (engineLoaded_) return true;
    if (engineLoadFailed_) return false;
    std::string dir = WideToUtf8(GetModuleDir());
    std::string dataDir = dir + "\\data";
    if (!engine_.Load(dataDir, dataDir + "\\kana.txt")) {
        engineLoadFailed_ = true;
        return false;
    }
    std::wstring ud = UserDictPath();
    if (!ud.empty()) {
        engine_.LoadUserDict(WideToUtf8(ud));
        std::wstring dir = ud.substr(0, ud.find_last_of(L'\\'));
        engine_.SetLearningPath(WideToUtf8(dir + L"\\learning.tsv"));
    }
    engineLoaded_ = true;
    session_.reset(new jime::LiveSession(engine_));
    candWnd_.reset(new CCandidateWindow());
    ReloadSettings();  // フォント/テーマ/入力設定をセッションへ反映
    if (!flashWnd_) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = FlashWndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = kFlashWndClass;
        RegisterClassW(&wc);  // 二重登録は無視
        flashWnd_ = CreateWindowW(kFlashWndClass, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, g_hInst, nullptr);
        if (flashWnd_)
            SetWindowLongPtrW(flashWnd_, GWLP_USERDATA, (LONG_PTR)this);
    }
    return true;
}

void CTextService::ReloadSettings() {
    auto readDword = [](const wchar_t* name, DWORD def) -> DWORD {
        DWORD val = def, size = sizeof(val);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\JIME", name,
                         RRF_RT_REG_DWORD, nullptr, &val, &size) != ERROR_SUCCESS)
            return def;
        return val;
    };
    optConvertKeyOn_ = readDword(L"ConvertKeyOn", 1) != 0;
    optNonConvertKeyOff_ = readDword(L"NonConvertKeyOff", 1) != 0;
    engine_.SetRescorerEnabled(readDword(L"HybridRescoring", 1) != 0);
    engine_.SetLearningEnabled(readDword(L"Learning", 1) != 0);
    // 表示設定
    auto readString = [](const wchar_t* name,
                         const std::wstring& def) -> std::wstring {
        wchar_t buf[512];
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\JIME", name,
                         RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
            return def;
        return buf;
    };
    candFontName_ = readString(L"CandidateFontName", L"Meiryo UI");
    candFontSize_ = (int)readDword(L"CandidateFontSize", 15);
    flashEnabled_ = readDword(L"FlashEnabled", 1) != 0;
    flashColor_ = (COLORREF)readDword(L"FlashColor", RGB(255, 221, 120));
    g_flashColor = flashColor_;
    g_underlineStyle = (LONG)readDword(L"UnderlineStyle", 0);
    if (candWnd_) {
        candWnd_->SetFont(candFontName_, candFontSize_);
        candWnd_->SetTheme(
            (COLORREF)readDword(L"CandBgColor", RGB(255, 255, 255)),
            (COLORREF)readDword(L"CandTextColor", RGB(32, 32, 32)),
            (COLORREF)readDword(L"CandSelColor", RGB(0, 103, 192)),
            readString(L"CandBgImage", L""));
    }
    // 入力設定
    if (session_) {
        session_->SetFullWidthSymbols(readDword(L"FullWidthSymbols", 1) != 0);
        session_->SetLatexMode((int)readDword(L"LatexMode", 1));
    }
}

bool CTextService::IsImeOn() {
    bool on = false;
    ITfCompartmentMgr* mgr = nullptr;
    if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(
                          IID_ITfCompartmentMgr, (void**)&mgr))) {
        ITfCompartment* comp = nullptr;
        if (SUCCEEDED(mgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                          &comp))) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(comp->GetValue(&var)) && var.vt == VT_I4)
                on = var.lVal != 0;
            VariantClear(&var);
            comp->Release();
        }
        mgr->Release();
    }
    return on;
}

void CTextService::SetImeOn(bool on) {
    ITfCompartmentMgr* mgr = nullptr;
    if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(
                          IID_ITfCompartmentMgr, (void**)&mgr))) {
        ITfCompartment* comp = nullptr;
        if (SUCCEEDED(mgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                          &comp))) {
            VARIANT var;
            var.vt = VT_I4;
            var.lVal = on ? 1 : 0;
            comp->SetValue(clientId_, &var);
            comp->Release();
        }
        mgr->Release();
    }
    tray::NotifyState(on);
}

// ---------------------------------------------------------------- key handling
char CTextService::VkToChar(WPARAM w) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (w >= 'A' && w <= 'Z') return shift ? (char)w : (char)(w - 'A' + 'a');
    if (w >= '0' && w <= '9') {
        if (!shift) return (char)w;
        static const char jp[] = {0, '!', '"', '#', '$', '%', '&', '\'', '(', ')'};
        return jp[w - '0'];  // JP 配列 (Shift+0 は無し)
    }
    if (w >= VK_NUMPAD0 && w <= VK_NUMPAD9) return (char)('0' + (w - VK_NUMPAD0));
    switch (w) {  // JP 配列基準
        case VK_OEM_MINUS:  return shift ? '=' : '-';
        case VK_OEM_PLUS:   return shift ? '+' : ';';
        case VK_OEM_1:      return shift ? '*' : ':';
        case VK_OEM_COMMA:  return shift ? '<' : ',';
        case VK_OEM_PERIOD: return shift ? '>' : '.';
        case VK_OEM_2:      return shift ? '?' : '/';
        case VK_OEM_3:      return shift ? '`' : '@';
        case VK_OEM_4:      return shift ? '{' : '[';
        case VK_OEM_5:      return shift ? '|' : '\\';
        case VK_OEM_6:      return shift ? '}' : ']';
        case VK_OEM_7:      return shift ? '~' : '^';
        case VK_OEM_102:    return shift ? '_' : '\\';
    }
    return 0;
}

static bool IsToggleKey(WPARAM w) {
    return w == VK_KANJI || w == VK_OEM_AUTO || w == VK_OEM_ENLW;
}

bool CTextService::IsKeyEaten(WPARAM w) {
    if (IsToggleKey(w)) return true;
    // Ctrl/Alt 併用ショートカット (コピペ等) は奪わない
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
        return false;
    bool imeOn = IsImeOn();
    if (w == VK_NONCONVERT) return optNonConvertKeyOff_ && imeOn;
    if (!imeOn) return w == VK_CONVERT && optConvertKeyOn_;
    bool composing = session_ && session_->Composing();
    if (composing) {
        switch (w) {
            case VK_RETURN: case VK_ESCAPE: case VK_BACK:
            case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
            case VK_SPACE: case VK_CONVERT:
            case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10:
                return true;
        }
        if (w == VK_TAB) return session_->LatexActive();
    } else if (w == VK_SPACE) {
        return true;  // 全角スペース挿入
    }
    return VkToChar(w) != 0;
}

STDMETHODIMP CTextService::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        ReloadSettings();
        tray::NotifyState(IsImeOn());  // Win+Space 等の外部切替も反映
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext*, WPARAM wParam, LPARAM,
                                         BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    EnsureEngine();
    *pfEaten = IsKeyEaten(wParam);
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM,
                                       BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM,
                                     BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    if (!EnsureEngine()) return S_OK;

    if (IsToggleKey(wParam)) {
        if (session_->Composing()) {
            jime::SessionResult r = session_->CommitAll();
            ApplyResult(pic, r);
        }
        SetImeOn(!IsImeOn());
        *pfEaten = TRUE;
        return S_OK;
    }
    // Ctrl/Alt 併用ショートカットは奪わない
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
        return S_OK;
    // 無変換: IME オフ / 変換: IME オン (設定で無効化可)
    if (wParam == VK_NONCONVERT) {
        if (!optNonConvertKeyOff_ || !IsImeOn()) return S_OK;
        if (session_->Composing()) {
            jime::SessionResult r = session_->CommitAll();
            ApplyResult(pic, r);
        }
        SetImeOn(false);
        *pfEaten = TRUE;
        return S_OK;
    }
    if (wParam == VK_CONVERT && !IsImeOn()) {
        if (optConvertKeyOn_) {
            SetImeOn(true);
            *pfEaten = TRUE;
        }
        return S_OK;
    }
    if (!IsImeOn()) return S_OK;

    bool composing = session_->Composing();
    jime::SessionResult r;
    bool handled = true;

    switch (wParam) {
        case VK_RETURN:
            if (!composing) { handled = false; break; }
            r = session_->CommitAll();
            break;
        case VK_ESCAPE:
            if (!composing) { handled = false; break; }
            r = session_->Clear();
            break;
        case VK_BACK:
            if (!composing) { handled = false; break; }
            r = session_->Backspace();
            break;
        case VK_LEFT:
            if (!composing) { handled = false; break; }
            r = session_->MoveFocus(-1);
            break;
        case VK_RIGHT:
            if (!composing) { handled = false; break; }
            r = session_->MoveFocus(+1);
            break;
        case VK_UP:
            if (!composing || !session_->CandidateOpen()) { handled = false; break; }
            r = session_->NextCandidate(-1);
            break;
        case VK_DOWN:
            if (!composing) { handled = false; break; }
            r = session_->NextCandidate(+1);
            break;
        case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10:
            if (!composing) { handled = false; break; }
            r = session_->FunctionKey((int)(wParam - VK_F6) + 6);
            break;
        case VK_TAB:
            if (!composing || !session_->LatexActive()) { handled = false; break; }
            r = session_->ConvertLatexToken();
            break;
        case VK_SPACE:
        case VK_CONVERT:
            if (composing && session_->LatexActive()) {
                r = session_->ConvertLatexToken();  // \token を明示変換
            } else if (composing) {
                r = session_->NextCandidate(+1);
            } else if (wParam == VK_SPACE) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                r.commitText = shift ? " " : "\xE3\x80\x80";  // 全角スペース
            } else {
                handled = false;
            }
            break;
        default: {
            char c = VkToChar(wParam);
            if (c == 0) { handled = false; break; }
            if (!composing) {
                // 新規コンポジション開始時に設定/辞書/学習の更新を反映
                ReloadSettings();
                engine_.ReloadUserDictIfChanged();
                engine_.ReloadLearningIfChanged();
            }
            r = session_->InputChar(c);
            break;
        }
    }
    if (!handled) return S_OK;
    if (!r.consumed) return S_OK;
    ApplyResult(pic, r);
    *pfEaten = TRUE;
    return S_OK;
}

// ---------------------------------------------------------------- composition
void CTextService::ApplyResult(ITfContext* pic, const jime::SessionResult& r) {
    if (!pic) return;
    CEditSession* es = new CEditSession([this, pic, r](TfEditCookie ec) {
        OnEditApply(ec, pic, r);
        return S_OK;
    });
    HRESULT hrSession = S_OK;
    pic->RequestEditSession(clientId_, es, TF_ES_SYNC | TF_ES_READWRITE,
                            &hrSession);
    es->Release();
}

void CTextService::OnEditApply(TfEditCookie ec, ITfContext* pic,
                               const jime::SessionResult& r) {
    std::wstring commitW = Utf8ToWide(r.commitText);
    std::wstring compW = Utf8ToWide(r.composition);

    // コンポジション開始 (必要なら)
    if (!composition_ && (!compW.empty() || !commitW.empty())) {
        ITfInsertAtSelection* pias = nullptr;
        if (SUCCEEDED(pic->QueryInterface(IID_ITfInsertAtSelection,
                                          (void**)&pias))) {
            ITfRange* range = nullptr;
            if (SUCCEEDED(pias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY,
                                                      nullptr, 0, &range)) &&
                range) {
                ITfContextComposition* pcc = nullptr;
                if (SUCCEEDED(pic->QueryInterface(IID_ITfContextComposition,
                                                  (void**)&pcc))) {
                    pcc->StartComposition(ec, range, (ITfCompositionSink*)this,
                                          &composition_);
                    pcc->Release();
                    if (composition_ && !compositionContext_) {
                        compositionContext_ = pic;
                        pic->AddRef();
                    }
                }
                range->Release();
            }
            pias->Release();
        }
    }
    if (!composition_) return;

    ITfRange* range = nullptr;
    if (FAILED(composition_->GetRange(&range)) || !range) return;

    // 全文 (確定分 + 未確定分) を一旦コンポジション範囲に書き込む
    std::wstring full = commitW + compW;
    range->SetText(ec, 0, full.c_str(), (LONG)full.size());

    if (!commitW.empty()) {
        // コンポジション開始位置を確定分の直後へ移動
        ITfRange* newStart = nullptr;
        if (SUCCEEDED(range->Clone(&newStart)) && newStart) {
            newStart->Collapse(ec, TF_ANCHOR_START);
            LONG moved = 0;
            newStart->ShiftStart(ec, (LONG)commitW.size(), &moved, nullptr);
            newStart->Collapse(ec, TF_ANCHOR_START);
            composition_->ShiftStart(ec, newStart);
            newStart->Release();
        }
    }

    if (compW.empty()) {
        // 未確定なし → コンポジション終了
        composition_->EndComposition(ec);
        composition_->Release();
        composition_ = nullptr;
        ReleaseCompositionContext();
        // キャレットは挿入末尾に置かれる
        ITfRange* endR = nullptr;
        if (SUCCEEDED(range->Clone(&endR)) && endR) {
            endR->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION sel;
            sel.range = endR;
            sel.style.ase = TF_AE_NONE;
            sel.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &sel);
            endR->Release();
        }
        range->Release();
        HideCandidates();
        return;
    }

    // 表示属性 (下線 + フォーカス文節) を適用
    lastSpans16_.clear();
    for (auto& sp : r.segmentSpans)
        lastSpans16_.push_back({Utf8OffsetToWide(r.composition, sp.first),
                                Utf8OffsetToWide(r.composition, sp.second)});
    lastFocusedIdx_ = r.focusedSegment;
    ReapplyAttributes(ec, pic);

    // 変換フラッシュ: 直前表示との差分に漢字が現れたら背景色を一瞬変える
    if (flashEnabled_ && commitW.empty()) {
        size_t p = 0;
        while (p < lastCompW_.size() && p < compW.size() &&
               lastCompW_[p] == compW[p])
            p++;
        size_t s = 0;
        while (s < lastCompW_.size() - p && s < compW.size() - p &&
               lastCompW_[lastCompW_.size() - 1 - s] ==
                   compW[compW.size() - 1 - s])
            s++;
        int fb = (int)p;
        int fe = (int)compW.size() - (int)s;
        bool hasKanji = false;
        for (int i = fb; i < fe && i < (int)compW.size(); i++) {
            wchar_t c = compW[i];
            if ((c >= 0x4E00 && c <= 0x9FFF) || c == 0x3005) {
                hasKanji = true;
                break;
            }
        }
        if (hasKanji && fe > fb) {
            ITfRange* cr = nullptr;
            if (SUCCEEDED(composition_->GetRange(&cr)) && cr) {
                ITfProperty* prop = nullptr;
                if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop))) {
                    ITfRange* fr = nullptr;
                    if (SUCCEEDED(cr->Clone(&fr)) && fr) {
                        fr->Collapse(ec, TF_ANCHOR_START);
                        LONG moved = 0;
                        fr->ShiftEnd(ec, fe, &moved, nullptr);
                        fr->ShiftStart(ec, fb, &moved, nullptr);
                        VARIANT var;
                        var.vt = VT_I4;
                        var.lVal = attrFlash_;
                        prop->SetValue(ec, fr, &var);
                        fr->Release();
                    }
                    prop->Release();
                }
                cr->Release();
            }
            if (flashWnd_) SetTimer(flashWnd_, 1, 180, nullptr);
        }
    }
    lastCompW_ = compW;

    // キャレットを末尾へ
    ITfRange* compRange = nullptr;
    if (SUCCEEDED(composition_->GetRange(&compRange)) && compRange) {
        ITfRange* endR = nullptr;
        if (SUCCEEDED(compRange->Clone(&endR)) && endR) {
            endR->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION sel;
            sel.range = endR;
            sel.style.ase = TF_AE_NONE;
            sel.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &sel);
            endR->Release();
        }
        compRange->Release();
    }
    range->Release();
    UpdateCandidateWindow(ec, pic);
}

void CTextService::ReapplyAttributes(TfEditCookie ec, ITfContext* pic) {
    if (!composition_ || !pic) return;
    ITfRange* range = nullptr;
    if (FAILED(composition_->GetRange(&range)) || !range) return;
    ITfProperty* prop = nullptr;
    if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop))) {
        VARIANT var;
        var.vt = VT_I4;
        var.lVal = attrInput_;
        prop->SetValue(ec, range, &var);
        if (lastFocusedIdx_ >= 0 &&
            lastFocusedIdx_ < (int)lastSpans16_.size()) {
            auto [b, e] = lastSpans16_[lastFocusedIdx_];
            ITfRange* seg = nullptr;
            if (SUCCEEDED(range->Clone(&seg)) && seg) {
                seg->Collapse(ec, TF_ANCHOR_START);
                LONG moved = 0;
                seg->ShiftEnd(ec, e, &moved, nullptr);
                seg->ShiftStart(ec, b, &moved, nullptr);
                VARIANT var2;
                var2.vt = VT_I4;
                var2.lVal = attrFocused_;
                prop->SetValue(ec, seg, &var2);
                seg->Release();
            }
        }
        prop->Release();
    }
    range->Release();
}

void CTextService::OnFlashTimer() {
    if (flashWnd_) KillTimer(flashWnd_, 1);
    if (!composition_ || !compositionContext_) return;
    ITfContext* pic = compositionContext_;
    pic->AddRef();
    CEditSession* es = new CEditSession([this, pic](TfEditCookie ec) {
        ReapplyAttributes(ec, pic);
        pic->Release();
        return S_OK;
    });
    HRESULT hr = S_OK;
    if (FAILED(pic->RequestEditSession(clientId_, es,
                                       TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                                       &hr)))
        pic->Release();
    es->Release();
}

void CTextService::ReleaseCompositionContext() {
    if (flashWnd_) KillTimer(flashWnd_, 1);
    if (compositionContext_) {
        compositionContext_->Release();
        compositionContext_ = nullptr;
    }
    lastCompW_.clear();
    lastFocusedIdx_ = -1;
    lastSpans16_.clear();
}

void CTextService::UpdateCandidateWindow(TfEditCookie ec, ITfContext* pic) {
    if (!candWnd_ || !session_) return;
    if (!session_->CandidateOpen()) {
        candWnd_->Hide();
        return;
    }
    // フォーカス文節の矩形を取得して表示位置を決める
    POINT pt = {0, 0};
    bool gotPos = false;
    if (composition_) {
        ITfRange* range = nullptr;
        if (SUCCEEDED(composition_->GetRange(&range)) && range) {
            ITfContextView* view = nullptr;
            if (SUCCEEDED(pic->GetActiveView(&view)) && view) {
                RECT rc = {};
                BOOL clipped = FALSE;
                ITfRange* target = nullptr;
                range->Clone(&target);
                if (target) {
                    // フォーカス文節範囲に絞る
                    // (lastSpans16_ は OnEditApply で更新済み)
                    if (SUCCEEDED(view->GetTextExt(ec, target, &rc, &clipped))) {
                        pt.x = rc.left;
                        pt.y = rc.bottom;
                        gotPos = true;
                    }
                    target->Release();
                }
                view->Release();
            }
            range->Release();
        }
    }
    if (!gotPos) GetCursorPos(&pt);
    std::vector<std::wstring> items;
    for (auto& s : session_->CandidateList()) items.push_back(Utf8ToWide(s));
    candWnd_->Show(items, session_->CandidateIndex(), pt);
}

void CTextService::HideCandidates() {
    if (candWnd_) candWnd_->Hide();
    if (session_) session_->CloseCandidates();
}

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie,
                                                   ITfComposition* pComposition) {
    // アプリ側でコンポジションが強制終了された
    if (composition_ && pComposition == composition_) {
        composition_->Release();
        composition_ = nullptr;
    }
    ReleaseCompositionContext();
    if (session_) session_->Clear();
    HideCandidates();
    return S_OK;
}

void CTextService::EndCompositionSession(ITfContext* pic) {
    if (!composition_ || !pic) return;
    CEditSession* es = new CEditSession([this](TfEditCookie ec) {
        if (composition_) {
            composition_->EndComposition(ec);
            composition_->Release();
            composition_ = nullptr;
        }
        return S_OK;
    });
    HRESULT hr = S_OK;
    pic->RequestEditSession(clientId_, es, TF_ES_SYNC | TF_ES_READWRITE, &hr);
    es->Release();
}

// ---------------------------------------------------------------- display attr
STDMETHODIMP CTextService::EnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new CEnumDisplayAttributeInfo();
    return S_OK;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(
    REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    if (IsEqualGUID(guid, GUID_JimeDisplayAttributeInput)) {
        *ppInfo = new CDisplayAttributeInfo(GUID_JimeDisplayAttributeInput,
                                            L"JIME Input", DaInput());
        return S_OK;
    }
    if (IsEqualGUID(guid, GUID_JimeDisplayAttributeFocused)) {
        *ppInfo = new CDisplayAttributeInfo(GUID_JimeDisplayAttributeFocused,
                                            L"JIME Focused", DaFocused());
        return S_OK;
    }
    if (IsEqualGUID(guid, GUID_JimeDisplayAttributeFlash)) {
        *ppInfo = new CDisplayAttributeInfo(GUID_JimeDisplayAttributeFlash,
                                            L"JIME Flash", DaFlash());
        return S_OK;
    }
    *ppInfo = nullptr;
    return E_INVALIDARG;
}
