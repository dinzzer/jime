// JIME — TSF テキストサービス宣言
#pragma once
#define NOMINMAX
#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
#include <memory>

#include "../engine/engine.h"

namespace Gdiplus { class Bitmap; }

// ---------------------------------------------------------------- GUIDs
// {8A3F0E2D-5B71-4C46-9D9B-2E1A7C64F0B3}
extern const CLSID CLSID_JimeTextService;
// {B6A4F1D2-8C3E-49B7-A5D0-3F2E1C9B8A74}
extern const GUID GUID_JimeProfile;
// {C2D8E5A1-4F6B-4E29-B0C3-7A918D5E2F46} 入力中下線
extern const GUID GUID_JimeDisplayAttributeInput;
// {D4E9F6B2-5A7C-4F3A-81D4-8B029E6F3A57} フォーカス文節
extern const GUID GUID_JimeDisplayAttributeFocused;

extern HINSTANCE g_hInst;
extern LONG g_cRefDll;

void DllAddRef();
void DllRelease();

// ---------------------------------------------------------------- 候補ウィンドウ
class CCandidateWindow {
public:
    CCandidateWindow();
    ~CCandidateWindow();
    void Show(const std::vector<std::wstring>& items, int sel, POINT pt);
    void UpdateSelection(int sel);
    void Hide();
    bool IsVisible() const;
    void SetFont(const std::wstring& name, int sizePx);
    // 配色と背景画像 (空パス = 画像なし)
    void SetTheme(COLORREF bg, COLORREF text, COLORREF sel,
                  const std::wstring& imagePath);
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Paint(HDC hdc);
    int LineHeight() const;
    HWND hwnd_ = nullptr;
    std::vector<std::wstring> items_;
    int sel_ = 0;
    int scrollTop_ = 0;
    bool dwmRounded_ = false;
    std::wstring fontName_ = L"Meiryo UI";
    int fontSize_ = 15;
    COLORREF bgColor_ = RGB(255, 255, 255);
    COLORREF textColor_ = RGB(32, 32, 32);
    COLORREF selColor_ = RGB(0, 103, 192);
    std::wstring imagePath_;
    Gdiplus::Bitmap* image_ = nullptr;
};

// ---------------------------------------------------------------- テキストサービス
class CTextService : public ITfTextInputProcessorEx,
                     public ITfKeyEventSink,
                     public ITfCompositionSink,
                     public ITfDisplayAttributeProvider {
public:
    CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor(Ex)
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD flags) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM, LPARAM, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM, LPARAM, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM, LPARAM, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM, LPARAM, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec,
                                         ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid,
                                         ITfDisplayAttributeInfo** ppInfo) override;

    // 内部 (エディットセッション/タイマーから使用)
    void OnEditApply(TfEditCookie ec, ITfContext* pic,
                     const jime::SessionResult& r);
    void OnFlashTimer();
private:
    ~CTextService();
    bool EnsureEngine();
    bool IsImeOn();
    void SetImeOn(bool on);
    void ReloadSettings();   // HKCU\Software\JIME から設定読込
    bool IsKeyEaten(WPARAM wParam);
    char VkToChar(WPARAM wParam);
    void ApplyResult(ITfContext* pic, const jime::SessionResult& r);
    void EndCompositionSession(ITfContext* pic);
    void UpdateCandidateWindow(TfEditCookie ec, ITfContext* pic);
    void HideCandidates();
    void ReapplyAttributes(TfEditCookie ec, ITfContext* pic);  // フラッシュ解除
    void ReleaseCompositionContext();

    LONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD activateFlags_ = 0;
    ITfComposition* composition_ = nullptr;
    ITfContext* compositionContext_ = nullptr;  // フラッシュ解除用
    TfGuidAtom attrInput_ = TF_INVALID_GUIDATOM;
    TfGuidAtom attrFocused_ = TF_INVALID_GUIDATOM;
    TfGuidAtom attrFlash_ = TF_INVALID_GUIDATOM;
    HWND flashWnd_ = nullptr;                   // WM_TIMER 受信用の隠しウィンドウ
    std::wstring lastCompW_;                    // 直前のコンポジション (差分検出)
    int lastFocusedIdx_ = -1;
    // 表示設定 (HKCU\Software\JIME)
    std::wstring candFontName_ = L"Meiryo UI";
    int candFontSize_ = 15;
    bool flashEnabled_ = true;
    COLORREF flashColor_ = RGB(255, 221, 120);

    jime::Engine engine_;
    bool engineLoaded_ = false;
    bool engineLoadFailed_ = false;
    bool optConvertKeyOn_ = true;     // 変換キー: IME オン
    bool optNonConvertKeyOff_ = true; // 無変換キー: IME オフ
    std::unique_ptr<jime::LiveSession> session_;
    std::unique_ptr<CCandidateWindow> candWnd_;
    std::vector<std::pair<int, int>> lastSpans16_;  // UTF-16 オフセット
};

// ---------------------------------------------------------------- utils
std::wstring Utf8ToWide(const std::string& s);
int Utf8OffsetToWide(const std::string& s, int byteOffset);
std::wstring GetModuleDir();
