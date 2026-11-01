#include <atlcom.h>
#include "fixture_registry_ids.h"
#include <cwchar>
#include <string>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

bool RunAtlSafeArrayTests();
bool RunAtlConnectionPointTests();
bool RunAtlPropertyNotifyTests();

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_COMPAT_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
inline constexpr IID iidDerived = {0xf9cb25d1, 0x2573, 0x4af4, {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr IID iidOther = {0xf9cb25d2, 0x2573, 0x4af4, {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr CLSID classId = {0xf9cb25d3, 0x2573, 0x4af4, {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
struct IDerived : IPersist {};
struct IOther : IPersist {};
std::atomic<int> live{0}, released{0};
int failure = 0;
OpenATL::COM::Module* observedModule = nullptr;
bool pinnedDuringDestruction = true;

class Compatible : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                   public ATL::CComCoClass<Compatible, &classId>,
                   public IDerived, public IOther, public IClassFactory {
public:
    DECLARE_NOT_AGGREGATABLE(Compatible)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    BEGIN_COM_MAP(Compatible)
        COM_INTERFACE_ENTRY_IID(iidDerived, IDerived)
        COM_INTERFACE_ENTRY2(IPersist, IDerived)
        COM_INTERFACE_ENTRY_IID(iidOther, IOther)
        COM_INTERFACE_ENTRY(IClassFactory)
    END_COM_MAP()
    Compatible() {
        if (failure == 4) throw std::bad_alloc();
        ++live;
    }
    ~Compatible() {
        if (observedModule && observedModule->CanUnloadNow() != S_FALSE)
            pinnedDuringDestruction = false;
        --live;
    }
    HRESULT FinalConstruct() {
        // A temporary QI/Release during construction must not destroy self.
        ATL::CComPtr<IUnknown> self;
        HRESULT hr = _InternalQueryInterface(IID_IUnknown, reinterpret_cast<void**>(&self));
        if (FAILED(hr)) return hr;
        if (failure == 1) return E_ACCESSDENIED;
        if (failure == 2) throw ATL::CAtlException(E_ABORT);
        if (failure == 3) throw std::runtime_error("FinalConstruct");
        return S_OK;
    }
    void FinalRelease() noexcept { ++released; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override {
        if (!id) return E_POINTER;
        *id = GetObjectCLSID(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*, REFIID, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr; return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return E_NOTIMPL; }
};

inline constexpr CLSID chainClassId = {0xf9cb25d4, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};

// Like Microsoft's COMMap CChainBase/COuter pattern, the derived object
// exposes an outer interface, then chains an inherited base COM map.
// Chaining must adjust MI pointers and never add an extra QI reference.
class ChainBase : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                  public IPersist {
public:
    BEGIN_COM_MAP(ChainBase)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = chainClassId;
        return S_OK;
    }
};
class ChainOuter : public ChainBase, public IClassFactory {
public:
    BEGIN_COM_MAP(ChainOuter)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_CHAIN(ChainBase)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*, REFIID,
                                              void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return E_NOTIMPL; }
};
class ChainOnly : public ChainBase {
public:
    BEGIN_COM_MAP(ChainOnly)
        COM_INTERFACE_ENTRY_CHAIN(ChainBase)
    END_COM_MAP()
};
bool ComMapChain() {
    {
        ATL::CComObject<ChainOuter>* raw = nullptr;
        CHECK(ATL::CComObject<ChainOuter>::CreateInstance(&raw) == S_OK);
        CHECK(raw != nullptr && raw->AddRef() == 1);
        ATL::CComPtr<IClassFactory> factory;
        ATL::CComPtr<IPersist> inherited;
        ATL::CComPtr<IUnknown> canonical, chainedIdentity;
        CHECK(raw->QueryInterface(IID_IClassFactory,
              reinterpret_cast<void**>(&factory.p)) == S_OK);
        CHECK(raw->QueryInterface(IID_IPersist,
              reinterpret_cast<void**>(&inherited.p)) == S_OK);
        CHECK(inherited.p == static_cast<IPersist*>(
              static_cast<ChainBase*>(raw)));
        CHECK(static_cast<IUnknown*>(factory.p) !=
              static_cast<IUnknown*>(inherited.p)); // Real MI adjustment.
        CHECK(factory->QueryInterface(IID_IUnknown,
              reinterpret_cast<void**>(&canonical.p)) == S_OK);
        CHECK(inherited->QueryInterface(IID_IUnknown,
              reinterpret_cast<void**>(&chainedIdentity.p)) == S_OK);
        CHECK(canonical.p == chainedIdentity.p);
        CHECK(canonical.p == raw->GetUnknown()); // Outer first entry wins.
        CLSID clsid{};
        CHECK(inherited->GetClassID(&clsid) == S_OK &&
              IsEqualCLSID(clsid, chainClassId));
        void* unsupported = reinterpret_cast<void*>(1);
        CHECK(raw->QueryInterface(IID_IStream, &unsupported) == E_NOINTERFACE &&
              !unsupported);
        CHECK(raw->QueryInterface(IID_IPersist, nullptr) == E_POINTER);
        chainedIdentity.Release();
        canonical.Release();
        inherited.Release();
        factory.Release();
        CHECK(raw->Release() == 0); // Chain did not AddRef twice.
    }
    {
        ATL::CComObject<ChainOnly>* raw = nullptr;
        CHECK(ATL::CComObject<ChainOnly>::CreateInstance(&raw) == S_OK);
        CHECK(raw != nullptr && raw->AddRef() == 1);
        ATL::CComPtr<IPersist> inherited;
        CHECK(raw->QueryInterface(IID_IPersist,
              reinterpret_cast<void**>(&inherited.p)) == S_OK);
        ATL::CComPtr<IUnknown> identity;
        CHECK(raw->QueryInterface(IID_IUnknown,
              reinterpret_cast<void**>(&identity.p)) == S_OK);
        CHECK(identity.p == static_cast<IUnknown*>(inherited.p) &&
              identity.p == raw->GetUnknown());
        identity.Release();
        inherited.Release();
        CHECK(raw->Release() == 0);
    }
    return true;
}
inline constexpr CLSID aggregateClassId = {0xf9cb25d5, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr CLSID onlyAggregateClassId = {0xf9cb25d6, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr CLSID noAggregateClassId = {0xf9cb25d7, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
int aggregateLive = 0, aggregateReleased = 0, aggregateFailure = 0;
OpenATL::COM::Module* aggregateModule = nullptr;
bool aggregatePinned = true;

class AggregateValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                       public ATL::CComCoClass<AggregateValue, &aggregateClassId>,
                       public IPersist {
public:
    DECLARE_AGGREGATABLE(AggregateValue)
    DECLARE_GET_CONTROLLING_UNKNOWN()
    BEGIN_COM_MAP(AggregateValue)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    AggregateValue() {
        if (aggregateFailure == 4) throw std::bad_alloc();
        ++aggregateLive;
    }
    ~AggregateValue() {
        if (aggregateModule && aggregateModule->CanUnloadNow() != S_FALSE)
            aggregatePinned = false;
        --aggregateLive;
    }
    HRESULT FinalConstruct() {
        // Interface QI must temporarily retain the controlling outer, while
        // the nondelegating inner reference count stays independent.
        ATL::CComPtr<IPersist> temporary;
        HRESULT hr = _InternalQueryInterface(IID_IPersist,
            reinterpret_cast<void**>(&temporary.p));
        if (FAILED(hr)) return hr;
        if (aggregateFailure == 1) return E_ACCESSDENIED;
        if (aggregateFailure == 2) throw ATL::CAtlException(E_ABORT);
        if (aggregateFailure == 3) throw std::runtime_error("aggregate FinalConstruct");
        return S_OK;
    }
    void FinalRelease() noexcept { ++aggregateReleased; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = aggregateClassId;
        return S_OK;
    }
};
class OnlyAggregateValue : public AggregateValue {
public:
    DECLARE_ONLY_AGGREGATABLE(OnlyAggregateValue)
};
class NoAggregateValue : public AggregateValue {
public:
    DECLARE_NOT_AGGREGATABLE(NoAggregateValue)
};

// A stack owner intentionally never deletes itself; refcount equality after
// every operation exposes leaks/double AddRef without test-owned lifetimes
// hiding a broken controlling identity. It owns only the inner IUnknown.
class AggregateOuter : public IUnknown {
public:
    std::atomic<ULONG> refs{1};
    ATL::CComPtr<IUnknown> inner;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            *output = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(iid, output) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs; }
};

// Both specific and blind aggregate map entries return an already-owned
// interface from inner QI. Chained maps must propagate that ownership too.
class AggregateMap : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                     public IClassFactory {
public:
    ATL::CComPtr<IUnknown> inner;
    BEGIN_COM_MAP(AggregateMap)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IPersist, inner)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*, REFIID, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr; return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return E_NOTIMPL; }
};
class AggregateBlindMap : public AggregateMap {
public:
    BEGIN_COM_MAP(AggregateBlindMap)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AGGREGATE_BLIND(inner)
    END_COM_MAP()
};
class AggregateChainedMap : public AggregateMap {
public:
    BEGIN_COM_MAP(AggregateChainedMap)
        COM_INTERFACE_ENTRY_CHAIN(AggregateMap)
    END_COM_MAP()
};

template<class Outer>
bool AggregateMapIdentity() {
    ATL::CComObject<Outer>* object = nullptr;
    CHECK(ATL::CComObject<Outer>::CreateInstance(&object) == S_OK && object);
    CHECK(object->AddRef() == 1);
    void* missing = reinterpret_cast<void*>(1);
    CHECK(object->QueryInterface(IID_IPersist, &missing) == E_NOINTERFACE && !missing);
    CHECK(AggregateValue::CreateInstance(object->GetControllingUnknown(), IID_IUnknown,
        reinterpret_cast<void**>(&object->inner.p)) == S_OK);
    ATL::CComPtr<IPersist> value;
    CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK);
    ATL::CComPtr<IUnknown> identity;
    CHECK(value->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK &&
          identity.p == object->GetUnknown());
    CLSID id{};
    CHECK(value->GetClassID(&id) == S_OK && IsEqualCLSID(id, aggregateClassId));
    CHECK(object->QueryInterface(IID_IStream, &missing) == E_NOINTERFACE && !missing);
    identity.Release();
    value.Release();
    CHECK(object->Release() == 0 && aggregateLive == 0); // No double QI AddRef/cycle.
    return true;
}

bool Aggregation() {
    AggregateOuter outer;
    using Raw = ATL::CComAggObject<AggregateValue>;
    Raw* raw = reinterpret_cast<Raw*>(1);
    CHECK(Raw::CreateInstance(nullptr, &raw) == E_INVALIDARG && !raw);
    CHECK(Raw::CreateInstance(&outer, nullptr) == E_POINTER);
    CHECK(Raw::CreateInstance(&outer, &raw) == S_OK && raw && outer.refs == 1);
    CHECK(raw->AddRef() == 1 && raw->m_contained.GetControllingUnknown() == &outer);
    ATL::CComPtr<IPersist> value;
    CHECK(raw->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK && outer.refs == 2);
    ATL::CComPtr<IUnknown> identity;
    CHECK(value->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK &&
          identity.p == &outer && outer.refs == 3);
    ATL::CComPtr<IUnknown> innerIdentity;
    CHECK(raw->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&innerIdentity.p)) == S_OK &&
          innerIdentity.p == static_cast<IUnknown*>(raw) && innerIdentity.p != identity.p && outer.refs == 3);
    innerIdentity.Release(); identity.Release(); value.Release();
    CHECK(outer.refs == 1 && raw->Release() == 0 && aggregateLive == 0);

    const OpenATL::COM::ClassEntry entries[] = {
        {aggregateClassId, nullptr, &ATL::detail::AutoClassCreator<AggregateValue>::CreateInstance},
        {onlyAggregateClassId, nullptr, &ATL::detail::AutoClassCreator<OnlyAggregateValue>::CreateInstance},
        {noAggregateClassId, nullptr, &ATL::detail::AutoClassCreator<NoAggregateValue>::CreateInstance}
    };
    const OpenATL::COM::ClassEntry invalid[] = {
        {aggregateClassId, nullptr, nullptr},
        {aggregateClassId, &ATL::CComObject<AggregateValue>::CreateInstance,
             &ATL::detail::AutoClassCreator<AggregateValue>::CreateInstance}
    };
    for (const auto& entry : invalid) {
        OpenATL::COM::Module rejected(&entry, 1);
        CHECK(rejected.Status() == E_INVALIDARG);
    }
    OpenATL::COM::Module module(entries, 3);
    CHECK(module.Status() == S_OK);
    aggregateModule = &module;
    for (const auto& entry : entries) {
        ATL::CComPtr<IClassFactory> factory;
        CHECK(module.GetClassObject(entry.clsid, IID_IClassFactory,
            reinterpret_cast<void**>(&factory.p)) == S_OK);
        void* output = reinterpret_cast<void*>(1);
        CHECK(factory->CreateInstance(&outer, IID_IPersist, &output) == CLASS_E_NOAGGREGATION &&
              !output && outer.refs == 1 && aggregateLive == 0);
        CHECK(factory->CreateInstance(&outer, IID_IUnknown, nullptr) == E_POINTER);
        if (IsEqualCLSID(entry.clsid, noAggregateClassId)) {
            CHECK(factory->CreateInstance(&outer, IID_IUnknown, &output) == CLASS_E_NOAGGREGATION && !output);
        } else {
            CHECK(factory->CreateInstance(&outer, IID_IUnknown,
                reinterpret_cast<void**>(&outer.inner.p)) == S_OK && outer.refs == 1 && aggregateLive == 1);
            CHECK(outer.QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK && outer.refs == 2);
            CHECK(value->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK &&
                  identity.p == &outer && outer.refs == 3);
            identity.Release(); value.Release();
            CHECK(outer.refs == 1);
            outer.inner.Release();
        }
        HRESULT expected = IsEqualCLSID(entry.clsid, onlyAggregateClassId) ? E_FAIL : S_OK;
        CHECK(factory->CreateInstance(nullptr, IID_IPersist,
              reinterpret_cast<void**>(&value.p)) == expected);
        value.Release(); factory.Release();
        CHECK(module.CanUnloadNow() == S_OK && aggregateLive == 0 && outer.refs == 1);
    }
    const HRESULT errors[] = {S_OK, E_ACCESSDENIED, E_ABORT, E_UNEXPECTED, E_OUTOFMEMORY};
    for (aggregateFailure = 1; aggregateFailure <= 4; ++aggregateFailure) {
        const int releasedBefore = aggregateReleased;
        void* output = reinterpret_cast<void*>(1);
        CHECK(ATL::detail::AutoClassCreator<AggregateValue>::CreateInstance(
            module, &outer, IID_IUnknown, &output) == errors[aggregateFailure] && !output);
        CHECK(aggregateReleased == releasedBefore + (aggregateFailure == 4 ? 0 : 1));
        CHECK(aggregateLive == 0 && outer.refs == 1 && module.CanUnloadNow() == S_OK);
    }
    aggregateFailure = 0;
    // An object must pin the engine even after its factory has retired.
    ATL::CComPtr<IClassFactory> factory;
    CHECK(module.GetClassObject(aggregateClassId, IID_IClassFactory,
        reinterpret_cast<void**>(&factory.p)) == S_OK);
    CHECK(factory->CreateInstance(&outer, IID_IUnknown,
        reinterpret_cast<void**>(&outer.inner.p)) == S_OK);
    factory.Release();
    CHECK(module.CanUnloadNow() == S_FALSE);
    outer.inner.Release();
    CHECK(module.CanUnloadNow() == S_OK && aggregatePinned && aggregateLive == 0 && outer.refs == 1);
    aggregateModule = nullptr;
    CHECK(AggregateMapIdentity<AggregateMap>());
    CHECK(AggregateMapIdentity<AggregateBlindMap>());
    CHECK(AggregateMapIdentity<AggregateChainedMap>());
    return true;
}

inline constexpr CLSID polyClassId = {0xf9cb25e5, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
std::atomic<int> polyLive{0}, polyFinalized{0};
int polyFailure = 0;
bool polyPinned = true;
OpenATL::COM::Module* polyModule = nullptr;
class PolyValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                  public ATL::CComCoClass<PolyValue, &polyClassId>, public IPersist {
public:
    DECLARE_POLY_AGGREGATABLE(PolyValue)
    DECLARE_GET_CONTROLLING_UNKNOWN()
    BEGIN_COM_MAP(PolyValue)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    PolyValue() {
        if (polyFailure == 4) throw std::bad_alloc();
        ++polyLive;
    }
    ~PolyValue() {
        if (polyModule && polyModule->CanUnloadNow() != S_FALSE) polyPinned = false;
        --polyLive;
    }
    HRESULT FinalConstruct() {
        // The same temporary public interface must be safe in both modes.
        ATL::CComPtr<IPersist> temporary;
        HRESULT hr = _InternalQueryInterface(IID_IPersist, reinterpret_cast<void**>(&temporary.p));
        if (FAILED(hr)) return hr;
        if (polyFailure == 1) return E_ACCESSDENIED;
        if (polyFailure == 2) throw ATL::CAtlException(E_ABORT);
        if (polyFailure == 3) throw std::runtime_error("poly FinalConstruct");
        return S_OK;
    }
    void FinalRelease() noexcept { ++polyFinalized; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = polyClassId;
        return S_OK;
    }
};
bool PolyObjects() {
    using Raw = ATL::CComPolyObject<PolyValue>;
    static_assert(std::is_same<PolyValue::_CreatorClass, ATL::CComCreator<Raw>>::value,
        "Both modes must select the same wrapper creator");
    static_assert(std::is_same<decltype(std::declval<Raw>().m_contained),
        ATL::CComContainedObject<PolyValue>>::value, "Published contained member type");
    AggregateOuter outer;
    Raw* raw = nullptr;
    CHECK(Raw::CreateInstance(nullptr, nullptr) == E_POINTER);
    for (bool aggregated : {false, true}) {
        CHECK(Raw::CreateInstance(aggregated ? &outer : nullptr, &raw) == S_OK && raw);
        CHECK(raw->AddRef() == 1 && outer.refs == 1); // Raw private count was zero.
        auto* control = aggregated ? static_cast<IUnknown*>(&outer) : static_cast<IUnknown*>(raw);
        CHECK(raw->m_contained.GetControllingUnknown() == control);
        ATL::CComPtr<IPersist> face;
        CHECK(raw->QueryInterface(&face.p) == S_OK && face.p);
        ATL::CComPtr<IUnknown> canonical, privateIdentity;
        CHECK(face->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK && canonical.p == control);
        CHECK(raw->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&privateIdentity.p)) == S_OK &&
            privateIdentity.p == static_cast<IUnknown*>(raw));
        CHECK(outer.refs == (aggregated ? 3u : 1u));
        void* missing = reinterpret_cast<void*>(1);
        CHECK(raw->QueryInterface(IID_IStream, &missing) == E_NOINTERFACE && !missing);
        CHECK(raw->QueryInterface(IID_IPersist, nullptr) == E_POINTER);
        privateIdentity.Release(); canonical.Release(); face.Release();
        CHECK(outer.refs == 1 && raw->Release() == 0 && polyLive == 0);
    }
    ATL::CComPtr<IPersist> direct;
    CHECK(PolyValue::CreateInstance(&direct.p) == S_OK);
    CLSID returned{};
    CHECK(direct->GetClassID(&returned) == S_OK && IsEqualCLSID(returned, polyClassId));
    direct.Release();
    CHECK(PolyValue::CreateInstance(&outer, &outer.inner.p) == S_OK && outer.refs == 1);
    CHECK(outer.QueryInterface(IID_IPersist, reinterpret_cast<void**>(&direct.p)) == S_OK && outer.refs == 2);
    direct.Release(); outer.inner.Release();
    void* missing = reinterpret_cast<void*>(1);
    CHECK(PolyValue::CreateInstance(&outer, IID_IPersist, &missing) == CLASS_E_NOAGGREGATION && !missing && !polyLive);

    const OpenATL::COM::ClassEntry entry{polyClassId, nullptr,
        &ATL::detail::AutoClassCreator<PolyValue>::CreateInstance};
    OpenATL::COM::Module module(&entry, 1);
    polyModule = &module;
    for (bool aggregated : {false, true}) {
        ATL::CComPtr<IClassFactory> factory;
        CHECK(module.GetClassObject(polyClassId, IID_IClassFactory, reinterpret_cast<void**>(&factory.p)) == S_OK);
        CHECK(factory->CreateInstance(aggregated ? &outer : nullptr, IID_IUnknown,
            reinterpret_cast<void**>(&outer.inner.p)) == S_OK);
        factory.Release();
        CHECK(module.CanUnloadNow() == S_FALSE && polyLive == 1 && outer.refs == 1);
        CHECK(outer.inner->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&direct.p)) == S_OK);
        ATL::CComPtr<IUnknown> identity;
        CHECK(direct->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK &&
            identity.p == (aggregated ? static_cast<IUnknown*>(&outer) : outer.inner.p));
        if (!aggregated) {
            // Retired factory, concurrent public references, same private root.
            std::atomic<bool> good{true};
            IPersist* shared = direct.p; // Owning pointer remains live until every caller joins.
            std::vector<std::thread> callers;
            for (int n = 0; n < 8; ++n) callers.emplace_back([&] {
                for (int i = 0; i < 100; ++i) {
                    shared->AddRef();
                    CLSID id{};
                    if (shared->GetClassID(&id) != S_OK || !IsEqualCLSID(id, polyClassId)) good = false;
                    shared->Release();
                }
            });
            for (auto& caller : callers) caller.join();
            CHECK(good);
        }
        identity.Release(); direct.Release(); outer.inner.Release();
        CHECK(polyLive == 0 && outer.refs == 1 && module.CanUnloadNow() == S_OK);
    }
    const HRESULT failures[] = {S_OK, E_ACCESSDENIED, E_ABORT, E_UNEXPECTED, E_OUTOFMEMORY};
    for (bool aggregated : {false, true}) {
        for (int mode = 0; mode <= 4; ++mode) {
            polyFailure = mode;
            const int finalized = polyFinalized;
            missing = reinterpret_cast<void*>(1);
            const IID& requested = mode == 0 ? IID_IStream : IID_IUnknown;
            // Unsupported IID is a nonaggregated creation failure; aggregation
            // rejects non-IUnknown requests before allocation.
            const HRESULT expected = mode == 0 ? (aggregated ? CLASS_E_NOAGGREGATION : E_NOINTERFACE) : failures[mode];
            CHECK(ATL::detail::AutoClassCreator<PolyValue>::CreateInstance(module,
                aggregated ? &outer : nullptr, requested, &missing) == expected && !missing);
            const int retired = mode == 4 || (mode == 0 && aggregated) ? 0 : 1;
            CHECK(polyFinalized == finalized + retired && !polyLive && outer.refs == 1 && module.CanUnloadNow() == S_OK);
        }
    }
    polyFailure = 0;
    CHECK(polyPinned);
    polyModule = nullptr;
    return true;
}

