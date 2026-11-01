// Registered COM factories and actual HWNDs exercise host transaction ordering
// and reentrant retirement without relying on a particular installed control.
#include <atlhost.h>
#include <cstdio>
#include <new>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "HOST_LIFECYCLE_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
template<class T> class OwnedWindow : public T {
public:
    ~OwnedWindow() { if (::IsWindow(this->m_hWnd)) ::DestroyWindow(this->m_hWnd); }
};
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
    bool modal = false, closeInInit = false, good = true;
    int initialized = 0, finalized = 0;
    int expectedNew = 0, expectedLoads = 1, expectedActivations = 1, expectedStorages = 0;
    explicit ResourceDialog(ControlState& value) : state(value) {}
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
        REQUIRE(dialog.finalized == (mode == 5 ? 1 : 0));
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
        REQUIRE(dialog.initialized == (mode == 4 ? 1 : 0) && dialog.finalized == (mode == 4 ? 1 : 0));
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
        REQUIRE(dialog.initialized == (mode == 6 ? 1 : 0) && dialog.finalized == (mode == 6 ? 1 : 0));
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
    return created && active && resources;
}
