#include <atlctl.h>
#include <openatl/com_registration.h>
#include "interfaces.h"
#include "fixture_registry_ids.h"

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
              public ATL::IPropertyNotifySinkCP<Value> {
    using ValueEvents = ATL::IConnectionPointImpl<Value, &IID_OpenATLValueEvents>;
    std::atomic<LONG> property_{17};
public:
    DECLARE_POLY_AGGREGATABLE(Value)
    BEGIN_COM_MAP(Value)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLDerived, IOpenATLDerived)
        COM_INTERFACE_ENTRY2_IID(IID_OpenATLValue, IOpenATLValue, IOpenATLDerived)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLSecond, IOpenATLSecond)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLProperty, IOpenATLProperty)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
    END_COM_MAP()
    BEGIN_CONNECTION_POINT_MAP(Value)
        CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
        CONNECTION_POINT_ENTRY(IID_OpenATLValueEvents)
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
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSecond(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 17;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 99;
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
};
OBJECT_ENTRY_AUTO(CLSID_OpenATLValue, Value)

class AuxValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                 public ATL::CComCoClass<AuxValue, &CLSID_OpenATLAuxValue>,
                 public IOpenATLValue {
public:
    DECLARE_NOT_AGGREGATABLE(AuxValue)
    BEGIN_COM_MAP(AuxValue)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLValue, IOpenATLValue)
    END_COM_MAP()
    DECLARE_REGISTRY_RESOURCE(L"OPENATL_AUX_NAMED")
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 84;
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