inline constexpr CLSID autoAggregateClassId = {0xf9cb25e4, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
std::atomic<int> autoAttempts{0}, autoLive{0}, autoFinalized{0}, autoFailure{0};
std::atomic<bool> autoValid{true};
OpenATL::COM::Module* autoModule = nullptr;
class AutoInner : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                  public ATL::CComCoClass<AutoInner, &autoAggregateClassId>, public IPersist {
public:
    AutoInner() {
        if (autoFailure == 4) throw std::bad_alloc();
        ++autoLive;
    }
    ~AutoInner() {
        if (autoModule && autoModule->CanUnloadNow() != S_FALSE) autoValid = false;
        --autoLive;
    }
    HRESULT FinalConstruct() {
        if (autoFailure == 2) return E_ABORT;
        if (autoFailure == 3) throw std::runtime_error("automatic aggregate FinalConstruct");
        if (autoFailure == 5) {
            void* recursive = reinterpret_cast<void*>(1);
            const HRESULT hr = GetControllingUnknown()->QueryInterface(IID_IPersist, &recursive);
            return hr == E_UNEXPECTED && !recursive ? hr : E_FAIL;
        }
        return S_OK;
    }
    void FinalRelease() noexcept { ++autoFinalized; }
    BEGIN_COM_MAP(AutoInner)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = autoAggregateClassId;
        return S_OK;
    }
};
HRESULT CreateAutoInner(OpenATL::COM::Module& module, IUnknown* outer,
                        REFIID iid, void** output) noexcept {
    ++autoAttempts;
    if (!outer || !IsEqualIID(iid, IID_IUnknown)) {
        autoValid = false;
        if (output) *output = nullptr;
        return E_INVALIDARG;
    }
    if (autoFailure == 1) { *output = nullptr; return E_ACCESSDENIED; }
    return ATL::detail::AutoClassCreator<AutoInner>::CreateInstance(module, outer, iid, output);
}
class AutoSpecific : public AggregateMap {
public:
    void FinalRelease() noexcept { inner.Release(); }
    BEGIN_COM_MAP(AutoSpecific)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_IPersist, inner.p, autoAggregateClassId)
    END_COM_MAP()
};
class AutoChained : public AutoSpecific {
public:
    BEGIN_COM_MAP(AutoChained)
        COM_INTERFACE_ENTRY_CHAIN(AutoSpecific)
    END_COM_MAP()
};
class AutoFirstMissing : public AutoSpecific {
public:
    BEGIN_COM_MAP(AutoFirstMissing)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_IStream, inner, autoAggregateClassId)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_IPersist, inner, autoAggregateClassId)
    END_COM_MAP()
};
class AutoBlind : public AutoSpecific, public IPersist {
public:
    BEGIN_COM_MAP(AutoBlind)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE_BLIND(inner, autoAggregateClassId)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = classId; // Distinguish the fallback from the actual inner.
        return S_OK;
    }
};
class AutoSpecificFallback : public AutoBlind {
public:
    BEGIN_COM_MAP(AutoSpecificFallback)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_IPersist, inner, autoAggregateClassId)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
};

