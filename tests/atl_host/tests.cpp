#include <atlhost.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
bool RunHostLifecycleTests();
#if defined(OPENATL_ORIGINAL_COMMAP_CLASSES)
bool RunMicrosoftCommapClassTests();
bool RunMicrosoftSafeArrayTests();
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ACTIVEX_FAIL line=%d: %s hr=%08lx\n", __LINE__, #x, static_cast<unsigned long>(::GetLastError())); return false; } } while (0)
namespace {
inline constexpr IID EventIID = {0x56b852d1,0xe47c,0x48a6,{0x94,0x1b,0xde,0xae,0x8c,0x6d,0x08,0xfb}};
struct FreeDialogState { bool modal = false; int initialized = 0; HWND window = nullptr; };
INT_PTR CALLBACK FreeDialogProc(HWND window, UINT message, WPARAM, LPARAM parameter) {
    if (message != WM_INITDIALOG) return FALSE;
    auto state = reinterpret_cast<FreeDialogState*>(parameter);
    ++state->initialized; state->window = window;
    if (state->modal) ::EndDialog(window, 83);
    return TRUE;
}
class PlainDialog : public ATL::CAxDialogImpl<PlainDialog> {
public:
    enum { IDD = 602 };
    bool modal = false, modeRejected = false;
    int initialized = 0, finalized = 0;
    LPARAM parameter = 0;
    LRESULT Init(UINT, WPARAM, LPARAM value, BOOL&) {
        ++initialized; parameter = value;
        if (modal) { modeRejected = !DestroyWindow(); EndDialog(73); }
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(PlainDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
class InitFailureDialog : public ATL::CAxDialogImpl<InitFailureDialog> {
public:
    enum { IDD = 602 };
    int initialized = 0, finalized = 0;
    HRESULT AdviseSinkMap(bool advise) noexcept { return advise ? E_ACCESSDENIED : S_OK; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) { ++initialized; return TRUE; }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(InitFailureDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
struct DialogRetirement { int initialized = 0, finalized = 0, destroyed = 0; };
class CloseFailureDialog : public ATL::CAxDialogImpl<CloseFailureDialog> {
public:
    enum { IDD = 602 };
    bool fail = false, preserved = false;
    HRESULT AdviseSinkMap(bool advise) noexcept { return !advise && fail ? E_ACCESSDENIED : S_OK; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) {
        if (m_bModal) {
            fail = true; preserved = !EndDialog(1) && ::IsWindow(this->m_hWnd);
            fail = false; EndDialog(82);
        }
        return TRUE;
    }
    BEGIN_MSG_MAP(CloseFailureDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
class DeletingDialog : public ATL::CAxDialogImpl<DeletingDialog> {
    DialogRetirement& state_;
    bool modal_;
public:
    enum { IDD = 602 };
    DeletingDialog(DialogRetirement& state, bool modal) : state_(state), modal_(modal) {}
    ~DeletingDialog() { ++state_.destroyed; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) {
        ++state_.initialized;
        if (modal_) EndDialog(91); else DestroyWindow();
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++state_.finalized; delete this; }
    BEGIN_MSG_MAP(DeletingDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool plainDialogs() {
    const HINSTANCE instance = ATL::_AtlBaseModule.GetResourceInstance();
    FreeDialogState free;
    HWND window = ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(602), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.window == window && free.initialized == 1 && ::GetDlgItem(window, 702));
    CHECK(::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, L"OPENATL_PLAIN", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.initialized == 2);
    CHECK(!ATL::AtlAxCreateDialog(instance, nullptr, nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(999), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() != ERROR_SUCCESS);
    for (WORD id : {WORD(604), WORD(605)}) {
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_NOT_SUPPORTED);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == ERROR_NOT_SUPPORTED);
    }
    PlainDialog plain; plain.modal = true;
    CHECK(plain.GetIDD() == 602 && plain.GetDialogProc());
    CHECK(plain.DoModal(nullptr, 1234) == 73 && plain.initialized == 1 && plain.finalized == 1 && plain.parameter == 1234 && plain.modeRejected && !plain.m_hWnd);
    plain.modal = false; RECT ignored{};
    CHECK(plain.Create(nullptr, ignored, 5678) && plain.parameter == 5678 && plain.initialized == 2);
    CHECK(!plain.Create(nullptr) && ::GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(!plain.EndDialog(0) && ::IsWindow(plain.m_hWnd));
    CHECK(plain.DestroyWindow() && plain.finalized == 2 && !plain.m_hWnd);
    InitFailureDialog failed;
    CHECK(!failed.Create(nullptr) && ::GetLastError() == ERROR_ACCESS_DENIED && !failed.initialized && failed.finalized == 1 && !failed.m_hWnd);
    CHECK(failed.DoModal(nullptr) == -1 && ::GetLastError() == ERROR_ACCESS_DENIED && !failed.initialized && failed.finalized == 2 && !failed.m_hWnd);
    CloseFailureDialog retry;
    CHECK(retry.Create(nullptr)); retry.fail = true;
    CHECK(!retry.DestroyWindow() && ::GetLastError() == ERROR_ACCESS_DENIED && ::IsWindow(retry.m_hWnd));
    retry.fail = false; CHECK(retry.DestroyWindow() && !retry.m_hWnd);
    CHECK(retry.DoModal(nullptr) == 82 && retry.preserved && !retry.m_hWnd);
    DialogRetirement modeless, modal;
    CHECK(!(new DeletingDialog(modeless, false))->Create(nullptr));
    CHECK(modeless.initialized == 1 && modeless.finalized == 1 && modeless.destroyed == 1);
    CHECK((new DeletingDialog(modal, true))->DoModal(nullptr) == 91);
    CHECK(modal.initialized == 1 && modal.finalized == 1 && modal.destroyed == 1);
    return true;
}
class Source : public IConnectionPointContainer, public IConnectionPoint {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IConnectionPointContainer) *value = static_cast<IConnectionPointContainer*>(this);
        else if (iid == IID_IConnectionPoint) *value = static_cast<IConnectionPoint*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE EnumConnectionPoints(IEnumConnectionPoints**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindConnectionPoint(REFIID iid, IConnectionPoint** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return iid == EventIID ? QueryInterface(IID_IConnectionPoint, reinterpret_cast<void**>(value)) : CONNECT_E_NOCONNECTION;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionInterface(IID* value) override { if (!value) return E_POINTER; *value = EventIID; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetConnectionPointContainer(IConnectionPointContainer** value) override {
        return QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(value));
    }
    HRESULT STDMETHODCALLTYPE Advise(IUnknown* value, DWORD* cookie) override {
        if (!value || !cookie) return E_POINTER;
        if (sink) return CONNECT_E_ADVISELIMIT;
        HRESULT hr = value->QueryInterface(EventIID, reinterpret_cast<void**>(&sink));
        if (SUCCEEDED(hr)) { *cookie = 42; ++advises; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override {
        if (cookie != 42 || !sink) return CONNECT_E_NOCONNECTION;
        sink.Release(); ++unadvises; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections**) override { return E_NOTIMPL; }
    HRESULT Fire(DISPID id, DISPPARAMS& params, UINT* argument = nullptr) {
        ATL::CComPtr<IDispatch> retained = sink;
        return retained ? retained->Invoke(id, IID_NULL, 0, DISPATCH_METHOD, &params, nullptr, nullptr, argument) : CONNECT_E_NOCONNECTION;
    }
    ULONG refs = 1;
    int advises = 0, unadvises = 0;
private:
    ATL::CComPtr<IDispatch> sink;
};
class Sink : public ATL::IDispEventSimpleImpl<7, Sink, &EventIID> {
public:
    inline static ATL::_ATL_FUNC_INFO scalar = {CC_STDCALL, VT_EMPTY, 3, {VT_I4, VT_BOOL, VT_BSTR}};
    inline static ATL::_ATL_FUNC_INFO ref = {CC_STDCALL, VT_EMPTY, 1, {VT_VARIANT | VT_BYREF}};
    inline static ATL::_ATL_FUNC_INFO throwing = {CC_STDCALL, VT_EMPTY, 0, {}};
    inline static ATL::_ATL_FUNC_INFO doubleRef = {CC_STDCALL, VT_EMPTY, 1, {VT_BYREF | VT_R4}};
    BEGIN_SINK_MAP(Sink)
        SINK_ENTRY_INFO(7, EventIID, 1, Scalar, &scalar)
        SINK_ENTRY_INFO(7, EventIID, 2, Ref, &ref)
        SINK_ENTRY_INFO(7, EventIID, 3, Throw, &throwing)
        SINK_ENTRY_INFO(7, EventIID, 4, Double, &doubleRef)
    END_SINK_MAP()
    void __stdcall Scalar(long number, VARIANT_BOOL enabled, BSTR text) {
        good = number == 123 && enabled == VARIANT_TRUE && text && std::wcscmp(text, L"event\u6771\u4eac") == 0;
        ++calls;
    }
    void __stdcall Ref(VARIANT* value) { value->lVal += 1; }
    void __stdcall Throw() { throw std::runtime_error("handler failure"); }
    void __stdcall Double(double*) { ++doubleCalls; }
    int doubleCalls = 0;
    int calls = 0;
    bool good = false;
};
bool events() {
    ATL::CComPtr<Source> source; source.Attach(new Source);
    ATL::CComPtr<Source> other; other.Attach(new Source);
    auto unknown = static_cast<IConnectionPointContainer*>(source.p);
    {
        ATL::CComPtr<IConnectionPoint> secondary;
        CHECK(source->FindConnectionPoint(EventIID, &secondary) == S_OK);
        ATL::CComQIPtr<IUnknown> identity(secondary.p);
        CHECK(identity.p == static_cast<IUnknown*>(unknown));
        {
            ATL::CComQIPtr<IUnknown, &IID_IConnectionPoint> explicitlySelected(unknown);
            CHECK(static_cast<void*>(explicitlySelected.p) == static_cast<void*>(secondary.p));
        }
        const ULONG references = source->refs;
        identity = secondary.p;
        CHECK(identity.p == static_cast<IUnknown*>(unknown) && source->refs == references);
        ATL::CComQIPtr<IUnknown> fromSmartPointer = secondary;
        CHECK(fromSmartPointer.p == identity.p);
        fromSmartPointer = secondary;
        CHECK(fromSmartPointer.p == identity.p && source->refs == references + 1);
    }
    CHECK(source->refs == 1);
    {
        Sink sink;
        CHECK(sink.DispEventAdvise(unknown) == S_OK);
        CHECK(sink.DispEventAdvise(unknown) == CONNECT_E_ADVISELIMIT);
        ATL::CComVariant args[3] = {ATL::CComVariant(L"event\u6771\u4eac"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(123L)};
        args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
        DISPPARAMS params = {args, nullptr, 3, 0};
        CHECK(source->Fire(1, params) == S_OK && sink.good && sink.calls == 1);
        Sink::scalar.pVarTypes[2] = VT_I4;
        CHECK(source->Fire(1, params) == E_INVALIDARG && sink.calls == 1);
        Sink::scalar.pVarTypes[2] = VT_BSTR;
        params.cNamedArgs = 1;
        CHECK(source->Fire(1, params) == DISP_E_NONAMEDARGS);
        params.cNamedArgs = 0;
        CHECK(source->Fire(99, params) == DISP_E_MEMBERNOTFOUND);
        params.cArgs = 2; CHECK(source->Fire(1, params) == DISP_E_BADPARAMCOUNT); params.cArgs = 3;
        args[2] = L"not a number";
        UINT argument = 99;
        CHECK(source->Fire(1, params, &argument) == DISP_E_TYPEMISMATCH && argument == 2 && sink.calls == 1);
        ATL::CComVariant value(40L);
        VARIANTARG reference{}; reference.vt = VT_VARIANT | VT_BYREF; reference.pvarVal = &value;
        DISPPARAMS byref = {&reference, nullptr, 1, 0};
        CHECK(source->Fire(2, byref) == S_OK && value.lVal == 41);
        reference.pvarVal = nullptr;
        CHECK(source->Fire(2, byref) == DISP_E_TYPEMISMATCH);
        DISPPARAMS empty{};
        CHECK(source->Fire(3, empty) == DISP_E_EXCEPTION);
        alignas(double) float number = 2.0f;
        VARIANTARG floating{}; floating.vt = VT_BYREF | VT_R4; floating.pfltVal = &number;
        DISPPARAMS floatingParams = {&floating, nullptr, 1, 0};
        CHECK(source->Fire(4, floatingParams) == E_INVALIDARG && sink.doubleCalls == 0);
        double wide = 2.0;
        Sink::doubleRef.pVarTypes[0] = VT_BYREF | VT_R8;
        floating.vt = VT_BYREF | VT_R8; floating.pdblVal = &wide;
        CHECK(source->Fire(4, floatingParams) == S_OK && sink.doubleCalls == 1);
        CHECK(sink.DispEventUnadvise(static_cast<IConnectionPointContainer*>(other.p)) == CONNECT_E_NOCONNECTION);
        CHECK(sink.DispEventUnadvise(unknown) == S_OK && source->unadvises == 1);
        CHECK(source->Fire(1, params) == CONNECT_E_NOCONNECTION);
        CHECK(sink.DispEventAdvise(unknown) == S_OK);
    }
    CHECK(source->advises == 2 && source->unadvises == 2 && source->refs == 1);
    return true;
}
class BrowserSink : public ATL::CWindow, public ATL::IDispEventImpl<1, BrowserSink, &DIID_DWebBrowserEvents2> {
public:
    explicit BrowserSink(HWND parent) : ATL::CWindow(parent) {}
    BEGIN_SINK_MAP(BrowserSink)
        SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, 259, &BrowserSink::Document)
    END_SINK_MAP()
    void __stdcall Document(IDispatch*, VARIANT* value) { if (value && value->vt == VT_BSTR) ++documents; }
    int documents = 0;
};
class ControlDialog : public ATL::CAxDialogImpl<ControlDialog>,
                      public ATL::IDispEventImpl<701, ControlDialog, &DIID_DWebBrowserEvents2> {
public:
    enum { IDD = 601 };
    bool modal = false, good = true;
    int initialized = 0, finalized = 0, documents = 0;
    LPARAM parameter = 0;
    ATL::CComPtr<IWebBrowser2> control;
    BEGIN_SINK_MAP(ControlDialog)
        SINK_ENTRY_EX(701, DIID_DWebBrowserEvents2, 259, &ControlDialog::Document)
    END_SINK_MAP()
    LRESULT Init(UINT, WPARAM, LPARAM value, BOOL&) {
        ++initialized; parameter = value;
        good = GetDlgControl(701, IID_IWebBrowser2, reinterpret_cast<void**>(&control.p)) == S_OK;
        good = good && ::SetTimer(this->m_hWnd, 1, 4500, nullptr) != 0;
        ::PostMessageW(this->m_hWnd, WM_APP + 81, 0, 0);
        return TRUE;
    }
    void Finish(int result) {
        if (modal) { if (!EndDialog(result)) { good = false; ::EndDialog(this->m_hWnd, -2); } }
        else if (!DestroyWindow()) { good = false; ::DestroyWindow(this->m_hWnd); }
    }
    LRESULT Start(UINT, WPARAM, LPARAM, BOOL&) {
        ATL::CComVariant url(L"about:blank#openatl-dialog"), empty;
        if (!good || control->Navigate2(&url, &empty, &empty, &empty, &empty) != S_OK) { good = false; Finish(-2); }
        return 0;
    }
    LRESULT Timeout(UINT, WPARAM, LPARAM, BOOL&) { good = false; Finish(-2); return 0; }
    LRESULT Destroy(UINT, WPARAM, LPARAM, BOOL&) { control.Release(); return 0; }
    void Document(IDispatch*, VARIANT* value) {
        if (!value || value->vt != VT_BSTR) return;
        ++documents; ::KillTimer(this->m_hWnd, 1);
        if (modal) Finish(73);
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(ControlDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
        MESSAGE_HANDLER(WM_APP + 81, Start)
        MESSAGE_HANDLER(WM_TIMER, Timeout)
        MESSAGE_HANDLER(WM_DESTROY, Destroy)
    END_MSG_MAP()
};
class MissingControlDialog : public ATL::CAxDialogImpl<MissingControlDialog> {
public:
    enum { IDD = 603 };
    int initialized = 0;
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) { ++initialized; return TRUE; }
    BEGIN_MSG_MAP(MissingControlDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool controlDialogs(ATL::CAtlModule& module) {
    ControlDialog modeless;
    CHECK(modeless.Create(nullptr, 312) && modeless.initialized == 1 && modeless.parameter == 312);
    CHECK(modeless.AdviseSinkMap(true) == CONNECT_E_ADVISELIMIT);
    const ULONGLONG deadline = ::GetTickCount64() + 6000;
    while (!modeless.documents && modeless.m_hWnd && ::GetTickCount64() < deadline) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (!modeless.IsDialogMessage(&message)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        }
        ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    CHECK(modeless.good && modeless.documents && modeless.DestroyWindow() && modeless.finalized == 1 && !modeless.m_hWnd);
    CHECK(modeless.AdviseSinkMap(false) == S_OK && module.GetLockCount() == 0);
    ControlDialog modal; modal.modal = true;
    CHECK(modal.DoModal(nullptr, 728) == 73 && modal.good && modal.documents && modal.initialized == 1 && modal.parameter == 728 && modal.finalized == 1 && !modal.m_hWnd);
    CHECK(module.GetLockCount() == 0);
    MissingControlDialog missing;
    CHECK(!missing.Create(nullptr) && !missing.initialized && !missing.m_hWnd && module.GetLockCount() == 0);
    CHECK(missing.DoModal(nullptr) == -1 && !missing.initialized && !missing.m_hWnd && module.GetLockCount() == 0);
    return true;
}
void BrowserPhase(int cycle, const char* phase) noexcept {
    // Keep real IWebBrowser2 admission and its original 25s CTest budget.
    // If a hosted Windows runner stalls inside a synchronous OLE call, log
    // the exact phase before it rather than replacing the real control.
    std::fprintf(stderr, "ACTIVEX_BROWSER cycle=%d phase=%s ticks=%llu\n",
                 cycle, phase, static_cast<unsigned long long>(::GetTickCount64()));
    std::fflush(stderr);
}
bool browser() {
    BrowserPhase(-1, "start");
    ATL::CAtlModule module;
    for (int cycle = 0; cycle < 3; ++cycle) {
        BrowserPhase(cycle, "begin");
        CHECK(module.GetLockCount() == 0);
        struct Parent {
            HWND window = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 320, 200,
                nullptr, nullptr, ATL::_AtlBaseModule.GetModuleInstance(), nullptr);
            ~Parent() { if (window) ::DestroyWindow(window); }
        } parent;
        CHECK(parent.window);
        ATL::CWindow container(parent.window);
        ATL::CAxWindow2 window;
        RECT rect{0, 0, 320, 200};
        CHECK(window.Create(parent.window, &rect, nullptr, WS_CHILD, 0, 1U) != nullptr);
        BrowserPhase(cycle, "window-created");
        ATL::CComPtr<IUnknown> raw;
        CHECK(ATL::AtlAxGetControl(window, &raw) == E_NOINTERFACE && !raw);
        BrowserPhase(cycle, "creating-webbrowser-control");
        CHECK(window.CreateControlLic(L"about:blank") == S_OK);
        BrowserPhase(cycle, "webbrowser-created");
        CHECK(module.GetLockCount() == 1);
        ATL::CComPtr<IWebBrowser2> browser;
        CHECK(container.GetDlgControl(1, IID_IWebBrowser2, reinterpret_cast<void**>(&browser.p)) == S_OK);
        void* missing = reinterpret_cast<void*>(1);
        CHECK(container.GetDlgControl(99, IID_IUnknown, &missing) == E_FAIL && !missing);
        CHECK(container.GetDlgControl(1, EventIID, &missing) == E_NOINTERFACE && !missing);
        CHECK(container.GetDlgControl(1, IID_IUnknown, nullptr) == E_POINTER);
        ATL::CComPtr<IAxWinAmbientDispatch> ambient;
        CHECK(window.QueryHost(&ambient) == S_OK);
        CHECK(ambient->put_DocHostFlags(DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui;
        CHECK(window.QueryHost(&ui) == S_OK);
        DOCHOSTUIINFO info{}; info.cbSize = sizeof(info);
        CHECK(ui->GetHostInfo(&info) == S_OK && (info.dwFlags & DOCHOSTUIFLAG_THEME));
        BrowserSink sink(parent.window);
        CHECK(ATL::AtlAdviseSinkMap(&sink, true) == S_OK);
        ATL::CComVariant url(L"about:blank#openatl"), empty;
        BrowserPhase(cycle, "navigating");
        CHECK(browser->Navigate2(&url, &empty, &empty, &empty, &empty) == S_OK);
        BrowserPhase(cycle, "awaiting-document-complete");
        const ULONGLONG deadline = ::GetTickCount64() + 6000;
        while (!sink.documents && ::GetTickCount64() < deadline) {
            MSG msg{};
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessageW(&msg); }
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
        CHECK(sink.documents > 0);
        BrowserPhase(cycle, "document-complete");
        ATL::CComPtr<IDispatch> document;
        CHECK(browser->get_Document(&document) == S_OK);
        ATL::CComQIPtr<IHTMLDocument2> html = document;
        CHECK(html.p != nullptr);
        html.Release(); html = document;
        CHECK(html.p != nullptr);
        CHECK(ATL::AtlAdviseSinkMap(&sink, false) == S_OK);
        ATL::CComPtr<IOleObject> object;
        CHECK(browser->QueryInterface(IID_IOleObject, reinterpret_cast<void**>(&object)) == S_OK);
        ATL::CComPtr<IAxWinHostWindow> host;
        CHECK(window.QueryHost(&host) == S_OK);
        HRESULT foreign = S_OK;
        HWND handle = window;
        std::thread reader([&] { ATL::CComPtr<IUnknown> value; foreign = ATL::AtlAxGetControl(handle, &value); });
        reader.join(); CHECK(foreign == RPC_E_WRONG_THREAD);
        BrowserPhase(cycle, "destroying-window");
        CHECK(window.DestroyWindow()); window.m_hWnd = nullptr;
        missing = reinterpret_cast<void*>(1);
        CHECK(container.GetDlgControl(1, IID_IUnknown, &missing) == E_FAIL && !missing);
        BrowserPhase(cycle, "window-destroyed");
        ATL::CComPtr<IOleClientSite> site;
        CHECK(object->GetClientSite(&site) == S_OK && !site);
        raw.Release();
        CHECK(host->QueryControl(IID_IUnknown, reinterpret_cast<void**>(&raw)) == CO_E_OBJNOTCONNECTED && !raw);
        CHECK(module.GetLockCount() == 1);
        BrowserPhase(cycle, "end");
    }
    CHECK(module.GetLockCount() == 0);
    BrowserPhase(-1, "complete");
    return controlDialogs(module);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    bool (*test)() = nullptr;
    if (std::strcmp(argv[1], "events") == 0) test = events;
    else if (std::strcmp(argv[1], "browser") == 0) test = browser;
    else if (std::strcmp(argv[1], "lifecycle") == 0) test = RunHostLifecycleTests;
    else return 2;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED || !ATL::AtlAxWinInit() ||
        ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    if (!plainDialogs() || ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    HRESULT hr = ::OleInitialize(nullptr); if (FAILED(hr)) return 1;
    bool ok = true;
#if defined(OPENATL_ORIGINAL_COMMAP_CLASSES)
    if (test == events) ok = RunMicrosoftCommapClassTests() && RunMicrosoftSafeArrayTests();
#endif
    if (ok) ok = test();
    ::OleUninitialize();
    ok = ok && ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
    if (ok) std::puts("ACTIVEX_OK");
    return ok ? 0 : 1;
}
