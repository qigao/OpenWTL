#include <atlctl.h>
#include <openatl/com_registration.h>
#include "interfaces.h"
#include "fixture_registry_ids.h"
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
#include "event_fixture_metadata.h"
#endif

namespace {
using namespace OpenATL::COM;

// This fixture selects per-user installation. The standard module supplies
// its own image, opened Classes root and synchronous registration context.
HRESULT registerFixture(BOOL installing) noexcept;

// The DLL contains two classes declared with genuine ATL OBJECT_ENTRY_AUTO.
// CAtlDllModuleT invokes each class's real UpdateRegistry callback and
// continues to use the same OpenATL::COM::Module factory/owner engine.
class Value : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
              public ATL::CComCoClass<Value, &CLSID_OpenATLValue>,
              public IOpenATLDerived, public IOpenATLSecond, public IOpenATLProperty,
              public ATL::ISupportErrorInfoImpl<&IID_OpenATLValue>,
              public ATL::IConnectionPointContainerImpl<Value>,
              public ATL::IConnectionPointImpl<Value, &IID_OpenATLValueEvents>,
              public ATL::IPropertyNotifySinkCP<Value>
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
              , public ATL::IConnectionPointImpl<Value, &DIID__IOpenATLDispatchEvents>,
                public IProvideClassInfo, public EventFixtureDispatch<Value>
#endif
              {
    using ValueEvents = ATL::IConnectionPointImpl<Value, &IID_OpenATLValueEvents>;
    std::atomic<LONG> property_{17};
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    HRESULT Emit(LPOLESTR name, DISPPARAMS& params) noexcept {
        using Events = ATL::IConnectionPointImpl<Value, &DIID__IOpenATLDispatchEvents>;
        ATL::CComPtr<IEnumConnections> snapshot;
        HRESULT hr = Events::EnumConnections(&snapshot.p);
        if (FAILED(hr)) return hr;
        ATL::CComPtr<ITypeInfo> info;
        DISPID member = DISPID_UNKNOWN;
        for (;;) {
            CONNECTDATA data{};
            hr = snapshot->Next(1, &data, nullptr);
            if (hr == S_FALSE) return S_OK;
            if (FAILED(hr)) return hr;
            ATL::CComPtr<IUnknown> retained; retained.Attach(data.pUnk);
            if (!info) {
                hr = LoadEventFixtureType(DIID__IOpenATLDispatchEvents, &info.p);
                if (FAILED(hr)) return hr;
                hr = info->GetIDsOfNames(&name, 1, &member);
                if (FAILED(hr)) return hr;
            }
            hr = static_cast<IDispatch*>(data.pUnk)->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT,
                DISPATCH_METHOD, &params, nullptr, nullptr, nullptr);
            if (FAILED(hr) && hr != DISP_E_MEMBERNOTFOUND) return hr;
        }
    }
#endif
public:
    DECLARE_POLY_AGGREGATABLE(Value)
    BEGIN_COM_MAP(Value)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLDerived, IOpenATLDerived)
        COM_INTERFACE_ENTRY2_IID(IID_OpenATLValue, IOpenATLValue, IOpenATLDerived)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLSecond, IOpenATLSecond)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLProperty, IOpenATLProperty)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        COM_INTERFACE_ENTRY(IProvideClassInfo)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID__IOpenATLSourceDispatch, IDispatch)