class ExplicitBlindFallback : public AutoBlind {
public:
    BEGIN_COM_MAP(ExplicitBlindFallback)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_AGGREGATE_BLIND(inner)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
};
class RejectingInner : public IUnknown {
public:
    ULONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr; return E_ACCESSDENIED;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};

bool AutoAggregateWithoutCom() {
    ATL::CComObject<AutoSpecific>* object = nullptr;
    CHECK(ATL::CComObject<AutoSpecific>::CreateInstance(&object) == S_OK && object->AddRef() == 1);
    void* output = reinterpret_cast<void*>(1);
    CHECK(object->QueryInterface(IID_IPersist, &output) == CO_E_NOTINITIALIZED && !output && !object->inner.p);
    CHECK(object->QueryInterface(IID_IPersist, nullptr) == E_POINTER);
    CHECK(object->AddRef() == 2 && object->Release() == 1);
    // A supplied private inner is reused even without COM initialization;
    // the macro must never activate or replace a populated slot.
    CHECK(AggregateValue::CreateInstance(object->GetControllingUnknown(), IID_IUnknown,
        reinterpret_cast<void**>(&object->inner.p)) == S_OK);
    ATL::CComPtr<IPersist> value;
    CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK);
    CLSID id{};
    CHECK(value->GetClassID(&id) == S_OK && IsEqualCLSID(id, aggregateClassId));
    value.Release();
    CHECK(object->Release() == 0 && aggregateLive == 0);
    RejectingInner rejecting;
    ATL::CComObject<ExplicitBlindFallback>* blind = nullptr;
    CHECK(ATL::CComObject<ExplicitBlindFallback>::CreateInstance(&blind) == S_OK && blind->AddRef() == 1);
    blind->inner = &rejecting;
    CHECK(blind->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK);
    CHECK(value->GetClassID(&id) == S_OK && IsEqualCLSID(id, classId));
    CHECK(blind->QueryInterface(IID_IStream, &output) == E_NOINTERFACE && !output);
    value.Release(); CHECK(blind->Release() == 0 && rejecting.refs == 1);
    ULONG_PTR token = 0;
    CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}

