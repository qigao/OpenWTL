#include "event_fixture_metadata.h"
#include "interfaces.h"
#include <cstdio>
#include <functional>

#define EVENT_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_TYPELIB_EVENT_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
using EventId = _IOpenATLDispatchEvents;
class Source : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
               public ATL::IConnectionPointContainerImpl<Source>,
               public ATL::IConnectionPointImpl<Source, &DIID__IOpenATLDispatchEvents>, public IProvideClassInfo2 {
    using Point = ATL::IConnectionPointImpl<Source, &DIID__IOpenATLDispatchEvents>;
public:
    HRESULT metadata = S_OK, connect = S_OK, disconnect = S_OK;
    bool eager = false;
    BEGIN_COM_MAP(Source)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
        COM_INTERFACE_ENTRY(IProvideClassInfo2)
        COM_INTERFACE_ENTRY2(IProvideClassInfo, IProvideClassInfo2)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(Source)
        CONNECTION_POINT_ENTRY(DIID__IOpenATLDispatchEvents)
    END_CONNECTION_POINT_MAP()
    HRESULT STDMETHODCALLTYPE GetClassInfo(ITypeInfo** output) noexcept override {
        if (!output) return E_POINTER; *output = nullptr;
        return FAILED(metadata) ? metadata : LoadEventFixtureType(CLSID_EventFixtureSource, output);
    }
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
    bool correct = false;
    std::function<void()> callback;
    void Changed(LONG number, VARIANT_BOOL enabled, BSTR text) {
        ++changed; correct = number == 42 && enabled == VARIANT_TRUE && text && std::wcscmp(text, L"typelib event") == 0;
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
    EVENT_CHECK(MetadataAndCalls() && BindingAndIdentities() && RegisteredMetadata());
    EVENT_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
bool RunDllTypeLibEventTests(IOpenATLValue* object) {
    ExplicitSink<FileHolder> explicitSink;
    DynamicSink inferred;
    EVENT_CHECK(inferred.Advise(object) == S_OK && explicitSink.Advise(object) == S_OK);
    LONG value = 0;
    EVENT_CHECK(object->GetValue(&value) == S_OK && value == 42 && inferred.correct && inferred.changed == 1 && explicitSink.correct && explicitSink.changed == 1 && explicitSink.bundles == 1);
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
    return true;
}
