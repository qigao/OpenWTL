#include <atlctl.h>
#include "interfaces.h"
#include <atomic>
#include <cstdio>
#include <functional>

#define PROP_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_PROPERTY_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
class Source : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
               public ATL::IConnectionPointContainerImpl<Source>,
               public ATL::IPropertyNotifySinkCP<Source>,
               public ATL::IConnectionPointImpl<Source, &IID_OpenATLValueEvents> {
public:
    inline static std::atomic<int> live{0};
    HRESULT enumeration = S_OK;
    Source() { ++live; }
    ~Source() { --live; }
    BEGIN_COM_MAP(Source)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(Source)
        CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
        CONNECTION_POINT_ENTRY(IID_OpenATLValueEvents)
    END_CONNECTION_POINT_MAP()
    HRESULT STDMETHODCALLTYPE EnumConnectionPoints(IEnumConnectionPoints** output) noexcept override {
        if (!output) return E_POINTER; *output = nullptr;
        return enumeration == S_OK ? ATL::IConnectionPointContainerImpl<Source>::EnumConnectionPoints(output) : enumeration;
    }
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
class NoPoints : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IPersist {
public:
    BEGIN_COM_MAP(NoPoints)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) noexcept override {
        if (!output) return E_POINTER; *output = CLSID_NULL; return S_OK;
    }
};
class FixedSource : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                    public ATL::IConnectionPointContainerImpl<FixedSource>,
                    public ATL::IPropertyNotifySinkCP<FixedSource, ATL::CComUnkArray<1>> {
public:
    BEGIN_COM_MAP(FixedSource)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(FixedSource)
        CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
    END_CONNECTION_POINT_MAP()
};
class BrokenPoint : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                    public ATL::IConnectionPointContainerImpl<BrokenPoint>,
                    public ATL::IPropertyNotifySinkCP<BrokenPoint> {
public:
    HRESULT enumeration = E_NOTIMPL;
    BEGIN_COM_MAP(BrokenPoint)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(BrokenPoint)
        CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
    END_CONNECTION_POINT_MAP()
    HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections** output) override {
        if (!output) return E_POINTER; *output = nullptr; return enumeration;
    }
};
template<class T> bool Make(ATL::CComPtr<ATL::CComObject<T>>& result) {
    ATL::CComObject<T>* raw = nullptr;
    PROP_CHECK(ATL::CComObject<T>::CreateInstance(&raw) == S_OK && raw);
    raw->AddRef(); result.Attach(raw); return true;
}
// The outgoing event face is distinct from the property-notification face.
// A dispatcher must QI each connected sink rather than reinterpret its pointer.
class NotifySink : public IPersist, public IPropertyNotifySink, public IOpenATLValueEvents {
public:
    inline static std::atomic<int> live{0};
    std::atomic<ULONG> refs{1};
    HRESULT query = S_OK, request = S_OK, changed = S_OK;
    int requests = 0, changes = 0, events = 0;
    DISPID requested = 0, notified = 0;
    std::function<void()> onRequest, onChanged;
    NotifySink() { ++live; }
    ~NotifySink() { --live; }
    IUnknown* Identity() noexcept { return static_cast<IPersist*>(this); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER; *output = nullptr;
        if (iid == IID_IUnknown || iid == IID_IPersist) *output = static_cast<IPersist*>(this);
        else if (iid == IID_IPropertyNotifySink) {
            if (query != S_OK) return query;
            *output = static_cast<IPropertyNotifySink*>(this);
        } else if (iid == IID_OpenATLValueEvents) *output = static_cast<IOpenATLValueEvents*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = --refs; if (!count) delete this; return count;
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) noexcept override {
        if (!output) return E_POINTER; *output = CLSID_NULL; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnRequestEdit(DISPID member) noexcept override {
        ++requests; requested = member; if (onRequest) onRequest(); return request;
    }
    HRESULT STDMETHODCALLTYPE OnChanged(DISPID member) noexcept override {
        ++changes; notified = member; if (onChanged) onChanged(); return changed;
    }
    HRESULT STDMETHODCALLTYPE OnValue(LONG value) noexcept override { ++events; return value == 42 ? S_OK : E_INVALIDARG; }
};
ATL::CComPtr<NotifySink> NewSink() { ATL::CComPtr<NotifySink> sink; sink.Attach(new NotifySink); return sink; }
struct Subscription {
    ATL::CComPtr<IUnknown> source;
    IID iid{};
    DWORD cookie = 0;
    HRESULT Bind(IUnknown* object, IUnknown* sink, REFIID outgoing) {
        if (cookie) return E_UNEXPECTED;
        source = object; iid = outgoing;
        return ATL::AtlAdvise(source.p, sink, iid, &cookie);
    }
    HRESULT Reset() {
        if (!cookie) return S_OK;
        const HRESULT hr = ATL::AtlUnadvise(source.p, iid, cookie);
        if (SUCCEEDED(hr)) { cookie = 0; source.Release(); }
        return hr;
    }
    ~Subscription() { if (FAILED(Reset())) std::terminate(); }
};
bool ContractsAndFailures() {
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(nullptr, 0) == E_POINTER);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(nullptr, 0) == E_POINTER);
    ATL::CComPtr<ATL::CComObject<NoPoints>> plain; PROP_CHECK(Make(plain));
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(plain->GetUnknown(), DISPID_UNKNOWN) == S_OK);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(plain->GetUnknown(), DISPID_UNKNOWN) == S_OK);
    ATL::CComPtr<ATL::CComObject<EmptySource>> empty; PROP_CHECK(Make(empty));
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(empty->GetUnknown(), 0) == S_OK);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(empty->GetUnknown(), 0) == S_OK);
    auto first = NewSink(), second = NewSink();
    ATL::CComPtr<ATL::CComObject<Source>> source; PROP_CHECK(Make(source));
    Subscription a, b;
    PROP_CHECK(a.Bind(source->GetUnknown(), first->Identity(), IID_IPropertyNotifySink) == S_OK);
    PROP_CHECK(b.Bind(source->GetUnknown(), second->Identity(), IID_OpenATLValueEvents) == S_OK);
    PROP_CHECK(static_cast<void*>(static_cast<IPropertyNotifySink*>(second.p)) != static_cast<void*>(static_cast<IOpenATLValueEvents*>(second.p)));
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(source->GetUnknown(), DISPID_UNKNOWN) == S_OK);
    PROP_CHECK(first->requests == 1 && second->requests == 1 && first->requested == DISPID_UNKNOWN && second->requested == DISPID_UNKNOWN);
    first->request = S_FALSE;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(source->GetUnknown(), 18) == S_FALSE);
    PROP_CHECK(first->requests == 2 && second->requests == 1 && first->changes == 0 && second->changes == 0);
    first->request = E_ACCESSDENIED;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(source->GetUnknown(), 18) == E_ACCESSDENIED && second->requests == 1);
    first->request = S_OK; first->changed = E_NOTIMPL;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), DISPID_UNKNOWN) == S_OK);
    PROP_CHECK(first->changes == 1 && second->changes == 1 && first->notified == DISPID_UNKNOWN && second->notified == DISPID_UNKNOWN && !second->events);
    // Preflight every sink before delivering any notification.
    second->query = E_ACCESSDENIED;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 18) == E_ACCESSDENIED && first->changes == 1);
    second->query = E_NOINTERFACE;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 18) == S_OK && first->changes == 2 && second->changes == 1);
    second->query = S_FALSE;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 18) == E_UNEXPECTED && first->changes == 2);
    second->query = S_OK;
    source->enumeration = E_ACCESSDENIED;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 18) == E_ACCESSDENIED && first->changes == 2);
    source->enumeration = S_OK;
    PROP_CHECK(a.Reset() == S_OK && b.Reset() == S_OK && first->refs == 1 && second->refs == 1);
    ATL::CComPtr<ATL::CComObject<FixedSource>> fixed; PROP_CHECK(Make(fixed));
    Subscription single, overflow;
    PROP_CHECK(single.Bind(fixed->GetUnknown(), first->Identity(), IID_IPropertyNotifySink) == S_OK);
    PROP_CHECK(overflow.Bind(fixed->GetUnknown(), second->Identity(), IID_IPropertyNotifySink) == CONNECT_E_ADVISELIMIT && !overflow.cookie);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(fixed->GetUnknown(), 19) == S_OK && first->notified == 19);
    ATL::CComPtr<ATL::CComObject<BrokenPoint>> broken; PROP_CHECK(Make(broken));
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(broken->GetUnknown(), 18) == E_NOTIMPL);
    broken->enumeration = S_OK;
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(broken->GetUnknown(), 18) == E_UNEXPECTED);
    return true;
}
bool ReentrantSubscriptions() {
    auto first = NewSink(), second = NewSink(), late = NewSink();
    ATL::CComPtr<ATL::CComObject<Source>> source; PROP_CHECK(Make(source));
    Subscription a, b, added;
    PROP_CHECK(a.Bind(source->GetUnknown(), first->Identity(), IID_IPropertyNotifySink) == S_OK);
    PROP_CHECK(b.Bind(source->GetUnknown(), second->Identity(), IID_OpenATLValueEvents) == S_OK);
    bool changed = false, okay = false;
    first->onChanged = [&] {
        if (!changed) { changed = true; okay = b.Reset() == S_OK && added.Bind(source->GetUnknown(), late->Identity(), IID_IPropertyNotifySink) == S_OK; }
    };
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 23) == S_OK && changed && okay);
    PROP_CHECK(first->changes == 1 && second->changes == 1 && late->changes == 0);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 24) == S_OK);
    PROP_CHECK(first->changes == 2 && second->changes == 1 && late->changes == 1 && late->notified == 24);
    return true;
}
bool DuplicateSubscriptions() {
    auto sink = NewSink();
    ATL::CComPtr<ATL::CComObject<Source>> source; PROP_CHECK(Make(source));
    Subscription first, second, otherPoint;
    PROP_CHECK(first.Bind(source->GetUnknown(), sink->Identity(), IID_IPropertyNotifySink) == S_OK);
    PROP_CHECK(second.Bind(source->GetUnknown(), sink->Identity(), IID_IPropertyNotifySink) == S_OK);
    PROP_CHECK(otherPoint.Bind(source->GetUnknown(), sink->Identity(), IID_OpenATLValueEvents) == S_OK);
    PROP_CHECK(first.cookie != second.cookie);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnRequestEdit(source->GetUnknown(), 25) == S_OK && sink->requests == 3);
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(source->GetUnknown(), 25) == S_OK && sink->changes == 3);
    PROP_CHECK(first.Reset() == S_OK && second.Reset() == S_OK && otherPoint.Reset() == S_OK && sink->refs == 1);
    return true;
}
bool SenderLifetime() {
    ATL::CAtlModule module;
    auto sink = NewSink();
    ATL::CComPtr<ATL::CComObject<Source>> source; PROP_CHECK(Make(source));
    IUnknown* identity = source->GetUnknown(); DWORD cookie = 0;
    PROP_CHECK(ATL::AtlAdvise(identity, sink->Identity(), IID_IPropertyNotifySink, &cookie) == S_OK);
    HRESULT nested = E_FAIL;
    sink->onChanged = [&] { source.Release(); nested = ATL::CFirePropNotifyEvent::FireOnRequestEdit(identity, 27); };
    PROP_CHECK(ATL::CFirePropNotifyEvent::FireOnChanged(identity, 26) == S_OK && nested == S_OK);
    PROP_CHECK(!source.p && Source::live == 0 && sink->refs == 1 && sink->changes == 1 && sink->requests == 1 && module.GetLockCount() == 0);
    return true;
}
}
bool RunAtlPropertyNotifyTests() {
    ULONG_PTR token = 0;
    PROP_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    PROP_CHECK(ContractsAndFailures() && ReentrantSubscriptions() && DuplicateSubscriptions() && SenderLifetime());
    PROP_CHECK(Source::live == 0 && NotifySink::live == 0);
    PROP_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
bool RunDllPropertyNotifyTests(IOpenATLValue* object) {
    ATL::CComPtr<IOpenATLProperty> property;
    PROP_CHECK(object->QueryInterface(IID_OpenATLProperty, reinterpret_cast<void**>(&property.p)) == S_OK);
    PROP_CHECK(property->get_Property(nullptr) == E_POINTER);
    auto first = NewSink(), otherPoint = NewSink();
    Subscription a, b;
    PROP_CHECK(a.Bind(object, first->Identity(), IID_IPropertyNotifySink) == S_OK);
    PROP_CHECK(b.Bind(object, otherPoint->Identity(), IID_OpenATLValueEvents) == S_OK);
    LONG value = 0;
    PROP_CHECK(property->get_Property(&value) == S_OK && value == 17);
    bool beforeCommit = false, afterCommit = false;
    first->onRequest = [&] { LONG current = 0; beforeCommit = property->get_Property(&current) == S_OK && current == 17; };
    first->onChanged = [&] { LONG current = 0; afterCommit = property->get_Property(&current) == S_OK && current == 31; };
    first->request = S_FALSE;
    PROP_CHECK(property->put_Property(31) == S_FALSE && beforeCommit && !afterCommit && !first->changes && !otherPoint->changes);
    PROP_CHECK(property->get_Property(&value) == S_OK && value == 17);
    first->request = E_ACCESSDENIED;
    PROP_CHECK(property->put_Property(31) == E_ACCESSDENIED && !otherPoint->requests);
    first->request = S_OK; first->changed = E_NOTIMPL;
    PROP_CHECK(property->put_Property(31) == S_OK && beforeCommit && afterCommit);
    PROP_CHECK(property->get_Property(&value) == S_OK && value == 31 && first->changes == 1 && otherPoint->changes == 1);
    PROP_CHECK(first->requested == DISPID_OpenATLProperty && first->notified == DISPID_OpenATLProperty && otherPoint->notified == DISPID_OpenATLProperty && !otherPoint->events);
    PROP_CHECK(a.Reset() == S_OK && b.Reset() == S_OK && first->refs == 1 && otherPoint->refs == 1);
    first->onRequest = {}; first->onChanged = {};
    PROP_CHECK(property->put_Property(17) == S_OK); // Restore this caller-owned fixture instance.
    return true;
}