template<class Owner> bool AutoAggregateIdentity() {
    ATL::CComObject<Owner>* object = nullptr;
    CHECK(ATL::CComObject<Owner>::CreateInstance(&object) == S_OK && object->AddRef() == 1);
    const int before = autoAttempts;
    ATL::CComPtr<IUnknown> canonical;
    CHECK(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK &&
        canonical.p == object->GetUnknown() && !object->inner.p && autoAttempts == before);
    canonical.Release();
    ATL::CComPtr<IPersist> first, repeated;
    CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&first.p)) == S_OK);
    CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&repeated.p)) == S_OK && repeated.p == first.p);
    CHECK(autoAttempts == before + 1 && autoLive == 1 && object->inner.p);
    CHECK(object->AddRef() == 4 && object->Release() == 3); // Exactly one ref per QI.
    CHECK(first->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK &&
        canonical.p == object->GetUnknown());
    canonical.Release();
    ATL::CComPtr<IUnknown> privateIdentity;
    CHECK(object->inner->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&privateIdentity.p)) == S_OK &&
        privateIdentity.p == object->inner.p && privateIdentity.p != object->GetUnknown());
    privateIdentity.Release();
    CHECK(object->Release() == 2); // Public inner interfaces now own the owner.
    CLSID id{};
    CHECK(first->GetClassID(&id) == S_OK && IsEqualCLSID(id, autoAggregateClassId));
    repeated.Release(); first.Release();
    CHECK(autoLive == 0 && autoValid);
    return true;
}