#endif
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(Value)
        CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
        CONNECTION_POINT_ENTRY(IID_OpenATLValueEvents)
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        CONNECTION_POINT_ENTRY(DIID__IOpenATLDispatchEvents)
#endif
    END_CONNECTION_POINT_MAP()
    DECLARE_REGISTRY_RESOURCEID(IDR_OPENATL_VALUE)
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return Error(UINT(IDS_OPENATL_ERROR), 711, L"fixture-help.chm", IID_OpenATLValue, E_POINTER);
        *value = 42;
        ATL::CComPtr<IEnumConnections> snapshot;
        HRESULT hr = ValueEvents::EnumConnections(&snapshot.p);
        if (FAILED(hr)) return hr;
        for (;;) {
            CONNECTDATA data{};
            hr = snapshot->Next(1, &data, nullptr);
            if (hr == S_FALSE) break;
            if (FAILED(hr)) return hr;
            ATL::CComPtr<IUnknown> held; held.Attach(data.pUnk);
            hr = static_cast<IOpenATLValueEvents*>(data.pUnk)->OnValue(*value);
            if (FAILED(hr)) return hr;
        }
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        ATL::CComVariant args[3] = {ATL::CComVariant(L"typelib event"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(42L)};
        args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
        DISPPARAMS params{args, nullptr, 3, 0};
        hr = Emit(const_cast<LPOLESTR>(L"Changed"), params);
        if (FAILED(hr)) return hr;
        ATL::CComVariant bundle(L"retained event");
        DISPPARAMS single{&bundle, nullptr, 1, 0};
        hr = Emit(const_cast<LPOLESTR>(L"Bundle"), single);
        if (FAILED(hr)) return hr;
#endif
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSecond(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 17;
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        VARIANTARG argument{}; argument.vt = VT_BYREF | VT_I4; argument.plVal = value;
        DISPPARAMS params{&argument, nullptr, 1, 0};
        return Emit(const_cast<LPOLESTR>(L"Adjust"), params);
#endif
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 99;
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        ATL::CComVariant args[2] = {ATL::CComVariant(99L), ATL::CComVariant(LONG(EventReady))};
        DISPPARAMS params{args, nullptr, 2, 0};
        return Emit(const_cast<LPOLESTR>(L"State"), params);
#endif
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Property(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = property_.load(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Property(LONG value) noexcept override {
        const HRESULT hr = ATL::CFirePropNotifyEvent::FireOnRequestEdit(GetUnknown(), DISPID_OpenATLProperty);
        if (hr != S_OK) return hr;
        property_ = value;
        return ATL::CFirePropNotifyEvent::FireOnChanged(GetUnknown(), DISPID_OpenATLProperty);
    }
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    HRESULT STDMETHODCALLTYPE GetClassInfo(ITypeInfo** output) noexcept override {
        return LoadEventFixtureType(CLSID_EventFixtureSource, output);
    }
    HRESULT GetFixtureDispatchTypeInfo(ITypeInfo** output) noexcept {
        return LoadEventFixtureType(DIID__IOpenATLSourceDispatch, output);
    }
    LONG FixtureCurrentValue() const noexcept { return 42; }
#endif
};
OBJECT_ENTRY_AUTO(CLSID_OpenATLValue, Value)

class AuxValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                 public ATL::CComCoClass<AuxValue, &CLSID_OpenATLAuxValue>,
                 public IOpenATLValue
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
                 , public EventFixtureDispatch<AuxValue>, public IPersist,
                   public ATL::IConnectionPointContainerImpl<AuxValue>,
                   public ATL::IConnectionPointImpl<AuxValue, &DIID__IOpenATLDispatchEvents>
#endif
                 {
public:
    DECLARE_NOT_AGGREGATABLE(AuxValue)
    BEGIN_COM_MAP(AuxValue)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLValue, IOpenATLValue)
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_IID(DIID__IOpenATLSourceDispatch, IDispatch)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
#endif
    END_COM_MAP()
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    BEGIN_CONNECTION_POINT_MAP(AuxValue)
        CONNECTION_POINT_ENTRY(DIID__IOpenATLDispatchEvents)
    END_CONNECTION_POINT_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) noexcept override {
        if (!output) return E_POINTER; *output = CLSID_OpenATLAuxValue; return S_OK;
    }
    HRESULT GetFixtureDispatchTypeInfo(ITypeInfo** output) noexcept {
        return LoadEventFixtureType(DIID__IOpenATLSourceDispatch, output);
    }
    LONG FixtureCurrentValue() const noexcept { return 84; }
#endif
    DECLARE_REGISTRY_RESOURCE(L"OPENATL_AUX_NAMED")
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 84;
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
        using Events = ATL::IConnectionPointImpl<AuxValue, &DIID__IOpenATLDispatchEvents>;
        ATL::CComPtr<IEnumConnections> snapshot;
        HRESULT hr = Events::EnumConnections(&snapshot.p);
        if (FAILED(hr)) return hr;
        ATL::CComPtr<ITypeInfo> info;
        DISPID member = DISPID_UNKNOWN;
        for (;;) {
            CONNECTDATA data{}; hr = snapshot->Next(1, &data, nullptr);
            if (hr == S_FALSE) break;
            if (FAILED(hr)) return hr;
            ATL::CComPtr<IUnknown> held; held.Attach(data.pUnk);
            if (!info) {
                hr = LoadEventFixtureType(DIID__IOpenATLDispatchEvents, &info.p);
                if (FAILED(hr)) return hr;
                LPOLESTR name = const_cast<LPOLESTR>(L"Changed");
                hr = info->GetIDsOfNames(&name, 1, &member);
                if (FAILED(hr)) return hr;
            }
            ATL::CComVariant args[3] = {ATL::CComVariant(L"typelib event"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(84L)};
            args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
            DISPPARAMS params{args, nullptr, 3, 0};
            hr = static_cast<IDispatch*>(data.pUnk)->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT,
                DISPATCH_METHOD, &params, nullptr, nullptr, nullptr);
            if (FAILED(hr) && hr != DISP_E_MEMBERNOTFOUND) return hr;
        }
#endif
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(CLSID_OpenATLAuxValue, AuxValue)

// The standard common-replacement hook supplies a real resource parameter.
class FixtureAtlModule : public ATL::CAtlDllModuleT<FixtureAtlModule> {
public:
    HRESULT AddCommonRGSReplacements(IRegistrarBase* registrar) noexcept override {
        // Exercise the operation's captured mode, including TypeLib failure
        // rollback: changing the global mode here must not redirect any work.
        ATL::AtlSetPerUserRegistration(false);
        wchar_t model[] = L"Both";
        const HRESULT hr = registrar->AddReplacement(L"THREADINGMODEL", model);
        model[0] = L'X'; // The registrar must own the original value.
        return hr;
    }
};
FixtureAtlModule dllModule;

HRESULT registerFixture(BOOL installing) noexcept {
    if (rgs_resource_detail::Current() || rgs_resource_detail::CurrentModule())
        return E_UNEXPECTED;
    bool previous = false;
    HRESULT hr = ATL::AtlGetPerUserRegistration(&previous);
    if (FAILED(hr)) return hr;
    struct RestoreMode {
        bool previous;
        ~RestoreMode() { ATL::AtlSetPerUserRegistration(previous); }
    } restore{previous};
    ATL::AtlSetPerUserRegistration(true);
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    hr = installing ? dllModule.DllRegisterServer()
                    : dllModule.DllUnregisterServer();
#else
    // Cross-host fixtures without native MIDL still select class-only work.
    hr = installing ? dllModule.DllRegisterServer(FALSE)
                    : dllModule.DllUnregisterServer(FALSE);
#endif
    // Check cleanup on both success and every injected native failure.
    if (rgs_resource_detail::Current() || rgs_resource_detail::CurrentModule())
        return E_UNEXPECTED;
    return hr;
}
} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, void* reserved) {
    // Native test gate: resource-only mapping must never execute this entry
    // point. The original fixture is loaded before the gate is armed.
    if (reason == DLL_PROCESS_ATTACH &&
        ::GetEnvironmentVariableW(OPENATL_RESOURCE_EXECUTION_GUARD, nullptr, 0)) return FALSE;
    return dllModule.DllMain(reason, reserved);
}
extern "C" HRESULT WINAPI DllGetClassObject(
    REFCLSID clsid, REFIID iid, void** output) {
    return dllModule.DllGetClassObject(clsid, iid, output);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return dllModule.DllCanUnloadNow();
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    return registerFixture(TRUE);
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    return registerFixture(FALSE);
}
