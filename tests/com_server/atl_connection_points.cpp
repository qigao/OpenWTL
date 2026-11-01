#include <atlcom.h>
#include "interfaces.h"
#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#define CP_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_CONNECTION_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
inline constexpr IID EventsIID = {0x1d9d6431,0x5413,0x476e,{0x90,0x21,0x62,0x03,0x11,0xd4,0x3e,0xa1}};
std::atomic<int> sources{0};
class Source : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
               public ATL::IConnectionPointContainerImpl<Source>,
               public ATL::IConnectionPointImpl<Source, &EventsIID>,
               public ATL::IConnectionPointImpl<Source, &IID_IPropertyNotifySink, ATL::CComUnkArray<1>> {
public:
    Source() { ++sources; }
    ~Source() { --sources; }
    BEGIN_COM_MAP(Source)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(Source)
        CONNECTION_POINT_ENTRY(EventsIID)
        CONNECTION_POINT_ENTRY_P(&IID_IPropertyNotifySink)
    END_CONNECTION_POINT_MAP()
};
class EmptySource : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                    public ATL::IConnectionPointContainerImpl<EmptySource> {
public:
    BEGIN_COM_MAP(EmptySource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(EmptySource)
    END_CONNECTION_POINT_MAP()
};
class UuidSource : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                   public ATL::IConnectionPointContainerImpl<UuidSource>,
                   public ATL::IConnectionPointImpl<UuidSource, &__uuidof(IDispatch)> {
public:
    BEGIN_COM_MAP(UuidSource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(UuidSource)
        CONNECTION_POINT_ENTRY(__uuidof(IDispatch))
    END_CONNECTION_POINT_MAP()
};
struct ThrowArray : ATL::CComDynamicUnkArray {
    DWORD Add(IUnknown*) { throw std::bad_alloc(); }
};
class FailingSource : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                      public ATL::IConnectionPointContainerImpl<FailingSource>,
                      public ATL::IConnectionPointImpl<FailingSource, &EventsIID, ThrowArray> {
public:
    BEGIN_COM_MAP(FailingSource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(FailingSource)
        CONNECTION_POINT_ENTRY(EventsIID)
    END_CONNECTION_POINT_MAP()
};
class OverrideSource : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                       public ATL::IConnectionPointContainerImpl<OverrideSource>,
                       public ATL::IConnectionPointImpl<OverrideSource, &EventsIID> {
    using Point = ATL::IConnectionPointImpl<OverrideSource, &EventsIID>;
public:
    int advises = 0, unadvises = 0;
    bool fail = false;
    BEGIN_COM_MAP(OverrideSource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(OverrideSource)
        CONNECTION_POINT_ENTRY(EventsIID)
    END_CONNECTION_POINT_MAP()
    STDMETHOD(Advise)(IUnknown* sink, DWORD* cookie) override {
        ++advises;
        if (fail) { if (cookie) *cookie = 99; return E_ACCESSDENIED; }
        return Point::Advise(sink, cookie);
    }
    STDMETHOD(Unadvise)(DWORD cookie) override { ++unadvises; return Point::Unadvise(cookie); }
};
// STDMETHOD includes the MinGW SDK's nothrow attribute. Exercise exception
// translation with an explicitly throwing C++ override, not that COM macro.
class ThrowingOverrideSource : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                               public ATL::IConnectionPointContainerImpl<ThrowingOverrideSource>,
                               public ATL::IConnectionPointImpl<ThrowingOverrideSource, &EventsIID> {
public:
    BEGIN_COM_MAP(ThrowingOverrideSource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(ThrowingOverrideSource)
        CONNECTION_POINT_ENTRY(EventsIID)
    END_CONNECTION_POINT_MAP()
    HRESULT STDMETHODCALLTYPE Advise(IUnknown*, DWORD* cookie) override {
        if (cookie) *cookie = 99;
        throw ATL::CAtlException(E_ACCESSDENIED);
    }
};
template<class T> bool Make(ATL::CComPtr<ATL::CComObject<T>>& result) {
    ATL::CComObject<T>* raw = nullptr;
    CP_CHECK(ATL::CComObject<T>::CreateInstance(&raw) == S_OK && raw);
    raw->AddRef(); result.Attach(raw); return true;
}
// A noncanonical outgoing interface proves that Advise retains the sink's
// actual QI result, not its identity pointer or the caller's original face.
class Sink : public IPersist, public IDispatch {
public:
    std::atomic<ULONG> refs{1};
    int calls = 0;
    LONG last = 0;
    HRESULT queryResult = S_OK;
    std::function<void()> callback;
    std::function<void()> releasing;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid == IID_IUnknown || iid == IID_IPersist) *output = static_cast<IPersist*>(this);
        else if (iid == EventsIID || iid == IID_IDispatch) {
            if (queryResult != S_OK) return queryResult;
            *output = static_cast<IDispatch*>(this);
        } else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG left = --refs;
        if (releasing) { auto action = std::move(releasing); releasing = {}; action(); }
        return left;
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) noexcept override {
        if (!output) return E_POINTER; *output = CLSID_NULL; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags, DISPPARAMS* params,
        VARIANT*, EXCEPINFO*, UINT*) noexcept override {
        if (id != 1 || iid != IID_NULL || flags != DISPATCH_METHOD || !params || params->cArgs != 1 ||
            !params->rgvarg || params->rgvarg[0].vt != VT_I4) return E_INVALIDARG;
        last = params->rgvarg[0].lVal; ++calls;
        if (callback) callback();
        return S_OK;
    }
};
class PropertySink : public IPropertyNotifySink {
public:
    ULONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IPropertyNotifySink) return E_NOINTERFACE;
        *output = static_cast<IPropertyNotifySink*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs; }
    HRESULT STDMETHODCALLTYPE OnChanged(DISPID) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnRequestEdit(DISPID) noexcept override { return S_OK; }
};
class TypedSink : public ATL::IDispEventSimpleImpl<9, TypedSink, &EventsIID> {
public:
    inline static ATL::_ATL_FUNC_INFO signature = {CC_STDCALL, VT_EMPTY, 1, {VT_I4}};
    BEGIN_SINK_MAP(TypedSink)
        SINK_ENTRY_INFO(9, EventsIID, 1, OnValue, &signature)
    END_SINK_MAP()
    void OnValue(LONG value) { last = value; ++calls; }
    LONG last = 0;
    int calls = 0;
};
bool Fire(IConnectionPoint* point, LONG value) {
    ATL::CComPtr<IEnumConnections> snapshot;
    CP_CHECK(point->EnumConnections(&snapshot.p) == S_OK);
    for (;;) {
        CONNECTDATA data{};
        const HRESULT hr = snapshot->Next(1, &data, nullptr);
        if (hr == S_FALSE) break;
        CP_CHECK(hr == S_OK && data.pUnk);
        ATL::CComPtr<IUnknown> release; release.Attach(data.pUnk);
        ATL::CComVariant argument(value);
        DISPPARAMS params{&argument, nullptr, 1, 0};
        CP_CHECK(static_cast<IDispatch*>(data.pUnk)->Invoke(1, IID_NULL, 0, DISPATCH_METHOD,
            &params, nullptr, nullptr, nullptr) == S_OK);
    }
    return true;
}
bool Arrays() {
    Sink sink;
    IUnknown* face = static_cast<IDispatch*>(&sink);
    {
        ATL::CComDynamicUnkArray dynamic;
        CP_CHECK(dynamic.GetSize() == 0 && dynamic.begin() == dynamic.end());
        const DWORD first = dynamic.Add(face), second = dynamic.Add(face);
        CP_CHECK(first && second && first != second && sink.refs == 1);
        CP_CHECK(dynamic.GetAt(0) == face && dynamic.GetAt(-1) == nullptr && !dynamic.GetUnknown(0));
        CP_CHECK(dynamic.GetCookie(dynamic.begin()) == first && dynamic.GetCookie(dynamic.begin() + 1) == second);
        CP_CHECK(dynamic.GetCookie(&face) == first && !dynamic.GetCookie(nullptr));
        CP_CHECK(dynamic.Remove(first) && !dynamic.Remove(first) && dynamic.GetUnknown(second) == face);
        CP_CHECK(dynamic.Add(face) == first && dynamic.GetSize() == dynamic.end() - dynamic.begin());
        for (int n = 0; n < 40; ++n) CP_CHECK(dynamic.Add(face));
        CP_CHECK(dynamic.GetUnknown(second) == face && sink.refs == 1);
        dynamic.clear(); CP_CHECK(dynamic.GetSize() == 0 && dynamic.begin() == dynamic.end() && sink.refs == 1);
        ATL::CComUnkArray<1> fixed;
        CP_CHECK(fixed.Add(face) && !fixed.Add(face) && fixed.GetCookie(fixed.begin()) == 1);
        CP_CHECK(fixed.Remove(1) && !fixed.Remove(1) && fixed.Add(face) == 1 && sink.refs == 1);
    }
    CP_CHECK(sink.refs == 1);
    return true;
}
bool MapsAndFailures() {
    Sink sink;
    PropertySink property;
    ATL::CComPtr<ATL::CComObject<Source>> source;
    CP_CHECK(Make(source));
    CP_CHECK(source->FindConnectionPoint(EventsIID, nullptr) == E_POINTER && source->EnumConnectionPoints(nullptr) == E_POINTER);
    IConnectionPoint* invalid = reinterpret_cast<IConnectionPoint*>(1);
    CP_CHECK(source->FindConnectionPoint(IID_IUnknown, &invalid) == CONNECT_E_NOCONNECTION && !invalid);
    ATL::CComPtr<IConnectionPoint> point, fixed;
    CP_CHECK(source->FindConnectionPoint(EventsIID, &point.p) == S_OK);
    CP_CHECK(source->FindConnectionPoint(IID_IPropertyNotifySink, &fixed.p) == S_OK && point.p != fixed.p);
    IID iid{};
    CP_CHECK(point->GetConnectionInterface(&iid) == S_OK && iid == EventsIID && point->GetConnectionInterface(nullptr) == E_POINTER);
    CP_CHECK(point->GetConnectionPointContainer(nullptr) == E_POINTER && point->EnumConnections(nullptr) == E_POINTER);
    ATL::CComPtr<IConnectionPointContainer> container;
    CP_CHECK(point->GetConnectionPointContainer(&container.p) == S_OK && container.p == static_cast<IConnectionPointContainer*>(source.p));
    ATL::CComPtr<IUnknown> identity;
    CP_CHECK(point->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == point.p);
    void* unknown = reinterpret_cast<void*>(1);
    CP_CHECK(point->QueryInterface(IID_IConnectionPointContainer, &unknown) == E_NOINTERFACE && !unknown);
    CP_CHECK(point->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    ATL::CComPtr<IConnectionPoint> same;
    CP_CHECK(container->FindConnectionPoint(EventsIID, &same.p) == S_OK && same.p == point.p);
    DWORD cookie = 91;
    CP_CHECK(point->Advise(nullptr, &cookie) == E_POINTER && !cookie);
    CP_CHECK(point->Advise(static_cast<IPersist*>(&sink), nullptr) == E_POINTER);
    sink.queryResult = E_ACCESSDENIED;
    CP_CHECK(point->Advise(static_cast<IPersist*>(&sink), &cookie) == CONNECT_E_CANNOTCONNECT && !cookie && sink.refs == 1);
    sink.queryResult = S_OK;
    CP_CHECK(point->Unadvise(0) == CONNECT_E_NOCONNECTION && point->Unadvise(0xffffffff) == CONNECT_E_NOCONNECTION);
    CP_CHECK(fixed->Advise(&property, &cookie) == S_OK && cookie && property.refs == 2);
    DWORD rejected = 91;
    CP_CHECK(fixed->Advise(&property, &rejected) == CONNECT_E_ADVISELIMIT && !rejected && property.refs == 2);
    CP_CHECK(fixed->Unadvise(cookie) == S_OK && fixed->Unadvise(cookie) == CONNECT_E_NOCONNECTION && property.refs == 1);
    ATL::CComPtr<IEnumConnectionPoints> points, clone;
    CP_CHECK(container->EnumConnectionPoints(&points.p) == S_OK);
    IConnectionPoint* results[3]{}; ULONG fetched = 99;
    CP_CHECK(points->Next(2, results, nullptr) == E_POINTER);
    CP_CHECK(points->Next(1, nullptr, &fetched) == E_POINTER && !fetched);
    CP_CHECK(points->Next(1, results, nullptr) == S_OK && results[0] == point.p); results[0]->Release();
    CP_CHECK(points->Clone(nullptr) == E_POINTER && points->Clone(&clone.p) == S_OK);
    CP_CHECK(clone->Next(1, results, &fetched) == S_OK && fetched == 1 && results[0] == fixed.p); results[0]->Release();
    CP_CHECK(points->Next(1, results, nullptr) == S_OK && results[0] == fixed.p); results[0]->Release();
    CP_CHECK(points->Skip(0xffffffff) == S_FALSE && points->Reset() == S_OK);
    CP_CHECK(points->Next(3, results, &fetched) == S_FALSE && fetched == 2);
    results[0]->Release(); results[1]->Release();
    CP_CHECK(points->Next(0, nullptr, nullptr) == S_OK);
    ATL::CComPtr<ATL::CComObject<EmptySource>> empty; CP_CHECK(Make(empty));
    ATL::CComPtr<IEnumConnectionPoints> none;
    CP_CHECK(empty->EnumConnectionPoints(&none.p) == S_OK && none->Next(1, results, &fetched) == S_FALSE && !fetched);
    ATL::CComPtr<ATL::CComObject<UuidSource>> uuid; CP_CHECK(Make(uuid));
    ATL::CComPtr<IConnectionPoint> uuidPoint;
    CP_CHECK(uuid->FindConnectionPoint(IID_IDispatch, &uuidPoint.p) == S_OK);
    ATL::CComPtr<ATL::CComObject<FailingSource>> failure; CP_CHECK(Make(failure));
    ATL::CComPtr<IConnectionPoint> failedPoint;
    CP_CHECK(failure->FindConnectionPoint(EventsIID, &failedPoint.p) == S_OK);
    cookie = 91;
    CP_CHECK(failedPoint->Advise(static_cast<IPersist*>(&sink), &cookie) == E_OUTOFMEMORY && !cookie && sink.refs == 1);
    ATL::CComPtr<ATL::CComObject<OverrideSource>> custom; CP_CHECK(Make(custom));
    ATL::CComPtr<IConnectionPoint> customPoint;
    CP_CHECK(custom->FindConnectionPoint(EventsIID, &customPoint.p) == S_OK);
    custom->fail = true; cookie = 91;
    CP_CHECK(customPoint->Advise(static_cast<IPersist*>(&sink), &cookie) == E_ACCESSDENIED && !cookie && sink.refs == 1);
    custom->fail = false;
    CP_CHECK(customPoint->Advise(static_cast<IPersist*>(&sink), &cookie) == S_OK && custom->advises == 2 && sink.refs == 2);
    CP_CHECK(customPoint->Unadvise(cookie) == S_OK && custom->unadvises == 1 && sink.refs == 1);
    ATL::CComPtr<ATL::CComObject<ThrowingOverrideSource>> throwing; CP_CHECK(Make(throwing));
    ATL::CComPtr<IConnectionPoint> throwingPoint;
    CP_CHECK(throwing->FindConnectionPoint(EventsIID, &throwingPoint.p) == S_OK);
    cookie = 91;
    CP_CHECK(throwingPoint->Advise(static_cast<IPersist*>(&sink), &cookie) == E_ACCESSDENIED && !cookie && sink.refs == 1);
    return true;
}
bool SnapshotsAndLifetime() {
    ATL::CAtlModule module;
    Sink sink;
    ATL::CComPtr<ATL::CComObject<Source>> source; CP_CHECK(Make(source));
    ATL::CComPtr<IConnectionPoint> point; CP_CHECK(source->FindConnectionPoint(EventsIID, &point.p) == S_OK);
    DWORD first = 0, second = 0;
    CP_CHECK(point->Advise(static_cast<IPersist*>(&sink), &first) == S_OK && point->Advise(static_cast<IPersist*>(&sink), &second) == S_OK);
    CP_CHECK(first && second && first != second && sink.refs == 3);
    ATL::CComPtr<IEnumConnections> snapshot, clone;
    CP_CHECK(point->EnumConnections(&snapshot.p) == S_OK && snapshot->Clone(nullptr) == E_POINTER);
    CONNECTDATA data[3]{}; ULONG fetched = 99;
    CP_CHECK(snapshot->Next(2, data, nullptr) == E_POINTER);
    CP_CHECK(snapshot->Next(1, nullptr, &fetched) == E_POINTER && !fetched);
    CP_CHECK(snapshot->Next(1, data, nullptr) == S_OK && data[0].dwCookie == first);
    CP_CHECK(data[0].pUnk == static_cast<IDispatch*>(&sink) && data[0].pUnk != static_cast<IPersist*>(&sink));
    data[0].pUnk->Release();
    CP_CHECK(snapshot->Clone(&clone.p) == S_OK);
    CP_CHECK(point->Unadvise(first) == S_OK && point->Unadvise(second) == S_OK);
    point.Release(); source.Release();
    CP_CHECK(sources == 1 && module.GetLockCount() == 1);
    CP_CHECK(clone->Next(1, data, &fetched) == S_OK && fetched == 1 && data[0].dwCookie == second);
    data[0].pUnk->Release();
    CP_CHECK(snapshot->Next(3, data, &fetched) == S_FALSE && fetched == 1 && data[0].dwCookie == second);
    data[0].pUnk->Release();
    CP_CHECK(snapshot->Reset() == S_OK && snapshot->Skip(1) == S_OK && snapshot->Skip(0xffffffff) == S_FALSE);
    CP_CHECK(clone->Reset() == S_OK && clone->Skip(0) == S_OK);
    snapshot.Release(); CP_CHECK(sources == 1 && sink.refs > 1);
    clone.Release(); CP_CHECK(sources == 0 && sink.refs == 1 && module.GetLockCount() == 0);
    // Destruction releases surviving connections without requiring Unadvise.
    CP_CHECK(Make(source) && source->FindConnectionPoint(EventsIID, &point.p) == S_OK);
    CP_CHECK(point->Advise(static_cast<IPersist*>(&sink), &first) == S_OK);
    point.Release(); source.Release(); CP_CHECK(sources == 0 && sink.refs == 1);
    return true;
}
bool ReentrancyAndTypedSink() {
    ATL::CComPtr<ATL::CComObject<Source>> source; CP_CHECK(Make(source));
    ATL::CComPtr<IConnectionPoint> point; CP_CHECK(source->FindConnectionPoint(EventsIID, &point.p) == S_OK);
    Sink first, second; DWORD one = 0, two = 0;
    CP_CHECK(ATL::AtlAdvise(source.p, static_cast<IPersist*>(&first), EventsIID, &one) == S_OK);
    CP_CHECK(ATL::AtlAdvise(source.p, static_cast<IPersist*>(&second), EventsIID, &two) == S_OK);
    bool disconnected = false, reentered = false;
    first.callback = [&] { disconnected = point->Unadvise(one) == S_OK && point->Unadvise(two) == S_OK; };
    CP_CHECK(Fire(point.p, 73) && disconnected && first.calls == 1 && second.calls == 1 && first.refs == 1 && second.refs == 1);
    first.callback = {};
    CP_CHECK(point->Advise(static_cast<IPersist*>(&first), &one) == S_OK);
    first.releasing = [&] { DWORD fresh = 0; reentered = point->Advise(static_cast<IPersist*>(&second), &fresh) == S_OK && point->Unadvise(fresh) == S_OK; };
    CP_CHECK(point->Unadvise(one) == S_OK && reentered && first.refs == 1 && second.refs == 1);
    {
        TypedSink sink;
        CP_CHECK(sink.DispEventAdvise(source.p) == S_OK && Fire(point.p, 81) && sink.calls == 1 && sink.last == 81);
        CP_CHECK(sink.DispEventUnadvise(source.p) == S_OK);
    }
    return true;
}
bool Threads() {
    ATL::CComPtr<ATL::CComObject<Source>> source; CP_CHECK(Make(source));
    ATL::CComPtr<IConnectionPoint> point; CP_CHECK(source->FindConnectionPoint(EventsIID, &point.p) == S_OK);
    Sink sinks[4]; std::atomic<bool> okay{true};
    std::vector<std::thread> workers;
    // Direct free-threaded C++ calls test the owner's lock, not COM marshaling.
    for (int t = 0; t < 4; ++t) workers.emplace_back([&, t] {
        for (int n = 0; n < 48; ++n) {
            DWORD cookie = 0;
            if (point->Advise(static_cast<IPersist*>(&sinks[t]), &cookie) != S_OK || !cookie) { okay = false; return; }
            ATL::CComPtr<IEnumConnections> snapshot;
            if (point->EnumConnections(&snapshot.p) != S_OK) { okay = false; return; }
            std::vector<DWORD> seen;
            for (;;) {
                CONNECTDATA item{}; const HRESULT hr = snapshot->Next(1, &item, nullptr);
                if (hr == S_FALSE) break;
                if (hr != S_OK || !item.pUnk || !item.dwCookie || std::find(seen.begin(), seen.end(), item.dwCookie) != seen.end()) { okay = false; break; }
                seen.push_back(item.dwCookie); item.pUnk->Release();
            }
            if (point->Unadvise(cookie) != S_OK) { okay = false; return; }
        }
    });
    for (auto& worker : workers) worker.join();
    CP_CHECK(okay);
    for (auto& sink : sinks) CP_CHECK(sink.refs == 1);
    return true;
}
class NativeSink : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IOpenATLValueEvents {
public:
    int calls = 0;
    BEGIN_COM_MAP(NativeSink)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLValueEvents, IOpenATLValueEvents)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE OnValue(LONG value) noexcept override { if (value != 42) return E_INVALIDARG; ++calls; return S_OK; }
};
}
bool RunAtlConnectionPointTests() {
    ULONG_PTR token = 0;
    CP_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    CP_CHECK(Arrays() && MapsAndFailures() && sources == 0 && SnapshotsAndLifetime() && ReentrancyAndTypedSink() && Threads() && sources == 0);
    CP_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
bool RunDllConnectionPointTests(IOpenATLValue* object, IEnumConnections** retained) {
    if (retained) *retained = nullptr;
    ATL::CComPtr<IConnectionPointContainer> container;
    CP_CHECK(object->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container.p)) == S_OK);
    ATL::CComPtr<IConnectionPoint> point;
    CP_CHECK(container->FindConnectionPoint(IID_OpenATLValueEvents, &point.p) == S_OK);
    ATL::CComPtr<IConnectionPointContainer> back;
    CP_CHECK(point->GetConnectionPointContainer(&back.p) == S_OK);
    ATL::CComQIPtr<IUnknown> ownerIdentity(object), backIdentity(back.p);
    CP_CHECK(ownerIdentity.p && ownerIdentity.p == backIdentity.p);
    ATL::CComPtr<ATL::CComObject<NativeSink>> sink; CP_CHECK(Make(sink));
    DWORD cookie = 0;
    CP_CHECK(ATL::AtlAdvise(object, sink.p, IID_OpenATLValueEvents, &cookie) == S_OK && cookie);
    LONG value = 0;
    CP_CHECK(object->GetValue(&value) == S_OK && value == 42 && sink->calls == 1);
    ATL::CComPtr<IEnumConnections> snapshot;
    CP_CHECK(point->EnumConnections(&snapshot.p) == S_OK);
    CP_CHECK(ATL::AtlUnadvise(object, IID_OpenATLValueEvents, cookie) == S_OK);
    CP_CHECK(object->GetValue(&value) == S_OK && sink->calls == 1);
    if (retained) *retained = snapshot.Detach();
    return true;
}