bool AutoAggregateNative(bool multiThreaded) {
    const OpenATL::COM::ClassEntry entry{autoAggregateClassId, nullptr, &CreateAutoInner};
    OpenATL::COM::Module module(&entry, 1);
    autoModule = &module;
    struct Reset { ~Reset() { autoModule = nullptr; autoFailure = 0; } } reset;
    ATL::CComPtr<IClassFactory> factory;
    CHECK(module.GetClassObject(autoAggregateClassId, IID_IClassFactory,
        reinterpret_cast<void**>(&factory.p)) == S_OK);
    struct Registration {
        DWORD cookie = 0;
        ~Registration() { if (cookie) ::CoRevokeClassObject(cookie); }
        HRESULT Revoke() { HRESULT hr = ::CoRevokeClassObject(cookie); if (SUCCEEDED(hr)) cookie = 0; return hr; }
    } registration;
    CHECK(::CoRegisterClassObject(autoAggregateClassId, factory.p, CLSCTX_INPROC_SERVER,
        REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    CHECK(AutoAggregateIdentity<AutoSpecific>());
    CHECK(AutoAggregateIdentity<AutoChained>());
    CHECK(AutoAggregateIdentity<AutoBlind>());
    {
        ATL::CComObject<AutoSpecific>* object = nullptr;
        CHECK(ATL::CComObject<AutoSpecific>::CreateInstance(&object) == S_OK && object->AddRef() == 1);
        const HRESULT errors[] = {S_OK, E_ACCESSDENIED, E_ABORT, E_UNEXPECTED, E_OUTOFMEMORY, E_UNEXPECTED};
        for (int mode = 1; mode <= 5; ++mode) {
            const int before = autoAttempts, finalized = autoFinalized;
            autoFailure = mode;
            void* output = reinterpret_cast<void*>(1);
            CHECK(object->QueryInterface(IID_IPersist, &output) == errors[mode] && !output && !object->inner.p);
            CHECK(autoAttempts == before + 1 && autoLive == 0 &&
                autoFinalized == finalized + (mode == 1 || mode == 4 ? 0 : 1));
            CHECK(object->AddRef() == 2 && object->Release() == 1);
            autoFailure = 0;
            ATL::CComPtr<IPersist> retry;
            CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&retry.p)) == S_OK && autoLive == 1);
            retry.Release(); object->inner.Release(); // Externally owned, no concurrent users.
        }
        CHECK(object->Release() == 0 && autoLive == 0);
    }
    {
        ATL::CComObject<AutoFirstMissing>* object = nullptr;
        CHECK(ATL::CComObject<AutoFirstMissing>::CreateInstance(&object) == S_OK && object->AddRef() == 1);
        const int before = autoAttempts;
        void* output = reinterpret_cast<void*>(1);
        CHECK(object->QueryInterface(IID_IStream, &output) == E_NOINTERFACE && !output && object->inner.p);
        CHECK(autoAttempts == before + 1 && autoLive == 1 && object->AddRef() == 2 && object->Release() == 1);
        ATL::CComPtr<IPersist> value;
        CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK && autoAttempts == before + 1);
        CHECK(object->QueryInterface(IID_IStream, &output) == E_NOINTERFACE && !output && autoAttempts == before + 1);
        value.Release(); CHECK(object->Release() == 0 && autoLive == 0);
    }
    {
        ATL::CComObject<AutoBlind>* object = nullptr;
        CHECK(ATL::CComObject<AutoBlind>::CreateInstance(&object) == S_OK && object->AddRef() == 1);
        autoFailure = 1;
        ATL::CComPtr<IPersist> fallback;
        CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&fallback.p)) == S_OK && !object->inner.p);
        CLSID id{};
        CHECK(fallback->GetClassID(&id) == S_OK && IsEqualCLSID(id, classId));
        fallback.Release();
        void* output = reinterpret_cast<void*>(1);
        CHECK(object->QueryInterface(IID_IStream, &output) == E_NOINTERFACE && !output && !object->inner.p);
        ATL::CComObject<AutoSpecificFallback>* specific = nullptr;
        CHECK(ATL::CComObject<AutoSpecificFallback>::CreateInstance(&specific) == S_OK && specific->AddRef() == 1);
        CHECK(specific->QueryInterface(IID_IPersist, &output) == E_ACCESSDENIED && !output && !specific->inner.p);
        CHECK(specific->Release() == 0);
        autoFailure = 0;
        CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&fallback.p)) == S_OK && object->inner.p);
        CHECK(fallback->GetClassID(&id) == S_OK && IsEqualCLSID(id, autoAggregateClassId));
        fallback.Release(); CHECK(object->Release() == 0 && autoLive == 0);
    }
    {
        AggregateOuter outer;
        ATL::CComAggObject<AutoSpecific>* contained = nullptr;
        CHECK(ATL::CComAggObject<AutoSpecific>::CreateInstance(&outer, &contained) == S_OK && contained->AddRef() == 1);
        outer.inner.Attach(contained);
        ATL::CComPtr<IPersist> value;
        CHECK(outer.QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK);
        ATL::CComPtr<IUnknown> identity;
        CHECK(value->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == &outer);
        identity.Release(); value.Release();
        CHECK(outer.refs == 1);
        outer.inner.Release(); CHECK(autoLive == 0);
    }
    if (multiThreaded) {
        ATL::CComObject<AutoSpecific>* object = nullptr;
        CHECK(ATL::CComObject<AutoSpecific>::CreateInstance(&object) == S_OK && object->AddRef() == 1);
        const int before = autoAttempts;
        std::atomic<void*> selected{nullptr};
        std::atomic<bool> good{true};
        std::vector<std::thread> workers;
        for (int n = 0; n < 8; ++n) workers.emplace_back([&] {
            const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(initialized)) { good = false; return; }
            for (int i = 0; i < 100; ++i) {
                ATL::CComPtr<IPersist> value;
                if (object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) != S_OK || !value.p) {
                    good = false; continue;
                }
                void* expected = nullptr;
                if (!selected.compare_exchange_strong(expected, value.p) && expected != value.p) good = false;
                CLSID id{};
                if (value->GetClassID(&id) != S_OK || !IsEqualCLSID(id, autoAggregateClassId)) good = false;
            }
            ::CoUninitialize();
        });
        for (auto& worker : workers) worker.join();
        CHECK(good && autoAttempts == before + 1 && autoLive == 1);
        CHECK(object->Release() == 0 && autoLive == 0);
    }
    // Retire the registered factory before releasing the last public inner
    // interface: only the actual inner allocation now pins the module.
    ATL::CComObject<AutoSpecific>* retained = nullptr;
    CHECK(ATL::CComObject<AutoSpecific>::CreateInstance(&retained) == S_OK && retained->AddRef() == 1);
    ATL::CComPtr<IPersist> value;
    CHECK(retained->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK);
    CHECK(registration.Revoke() == S_OK);
    factory.Release();
    CHECK(module.CanUnloadNow() == S_FALSE && retained->Release() == 1);
    value.Release();
    CHECK(autoLive == 0 && autoValid && module.CanUnloadNow() == S_OK);
    return true;
}

