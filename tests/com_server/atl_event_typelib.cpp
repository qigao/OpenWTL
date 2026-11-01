#include "event_fixture_metadata.h"
#include "interfaces.h"
#include <cstdio>
#include <functional>

#define EVENT_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_TYPELIB_EVENT_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
using EventId = _IOpenATLDispatchEvents;
class Source : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
               public ATL::IConnectionPointContainerImpl<Source>,
               public ATL::IConnectionPointImpl<Source, &DIID__IOpenATLDispatchEvents>, public IProvideClassInfo2,
               public EventFixtureDispatch<Source> {
    using Point = ATL::IConnectionPointImpl<Source, &DIID__IOpenATLDispatchEvents>;
public:
    HRESULT metadata = S_OK, dispatchMetadata = S_OK, connect = S_OK, disconnect = S_OK;
    CLSID coclass = CLSID_EventFixtureSource;
    bool eager = false;
    BEGIN_COM_MAP(Source)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
        COM_INTERFACE_ENTRY(IProvideClassInfo2)
        COM_INTERFACE_ENTRY2(IProvideClassInfo, IProvideClassInfo2)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID__IOpenATLSourceDispatch, IDispatch)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(Source)
        CONNECTION_POINT_ENTRY(DIID__IOpenATLDispatchEvents)
    END_CONNECTION_POINT_MAP()
    HRESULT STDMETHODCALLTYPE GetClassInfo(ITypeInfo** output) noexcept override {
        if (!output) return E_POINTER; *output = nullptr;
        return FAILED(metadata) ? metadata : LoadEventFixtureType(coclass, output);
    }
    HRESULT GetFixtureDispatchTypeInfo(ITypeInfo** output) noexcept {
        if (!output) return E_POINTER; *output = nullptr;
        return FAILED(dispatchMetadata) ? dispatchMetadata : LoadEventFixtureType(DIID__IOpenATLSourceDispatch, output);
    }
    LONG FixtureCurrentValue() const noexcept { return 42; }
    HRESULT STDMETHODCALLTYPE GetGUID(DWORD kind, GUID* output) noexcept override {
        if (!output) return E_POINTER; *output = GUID_NULL;
        if (kind != GUIDKIND_DEFAULT_SOURCE_DISP_IID) return E_INVALIDARG;
        *output = DIID__IOpenATLDispatchEvents; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Advise(IUnknown* sink, DWORD* cookie) override {
        if (FAILED(connect)) { if (cookie) *cookie = 0; return connect; }
        HRESULT hr = Point::Advise(sink, cookie);
        if (hr == S_OK && eager) {
            ATL::CComVariant args[3] = {ATL::CComVariant(L"typelib event"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(42L)};
            args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
            DISPPARAMS params{args, nullptr, 3, 0};
            hr = Fire(101, params);
            if (FAILED(hr)) Point::Unadvise(*cookie);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override { return FAILED(disconnect) ? disconnect : Point::Unadvise(cookie); }
    HRESULT Fire(DISPID member, DISPPARAMS& params, UINT* argument = nullptr) {
        ATL::CComPtr<IEnumConnections> snapshot;
        HRESULT hr = Point::EnumConnections(&snapshot.p);
        if (FAILED(hr)) return hr;
        for (;;) {
            CONNECTDATA data{}; hr = snapshot->Next(1, &data, nullptr);
            if (hr == S_FALSE) return S_OK;
            if (FAILED(hr)) return hr;
            ATL::CComPtr<IUnknown> held; held.Attach(data.pUnk);
            hr = static_cast<IDispatch*>(data.pUnk)->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT,
                DISPATCH_METHOD, &params, nullptr, nullptr, argument);
            if (FAILED(hr)) return hr;
        }
    }
};
// Each object has its own COM map. QueryInterface through any exposed interface
// therefore advertises exactly the provider set under test.
class LegacySource : public Source {
public:
    BEGIN_COM_MAP(LegacySource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
        COM_INTERFACE_ENTRY(IProvideClassInfo)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID__IOpenATLSourceDispatch, IDispatch)
    END_COM_MAP()
};
class PersistSource : public Source, public IPersist {
public:
    CLSID identity = CLSID_EventFixtureSource;
    HRESULT classId = S_OK;
    BEGIN_COM_MAP(PersistSource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID__IOpenATLSourceDispatch, IDispatch)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) noexcept override {
        if (!output) return E_POINTER; *output = CLSID_NULL;
        if (FAILED(classId)) return classId;
        *output = identity; return S_OK;
    }
};
class DispatchOnlySource : public Source {
public:
    BEGIN_COM_MAP(DispatchOnlySource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID__IOpenATLSourceDispatch, IDispatch)
    END_COM_MAP()
};
template<class T> bool Make(ATL::CComPtr<ATL::CComObject<T>>& result) {
    ATL::CComObject<T>* raw = nullptr;
    EVENT_CHECK(ATL::CComObject<T>::CreateInstance(&raw) == S_OK && raw);
    raw->AddRef(); result.Attach(raw); return true;
}
class FileHolder {
public:
    HRESULT GetTI(const IID* iid, const GUID* libid, WORD major, WORD minor, LCID, ITypeInfo** output) const noexcept {
        wchar_t image[8193]{};
        if (!::GetModuleFileNameW(nullptr, image, 8193)) return E_FAIL;
        try { return ATL::CComTypeInfoHolder().GetTIFromFile(std::wstring(image) + L"\\4", iid, libid, major, minor, output); }
        catch (...) { return E_OUTOFMEMORY; }
    }
};
struct HandlerBase {
    int changed = 0;
    LONG expected = 42;
    bool correct = false;
    std::function<void()> callback;
    void Changed(LONG number, VARIANT_BOOL enabled, BSTR text) {
        ++changed; correct = number == expected && enabled == VARIANT_TRUE && text && std::wcscmp(text, L"typelib event") == 0;
        if (callback) callback();
    }
};
struct Prefix { virtual ~Prefix() = default; LONG padding = 7; };
template<class Holder = ATL::CComTypeInfoHolder>
class ExplicitSink : public Prefix, public HandlerBase,
    public ATL::IDispEventImpl<31, ExplicitSink<Holder>, &DIID__IOpenATLDispatchEvents, &LIBID_OpenATLEventLib, 1, 0, Holder> {
public:
    int adjusts = 0, states = 0, bundles = 0, unsupported = 0;
    ATL::CComVariant retained;
    BEGIN_SINK_MAP(ExplicitSink)
        SINK_ENTRY_EX(31, DIID__IOpenATLDispatchEvents, 101, &HandlerBase::Changed)
        SINK_ENTRY_EX_P(31, &DIID__IOpenATLDispatchEvents, 102, &ExplicitSink::Adjust)
        SINK_ENTRY_EX(31, DIID__IOpenATLDispatchEvents, 103, &ExplicitSink::State)
        SINK_ENTRY_EX(31, DIID__IOpenATLDispatchEvents, 104, &ExplicitSink::Bundle)
        SINK_ENTRY_EX(31, DIID__IOpenATLDispatchEvents, 105, &ExplicitSink::TooMany)
    END_SINK_MAP()
    void Adjust(LONG* value) { ++adjusts; ++*value; }
    void State(OpenATLEventState state, LONG count) { if (state == EventReady && count == 99) ++states; }
    void Bundle(VARIANT value) { retained = value; ++bundles; }
    void TooMany() { ++unsupported; }
};
class DynamicSink : public ATL::IDispEventImpl<32, DynamicSink>, public HandlerBase {
public:
    BEGIN_SINK_MAP(DynamicSink)
        SINK_ENTRY(32, 101, &HandlerBase::Changed)
    END_SINK_MAP()
};
template<const IID* Outgoing = &IID_NULL>
class SimpleSink : public ATL::IDispEventSimpleImpl<33, SimpleSink<Outgoing>, Outgoing>, public HandlerBase {
public:
    inline static ATL::_ATL_FUNC_INFO signature = {CC_STDCALL, VT_EMPTY, 3, {VT_I4, VT_BOOL, VT_BSTR}};
    BEGIN_SINK_MAP(SimpleSink)
        SINK_ENTRY_INFO(33, IID_NULL, 101, Changed, &signature)
    END_SINK_MAP()
};
class PairSink : public ATL::IDispEventImpl<41, PairSink>, public ATL::IDispEventImpl<42, PairSink> {
public:
    using LeftBase = ATL::IDispEventImpl<41, PairSink>;
    using RightBase = ATL::IDispEventImpl<42, PairSink>;
    int left = 0, right = 0;
    BEGIN_SINK_MAP(PairSink)
        SINK_ENTRY(41, 101, &PairSink::Left)
        SINK_ENTRY(42, 101, &PairSink::Right)
    END_SINK_MAP()
    void Left(LONG, VARIANT_BOOL, BSTR) { ++left; }
    void Right(LONG, VARIANT_BOOL, BSTR) { ++right; }
};
class MapSink : public ATL::IDispEventImpl<51, MapSink>,
                public ATL::IDispEventSimpleImpl<52, MapSink, &IID_NULL> {
public:
    using LeftBase = ATL::IDispEventImpl<51, MapSink>;
    using RightBase = ATL::IDispEventSimpleImpl<52, MapSink, &IID_NULL>;
    inline static ATL::_ATL_FUNC_INFO signature = {CC_STDCALL, VT_EMPTY, 3, {VT_I4, VT_BOOL, VT_BSTR}};
    IUnknown* first = nullptr;
    IUnknown* second = nullptr;
    HRESULT lookupSecond = S_OK;
    int lookups = 0, left = 0, right = 0, bundles = 0;
    LONG expected = 42;
    bool correct = true;
    std::function<void()> callback;
    ATL::CComVariant retained;
    BEGIN_SINK_MAP(MapSink)
        SINK_ENTRY(51, 101, &MapSink::Left)
        SINK_ENTRY(51, 104, &MapSink::Bundle)
        SINK_ENTRY_INFO(52, IID_NULL, 101, Right, &signature)
    END_SINK_MAP()
    HRESULT GetDlgControl(int id, REFIID iid, void** output) {
        if (!output) return E_POINTER; *output = nullptr;
        ++lookups;
        if (id == 52 && FAILED(lookupSecond)) return lookupSecond;
        IUnknown* source = id == 51 ? first : id == 52 ? second : nullptr;
        return source ? source->QueryInterface(iid, output) : E_FAIL;
    }
    void Check(LONG value, VARIANT_BOOL enabled, BSTR text) {
        correct = correct && value == expected && enabled == VARIANT_TRUE && text && std::wcscmp(text, L"typelib event") == 0;
    }
    void Left(LONG value, VARIANT_BOOL enabled, BSTR text) { ++left; Check(value, enabled, text); if (callback) callback(); }
    void Right(LONG value, VARIANT_BOOL enabled, BSTR text) { ++right; Check(value, enabled, text); }
    void Bundle(VARIANT value) { ++bundles; retained = value; }
};
class EmptyMap {
public:
    BEGIN_SINK_MAP(EmptyMap)
    END_SINK_MAP()
    HRESULT GetDlgControl(int, REFIID, void**) { return E_FAIL; }
};
class DerivedMap : public Prefix, public MapSink {};
class ConflictingMap : public ATL::IDispEventSimpleImpl<53, ConflictingMap, &IID_NULL> {
public:
    inline static ATL::_ATL_FUNC_INFO signature = {CC_STDCALL, VT_EMPTY, 0, {}};
    int lookups = 0;
    BEGIN_SINK_MAP(ConflictingMap)
        SINK_ENTRY_INFO(53, DIID__IOpenATLDispatchEvents, 101, Ignore, &signature)
        SINK_ENTRY_INFO(53, DIID__IOpenATLSourceDispatch, 102, Ignore, &signature)
    END_SINK_MAP()
    void Ignore() {}
    HRESULT GetDlgControl(int, REFIID, void**) { ++lookups; return E_FAIL; }
};
class WrongPointee : public ATL::IDispEventImpl<43, WrongPointee, &DIID__IOpenATLDispatchEvents, &LIBID_OpenATLEventLib, 1, 0, FileHolder> {
public:
    int calls = 0;
    BEGIN_SINK_MAP(WrongPointee)
        SINK_ENTRY_EX(43, DIID__IOpenATLDispatchEvents, 102, &WrongPointee::Adjust)
    END_SINK_MAP()
    void Adjust(double*) { ++calls; }
};
bool Scalar(Source* source) {
    ATL::CComVariant args[3] = {ATL::CComVariant(L"typelib event"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(42L)};
    args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
    DISPPARAMS params{args, nullptr, 3, 0};
    EVENT_CHECK(source->Fire(101, params) == S_OK);
    return true;
}
bool SourceInformation(IUnknown* object, HRESULT expected = S_OK) {
    GUID library = IID_IUnknown; IID outgoing = IID_IDispatch;
    unsigned short major = 99, minor = 99;
    EVENT_CHECK(ATL::AtlGetObjectSourceInterface(object, &library, &outgoing, &major, &minor) == expected);
    if (expected == S_OK) {
        EVENT_CHECK(library == LIBID_OpenATLEventLib && outgoing == DIID__IOpenATLDispatchEvents && major == 1 && minor == 0);
    } else {
        EVENT_CHECK(library == GUID_NULL && outgoing == IID_NULL && !major && !minor);
    }
    return true;
}
bool IncomingDispatch(IUnknown* object, LONG expected) {
    ATL::CComPtr<IDispatch> dispatch;
    EVENT_CHECK(object->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&dispatch.p)) == S_OK);
    LPOLESTR name = const_cast<LPOLESTR>(L"Current"); DISPID member = DISPID_UNKNOWN;
    EVENT_CHECK(dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &member) == S_OK);
    DISPPARAMS empty{}; ATL::CComVariant result;
    EVENT_CHECK(dispatch->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
        &empty, &result, nullptr, nullptr) == S_OK && result.vt == VT_I4 && result.lVal == expected);
    return true;
}
bool DiscoveryFallbacks() {
    EVENT_CHECK(SourceInformation(nullptr, E_POINTER));
    ATL::CComPtr<ATL::CComObject<Source>> modern;
    ATL::CComPtr<ATL::CComObject<LegacySource>> legacy;
    ATL::CComPtr<ATL::CComObject<PersistSource>> persisted;
    ATL::CComPtr<ATL::CComObject<DispatchOnlySource>> dispatchOnly;
    EVENT_CHECK(Make(modern) && Make(legacy) && Make(persisted) && Make(dispatchOnly));
    GUID library = IID_IUnknown; IID outgoing = IID_IDispatch; unsigned short major = 99, minor = 99;
    EVENT_CHECK(ATL::AtlGetObjectSourceInterface(modern->GetUnknown(), nullptr, &outgoing, &major, &minor) == E_POINTER);
    EVENT_CHECK(outgoing == IID_NULL && !major && !minor);
    EVENT_CHECK(ATL::AtlGetObjectSourceInterface(modern->GetUnknown(), &library, nullptr, &major, &minor) == E_POINTER && library == GUID_NULL);
    EVENT_CHECK(ATL::AtlGetObjectSourceInterface(modern->GetUnknown(), &library, &outgoing, nullptr, &minor) == E_POINTER);
    EVENT_CHECK(ATL::AtlGetObjectSourceInterface(modern->GetUnknown(), &library, &outgoing, &major, nullptr) == E_POINTER);
    EVENT_CHECK(SourceInformation(modern->GetUnknown()) && SourceInformation(legacy->GetUnknown()));
    EVENT_CHECK(IncomingDispatch(legacy->GetUnknown(), 42));
    ATL::CComPtr<IProvideClassInfo2> absent;
    EVENT_CHECK(legacy->QueryInterface(IID_IProvideClassInfo2, reinterpret_cast<void**>(&absent.p)) == E_NOINTERFACE && !absent.p);
    DynamicSink sink;
    legacy->coclass = CLSID_NoDefaultEventFixtureSource;
    EVENT_CHECK(SourceInformation(legacy->GetUnknown(), CONNECT_E_NOCONNECTION));
    EVENT_CHECK(sink.Advise(legacy->GetUnknown()) == CONNECT_E_NOCONNECTION);
    legacy->coclass = DIID__IOpenATLDispatchEvents;
    EVENT_CHECK(SourceInformation(legacy->GetUnknown(), E_UNEXPECTED) && sink.Advise(legacy->GetUnknown()) == E_UNEXPECTED);
    legacy->coclass = CLSID_EventFixtureSource; legacy->metadata = E_ACCESSDENIED;
    EVENT_CHECK(SourceInformation(legacy->GetUnknown(), E_ACCESSDENIED) && sink.Advise(legacy->GetUnknown()) == E_ACCESSDENIED);
    legacy->metadata = S_OK; legacy->connect = E_ACCESSDENIED;
    EVENT_CHECK(sink.Advise(legacy->GetUnknown()) == E_ACCESSDENIED);
    legacy->connect = S_OK;
    EVENT_CHECK(sink.Advise(legacy->GetUnknown()) == S_OK && Scalar(legacy.p) && sink.correct && sink.Unadvise(legacy->GetUnknown()) == S_OK);
    ATL::CComPtr<IProvideClassInfo> noProvider;
    EVENT_CHECK(persisted->QueryInterface(IID_IProvideClassInfo, reinterpret_cast<void**>(&noProvider.p)) == E_NOINTERFACE && !noProvider.p);
    persisted->classId = E_ACCESSDENIED;
    EVENT_CHECK(SourceInformation(persisted->GetUnknown(), E_ACCESSDENIED) && sink.Advise(persisted->GetUnknown()) == E_ACCESSDENIED);
    persisted->classId = S_OK; persisted->identity = CLSID_NULL;
    EVENT_CHECK(SourceInformation(persisted->GetUnknown(), E_UNEXPECTED));
    persisted->identity = CLSID_EventFixtureSource; persisted->identity.Data1 ^= 0x10000000;
    EVENT_CHECK(SourceInformation(persisted->GetUnknown(), TYPE_E_ELEMENTNOTFOUND));
    persisted->identity = CLSID_NoDefaultEventFixtureSource;
    EVENT_CHECK(SourceInformation(persisted->GetUnknown(), CONNECT_E_NOCONNECTION));
    persisted->identity = CLSID_EventFixtureSource; persisted->dispatchMetadata = E_ACCESSDENIED;
    EVENT_CHECK(SourceInformation(persisted->GetUnknown(), E_ACCESSDENIED) && sink.Advise(persisted->GetUnknown()) == E_ACCESSDENIED);
    persisted->dispatchMetadata = S_OK;
    EVENT_CHECK(SourceInformation(persisted->GetUnknown()) && IncomingDispatch(persisted->GetUnknown(), 42));
    EVENT_CHECK(sink.Advise(persisted->GetUnknown()) == S_OK && Scalar(persisted.p) && sink.correct);
    persisted->disconnect = E_ACCESSDENIED;
    EVENT_CHECK(sink.Unadvise(persisted->GetUnknown()) == E_ACCESSDENIED && Scalar(persisted.p));
    persisted->disconnect = S_OK;
    EVENT_CHECK(sink.Unadvise(persisted->GetUnknown()) == S_OK);
    EVENT_CHECK(SourceInformation(dispatchOnly->GetUnknown(), E_NOINTERFACE));
    EVENT_CHECK(sink.Advise(dispatchOnly->GetUnknown()) == E_NOINTERFACE);
    // An explicitly supplied outgoing IID needs only the incoming library.
    EVENT_CHECK(sink.DispEventAdvise(dispatchOnly->GetUnknown(), &DIID__IOpenATLDispatchEvents) == S_OK && Scalar(dispatchOnly.p) && sink.correct);
    EVENT_CHECK(sink.Unadvise(dispatchOnly->GetUnknown()) == S_OK);
    return true;
}
bool SimpleDiscovery() {
    ATL::CComPtr<ATL::CComObject<Source>> modern, other;
    ATL::CComPtr<ATL::CComObject<LegacySource>> legacy;
    ATL::CComPtr<ATL::CComObject<PersistSource>> persisted;
    ATL::CComPtr<ATL::CComObject<DispatchOnlySource>> dispatchOnly;
    EVENT_CHECK(Make(modern) && Make(other) && Make(legacy) && Make(persisted) && Make(dispatchOnly));
    SimpleSink<> sink;
    EVENT_CHECK(sink.Advise(nullptr) == E_POINTER && sink.Unadvise(nullptr) == E_POINTER);
    EVENT_CHECK(sink.m_dwEventCookie == 0xFEFEFEFE);
    ATL::CComPtr<IUnknown> outgoing;
    auto identity = [&](HRESULT expected) {
        outgoing.Release();
        const HRESULT hr = sink.QueryInterface(DIID__IOpenATLDispatchEvents, reinterpret_cast<void**>(&outgoing.p));
        const bool correct = hr == expected && (expected == S_OK ? outgoing.p != nullptr : outgoing.p == nullptr);
        outgoing.Release(); return correct;
    };
    EVENT_CHECK(identity(E_NOINTERFACE));
    modern->metadata = E_ACCESSDENIED;
    EVENT_CHECK(sink.Advise(modern->GetUnknown()) == E_ACCESSDENIED && identity(E_NOINTERFACE));
    modern->metadata = S_OK; modern->connect = E_ACCESSDENIED;
    EVENT_CHECK(sink.Advise(modern->GetUnknown()) == E_ACCESSDENIED && identity(E_NOINTERFACE) && sink.m_dwEventCookie == 0xFEFEFEFE);
    modern->connect = S_OK; modern->eager = true;
    HRESULT reentrant = S_OK;
    sink.callback = [&] { reentrant = sink.Unadvise(modern->GetUnknown()); };
    EVENT_CHECK(sink.Advise(modern->GetUnknown()) == S_OK && sink.changed == 1 && sink.correct && reentrant == E_UNEXPECTED && identity(S_OK));
    sink.callback = {}; modern->eager = false;
    EVENT_CHECK(sink.Advise(other->GetUnknown()) == CONNECT_E_ADVISELIMIT);
    const DWORD cookie = sink.m_dwEventCookie;
    EVENT_CHECK(sink.Unadvise(other->GetUnknown()) == CONNECT_E_NOCONNECTION && sink.m_dwEventCookie == cookie);
    modern->disconnect = E_ACCESSDENIED;
    EVENT_CHECK(sink.Unadvise(modern->GetUnknown()) == E_ACCESSDENIED && sink.m_dwEventCookie == cookie && identity(S_OK) && Scalar(modern.p));
    modern->disconnect = S_OK;
    EVENT_CHECK(sink.Unadvise(modern->GetUnknown()) == S_OK && identity(E_NOINTERFACE) && sink.m_dwEventCookie == 0xFEFEFEFE);
    legacy->coclass = CLSID_NoDefaultEventFixtureSource;
    EVENT_CHECK(sink.Advise(legacy->GetUnknown()) == CONNECT_E_NOCONNECTION && identity(E_NOINTERFACE));
    legacy->coclass = DIID__IOpenATLDispatchEvents;
    EVENT_CHECK(sink.Advise(legacy->GetUnknown()) == E_UNEXPECTED && identity(E_NOINTERFACE));
    legacy->coclass = CLSID_EventFixtureSource;
    EVENT_CHECK(sink.DispEventAdvise(legacy->GetUnknown(), nullptr) == S_OK && Scalar(legacy.p) && sink.correct);
    UINT count = 99; ATL::CComPtr<ITypeInfo> info;
    EVENT_CHECK(sink.GetTypeInfoCount(&count) == E_NOTIMPL && count == 0);
    EVENT_CHECK(sink.GetTypeInfo(0, 0, &info.p) == E_NOTIMPL && !info.p);
    EVENT_CHECK(sink.Unadvise(legacy->GetUnknown()) == S_OK && identity(E_NOINTERFACE));
    persisted->classId = E_ACCESSDENIED;
    EVENT_CHECK(sink.Advise(persisted->GetUnknown()) == E_ACCESSDENIED && identity(E_NOINTERFACE));
    persisted->classId = S_OK; persisted->dispatchMetadata = E_ACCESSDENIED;
    EVENT_CHECK(sink.Advise(persisted->GetUnknown()) == E_ACCESSDENIED && identity(E_NOINTERFACE));
    persisted->dispatchMetadata = S_OK;
    EVENT_CHECK(sink.Advise(persisted->GetUnknown()) == S_OK && Scalar(persisted.p) && sink.correct);
    EVENT_CHECK(sink.Unadvise(persisted->GetUnknown()) == S_OK && identity(E_NOINTERFACE));
    EVENT_CHECK(sink.Advise(dispatchOnly->GetUnknown()) == E_NOINTERFACE && identity(E_NOINTERFACE));
    // Explicit IID selection needs no source metadata, including when an
    // advertised provider fails. Advise must still discover the default.
    SimpleSink<&DIID__IOpenATLDispatchEvents> fixed;
    modern->metadata = E_ACCESSDENIED;
    EVENT_CHECK(fixed.Advise(modern->GetUnknown()) == E_ACCESSDENIED);
    EVENT_CHECK(fixed.DispEventAdvise(modern->GetUnknown()) == S_OK && Scalar(modern.p) && fixed.correct);
    EVENT_CHECK(fixed.DispEventUnadvise(modern->GetUnknown()) == S_OK);
    EVENT_CHECK(fixed.DispEventAdvise(dispatchOnly->GetUnknown()) == S_OK && Scalar(dispatchOnly.p) && fixed.correct);
    EVENT_CHECK(fixed.DispEventUnadvise(dispatchOnly->GetUnknown()) == S_OK);
    SimpleSink<&DIID__IOpenATLSourceDispatch> different;
    EVENT_CHECK(different.DispEventAdvise(legacy->GetUnknown()) == CONNECT_E_NOCONNECTION);
    EVENT_CHECK(different.Advise(legacy->GetUnknown()) == S_OK && Scalar(legacy.p) && different.correct);
    EVENT_CHECK(different.Unadvise(legacy->GetUnknown()) == S_OK);
    EVENT_CHECK(different.QueryInterface(DIID__IOpenATLSourceDispatch, reinterpret_cast<void**>(&outgoing.p)) == S_OK);
    outgoing.Release();
    EVENT_CHECK(different.QueryInterface(DIID__IOpenATLDispatchEvents, reinterpret_cast<void**>(&outgoing.p)) == E_NOINTERFACE && !outgoing.p);
    { SimpleSink<> automatic; EVENT_CHECK(automatic.Advise(persisted->GetUnknown()) == S_OK && Scalar(persisted.p)); }
    ATL::CComPtr<IConnectionPoint> point;
    ATL::CComPtr<IEnumConnections> connections;
    CONNECTDATA data{};
    EVENT_CHECK(persisted->FindConnectionPoint(DIID__IOpenATLDispatchEvents, &point.p) == S_OK);
    EVENT_CHECK(point->EnumConnections(&connections.p) == S_OK && connections->Next(1, &data, nullptr) == S_FALSE);
    return true;
}
bool MapBinding() {
    EVENT_CHECK(ATL::AtlAdviseSinkMap<MapSink>(nullptr, true) == E_POINTER);
    EmptyMap empty;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&empty, true) == S_OK && ATL::AtlAdviseSinkMap(&empty, false) == S_OK);
    ConflictingMap conflicting;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&conflicting, true) == E_INVALIDARG && !conflicting.lookups);
    ATL::CComPtr<ATL::CComObject<LegacySource>> first;
    ATL::CComPtr<ATL::CComObject<PersistSource>> second;
    EVENT_CHECK(Make(first) && Make(second));
    MapSink sink; sink.first = first->GetUnknown(); sink.second = second->GetUnknown();
    auto& left = static_cast<MapSink::LeftBase&>(sink);
    auto& right = static_cast<MapSink::RightBase&>(sink);
    sink.lookupSecond = E_ACCESSDENIED;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, true) == E_ACCESSDENIED && sink.lookups == 2);
    EVENT_CHECK(left.m_dwEventCookie == 0xFEFEFEFE && right.m_dwEventCookie == 0xFEFEFEFE);
    sink.lookupSecond = S_OK; second->connect = E_ACCESSDENIED; first->eager = true;
    HRESULT reentrant = S_OK;
    sink.callback = [&] { reentrant = ATL::AtlAdviseSinkMap(&sink, false); };
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, true) == E_ACCESSDENIED && reentrant == E_UNEXPECTED && sink.left == 1 && sink.correct);
    EVENT_CHECK(left.m_dwEventCookie == 0xFEFEFEFE && right.m_dwEventCookie == 0xFEFEFEFE);
    ATL::CComPtr<ITypeInfo> info;
    EVENT_CHECK(left.GetTypeInfo(0, 0, &info.p) == TYPE_E_LIBNOTREGISTERED && !info.p);
    first->disconnect = E_ABORT;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, true) == E_ABORT && left.m_dwEventCookie != 0xFEFEFEFE);
    EVENT_CHECK(left.GetTypeInfo(0, 0, &info.p) == S_OK && info.p); info.Release();
    first->disconnect = S_OK; sink.first = nullptr; sink.second = nullptr; sink.lookupSecond = E_ACCESSDENIED;
    const int lookups = sink.lookups;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, false) == S_OK && sink.lookups == lookups);
    sink.first = first->GetUnknown(); sink.second = second->GetUnknown(); sink.lookupSecond = S_OK;
    second->connect = S_OK; first->eager = false; sink.callback = {};
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, true) == S_OK);
    EVENT_CHECK(left.GetTypeInfo(0, 0, &info.p) == S_OK && info.p); info.Release();
    EVENT_CHECK(right.GetTypeInfo(0, 0, &info.p) == E_NOTIMPL && !info.p);
    EVENT_CHECK(Scalar(first.p) && Scalar(second.p) && sink.correct && sink.right == 1);
    const int acquired = sink.lookups;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, true) == CONNECT_E_ADVISELIMIT && sink.lookups == acquired);
    ATL::CComVariant value(L"retained event"); DISPPARAMS params{&value, nullptr, 1, 0};
    EVENT_CHECK(first->Fire(104, params) == S_OK && sink.bundles == 1 && sink.retained.bstrVal != value.bstrVal);
    first->disconnect = E_ACCESSDENIED;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, false) == E_ACCESSDENIED && right.m_dwEventCookie == 0xFEFEFEFE);
    EVENT_CHECK(Scalar(first.p) && Scalar(second.p) && sink.right == 1);
    first->disconnect = S_OK;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, false) == S_OK && ATL::AtlAdviseSinkMap(&sink, false) == S_OK);
    EVENT_CHECK(left.Advise(first->GetUnknown()) == S_OK);
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, true) == CONNECT_E_ADVISELIMIT);
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&sink, false) == S_OK);
    EVENT_CHECK(sink.retained.vt == VT_BSTR && std::wcscmp(sink.retained.bstrVal, L"retained event") == 0);
    DerivedMap inherited;
    EVENT_CHECK(static_cast<void*>(static_cast<MapSink*>(&inherited)) != static_cast<void*>(&inherited));
    inherited.first = first->GetUnknown(); inherited.second = second->GetUnknown();
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&inherited, true) == S_OK && Scalar(first.p) && Scalar(second.p));
    EVENT_CHECK(inherited.correct && inherited.left == 1 && inherited.right == 1);
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&inherited, false) == S_OK);
    return true;
}
bool MetadataAndCalls() {
    ExplicitSink<FileHolder> sink;
    EVENT_CHECK(static_cast<void*>(static_cast<HandlerBase*>(&sink)) != static_cast<void*>(&sink));
    UINT count = 0;
    EVENT_CHECK(sink.GetTypeInfoCount(&count) == S_OK && count == 1 && sink.GetTypeInfoCount(nullptr) == E_POINTER);
    ATL::CComPtr<ITypeInfo> info;
    EVENT_CHECK(sink.GetTypeInfo(1, 0, &info.p) == DISP_E_BADINDEX && !info.p);
    EVENT_CHECK(sink.GetTypeInfo(0, 0, nullptr) == E_POINTER && sink.GetTypeInfo(0, 0, &info.p) == S_OK);
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"Changed")}; DISPID id = DISPID_UNKNOWN;
    EVENT_CHECK(sink.GetIDsOfNames(IID_NULL, names, 1, 0, &id) == S_OK && id == 101);
    EVENT_CHECK(sink.GetIDsOfNames(IID_IUnknown, names, 1, 0, &id) == DISP_E_UNKNOWNINTERFACE && id == DISPID_UNKNOWN);
    ATL::_ATL_FUNC_INFO function{};
    EVENT_CHECK(sink.GetFuncInfoFromId(DIID__IOpenATLDispatchEvents, 101, 0, function) == S_OK && function.nParams == 3);
    EVENT_CHECK(function.pVarTypes[0] == VT_I4 && function.pVarTypes[1] == VT_BOOL && function.pVarTypes[2] == VT_BSTR);
    EVENT_CHECK(sink.GetFuncInfoFromId(DIID__IOpenATLDispatchEvents, 102, 0, function) == S_OK && function.pVarTypes[0] == (VT_BYREF | VT_I4));
    EVENT_CHECK(sink.GetFuncInfoFromId(DIID__IOpenATLDispatchEvents, 103, 0, function) == S_OK && function.pVarTypes[0] == VT_I4 && function.pVarTypes[1] == VT_I4);
    EVENT_CHECK(sink.GetFuncInfoFromId(DIID__IOpenATLDispatchEvents, 106, 0, function) == S_OK && function.pVarTypes[0] == VT_DISPATCH);
    EVENT_CHECK(sink.GetFuncInfoFromId(DIID__IOpenATLDispatchEvents, 105, 0, function) == E_NOTIMPL && !function.nParams);
    EVENT_CHECK(sink.GetFuncInfoFromId(DIID__IOpenATLDispatchEvents, 900, 0, function) == DISP_E_MEMBERNOTFOUND);
    EVENT_CHECK(sink.GetFuncInfoFromId(IID_IUnknown, 101, 0, function) == E_NOINTERFACE);
    EVENT_CHECK(sink.GetUserDefinedType(nullptr, 0) == VT_EMPTY);
    ATL::CComPtr<ATL::CComObject<Source>> source; EVENT_CHECK(Make(source));
    EVENT_CHECK(sink.DispEventAdvise(source->GetUnknown()) == S_OK && Scalar(source.p) && sink.correct && sink.changed == 1);
    LONG value = 17;
    VARIANTARG argument{}; argument.vt = VT_BYREF | VT_I4; argument.plVal = &value;
    DISPPARAMS reference{&argument, nullptr, 1, 0};
    EVENT_CHECK(source->Fire(102, reference) == S_OK && value == 18 && sink.adjusts == 1);
    argument.plVal = nullptr; UINT error = 77;
    EVENT_CHECK(source->Fire(102, reference, &error) == DISP_E_TYPEMISMATCH && error == 0);
    ATL::CComVariant state[2] = {ATL::CComVariant(99L), ATL::CComVariant(LONG(EventReady))};
    DISPPARAMS enumeration{state, nullptr, 2, 0};
    EVENT_CHECK(source->Fire(103, enumeration) == S_OK && sink.states == 1);
    ATL::CComVariant owned(L"retained event"); DISPPARAMS bundle{&owned, nullptr, 1, 0};
    EVENT_CHECK(source->Fire(104, bundle) == S_OK && sink.bundles == 1 && sink.retained.vt == VT_BSTR && sink.retained.bstrVal != owned.bstrVal);
    EVENT_CHECK(owned.Clear() == S_OK && std::wcscmp(sink.retained.bstrVal, L"retained event") == 0);
    DISPPARAMS empty{};
    EVENT_CHECK(source->Fire(105, empty) == E_NOTIMPL && !sink.unsupported);
    EVENT_CHECK(sink.Invoke(101, IID_IUnknown, 0, DISPATCH_METHOD, &empty, nullptr, nullptr, nullptr) == DISP_E_UNKNOWNINTERFACE);
    EVENT_CHECK(sink.Invoke(101, IID_NULL, 0, DISPATCH_PROPERTYGET, &empty, nullptr, nullptr, nullptr) == DISP_E_MEMBERNOTFOUND);
    EVENT_CHECK(source->Fire(101, empty) == DISP_E_BADPARAMCOUNT);
    WrongPointee wrong;
    value = 17; argument.plVal = &value;
    EVENT_CHECK(wrong.Invoke(102, IID_NULL, 0, DISPATCH_METHOD, &reference, nullptr, nullptr, nullptr) == E_INVALIDARG && value == 17 && !wrong.calls);
    EVENT_CHECK(sink.DispEventUnadvise(source->GetUnknown()) == S_OK);
    return true;
}
bool BindingAndIdentities() {
    DynamicSink sink;
    ATL::CComPtr<ITypeInfo> absent;
    EVENT_CHECK(sink.GetTypeInfo(0, 0, &absent.p) == TYPE_E_LIBNOTREGISTERED && !absent.p);
    ATL::CComPtr<ATL::CComObject<Source>> first, second;
    EVENT_CHECK(Make(first) && Make(second));
    first->metadata = E_ACCESSDENIED;
    EVENT_CHECK(sink.DispEventAdvise(first->GetUnknown()) == E_ACCESSDENIED);
    first->metadata = S_OK; first->connect = E_ACCESSDENIED;
    EVENT_CHECK(sink.DispEventAdvise(first->GetUnknown()) == E_ACCESSDENIED && sink.GetTypeInfo(0, 0, &absent.p) == TYPE_E_LIBNOTREGISTERED);
    first->connect = S_OK; first->eager = true;
    HRESULT reentrant = S_OK;
    sink.callback = [&] { reentrant = sink.Unadvise(first->GetUnknown()); };
    EVENT_CHECK(sink.Advise(first->GetUnknown()) == S_OK && sink.changed == 1 && sink.correct);
    EVENT_CHECK(reentrant == E_UNEXPECTED);
    sink.callback = {};
    EVENT_CHECK(sink.Advise(first->GetUnknown()) == CONNECT_E_ADVISELIMIT);
    EVENT_CHECK(sink.Unadvise(second->GetUnknown()) == CONNECT_E_NOCONNECTION && Scalar(first.p));
    first->disconnect = E_ACCESSDENIED;
    EVENT_CHECK(sink.Unadvise(first->GetUnknown()) == E_ACCESSDENIED && Scalar(first.p));
    first->disconnect = S_OK;
    EVENT_CHECK(sink.Unadvise(first->GetUnknown()) == S_OK && sink.GetTypeInfo(0, 0, &absent.p) == TYPE_E_LIBNOTREGISTERED);
    EVENT_CHECK(sink.Advise(second->GetUnknown()) == S_OK && Scalar(second.p) && sink.Unadvise(second->GetUnknown()) == S_OK);
    PairSink pair;
    auto& left = static_cast<PairSink::LeftBase&>(pair);
    auto& right = static_cast<PairSink::RightBase&>(pair);
    first->eager = false;
    EVENT_CHECK(left.Advise(first->GetUnknown()) == S_OK && right.Advise(second->GetUnknown()) == S_OK);
    ATL::CComPtr<IUnknown> a, b;
    EVENT_CHECK(left.QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a.p)) == S_OK);
    EVENT_CHECK(right.QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b.p)) == S_OK && a.p != b.p);
    EVENT_CHECK(Scalar(first.p) && pair.left == 1 && pair.right == 0 && Scalar(second.p) && pair.right == 1);
    EVENT_CHECK(left.Unadvise(first->GetUnknown()) == S_OK && Scalar(second.p) && pair.right == 2 && right.Unadvise(second->GetUnknown()) == S_OK);
    { DynamicSink automatic; EVENT_CHECK(automatic.Advise(first->GetUnknown()) == S_OK); }
    ATL::CComPtr<IConnectionPoint> point;
    EVENT_CHECK(first->FindConnectionPoint(DIID__IOpenATLDispatchEvents, &point.p) == S_OK);
    ATL::CComPtr<IEnumConnections> connections;
    CONNECTDATA data{};
    EVENT_CHECK(point->EnumConnections(&connections.p) == S_OK && connections->Next(1, &data, nullptr) == S_FALSE);
    return true;
}
struct Registration {
    bool previous = false, active = false;
    HRESULT Install() {
        HRESULT hr = ATL::AtlGetPerUserRegistration(&previous);
        if (FAILED(hr)) return hr;
        ATL::AtlSetPerUserRegistration(true);
        hr = ATL::AtlRegisterTypeLib(ATL::_AtlBaseModule.GetModuleInstance(), L"\\4");
        ATL::AtlSetPerUserRegistration(previous);
        active = SUCCEEDED(hr); return hr;
    }
    HRESULT Remove() {
        ATL::AtlSetPerUserRegistration(true);
        const HRESULT hr = ATL::AtlUnRegisterTypeLib(ATL::_AtlBaseModule.GetModuleInstance(), L"\\4");
        ATL::AtlSetPerUserRegistration(previous);
        if (SUCCEEDED(hr)) active = false;
        return hr;
    }
    ~Registration() { if (active && FAILED(Remove())) std::terminate(); }
};
bool RegisteredMetadata() {
    ATL::CComPtr<ITypeLib> absent;
    EVENT_CHECK(::LoadRegTypeLib(LIBID_OpenATLEventLib, 1, 0, LOCALE_USER_DEFAULT, &absent.p) == TYPE_E_LIBNOTREGISTERED);
    ExplicitSink<> sink;
    ATL::CComPtr<ITypeInfo> info;
    EVENT_CHECK(sink.GetTypeInfo(0, 0, &info.p) == TYPE_E_LIBNOTREGISTERED && !info.p);
    Registration registration; EVENT_CHECK(registration.Install() == S_OK);
    EVENT_CHECK(sink.GetTypeInfo(0, LOCALE_USER_DEFAULT, &info.p) == S_OK && info.p);
    info.Release();
    ATL::CComPtr<ATL::CComObject<Source>> source; EVENT_CHECK(Make(source));
    EVENT_CHECK(sink.Advise(source->GetUnknown()) == S_OK && Scalar(source.p));
    EVENT_CHECK(registration.Remove() == S_OK && Scalar(source.p)); // Active connection owns its metadata.
    EVENT_CHECK(sink.Unadvise(source->GetUnknown()) == S_OK);
    EVENT_CHECK(sink.GetTypeInfo(0, 0, &info.p) == TYPE_E_LIBNOTREGISTERED && !info.p);
    return true;
}
}
bool RunAtlTypeLibEventTests() {
    ULONG_PTR token = 0;
    EVENT_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    EVENT_CHECK(MetadataAndCalls() && BindingAndIdentities() && DiscoveryFallbacks() && SimpleDiscovery() && MapBinding() && RegisteredMetadata());
    EVENT_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
bool RunDllTypeLibEventTests(IOpenATLValue* object) {
    EVENT_CHECK(SourceInformation(object) && IncomingDispatch(object, 42));
    ATL::CComPtr<IProvideClassInfo2> absent;
    EVENT_CHECK(object->QueryInterface(IID_IProvideClassInfo2, reinterpret_cast<void**>(&absent.p)) == E_NOINTERFACE && !absent.p);
    ExplicitSink<FileHolder> explicitSink;
    DynamicSink inferred;
    SimpleSink<> simple;
    EVENT_CHECK(inferred.Advise(object) == S_OK && explicitSink.Advise(object) == S_OK && simple.Advise(object) == S_OK);
    LONG value = 0;
    EVENT_CHECK(object->GetValue(&value) == S_OK && value == 42 && inferred.correct && inferred.changed == 1 && explicitSink.correct && explicitSink.changed == 1 && explicitSink.bundles == 1);
    EVENT_CHECK(simple.correct && simple.changed == 1 && simple.Unadvise(object) == S_OK);
    ATL::CComPtr<IOpenATLSecond> second;
    EVENT_CHECK(object->QueryInterface(IID_OpenATLSecond, reinterpret_cast<void**>(&second.p)) == S_OK);
    // The inferred sink handles only Changed, so disconnect it before other events.
    EVENT_CHECK(inferred.Unadvise(object) == S_OK);
    EVENT_CHECK(second->GetSecond(&value) == S_OK && value == 18 && explicitSink.adjusts == 1);
    ATL::CComPtr<IOpenATLDerived> derived;
    EVENT_CHECK(object->QueryInterface(IID_OpenATLDerived, reinterpret_cast<void**>(&derived.p)) == S_OK);
    EVENT_CHECK(derived->GetExtra(&value) == S_OK && value == 99 && explicitSink.states == 1);
    EVENT_CHECK(explicitSink.Unadvise(object) == S_OK);
    EVENT_CHECK(explicitSink.retained.vt == VT_BSTR && std::wcscmp(explicitSink.retained.bstrVal, L"retained event") == 0);
    MapSink mapped; mapped.first = object; mapped.second = object;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&mapped, true) == S_OK);
    EVENT_CHECK(object->GetValue(&value) == S_OK && value == 42 && mapped.correct && mapped.left == 1 && mapped.right == 1 && mapped.bundles == 1);
    mapped.first = nullptr; mapped.second = nullptr;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&mapped, false) == S_OK);
    return true;
}
bool RunDllPersistEventTests(IOpenATLValue* object) {
    EVENT_CHECK(SourceInformation(object) && IncomingDispatch(object, 84));
    ATL::CComPtr<IProvideClassInfo> absent;
    EVENT_CHECK(object->QueryInterface(IID_IProvideClassInfo, reinterpret_cast<void**>(&absent.p)) == E_NOINTERFACE && !absent.p);
    DynamicSink sink; sink.expected = 84;
    SimpleSink<> simple; simple.expected = 84;
    EVENT_CHECK(sink.Advise(object) == S_OK && simple.Advise(object) == S_OK);
    LONG value = 0;
    EVENT_CHECK(object->GetValue(&value) == S_OK && value == 84 && sink.correct && sink.changed == 1);
    EVENT_CHECK(simple.correct && simple.changed == 1 && simple.Unadvise(object) == S_OK);
    EVENT_CHECK(sink.Unadvise(object) == S_OK && object->GetValue(&value) == S_OK && value == 84 && sink.changed == 1);
    MapSink mapped; mapped.first = object; mapped.second = object; mapped.expected = 84;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&mapped, true) == S_OK);
    EVENT_CHECK(object->GetValue(&value) == S_OK && value == 84 && mapped.correct && mapped.left == 1 && mapped.right == 1);
    mapped.first = nullptr; mapped.second = nullptr;
    EVENT_CHECK(ATL::AtlAdviseSinkMap(&mapped, false) == S_OK);
    return true;
}
