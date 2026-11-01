// Registered COM factories and actual HWNDs exercise host transaction ordering
// and reentrant retirement without relying on a particular installed control.
#include <atlhost.h>
#include <cstdio>
#include <new>
#include <cstring>
#include <thread>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "HOST_LIFECYCLE_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
template<class T> class OwnedWindow : public T {
public:
    ~OwnedWindow() { if (::IsWindow(this->m_hWnd)) ::DestroyWindow(this->m_hWnd); }
};
struct UIObjectState { ULONG refs = 1; int destroyed = 0; };
class UIObject : public IDispatch, public IDropTarget, public IDataObject,
                 public IOleCommandTarget, public IOleInPlaceActiveObject {
public:
    explicit UIObject(UIObjectState* state = nullptr) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch) *value = static_cast<IDispatch*>(this);
        else if (iid == IID_IDropTarget) *value = static_cast<IDropTarget*>(this);
        else if (iid == IID_IDataObject) *value = static_cast<IDataObject*>(this);
        else if (iid == IID_IOleCommandTarget) *value = static_cast<IOleCommandTarget*>(this);
        else if (iid == IID_IOleInPlaceActiveObject) *value = static_cast<IOleInPlaceActiveObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { ++refs_; if (state_) state_->refs = refs_; return refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (state_) state_->refs = n; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count, LCID, DISPID* ids) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !ids) return E_POINTER;
        if (count != 1 || !names[0] || ::CompareStringOrdinal(names[0], -1, L"OpenWTLHostToken", -1, TRUE) != CSTR_EQUAL) return DISP_E_UNKNOWNNAME;
        ids[0] = 1234; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags, DISPPARAMS* args, VARIANT* result, EXCEPINFO*, UINT*) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (id != 1234 || flags != DISPATCH_PROPERTYGET) return DISP_E_MEMBERNOTFOUND;
        if (!args || !result) return E_POINTER;
        if (args->cArgs || args->cNamedArgs) return DISP_E_BADPARAMCOUNT;
        ::VariantInit(result); result->vt = VT_I4; result->lVal = 0x4f57544c; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryStatus(const GUID*, ULONG, OLECMD[], OLECMDTEXT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Exec(const GUID*, DWORD, DWORD, VARIANT*, VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* value) override { if (!value) return E_POINTER; *value = nullptr; return E_FAIL; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
private:
    ~UIObject() { if (state_) ++state_->destroyed; }
    ULONG refs_ = 1;
    UIObjectState* state_;
};
struct UIState {
    int calls[15]{}, destroyed = 0, action = 0;
    bool good = true, inside = false, diedInside = false, omitResult = false, nullResult = false;
    bool wrongInterface = false, nullGroup = false, browser = false, destroyOnRetain = false;
    HRESULT transport = S_OK, result = S_FALSE;
    IAxWinHostWindow* host = nullptr;
    IDocHostUIHandlerDispatch* replacement = nullptr;
    UIObject* object = nullptr;
    IUnknown* frame = nullptr;
    HWND window = nullptr;
    MSG message{};
};
class UIHandler : public IDocHostUIHandlerDispatch {
public:
    explicit UIHandler(UIState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch && iid != __uuidof(IDocHostUIHandlerDispatch)) return E_NOINTERFACE;
        *value = static_cast<IDocHostUIHandlerDispatch*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        ++refs_;
        if (state_.destroyOnRetain) { state_.destroyOnRetain = false; ::DestroyWindow(state_.window); }
        return refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD id, DWORD x, DWORD y, IUnknown* command, IDispatch* reserved, HRESULT* result) override {
        ++state_.calls[0];
        state_.good = state_.browser || (id == CONTEXT_MENU_TEXTSELECT && x == static_cast<DWORD>(-19) && y == 27 &&
            command == static_cast<IDispatch*>(state_.object) && reserved == static_cast<IDispatch*>(state_.object) && state_.good);
        return Result(result);
    }
    HRESULT STDMETHODCALLTYPE GetHostInfo(DWORD* flags, DWORD* click) override {
        ++state_.calls[1];
        if (!state_.browser) {
            state_.good = state_.good && *flags == DOCHOSTUIFLAG_NO3DBORDER && *click == DOCHOSTUIDBLCLK_DEFAULT;
            *flags |= docHostUIFlagDIALOG; *click = docHostUIDblClkSHOWCODE;
        }
        return state_.transport;
    }
    HRESULT STDMETHODCALLTYPE ShowUI(DWORD id, IUnknown* active, IUnknown* target, IUnknown* frame, IUnknown* document, HRESULT* result) override {
        ++state_.calls[2];
        if (!state_.browser) state_.good = state_.good && id == 77 &&
            active == static_cast<IOleInPlaceActiveObject*>(state_.object) && target == static_cast<IOleCommandTarget*>(state_.object) &&
            frame == state_.frame && document == state_.frame;
        return Result(result);
    }
    HRESULT STDMETHODCALLTYPE HideUI() override {
        auto& state = state_; ++state.calls[3]; state.inside = true;
        const int action = state.action; state.action = 0;
        if (action == 1 || action == 2) state.good = state.host->SetExternalUIHandler(action == 1 ? nullptr : state.replacement) == S_OK && state.good;
        else if (action == 3) state.good = ::DestroyWindow(state.window) && state.good;
        state.inside = false; return state.transport;
    }
    HRESULT STDMETHODCALLTYPE UpdateUI() override { ++state_.calls[4]; return state_.transport; }
    HRESULT STDMETHODCALLTYPE EnableModeless(VARIANT_BOOL enable) override { ++state_.calls[5]; state_.good = state_.good && (state_.browser || enable == VARIANT_TRUE); return state_.transport; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(VARIANT_BOOL active) override { ++state_.calls[6]; state_.good = state_.good && (state_.browser || active == VARIANT_TRUE); return state_.transport; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(VARIANT_BOOL active) override { ++state_.calls[7]; state_.good = state_.good && (state_.browser || active == VARIANT_FALSE); return state_.transport; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(long left, long top, long right, long bottom, IUnknown* window, VARIANT_BOOL frame) override {
        ++state_.calls[8]; state_.good = state_.good && (state_.browser ||
            (left == -2 && top == 3 && right == 19 && bottom == 29 && window == state_.frame && frame == VARIANT_TRUE));
        return state_.transport;
    }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(DWORD_PTR window, DWORD message, DWORD_PTR w, DWORD_PTR l, BSTR group, DWORD id, HRESULT* result) override {
        ++state_.calls[9];
        if (!state_.browser) state_.good = state_.good && window == reinterpret_cast<DWORD_PTR>(state_.message.hwnd) &&
            message == state_.message.message && w == state_.message.wParam && l == static_cast<DWORD_PTR>(state_.message.lParam) && id == 99 &&
            (state_.nullGroup ? !group : group && !::wcscmp(group, L"{00020400-0000-0000-C000-000000000046}"));
        return Result(result);
    }
    HRESULT STDMETHODCALLTYPE GetOptionKeyPath(BSTR* value, DWORD flags) override {
        ++state_.calls[10]; if (!state_.browser) state_.good = state_.good && flags == 123;
        *value = state_.nullResult || state_.browser ? nullptr : ::SysAllocStringLen(L"key\0tail", 8);
        return state_.browser ? S_FALSE : state_.transport;
    }
    HRESULT STDMETHODCALLTYPE GetDropTarget(IUnknown* original, IUnknown** output) override {
        ++state_.calls[11]; if (!state_.browser) state_.good = state_.good && original == static_cast<IDropTarget*>(state_.object);
        return Object(output);
    }
    HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** output) override {
        ++state_.calls[12]; *output = state_.nullResult ? nullptr : static_cast<IDispatch*>(state_.object);
        if (*output) (*output)->AddRef(); return state_.transport;
    }
    HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD flags, BSTR input, BSTR* output) override {
        ++state_.calls[13]; if (!state_.browser) state_.good = state_.good && flags == 17 && input && !::wcscmp(input, L"about:blank#\u6771");
        *output = state_.nullResult || state_.browser ? nullptr : ::SysAllocString(L"about:blank#\u4eac");
        return state_.browser ? S_FALSE : state_.transport;
    }
    HRESULT STDMETHODCALLTYPE FilterDataObject(IUnknown* original, IUnknown** output) override {
        ++state_.calls[14]; if (!state_.browser) state_.good = state_.good && original == static_cast<IDataObject*>(state_.object);
        return Object(output);
    }
private:
    HRESULT Result(HRESULT* result) { if (!state_.omitResult) *result = state_.result; return state_.transport; }
    HRESULT Object(IUnknown** output) {
        *output = state_.nullResult || state_.browser ? nullptr : state_.wrongInterface ?
            static_cast<IDocHostUIHandlerDispatch*>(this) : static_cast<IDispatch*>(state_.object);
        if (*output) (*output)->AddRef(); return state_.browser ? E_NOTIMPL : state_.transport;
    }
    ~UIHandler() { ++state_.destroyed; state_.diedInside = state_.inside; }
    ULONG refs_ = 1;
    UIState& state_;
};
bool uiHandlers() {
    ATL::CAtlModule module;
    {
        UIState state; UIObjectState objects;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IAxWinHostWindow> host; REQUIRE(window.QueryHost(&host) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui; REQUIRE(window.QueryHost(&ui) == S_OK);
        ATL::CComPtr<IAxWinAmbientDispatch> ambient; REQUIRE(window.QueryHost(&ambient) == S_OK);
        ATL::CComPtr<IOleInPlaceFrame> frame; REQUIRE(window.QueryHost(&frame) == S_OK);
        state.frame = frame; state.host = host; state.window = window;
        ATL::CComPtr<UIObject> object; object.Attach(new UIObject(&objects)); state.object = object;
        REQUIRE(window.SetExternalDispatch(object) == S_OK && window.SetExternalDispatch(object) == S_OK && objects.refs == 2);
        ATL::CComPtr<IDispatch> external;
        REQUIRE(ui->GetExternal(&external) == S_OK && external.p == static_cast<IDispatch*>(object.p)); external.Release();
        ATL::CComPtr<IDocHostUIHandlerDispatch> handler; handler.Attach(new UIHandler(state));
        ATL::CComPtr<IDocHostUIHandler> unsupported;
        REQUIRE(handler->QueryInterface(IID_IDocHostUIHandler, reinterpret_cast<void**>(&unsupported)) == E_NOINTERFACE && !unsupported);
        REQUIRE(window.SetExternalUIHandler(handler) == S_OK);
        REQUIRE(ambient->put_DocHostFlags(DOCHOSTUIFLAG_NO3DBORDER) == S_OK);
        DOCHOSTUIINFO info{}; REQUIRE(ui->GetHostInfo(&info) == S_OK && info.cbSize == sizeof(info) &&
            info.dwFlags == (DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_DIALOG) && info.dwDoubleClick == DOCHOSTUIDBLCLK_SHOWCODE && !info.pchHostCss && !info.pchHostNS);
        POINT point{-19,27}; REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == S_FALSE);
        state.result = S_OK; REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == S_OK);
        REQUIRE(ui->ShowUI(77, object, object, frame, frame) == S_OK);
        REQUIRE(ui->HideUI() == S_OK && ui->UpdateUI() == S_OK && ui->EnableModeless(42) == S_OK &&
            ui->OnDocWindowActivate(2) == S_OK && ui->OnFrameWindowActivate(FALSE) == S_OK);
        RECT border{-2,3,19,29}; REQUIRE(ui->ResizeBorder(&border, frame, 2) == S_OK);
        state.message.hwnd = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x12345678abcdef12ULL));
        state.message.message = WM_KEYDOWN; state.message.wParam = static_cast<WPARAM>(0xfedcba9876543210ULL);
        state.message.lParam = static_cast<LPARAM>(-123456789);
        REQUIRE(ui->TranslateAccelerator(&state.message, &IID_IDispatch, 99) == S_OK);
        state.nullGroup = true; state.omitResult = true;
        REQUIRE(ui->TranslateAccelerator(&state.message, nullptr, 99) == S_FALSE);
        REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == S_FALSE);
        REQUIRE(ui->ShowUI(77, object, object, frame, frame) == S_FALSE); state.omitResult = false;
        LPOLESTR text = nullptr;
        REQUIRE(ui->GetOptionKeyPath(&text, 123) == S_OK && text && !std::memcmp(text, L"key\0tail", 9 * sizeof(WCHAR))); ::CoTaskMemFree(text);
        WCHAR url[] = L"about:blank#\u6771";
        REQUIRE(ui->TranslateUrl(17, url, &text) == S_OK && text && !::wcscmp(text, L"about:blank#\u4eac")); ::CoTaskMemFree(text);
        ATL::CComPtr<IDropTarget> target; REQUIRE(ui->GetDropTarget(object, &target) == S_OK && target.p == static_cast<IDropTarget*>(object.p)); target.Release();
        ATL::CComPtr<IDataObject> data; REQUIRE(ui->FilterDataObject(object, &data) == S_OK && data.p == static_cast<IDataObject*>(object.p)); data.Release();
        REQUIRE(ui->GetExternal(&external) == S_OK && external.p == static_cast<IDispatch*>(object.p)); external.Release();
        for (int calls : state.calls) REQUIRE(calls > 0);
        REQUIRE(objects.refs == 2 && state.good);
        state.wrongInterface = true;
        REQUIRE(ui->GetDropTarget(object, &target) == E_NOINTERFACE && !target);
        REQUIRE(ui->FilterDataObject(object, &data) == E_NOINTERFACE && !data); state.wrongInterface = false;
        state.transport = E_ACCESSDENIED;
        REQUIRE(ui->ShowContextMenu(CONTEXT_MENU_TEXTSELECT, &point, static_cast<IDispatch*>(object.p), object) == E_ACCESSDENIED);
        REQUIRE(ui->ShowUI(77, object, object, frame, frame) == E_ACCESSDENIED &&
            ui->TranslateAccelerator(&state.message, nullptr, 99) == E_ACCESSDENIED);
        REQUIRE(ui->GetOptionKeyPath(&text, 123) == E_ACCESSDENIED && !text);
        REQUIRE(ui->TranslateUrl(17, url, &text) == E_ACCESSDENIED && !text);
        REQUIRE(ui->GetDropTarget(object, &target) == E_ACCESSDENIED && !target);
        REQUIRE(ui->FilterDataObject(object, &data) == E_ACCESSDENIED && !data);
        REQUIRE(ui->GetExternal(&external) == E_ACCESSDENIED && !external && objects.refs == 2);
        state.transport = S_FALSE; state.nullResult = true;
        REQUIRE(ui->GetOptionKeyPath(&text, 123) == S_FALSE && !text && ui->TranslateUrl(17, url, &text) == S_FALSE && !text);
        REQUIRE(ui->GetDropTarget(object, &target) == S_FALSE && !target && ui->FilterDataObject(object, &data) == S_FALSE && !data);
        REQUIRE(ui->GetExternal(&external) == S_FALSE && !external);
        REQUIRE(ui->GetHostInfo(nullptr) == E_POINTER && ui->ShowContextMenu(0, nullptr, nullptr, nullptr) == E_POINTER &&
            ui->ResizeBorder(nullptr, nullptr, FALSE) == E_POINTER && ui->TranslateAccelerator(nullptr, nullptr, 0) == E_POINTER);
        REQUIRE(ui->GetOptionKeyPath(nullptr, 0) == E_POINTER && ui->TranslateUrl(0, nullptr, nullptr) == E_POINTER &&
            ui->GetDropTarget(nullptr, nullptr) == E_POINTER && ui->FilterDataObject(nullptr, nullptr) == E_POINTER && ui->GetExternal(nullptr) == E_POINTER);
        HRESULT foreignUI = S_OK, foreignObject = S_OK;
        std::thread foreign([&] { foreignUI = host->SetExternalUIHandler(handler); foreignObject = host->SetExternalDispatch(object); }); foreign.join();
        REQUIRE(foreignUI == RPC_E_WRONG_THREAD && foreignObject == RPC_E_WRONG_THREAD);
        REQUIRE(window.SetExternalUIHandler(nullptr) == S_OK);
        REQUIRE(ui->GetExternal(&external) == S_OK && external.p == static_cast<IDispatch*>(object.p)); external.Release();
        REQUIRE(window.SetExternalDispatch(nullptr) == S_OK);
        handler.Release(); REQUIRE(state.destroyed == 1 && state.good && !state.diedInside && objects.refs == 1);
        object.Release(); REQUIRE(objects.destroyed == 1 && objects.refs == 0);
    }
    REQUIRE(module.GetLockCount() == 0);
    for (int action = 1; action <= 4; ++action) {
        UIState state, replacement;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IAxWinHostWindow> host; REQUIRE(window.QueryHost(&host) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui; REQUIRE(window.QueryHost(&ui) == S_OK);
        state.host = host; state.window = window; state.action = action;
        ATL::CComPtr<IDocHostUIHandlerDispatch> next; next.Attach(new UIHandler(replacement)); state.replacement = next;
        auto handler = new UIHandler(state);
        if (action == 4) state.destroyOnRetain = true;
        REQUIRE(window.SetExternalUIHandler(handler) == (action == 4 ? CO_E_OBJNOTCONNECTED : S_OK)); handler->Release();
        if (action != 4) REQUIRE(ui->HideUI() == S_OK);
        if (action == 3) REQUIRE(state.destroyed == 0 && host->SetExternalUIHandler(next) == CO_E_OBJNOTCONNECTED);
        REQUIRE(host->SetExternalUIHandler(nullptr) == S_OK);
        REQUIRE(state.good && state.destroyed == 1 && !state.diedInside);
        next.Release(); REQUIRE(replacement.destroyed == 1);
        if (action >= 3) { REQUIRE(!::IsWindow(window)); window.m_hWnd = nullptr; }
    }
    REQUIRE(module.GetLockCount() == 0);
    return true;
}
struct ControlState {
    CLSID classId{};
    bool siteFirst = true, initInterface = true, failInit = false, destroyDuringInit = false;
    bool siteOrder = true, licenseSeen = false;
    bool storageInterface = true, storageReadOnly = true, rejectLicense = false, destroyDuringStorageQuery = false;
    bool discoverWindow = false, destroyDialogDuringInit = false, sendInitDuringLoad = false;
    ULONG streamBytes = 4;
    int failInitializationAt = 0;
    HWND window = nullptr;
    int initialized = 0, loaded = 0, activated = 0, detached = 0, closed = 0, destroyed = 0;
    int storageLoaded = 0, storageReleased = 0, lateStorageReads = 0;
};
class Control : public IOleObject, public IPersistStreamInit, public IPersistStream, public IPersistStorage, public IRunnableObject {
public:
    explicit Control(ControlState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IOleObject) *value = static_cast<IOleObject*>(this);
        else if (iid == IID_IPersistStreamInit && state_.initInterface) *value = static_cast<IPersistStreamInit*>(this);
        else if (iid == IID_IPersist || iid == IID_IPersistStream) *value = static_cast<IPersistStream*>(this);
        else if (iid == IID_IPersistStorage && state_.storageInterface) {
            *value = static_cast<IPersistStorage*>(this);
            if (state_.destroyDuringStorageQuery) ::DestroyWindow(state_.window);
        }
        else if (iid == IID_IRunnableObject) *value = static_cast<IRunnableObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetClientSite(IOleClientSite* site) override {
        if (!site && site_) ++state_.detached;
        if (site && state_.discoverWindow) {
            ATL::CComPtr<IOleWindow> window;
            HRESULT hr = site->QueryInterface(IID_IOleWindow, reinterpret_cast<void**>(&window));
            if (FAILED(hr)) return hr;
            hr = window->GetWindow(&state_.window);
            if (FAILED(hr)) return hr;
        }
        site_ = site; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetClientSite(IOleClientSite** site) override { return site_.CopyTo(site); }
    HRESULT STDMETHODCALLTYPE SetHostNames(LPCOLESTR, LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close(DWORD) override {
        ++state_.closed;
        if (storage_) { storage_.Release(); ++state_.storageReleased; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMoniker(DWORD, IMoniker*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InitFromData(IDataObject*, BOOL, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetClipboardData(DWORD, IDataObject**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DoVerb(LONG, LPMSG, IOleClientSite* site, LONG, HWND parent, LPCRECT) override {
        if (parent != state_.window || site != site_.p) return E_FAIL;
        ++state_.activated; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumVerbs(IEnumOLEVERB**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Update() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsUpToDate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetUserClassID(CLSID* id) override { return GetClassID(id); }
    HRESULT STDMETHODCALLTYPE GetUserType(DWORD, LPOLESTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetExtent(DWORD, SIZEL*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetExtent(DWORD, SIZEL* size) override {
        if (!storage_) return E_NOTIMPL;
        if (!size) return E_POINTER;
        DWORD marker = 0;
        HRESULT hr = ReadStorage(storage_, marker);
        if (SUCCEEDED(hr)) { size->cx = static_cast<LONG>(marker); size->cy = 4; ++state_.lateStorageReads; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Advise(IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD) override { return OLE_E_NOCONNECTION; }
    HRESULT STDMETHODCALLTYPE EnumAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMiscStatus(DWORD, DWORD* misc) override { if (!misc) return E_POINTER; *misc = state_.siteFirst ? OLEMISC_SETCLIENTSITEFIRST : 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetColorScheme(LOGPALETTE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override { if (!id) return E_POINTER; *id = state_.classId; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsDirty() override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Load(IStream* stream) override {
        ULONG read = 0; DWORD marker = 0;
        if (!stream || stream->Read(&marker, sizeof(marker), &read) != S_OK || read != sizeof(marker) || marker != 42) return E_FAIL;
        STATSTG stat{};
        if (stream->Stat(&stat, STATFLAG_NONAME) != S_OK || stat.cbSize.QuadPart != state_.streamBytes) return E_FAIL;
        if (state_.streamBytes == 5) {
            BYTE tail = 0;
            if (stream->Read(&tail, 1, &read) != S_OK || read != 1 || tail != 0xA5) return E_FAIL;
        }
        BYTE end = 0;
        // Native HGLOBAL streams return S_OK with zero bytes at EOF.
        if (FAILED(stream->Read(&end, 1, &read)) || read) return E_FAIL;
        ++state_.loaded; return Initialize();
    }
    HRESULT STDMETHODCALLTYPE Save(IStream*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* size) override { if (!size) return E_POINTER; size->QuadPart = 4; return S_OK; }
    HRESULT STDMETHODCALLTYPE InitNew() override { ++state_.initialized; return Initialize(); }
    HRESULT STDMETHODCALLTYPE Load(IStorage* storage) override {
        if (!storage) return E_POINTER;
        if (storage_) return CO_E_ALREADYINITIALIZED;
        DWORD marker = 0;
        HRESULT hr = ReadStorage(storage, marker);
        if (FAILED(hr)) return hr;
        STATSTG stat{};
        if (storage->Stat(&stat, STATFLAG_NONAME) != S_OK || (stat.grfMode & 3) != STGM_READ) return E_FAIL;
        ATL::CComPtr<IStream> forbidden;
        state_.storageReadOnly = state_.storageReadOnly && FAILED(storage->CreateStream(L"Forbidden", STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &forbidden));
        if (!state_.storageReadOnly || forbidden) return E_FAIL;
        storage_ = storage; ++state_.storageLoaded;
        return Initialize();
    }
    HRESULT STDMETHODCALLTYPE InitNew(IStorage*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Save(IStorage*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SaveCompleted(IStorage*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE HandsOffStorage() override {
        if (storage_) { storage_.Release(); ++state_.storageReleased; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRunningClass(CLSID* id) override { return GetClassID(id); }
    HRESULT STDMETHODCALLTYPE Run(IBindCtx*) override { return S_OK; }
    BOOL STDMETHODCALLTYPE IsRunning() override { return TRUE; }
    HRESULT STDMETHODCALLTYPE LockRunning(BOOL, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetContainedObject(BOOL) override { return S_OK; }
private:
    static HRESULT ReadStorage(IStorage* storage, DWORD& marker) {
        ATL::CComPtr<IStream> stream;
        HRESULT hr = storage->OpenStream(L"Marker", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream);
        if (FAILED(hr)) return hr;
        ULONG read = 0;
        hr = stream->Read(&marker, sizeof(marker), &read);
        return SUCCEEDED(hr) && read == sizeof(marker) && marker == 42 ? S_OK : E_FAIL;
    }
    HRESULT Initialize() {
        state_.siteOrder = state_.siteOrder && (static_cast<bool>(site_.p) == state_.siteFirst);
        if (state_.destroyDuringInit) ::DestroyWindow(state_.window);
        if (state_.destroyDialogDuringInit) ::DestroyWindow(::GetParent(state_.window));
        if (state_.sendInitDuringLoad) ::SendMessageW(::GetParent(state_.window), WM_INITDIALOG, 0, 0);
        return state_.failInit || !state_.siteOrder || (state_.failInitializationAt &&
            state_.initialized + state_.loaded + state_.storageLoaded == state_.failInitializationAt) ? E_FAIL : S_OK;
    }
    ~Control() { ++state_.destroyed; }
    ULONG refs_ = 1;
    ControlState& state_;
    ATL::CComPtr<IOleClientSite> site_;
    ATL::CComPtr<IStorage> storage_;
};
class Factory : public IClassFactory2 {
public:
    explicit Factory(ControlState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory && iid != IID_IClassFactory2) return E_NOINTERFACE;
        *value = static_cast<IClassFactory2*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto object = new (std::nothrow) Control(state_); if (!object) return E_OUTOFMEMORY;
        HRESULT hr = object->QueryInterface(iid, value); object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLicInfo(LICINFO* info) override {
        if (!info) return E_POINTER; *info = {}; info->cbLicInfo = sizeof(*info); info->fRuntimeKeyAvail = TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLicKey(DWORD, BSTR* key) override {
        if (!key) return E_POINTER; *key = ::SysAllocString(L"OpenATL-test-license"); return *key ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateInstanceLic(IUnknown* outer, IUnknown*, REFIID iid, BSTR key, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (state_.rejectLicense || !key || ::SysStringLen(key) != 20 || std::memcmp(key, L"OpenATL-test-license", 20 * sizeof(WCHAR))) return CLASS_E_NOTLICENSED;
        state_.licenseSeen = true; return CreateInstance(outer, iid, value);
    }
private:
    ~Factory() = default;
    ULONG refs_ = 1;
    ControlState& state_;
};
struct Registration {
    DWORD cookie = 0;
    ~Registration() { if (cookie) ::CoRevokeClassObject(cookie); }
};
bool creation() {
    ControlState state;
    CLSID classId{}; REQUIRE(::CoCreateGuid(&classId) == S_OK);
    wchar_t className[40]{}; REQUIRE(::StringFromGUID2(classId, className, 40) > 0);
    state.classId = classId;
    ATL::CComPtr<IClassFactory2> factory; factory.Attach(new Factory(state));
    Registration registration;
    REQUIRE(::CoRegisterClassObject(classId, factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    for (int mode = 0; mode < 6; ++mode) {
        state = ControlState{};
        state.classId = classId;
        state.siteFirst = mode != 1;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.initInterface = mode != 5;
        OwnedWindow<ATL::CAxWindow2> window;
        RECT rect{0,0,100,100};
        REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        state.window = window;
        ATL::CComPtr<IStream> stream;
        if (mode == 4 || mode == 5) {
            REQUIRE(::CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK);
            DWORD marker = 42; ULONG written = 0;
            REQUIRE(stream->Write(&marker, sizeof(marker), &written) == S_OK && written == sizeof(marker));
            LARGE_INTEGER start{}; REQUIRE(stream->Seek(start, STREAM_SEEK_SET, nullptr) == S_OK);
        }
        ATL::CComBSTR license(mode == 4 ? L"OpenATL-test-license" : nullptr);
        HRESULT hr = window.CreateControlLic(className, stream, nullptr, license);
        if (mode == 2 || mode == 3) {
            REQUIRE(FAILED(hr) && state.activated == 0);
            REQUIRE(state.detached == 1 && state.closed == 1 && state.destroyed == 1);
        } else {
            REQUIRE(hr == S_OK && state.siteOrder && state.activated == 1);
            REQUIRE((mode >= 4 ? state.loaded : state.initialized) == 1);
            if (mode == 4) REQUIRE(state.licenseSeen);
        }
        if (::IsWindow(window)) REQUIRE(window.DestroyWindow());
        window.m_hWnd = nullptr;
        REQUIRE(state.destroyed == 1);
    }
    return true;
}
class ResourceDialog : public ATL::CAxDialogImpl<ResourceDialog> {
public:
    enum { IDD = 610 };
    WORD resource = IDD;
    ControlState& state;
    bool modal = false, closeInInit = false, good = true, attached = false;
    int initialized = 0, finalized = 0;
    int expectedNew = 0, expectedLoads = 1, expectedActivations = 1, expectedStorages = 0;
    explicit ResourceDialog(ControlState& value) : state(value) {}
    DLGPROC GetDialogProc() override { return Trace; }
    static INT_PTR CALLBACK Trace(HWND context, UINT message, WPARAM w, LPARAM l) {
        auto self = static_cast<ResourceDialog*>(reinterpret_cast<ATL::CDialogImplBaseT<ATL::CWindow>*>(context));
        self->attached = true;
        return ATL::CAxDialogImpl<ResourceDialog>::DialogProc(context, message, w, l);
    }
    int GetIDD() const { return resource; }
    LRESULT Init(UINT, WPARAM, LPARAM parameter, BOOL&) {
        ++initialized;
        ATL::CComPtr<IOleObject> control;
        good = parameter == 1773 && state.loaded == expectedLoads && state.initialized == expectedNew && state.activated == expectedActivations &&
            GetDlgControl(711, IID_IOleObject, reinterpret_cast<void**>(&control)) == S_OK && ::GetDlgItem(m_hWnd, 714);
        good = good && state.storageLoaded == expectedStorages;
        if (expectedStorages) {
            SIZEL extent{};
            good = good && control->GetExtent(DVASPECT_CONTENT, &extent) == S_OK && extent.cx == 42 && extent.cy == 4;
        }
        if (resource == 648) {
            WCHAR text[16]{};
            const HWND list = ::GetDlgItem(m_hWnd, 715), combo = ::GetDlgItem(m_hWnd, 716);
            good = good && ::SendMessageW(list, LB_GETCOUNT, 0, 0) == 1 && ::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 1;
            good = good && ::SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, L"mix");
            good = good && ::SendMessageW(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, L"mix");
        }
        WCHAR hostClass[32]{};
        good = ::GetClassNameW(::GetDlgItem(m_hWnd, 711), hostClass, 32) &&
            !::wcscmp(hostClass, resource == 611 || resource == 621 || resource == 632 ? L"AtlAxWinLic140" : L"AtlAxWin140") && good;
        if (modal) good = EndDialog(93) && good;
        else if (closeInInit) good = DestroyWindow() && good;
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(ResourceDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool resourceInitialization() {
    ControlState state;
    CLSID classId{};
    REQUIRE(::CLSIDFromString(L"{2D71A87E-15C3-491D-9F8B-8319472CEA04}", &classId) == S_OK);
    ATL::CComPtr<IClassFactory2> factory; factory.Attach(new Factory(state));
    Registration registration;
    REQUIRE(::CoRegisterClassObject(classId, factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    ATL::CAtlModule module;
    for (WORD resource : {WORD(610), WORD(611), WORD(612), WORD(619), WORD(620), WORD(621), WORD(622), WORD(631), WORD(632), WORD(633), WORD(648)}) for (int mode = 0; mode < 5; ++mode) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.siteFirst = mode != 1; state.initInterface = mode != 2;
        state.sendInitDuringLoad = mode == 4;
        state.streamBytes = resource == 619 ? 5 : 4;
        ResourceDialog dialog(state); dialog.resource = resource; dialog.modal = mode == 3;
        if (resource == 648) {
            dialog.expectedNew = state.initInterface ? 1 : 0;
            dialog.expectedLoads = 0;
        } else if (resource >= 631) {
            dialog.expectedStorages = 1;
            dialog.expectedNew = resource == 633 && state.initInterface ? 1 : 0;
            dialog.expectedLoads = resource == 633 ? 1 : 0;
            dialog.expectedActivations = resource == 633 ? 3 : 1;
        } else if (resource >= 620) {
            dialog.expectedNew = state.initInterface ? 1 : 0;
            dialog.expectedLoads = resource == 622 ? 1 : 0;
            dialog.expectedActivations = resource == 622 ? 2 : 1;
        }
        if (dialog.modal) REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        else {
            REQUIRE(dialog.Create(nullptr, 1773));
            REQUIRE(module.GetLockCount() == dialog.expectedActivations);
            REQUIRE(dialog.DestroyWindow());
        }
        REQUIRE(dialog.good && dialog.initialized == 1 && dialog.finalized == 1 && !dialog.m_hWnd);
        REQUIRE(state.siteOrder && state.loaded == dialog.expectedLoads && state.initialized == dialog.expectedNew && state.activated == dialog.expectedActivations);
        REQUIRE(state.licenseSeen == (resource == 612 || resource == 619 || resource == 632));
        REQUIRE(state.storageReadOnly && state.storageReleased == dialog.expectedStorages);
        if (dialog.expectedStorages) REQUIRE(state.lateStorageReads == 1);
        REQUIRE(state.detached == dialog.expectedActivations && state.closed == dialog.expectedActivations &&
            state.destroyed == dialog.expectedActivations && module.GetLockCount() == 0);
    }
    // Failure must finish before application initialization, clean all earlier
    // children and leave the C++ owner reusable without pending creation data.
    for (int mode = 0; mode < 7; ++mode) for (bool modal : {false, true}) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.destroyDialogDuringInit = mode == 4;
        ResourceDialog dialog(state); dialog.modal = modal;
        dialog.resource = mode == 0 ? 613 : mode == 1 ? 614 : 610;
        dialog.closeInInit = mode == 5;
        if (mode == 6) state.initInterface = false; // Stream fallback failure, not InitNew.
        if (mode == 6) state.failInit = true;
        if (mode == 5 && modal) {
            REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        } else if (modal) REQUIRE(dialog.DoModal(nullptr, 1773) == -1);
        else REQUIRE(!dialog.Create(nullptr, 1773));
        REQUIRE(!dialog.m_hWnd && module.GetLockCount() == 0);
        REQUIRE(dialog.initialized == (mode == 5 ? 1 : 0));
        REQUIRE(dialog.finalized == (dialog.attached ? 1 : 0));
        REQUIRE(state.destroyed == (mode == 0 ? 0 : mode == 1 ? 2 : 1));
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        dialog.resource = 610; dialog.modal = false; dialog.closeInInit = false;
        REQUIRE(dialog.Create(nullptr, 1773) && dialog.good);
        REQUIRE(dialog.DestroyWindow() && module.GetLockCount() == 0);
    }
    for (int mode = 0; mode < 6; ++mode) for (bool modal : {false, true}) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.failInit = mode == 0;
        state.destroyDuringInit = mode == 1;
        state.destroyDialogDuringInit = mode == 2;
        state.failInitializationAt = mode == 3 ? 2 : 0;
        ResourceDialog dialog(state); dialog.modal = modal;
        dialog.resource = mode == 3 ? 627 : mode == 5 ? 625 : 620;
        dialog.expectedNew = 1; dialog.expectedLoads = 0;
        dialog.closeInInit = mode == 4;
        if (mode == 4 && modal) REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        else if (modal) REQUIRE(dialog.DoModal(nullptr, 1773) == -1);
        else REQUIRE(!dialog.Create(nullptr, 1773));
        REQUIRE(!dialog.m_hWnd && module.GetLockCount() == 0);
        REQUIRE(dialog.initialized == (mode == 4 ? 1 : 0) && dialog.finalized == (dialog.attached ? 1 : 0));
        REQUIRE(state.initialized == (mode == 3 ? 2 : mode == 5 ? 0 : 1) && state.loaded == 0);
        REQUIRE(state.activated == (mode == 3 || mode == 4 ? 1 : 0));
        REQUIRE(state.destroyed == (mode == 3 ? 2 : 1));
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        dialog.resource = 620; dialog.modal = false; dialog.closeInInit = false;
        REQUIRE(dialog.Create(nullptr, 1773) && dialog.good);
        REQUIRE(dialog.DestroyWindow() && module.GetLockCount() == 0);
    }
    for (int mode = 0; mode < 9; ++mode) for (bool modal : {false, true}) {
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        state.storageInterface = mode != 1;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.destroyDialogDuringInit = mode == 4;
        state.failInitializationAt = mode == 5 ? 2 : 0;
        state.rejectLicense = mode == 7;
        state.destroyDuringStorageQuery = mode == 8;
        ResourceDialog dialog(state); dialog.modal = modal;
        dialog.resource = mode == 0 ? 634 : mode == 5 ? 636 : mode == 7 ? 632 : 631;
        dialog.expectedLoads = 0; dialog.expectedStorages = 1; dialog.closeInInit = mode == 6;
        if (mode == 6 && modal) REQUIRE(dialog.DoModal(nullptr, 1773) == 93);
        else if (modal) REQUIRE(dialog.DoModal(nullptr, 1773) == -1);
        else REQUIRE(!dialog.Create(nullptr, 1773));
        REQUIRE(!dialog.m_hWnd && module.GetLockCount() == 0 && !state.initialized && !state.loaded);
        REQUIRE(dialog.initialized == (mode == 6 ? 1 : 0) && dialog.finalized == (dialog.attached ? 1 : 0));
        REQUIRE(state.storageLoaded == (mode == 0 || mode == 1 || mode == 7 || mode == 8 ? 0 : mode == 5 ? 2 : 1));
        REQUIRE(state.storageReleased == state.storageLoaded && state.storageReadOnly);
        REQUIRE(state.activated == (mode == 5 || mode == 6 ? 1 : 0));
        REQUIRE(state.destroyed == (mode == 0 || mode == 7 ? 0 : mode == 5 ? 2 : 1));
        state = ControlState{}; state.classId = classId; state.discoverWindow = true;
        dialog.resource = 631; dialog.modal = false; dialog.closeInInit = false;
        REQUIRE(dialog.Create(nullptr, 1773) && dialog.good);
        REQUIRE(dialog.DestroyWindow() && state.storageReleased == 1 && module.GetLockCount() == 0);
    }
    state = ControlState{}; state.classId = classId; state.discoverWindow = true;
    ResourceDialog retainedDialog(state); retainedDialog.resource = 631;
    retainedDialog.expectedLoads = 0; retainedDialog.expectedStorages = 1;
    REQUIRE(retainedDialog.Create(nullptr, 1773) && retainedDialog.good && state.lateStorageReads == 1);
    ATL::CComPtr<IOleObject> retained;
    REQUIRE(retainedDialog.GetDlgControl(711, IID_IOleObject, reinterpret_cast<void**>(&retained)) == S_OK);
    REQUIRE(retainedDialog.DestroyWindow() && !state.destroyed && state.storageReleased == 1 && module.GetLockCount() == 0);
    SIZEL closedExtent{}; REQUIRE(retained->GetExtent(DVASPECT_CONTENT, &closedExtent) == E_NOTIMPL);
    retained.Release(); REQUIRE(state.destroyed == 1);
    return true;
}
struct ActiveState { bool inside = false, diedInside = false; int destroyed = 0, calls = 0; };
class Active : public IOleInPlaceActiveObject {
public:
    Active(ActiveState& state, IOleInPlaceFrame* frame, HWND window, bool destroy) : state_(state), frame_(frame), window_(window), destroy_(destroy) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IOleWindow && iid != IID_IOleInPlaceActiveObject) return E_NOINTERFACE;
        *value = static_cast<IOleInPlaceActiveObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* window) override { if (!window) return E_POINTER; *window = window_; return S_OK; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG) override {
        ActiveState& state = state_; // remains valid even if the old host deletes this
        state.inside = true; ++state.calls;
        if (destroy_) ::DestroyWindow(window_); else frame_->SetActiveObject(nullptr, nullptr);
        state.inside = false; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
private:
    ~Active() { state_.diedInside = state_.inside; ++state_.destroyed; }
    ULONG refs_ = 1;
    ActiveState& state_;
    IOleInPlaceFrame* frame_; // test keeps its frame reference through the callback
    HWND window_;
    bool destroy_;
};
bool accelerators() {
    for (int mode = 0; mode < 4; ++mode) {
        ActiveState state;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IOleInPlaceFrame> frame; REQUIRE(window.QueryHost(&frame) == S_OK);
        ATL::CComPtr<IOleControlSite> site; REQUIRE(window.QueryHost(&site) == S_OK);
        auto active = new Active(state, frame, window, mode >= 2);
        REQUIRE(frame->SetActiveObject(active, nullptr) == S_OK); active->Release();
        MSG message{}; message.hwnd = window; message.message = WM_KEYDOWN; message.wParam = VK_TAB;
        if (mode % 2) REQUIRE(site->TranslateAccelerator(&message, 0) == S_OK);
        else REQUIRE(::SendMessageW(window, WM_FORWARDMSG, 0, reinterpret_cast<LPARAM>(&message)) == TRUE);
        REQUIRE(state.calls == 1 && state.destroyed == 1 && !state.diedInside);
        if (mode >= 2) {
            ActiveState lateState;
            auto late = new Active(lateState, frame, window, false);
            HRESULT lateResult = frame->SetActiveObject(late, nullptr);
            late->Release();
            frame->SetActiveObject(nullptr, nullptr);
            REQUIRE(lateResult == CO_E_OBJNOTCONNECTED && lateState.destroyed == 1);
        }
        if (::IsWindow(window)) REQUIRE(window.DestroyWindow());
        window.m_hWnd = nullptr;
    }
    return true;
}
}
bool RunHostLifecycleTests() {
    const bool created = creation();
    const bool active = accelerators();
    const bool resources = resourceInitialization();
    const bool ui = uiHandlers();
    return created && active && resources && ui;
}
bool RunBrowserUIHandlerTest(ATL::CAxWindow& window, IHTMLDocument2* document) {
    UIState state; state.browser = true;
    ATL::CComPtr<UIObject> object; object.Attach(new UIObject); state.object = object;
    ATL::CComPtr<IDocHostUIHandlerDispatch> handler; handler.Attach(new UIHandler(state));
    ATL::CComPtr<IAxWinHostWindow> host; REQUIRE(window.QueryHost(&host) == S_OK);
    struct Reset {
        IAxWinHostWindow* host;
        ~Reset() { host->SetExternalUIHandler(nullptr); }
    } reset{host}; // Clear before the borrowed fixture state leaves scope, including failures.
    REQUIRE(window.SetExternalUIHandler(handler) == S_OK);
    ATL::CComPtr<IHTMLWindow2> script; REQUIRE(document->get_parentWindow(&script) == S_OK && script);
    ATL::CComPtr<IDispatch> external; REQUIRE(script->get_external(&external) == S_OK);
    REQUIRE(state.calls[12] > 0 && external);
    LPOLESTR name = const_cast<LPOLESTR>(L"OpenWTLHostToken"); DISPID id = DISPID_UNKNOWN;
    REQUIRE(external->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id) == S_OK);
    DISPPARAMS args{}; ATL::CComVariant value;
    REQUIRE(external->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &args, &value, nullptr, nullptr) == S_OK &&
        value.vt == VT_I4 && value.lVal == 0x4f57544c);
    REQUIRE(window.SetExternalUIHandler(nullptr) == S_OK);
    handler.Release(); REQUIRE(state.destroyed == 1 && !state.diedInside);
    return true;
}