class ErrorOwner : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                   public ATL::CComCoClass<ErrorOwner, &classId>,
                   public ATL::ISupportErrorInfoImpl<&IID_IPersist>, public IPersist {
public:
    BEGIN_COM_MAP(ErrorOwner)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = classId; return S_OK;
    }
};
bool ErrorFields(const wchar_t* text, REFIID expected, DWORD help = 0,
                  const wchar_t* file = L"") {
    ATL::CComPtr<IErrorInfo> error;
    CHECK(::GetErrorInfo(0, &error.p) == S_OK && error.p);
    GUID iid{};
    CHECK(error->GetGUID(&iid) == S_OK && IsEqualGUID(iid, expected));
    ATL::CComBSTR description, helpFile, source;
    DWORD context = 0;
    CHECK(error->GetDescription(&description.m_str) == S_OK);
    if (std::wstring_view(description.m_str ? description.m_str : L"", description.Length()) != text) {
        std::fprintf(stderr, "ATL_ERROR_DESCRIPTION expected=%zu actual=%u utf16=", std::wcslen(text), description.Length());
        for (UINT i = 0; i < description.Length() && i < 64; ++i)
            std::fprintf(stderr, "%04x ", static_cast<unsigned>(description.m_str[i]));
        std::fputc('\n', stderr);
        CHECK(false);
    }
    CHECK(error->GetHelpFile(&helpFile.m_str) == S_OK &&
        std::wstring_view(helpFile.m_str ? helpFile.m_str : L"", helpFile.Length()) == file);
    CHECK(error->GetHelpContext(&context) == S_OK && context == help);
    CHECK(error->GetSource(&source.m_str) == S_OK && source.Length() == 0); // Unregistered CLSID.
    ATL::CComPtr<IErrorInfo> consumed;
    CHECK(::GetErrorInfo(0, &consumed.p) == S_FALSE && !consumed.p);
    return true;
}
bool NativeErrorInformation() {
    ATL::CComObject<ErrorOwner>* owner = nullptr;
    CHECK(ATL::CComObject<ErrorOwner>::CreateInstance(&owner) == S_OK && owner->AddRef() == 1);
    ATL::CComPtr<ISupportErrorInfo> support;
    CHECK(owner->QueryInterface(IID_ISupportErrorInfo, reinterpret_cast<void**>(&support.p)) == S_OK);
    CHECK(support->InterfaceSupportsErrorInfo(IID_IPersist) == S_OK &&
        support->InterfaceSupportsErrorInfo(IID_IStream) == S_FALSE);
    support.Release(); CHECK(owner->Release() == 0);

    CHECK(ErrorOwner::Error(L"Unicode \u4e2d\u6587") == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"Unicode \u4e2d\u6587", GUID_NULL));
    wchar_t description[] = L"owned description", helpFile[] = L"help.chm";
    CHECK(ErrorOwner::Error(description, 123, helpFile, IID_IPersist, E_ACCESSDENIED) == E_ACCESSDENIED);
    description[0] = L'X'; helpFile[0] = L'X';
    CHECK(ErrorFields(L"owned description", IID_IPersist, 123, L"help.chm"));
    CHECK(ErrorOwner::Error("ANSI description") == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"ANSI description", GUID_NULL));
    char narrow[] = "owned ANSI", narrowHelp[] = "ansi.chm";
    CHECK(ErrorOwner::Error(narrow, 124, narrowHelp, IID_IPersist, S_FALSE) == S_FALSE);
    narrow[0] = 'X'; narrowHelp[0] = 'X';
    CHECK(ErrorFields(L"owned ANSI", IID_IPersist, 124, L"ansi.chm"));
    CHECK(ErrorOwner::Error(UINT(IDS_OPENATL_ERROR)) == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, IDS_OPENATL_ERROR));
    CHECK(ErrorFields(L"OpenWTL resource error \u4e2d\u6587", GUID_NULL));
    CHECK(ErrorOwner::Error(UINT(IDS_OPENATL_ERROR), 125, L"resource.chm", IID_IPersist,
        E_ABORT, ATL::_AtlBaseModule.GetModuleInstance()) == E_ABORT);
    CHECK(ErrorFields(L"OpenWTL resource error \u4e2d\u6587", IID_IPersist, 125, L"resource.chm"));

    CHECK(ATL::AtlReportError(classId, L"global wide", IID_IPersist, E_FAIL) == E_FAIL);
    CHECK(ErrorFields(L"global wide", IID_IPersist));
    CHECK(ATL::AtlReportError(classId, L"global help", 126, L"global.chm") == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"global help", GUID_NULL, 126, L"global.chm"));
    CHECK(ATL::AtlReportError(classId, "global ANSI") == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"global ANSI", GUID_NULL));
    CHECK(ATL::AtlReportError(classId, "global ANSI help", 127, "global-ansi.chm", IID_IPersist) == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"global ANSI help", IID_IPersist, 127, L"global-ansi.chm"));
    CHECK(ATL::AtlReportError(classId, UINT(IDS_OPENATL_ERROR), IID_IPersist, E_POINTER) == E_POINTER);
    CHECK(ErrorFields(L"OpenWTL resource error \u4e2d\u6587", IID_IPersist));
    CHECK(ATL::AtlReportError(classId, UINT(IDS_OPENATL_ERROR), 128, L"last.chm") ==
        MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, IDS_OPENATL_ERROR));
    CHECK(ErrorFields(L"OpenWTL resource error \u4e2d\u6587", GUID_NULL, 128, L"last.chm"));
    CHECK(ErrorOwner::Error(static_cast<LPCOLESTR>(nullptr)) == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"", GUID_NULL));
#ifdef NDEBUG
    CHECK(ErrorOwner::Error(UINT(0xfffe)) == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0xfffe));
    CHECK(ErrorFields(L"Unknown Error.", GUID_NULL));
#endif
    CHECK(ErrorOwner::Error(UINT(IDS_OPENATL_ERROR_LONG)) ==
        MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, IDS_OPENATL_ERROR_LONG));
    std::wstring longResource;
    for (int i = 0; i < 80; ++i) longResource += L"0123456789abcdef";
    CHECK(ErrorFields(longResource.c_str(), GUID_NULL));
    const std::string longAnsi(4096, 'a');
    CHECK(ErrorOwner::Error(longAnsi.c_str()) == DISP_E_EXCEPTION);
    const std::wstring wideAnsi(4096, L'a');
    CHECK(ErrorFields(wideAnsi.c_str(), GUID_NULL));

    CHECK(ErrorOwner::Error(L"previous") == DISP_E_EXCEPTION);
    ATL::CComPtr<IErrorInfo> previous;
    CHECK(::GetErrorInfo(0, &previous.p) == S_OK && previous.p);
    CHECK(::SetErrorInfo(0, previous.p) == S_OK);
    CHECK(ErrorOwner::Error(L"replacement") == DISP_E_EXCEPTION);
    CHECK(ErrorFields(L"replacement", GUID_NULL));
    ATL::CComBSTR previousText;
    CHECK(previous->GetDescription(&previousText.m_str) == S_OK && std::wcscmp(previousText.m_str, L"previous") == 0);
    previous.Release();

    CHECK(ErrorOwner::Error(L"main pending", 1000, L"thread.chm", IID_IPersist) == DISP_E_EXCEPTION);
    std::atomic<bool> good{true};
    std::vector<std::thread> workers;
    for (DWORD n = 1; n <= 4; ++n) workers.emplace_back([&, n] {
        const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) { good = false; return; }
        for (int i = 0; i < 20; ++i) {
            if (ErrorOwner::Error(L"worker", n, L"worker.chm", IID_IPersist) != DISP_E_EXCEPTION) good = false;
            std::this_thread::yield();
            if (!ErrorFields(L"worker", IID_IPersist, n, L"worker.chm")) good = false;
        }
        ::CoUninitialize();
    });
    for (auto& worker : workers) worker.join();
    CHECK(good && ErrorFields(L"main pending", IID_IPersist, 1000, L"thread.chm"));
    return true;
}

inline constexpr IID iidOrdinaryTear = {0xf9cb25e1, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr IID iidCachedTear = {0xf9cb25e2, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr CLSID tearOwnerClassId = {0xf9cb25e3, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
struct ITearProbe : IUnknown { virtual HRESULT STDMETHODCALLTYPE Read(LONG*) = 0; };
class TearOwner;
std::atomic<int> tearLive[2]{}, tearConstructed[2]{}, tearFinalized[2]{};
std::atomic<int> tearOwnerLive{0}, tearFailure{0};
std::atomic<bool> tearOwnerValid{true};
OpenATL::COM::Module* tearModule = nullptr;

template<bool Cached>
class CoreTear : public ATL::CComTearOffObjectBase<TearOwner,
                     std::conditional_t<Cached, ATL::CComMultiThreadModel, ATL::CComSingleThreadModel>>,
                 public ITearProbe {
public:
    CoreTear() {
        ++tearConstructed[Cached];
        if (tearFailure == 4) throw std::bad_alloc();
        ++tearLive[Cached];
    }
    ~CoreTear();
    HRESULT FinalConstruct();
    void FinalRelease() noexcept { ++tearFinalized[Cached]; }
    HRESULT STDMETHODCALLTYPE Read(LONG* output) override;
    BEGIN_COM_MAP(CoreTear)
        COM_INTERFACE_ENTRY_IID(Cached ? iidCachedTear : iidOrdinaryTear, ITearProbe)
    END_COM_MAP()
};
class TearOwner : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IPersist {
public:
    ATL::CComPtr<IUnknown> cached;
    const LONG payload = 41;
    TearOwner() { ++tearOwnerLive; }
    ~TearOwner() { --tearOwnerLive; }
    void FinalRelease() noexcept { cached.Release(); }
    BEGIN_COM_MAP(TearOwner)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY_TEAR_OFF(iidOrdinaryTear, CoreTear<false>)
        COM_INTERFACE_ENTRY_CACHED_TEAR_OFF(iidCachedTear, CoreTear<true>, cached.p)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = tearOwnerClassId;
        return S_OK;
    }
};
template<bool Cached> CoreTear<Cached>::~CoreTear() {
    if (!this->m_pOwner || this->m_pOwner->payload != 41 || tearOwnerLive == 0 ||
        (tearModule && tearModule->CanUnloadNow() != S_FALSE)) tearOwnerValid = false;
    --tearLive[Cached];
}
template<bool Cached> HRESULT CoreTear<Cached>::Read(LONG* output) {
    if (!output) return E_POINTER;
    if (!this->m_pOwner) return E_UNEXPECTED;
    *output = this->m_pOwner->payload + (Cached ? 2 : 1);
    return S_OK;
}
template<bool Cached> HRESULT CoreTear<Cached>::FinalConstruct() {
    if (tearFailure == 1) return E_ACCESSDENIED;
    if (tearFailure == 2) throw ATL::CAtlException(E_ABORT);
    if (tearFailure == 3) throw std::runtime_error("tear-off FinalConstruct");
    if (tearFailure == 5) {
        void* recursive = reinterpret_cast<void*>(1);
        HRESULT hr = this->m_pOwner->GetControllingUnknown()->QueryInterface(
            Cached ? iidCachedTear : iidOrdinaryTear, &recursive);
        return hr == E_UNEXPECTED && !recursive ? hr : E_FAIL;
    }
    return this->m_pOwner && this->m_pOwner->payload == 41 ? S_OK : E_UNEXPECTED;
}

bool TearOffLifetime() {
    const OpenATL::COM::ClassEntry entry{tearOwnerClassId,
        &ATL::CComObject<TearOwner>::CreateInstance};
    OpenATL::COM::Module module(&entry, 1);
    tearModule = &module;
    ATL::CComPtr<IClassFactory> factory;
    CHECK(module.GetClassObject(tearOwnerClassId, IID_IClassFactory,
        reinterpret_cast<void**>(&factory.p)) == S_OK);
    ATL::CComPtr<IPersist> owner;
    CHECK(factory->CreateInstance(nullptr, IID_IPersist, reinterpret_cast<void**>(&owner.p)) == S_OK);
    factory.Release();
    auto* data = static_cast<TearOwner*>(owner.p);
    CHECK(!data->cached.p && tearLive[0] == 0 && tearLive[1] == 0);
    ATL::CComPtr<ITearProbe> a, b, c, d;
    CHECK(owner->QueryInterface(iidOrdinaryTear, reinterpret_cast<void**>(&a.p)) == S_OK);
    CHECK(owner->QueryInterface(iidOrdinaryTear, reinterpret_cast<void**>(&b.p)) == S_OK && a.p != b.p);
    CHECK(owner->QueryInterface(iidCachedTear, reinterpret_cast<void**>(&c.p)) == S_OK);
    CHECK(owner->QueryInterface(iidCachedTear, reinterpret_cast<void**>(&d.p)) == S_OK && c.p == d.p);
    CHECK(tearLive[0] == 2 && tearLive[1] == 1 && data->cached.p);
    ATL::CComPtr<IUnknown> canonical;
    CHECK(owner->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK);
    for (ITearProbe* item : {a.p, b.p, c.p, d.p}) {
        ATL::CComPtr<IUnknown> identity;
        CHECK(item->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == canonical.p);
        void* missing = reinterpret_cast<void*>(1);
        CHECK(item->QueryInterface(IID_IStream, &missing) == E_NOINTERFACE && !missing);
        CHECK(item->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    }
    ATL::CComPtr<ITearProbe> cross;
    CHECK(a->QueryInterface(iidCachedTear, reinterpret_cast<void**>(&cross.p)) == S_OK && cross.p == c.p);
    cross.Release();
    ATL::CComPtr<IPersist> parent;
    CHECK(c->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&parent.p)) == S_OK && parent.p == owner.p);
    parent.Release();
    ATL::CComPtr<IUnknown> privateCache;
    CHECK(data->cached->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&privateCache.p)) == S_OK &&
          privateCache.p == data->cached.p && privateCache.p != canonical.p);
    privateCache.Release();
    canonical.Release(); owner.Release();
    CHECK(tearOwnerLive == 1 && module.CanUnloadNow() == S_FALSE);
    LONG value = 0;
    CHECK(a->Read(&value) == S_OK && value == 42 && c->Read(&value) == S_OK && value == 43);
    c.Release(); d.Release();
    CHECK(tearLive[1] == 1); // Cache survives external tear-interface release.
    a.Release();
    CHECK(tearOwnerLive == 1 && tearLive[0] == 1);
    b.Release();
    CHECK(tearOwnerLive == 0 && tearLive[0] == 0 && tearLive[1] == 0 && tearOwnerValid);
    CHECK(module.CanUnloadNow() == S_OK);
    tearModule = nullptr;
    return true;
}

bool TearOffFailuresAndThreads() {
    ATL::CComObject<TearOwner>* owner = nullptr;
    CHECK(ATL::CComObject<TearOwner>::CreateInstance(&owner) == S_OK && owner->AddRef() == 1);
    const HRESULT failures[] = {S_OK, E_ACCESSDENIED, E_ABORT, E_UNEXPECTED, E_OUTOFMEMORY, E_UNEXPECTED};
    for (bool cached : {false, true}) {
        for (int mode = 1; mode <= 5; ++mode) {
            tearFailure = mode;
            const int finalized = tearFinalized[cached];
            void* output = reinterpret_cast<void*>(1);
            CHECK(owner->QueryInterface(cached ? iidCachedTear : iidOrdinaryTear, &output) == failures[mode] && !output);
            CHECK(!owner->cached.p && tearLive[0] == 0 && tearLive[1] == 0);
            CHECK(tearFinalized[cached] == finalized + (mode == 4 ? 0 : 1));
            CHECK(owner->AddRef() == 2 && owner->Release() == 1);
            tearFailure = 0;
            ATL::CComPtr<ITearProbe> retry;
            CHECK(owner->QueryInterface(cached ? iidCachedTear : iidOrdinaryTear,
                reinterpret_cast<void**>(&retry.p)) == S_OK);
            retry.Release();
            owner->cached.Release(); // Caller retains owner; no concurrent cache users.
        }
    }
    void* missing = reinterpret_cast<void*>(1);
    CHECK((ATL::CComCreator<ATL::CComTearOffObject<CoreTear<false>>>::CreateInstance(
        owner, IID_IStream, &missing)) == E_NOINTERFACE && !missing);
    CHECK((ATL::CComCreator<ATL::CComTearOffObject<CoreTear<false>>>::CreateInstance(
        nullptr, iidOrdinaryTear, &missing)) == E_INVALIDARG && !missing);
    CHECK((ATL::CComCreator<ATL::CComCachedTearOffObject<CoreTear<true>>>::CreateInstance(
        owner, iidCachedTear, &missing)) == E_NOINTERFACE && !missing);
    ATL::CComPtr<IUnknown> rawCache;
    CHECK((ATL::CComCreator<ATL::CComCachedTearOffObject<CoreTear<true>>>::CreateInstance(
        owner, IID_IUnknown, reinterpret_cast<void**>(&rawCache.p))) == S_OK);
    ATL::CComPtr<ITearProbe> rawData;
    CHECK(rawCache->QueryInterface(iidCachedTear, reinterpret_cast<void**>(&rawData.p)) == S_OK);
    rawData.Release(); rawCache.Release();
    const int constructed = tearConstructed[1];
    std::atomic<void*> selected{nullptr};
    std::atomic<bool> good{true};
    std::vector<std::thread> callers;
    for (int n = 0; n < 8; ++n) callers.emplace_back([&] {
        for (int i = 0; i < 100; ++i) {
            ITearProbe* result = nullptr;
            if (owner->QueryInterface(iidCachedTear, reinterpret_cast<void**>(&result)) != S_OK || !result) {
                good = false; continue;
            }
            void* expected = nullptr;
            if (!selected.compare_exchange_strong(expected, result) && expected != result) good = false;
            LONG value = 0;
            if (result->Read(&value) != S_OK || value != 43) good = false;
            result->Release();
        }
    });
    for (auto& caller : callers) caller.join();
    CHECK(good && tearConstructed[1] == constructed + 1 && tearLive[1] == 1);
    CHECK(owner->Release() == 0 && tearOwnerLive == 0 && tearLive[0] == 0 && tearLive[1] == 0 && tearOwnerValid);
    // Owner data is a real contained subobject when aggregation is used,
    // while canonical identity remains the outside controlling IUnknown.
    AggregateOuter outer;
    ATL::CComAggObject<TearOwner>* inner = nullptr;
    CHECK(ATL::CComAggObject<TearOwner>::CreateInstance(&outer, &inner) == S_OK && inner->AddRef() == 1);
    outer.inner.Attach(inner);
    for (const IID* iid : {&iidOrdinaryTear, &iidCachedTear}) {
        ATL::CComPtr<ITearProbe> result;
        CHECK(outer.QueryInterface(*iid, reinterpret_cast<void**>(&result.p)) == S_OK);
        LONG value = 0;
        CHECK(result->Read(&value) == S_OK && value == (iid == &iidOrdinaryTear ? 42 : 43));
        ATL::CComPtr<IUnknown> identity;
        CHECK(result->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == &outer);
    }
    CHECK(outer.refs == 1);
    outer.inner.Release();
    CHECK(tearOwnerLive == 0 && tearLive[0] == 0 && tearLive[1] == 0 && tearOwnerValid);
    // Poly owners also contain the real owner subobject in both modes.
    for (bool aggregated : {false, true}) {
        ATL::CComPolyObject<TearOwner>* poly = nullptr;
        CHECK(ATL::CComPolyObject<TearOwner>::CreateInstance(aggregated ? &outer : nullptr, &poly) == S_OK);
        CHECK(poly->AddRef() == 1);
        outer.inner.Attach(poly);
        IUnknown* control = aggregated ? static_cast<IUnknown*>(&outer) : static_cast<IUnknown*>(poly);
        for (const IID* iid : {&iidOrdinaryTear, &iidCachedTear}) {
            ATL::CComPtr<ITearProbe> face;
            CHECK(control->QueryInterface(*iid, reinterpret_cast<void**>(&face.p)) == S_OK);
            LONG value = 0;
            CHECK(face->Read(&value) == S_OK && value == (iid == &iidOrdinaryTear ? 42 : 43));
            ATL::CComPtr<IUnknown> identity;
            CHECK(face->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == control);
        }
        CHECK(outer.refs == 1);
        outer.inner.Release();
        CHECK(tearOwnerLive == 0 && tearLive[0] == 0 && tearLive[1] == 0 && tearOwnerValid);
    }
    return true;
}

class Single : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>, public IPersist {
public:
    BEGIN_COM_MAP(Single)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override {
        if (!id) return E_POINTER;
        *id = CLSID_NULL; return S_OK;
    }
};
}

bool RunAtlCompatibilityTests() {
    CHECK(ComMapChain());
    CHECK(Aggregation());
    CHECK(PolyObjects());
    CHECK(RunAtlSafeArrayTests());
    CHECK(RunAtlConnectionPointTests());
    CHECK(RunAtlPropertyNotifyTests());
    CHECK(AutoAggregateWithoutCom());
    CHECK(TearOffLifetime());
    CHECK(TearOffFailuresAndThreads());
    using Object = ATL::CComObject<Compatible>;
    CHECK(Object::CreateInstance(static_cast<Object**>(nullptr)) == E_POINTER);
    Object* object = nullptr;
    CHECK(Object::CreateInstance(&object) == S_OK && object && live == 1);
    CHECK(object->AddRef() == 1); // CreateInstance returns zero, not one.
    object->Lock(); object->Unlock();
    {
        ATL::CComPtr<IClassFactory> other;
        CHECK(object->QueryInterface(IID_IClassFactory, reinterpret_cast<void**>(&other)) == S_OK);
        ATL::CComPtr<IPersist> typed;
        CHECK(object->QueryInterface(&typed.p) == S_OK);
        ATL::CComPtr<IUnknown> a, b;
        CHECK(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a)) == S_OK);
        CHECK(other->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b)) == S_OK);
        CHECK(a.p == b.p && a.p == object->GetUnknown());
        ATL::CComPtr<IPersist> persist;
        CHECK(other->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&persist)) == S_OK);
        CHECK(persist.p == static_cast<IPersist*>(static_cast<IDerived*>(object)));
        CHECK(static_cast<IPersist*>(static_cast<IOther*>(object)) != persist.p);
        void* output = reinterpret_cast<void*>(1);
        CHECK(object->QueryInterface(IID_IStream, &output) == E_NOINTERFACE && !output);
        CHECK(object->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    }
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([object] {
        for (int j = 0; j < 1000; ++j) { object->AddRef(); object->Release(); }
    });
    for (auto& thread : threads) thread.join();
    CHECK(object->Release() == 0 && live == 0 && released == 1);
    const HRESULT errors[] = {S_OK, E_ACCESSDENIED, E_ABORT, E_UNEXPECTED, E_OUTOFMEMORY};
    for (failure = 1; failure <= 4; ++failure) {
        object = reinterpret_cast<Object*>(1);
        CHECK(Object::CreateInstance(&object) == errors[failure] && !object && live == 0);
    }
    CHECK(released == 4); // Failed constructor never calls FinalRelease.
    failure = 0;
    ATL::CComPtr<IPersist> local;
    CHECK((ATL::CComCoClass<Compatible, &classId>::CreateInstance(&local.p)) == S_OK);
    CLSID id{};
    CHECK(local->GetClassID(&id) == S_OK && IsEqualCLSID(id, classId));
    void* output = reinterpret_cast<void*>(1);
    CHECK(Compatible::_CreatorClass::CreateInstance(local.p, IID_IPersist, &output) == CLASS_E_NOAGGREGATION && !output);
    local.Release();
    CHECK(live == 0);
    ATL::CComObject<Single>* single = nullptr;
    CHECK(ATL::CComObject<Single>::CreateInstance(&single) == S_OK);
    CHECK(single->AddRef() == 1 && single->Release() == 0);
    {
        AggregateOuter outer;
        ATL::CComAggObject<Single>* aggregate = nullptr;
        CHECK(ATL::CComAggObject<Single>::CreateInstance(&outer, &aggregate) == S_OK);
        CHECK(aggregate->AddRef() == 1);
        ATL::CComPtr<IPersist> singleInterface;
        CHECK(aggregate->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&singleInterface.p)) == S_OK && outer.refs == 2);
        singleInterface.Release();
        CHECK(outer.refs == 1 && aggregate->Release() == 0);
    }

    const OpenATL::COM::ClassEntry entries[] = {{classId, &Object::CreateInstance}};
    OpenATL::COM::Module module(entries, 1);
    observedModule = &module;
    ATL::CComPtr<IClassFactory> factory;
    CHECK(module.GetClassObject(classId, IID_IClassFactory, reinterpret_cast<void**>(&factory)) == S_OK);
    CHECK(factory->CreateInstance(nullptr, IID_IPersist, reinterpret_cast<void**>(&local)) == S_OK);
    factory.Release();
    CHECK(module.CanUnloadNow() == S_FALSE && live == 1);
    local.Release();
    CHECK(module.CanUnloadNow() == S_OK && live == 0);
    CHECK(Object::CreateInstance(module, IID_IStream, &output) == E_NOINTERFACE && !output);
    CHECK(module.CanUnloadNow() == S_OK && live == 0);
    failure = 1;
    CHECK(Object::CreateInstance(module, IID_IPersist, &output) == E_ACCESSDENIED && !output);
    CHECK(module.CanUnloadNow() == S_OK && live == 0);
    failure = 0;
    observedModule = nullptr;
    CHECK(pinnedDuringDestruction);
    return true;
}

// Called only by the existing native STA/MTA entry points after COM startup.
bool RunAtlAutoAggregateTests(bool multiThreaded) {
    return AutoAggregateNative(multiThreaded);
}

bool RunAtlErrorInfoTests() { return NativeErrorInformation(); }
