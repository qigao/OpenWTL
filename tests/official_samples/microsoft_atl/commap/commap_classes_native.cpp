// Native acceptance of unchanged original Microsoft class implementations.
// The original COMMap/aggreg DLL module entry points are not linked here.
#include "premap.h"
#include "COMMap.h"
#include "Outer.h"
#include "aggreg/Agg.h"
#include "aggreg/AggBlind.h"
#include "aggreg/AutoAgg.h"
#include "aggreg/AutoAggB.h"
#include <cstdio>
#include <cwchar>

#define ORIGINAL_CLASS_CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "MICROSOFT_COMMAP_CLASS_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
class OriginalClassModule : public ATL::CAtlDllModuleT<OriginalClassModule> {
public:
    DECLARE_LIBID(LIBID_COMMAPLib)
};
struct ClassRegistrations {
    OriginalClassModule& module;
    DWORD cookies[6]{};
    ATL::CComPtr<IClassFactory> factories[6];
    size_t count = 0;
    explicit ClassRegistrations(OriginalClassModule& value) : module(value) {}
    HRESULT Add(REFCLSID clsid) {
        if (count == 6) return E_UNEXPECTED;
        ATL::CComPtr<IClassFactory> factory;
        HRESULT hr = module.DllGetClassObject(clsid, IID_IClassFactory,
            reinterpret_cast<void**>(&factory.p));
        if (FAILED(hr)) return hr;
        hr = ::CoRegisterClassObject(clsid, factory.p, CLSCTX_INPROC_SERVER,
            REGCLS_MULTIPLEUSE, &cookies[count]);
        if (SUCCEEDED(hr)) factories[count++].Attach(factory.Detach());
        return hr;
    }
    HRESULT Revoke() {
        for (size_t i = count; i > 0; --i) {
            if (cookies[i - 1]) {
                HRESULT hr = ::CoRevokeClassObject(cookies[i - 1]);
                if (FAILED(hr)) return hr;
                cookies[i - 1] = 0;
            }
            factories[i - 1].Release();
        }
        count = 0;
        return S_OK;
    }
    ~ClassRegistrations() { if (FAILED(Revoke())) std::terminate(); }
};
bool NameIs(BSTR text, const wchar_t* expected) {
    return text && std::wcscmp(text, expected) == 0;
}
bool OriginalClassBodies() {
    ORIGINAL_CLASS_CHECK(!ATL::_pAtlModule);
    const CLSID* ids[] = {&CLSID_COuter, &CLSID_CChain, &CLSID_CAgg,
        &CLSID_CAggBlind, &CLSID_CAutoAgg, &CLSID_CAutoAggB};
    size_t count = 0;
    for (const auto* entry = ATL::detail::autoObjectEntries.load(std::memory_order_acquire);
         entry; entry = entry->next) {
        ++count;
        bool known = false;
        for (const auto* id : ids) if (IsEqualCLSID(entry->clsid, *id)) known = true;
        ORIGINAL_CLASS_CHECK(known);
    }
    // Original Chain.h occurs in Chain.cpp, Outer.cpp and this translation
    // unit; each aggregate header occurs twice. One logical entry per class.
    ORIGINAL_CLASS_CHECK(count == 6);
    OriginalClassModule module;
    ClassRegistrations registrations(module);
    ATL::CComPtr<IClassFactory> outerFactory;
    ORIGINAL_CLASS_CHECK(module.DllGetClassObject(CLSID_COuter, IID_IClassFactory,
        reinterpret_cast<void**>(&outerFactory.p)) == S_OK);
    void* failed = reinterpret_cast<void*>(1);
    ORIGINAL_CLASS_CHECK(outerFactory->CreateInstance(nullptr, IID_IOuter, &failed) == REGDB_E_CLASSNOTREG && !failed);
    ORIGINAL_CLASS_CHECK(module.GetLockCount() == 0);
    // Let the first real aggregate succeed, then fail the second. Original
    // COuter's FinalRelease must retire the partially constructed aggregate.
    ORIGINAL_CLASS_CHECK(registrations.Add(CLSID_CAgg) == S_OK);
    ORIGINAL_CLASS_CHECK(outerFactory->CreateInstance(nullptr, IID_IOuter, &failed) == REGDB_E_CLASSNOTREG && !failed);
    ORIGINAL_CLASS_CHECK(module.GetLockCount() == 0);
    outerFactory.Release();
    for (const CLSID* id : {&CLSID_CAggBlind, &CLSID_CAutoAgg, &CLSID_CAutoAggB, &CLSID_COuter, &CLSID_CChain})
        ORIGINAL_CLASS_CHECK(registrations.Add(*id) == S_OK);
    {
        ATL::CComPtr<IChain> chain;
        ORIGINAL_CLASS_CHECK(::CoCreateInstance(CLSID_CChain, nullptr, CLSCTX_INPROC_SERVER,
            IID_IChain, reinterpret_cast<void**>(&chain.p)) == S_OK);
        ATL::CComBSTR text;
        ORIGINAL_CLASS_CHECK(chain->get_Name2(&text.m_str) == S_OK && NameIs(text, L"IChain"));
    }
    ATL::CComPtr<IOuter> outer;
    ORIGINAL_CLASS_CHECK(::CoCreateInstance(CLSID_COuter, nullptr, CLSCTX_INPROC_SERVER,
        IID_IOuter, reinterpret_cast<void**>(&outer.p)) == S_OK);
    auto* data = static_cast<COuter*>(outer.p);
    ORIGINAL_CLASS_CHECK(data->m_pUnkAgg.p && data->m_pUnkAggBlind.p &&
        !data->m_pUnkAutoAgg.p && !data->m_pUnkAutoAggB.p && !data->m_pUnkTearOff2.p);
    const wchar_t* names[] = {L"IOuter", L"ITearOff1", L"ITearOff2", L"IChain",
        L"IAgg", L"IAutoAgg", L"IAggBlind", L"IAutoAggB"};
    for (short n = 0; n < 8; ++n) {
        ATL::CComBSTR text;
        ORIGINAL_CLASS_CHECK(outer->GetName(n, &text.m_str) == S_OK && NameIs(text, names[n]));
    }
    ORIGINAL_CLASS_CHECK(data->m_pUnkAutoAgg.p && data->m_pUnkAutoAggB.p && data->m_pUnkTearOff2.p);
    ATL::CComPtr<IUnknown> canonical;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK);
    ATL::CComPtr<ITearOff1> first, another;
    ATL::CComPtr<ITearOff2> cached, repeated;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&first.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&another.p)) == S_OK && first.p != another.p);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&cached.p)) == S_OK);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&repeated.p)) == S_OK && cached.p == repeated.p);
    const IID* interfaces[] = {&IID_IOuter, &IID_IChain, &IID_ITearOff1, &IID_ITearOff2,
        &IID_IAgg, &IID_IAggBlind, &IID_IAutoAgg, &IID_IAutoAggB};
    for (const IID* iid : interfaces) {
        ATL::CComPtr<IUnknown> face, identity;
        ORIGINAL_CLASS_CHECK(outer->QueryInterface(*iid, reinterpret_cast<void**>(&face.p)) == S_OK);
        ORIGINAL_CLASS_CHECK(face->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == canonical.p);
    }
    ATL::CComPtr<ISupportErrorInfo> support;
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_ISupportErrorInfo, reinterpret_cast<void**>(&support.p)) == S_OK);
    // The original map places the explicit blind aggregate before its local
    // ISupportErrorInfo entry. That aggregate's support view wins this QI.
    ORIGINAL_CLASS_CHECK(support->InterfaceSupportsErrorInfo(IID_IAggBlind) == S_OK &&
        support->InterfaceSupportsErrorInfo(IID_IOuter) == S_FALSE &&
        support->InterfaceSupportsErrorInfo(IID_IChain) == S_FALSE &&
        support->InterfaceSupportsErrorInfo(IID_IStream) == S_FALSE);
    ORIGINAL_CLASS_CHECK(data->COuter::InterfaceSupportsErrorInfo(IID_IOuter) == S_OK &&
        data->COuter::InterfaceSupportsErrorInfo(IID_IChain) == S_OK);
    support.Release();
    // Real original ITypeInfo drives the unmodified GetName vtable, without
    // registry writes or replacing the object's registered-mode IDispatchImpl.
    ATL::CComTypeInfoHolder holder;
    ATL::CComPtr<ITypeInfo> info;
    ORIGINAL_CLASS_CHECK(holder.GetTI(&IID_IOuter, &LIBID_COMMAPLib, 0xffff, 0xffff,
        LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK);
    LPOLESTR method = const_cast<LPOLESTR>(L"GetName");
    MEMBERID member = MEMBERID_NIL;
    ORIGINAL_CLASS_CHECK(info->GetIDsOfNames(&method, 1, &member) == S_OK && member != MEMBERID_NIL);
    for (short n = 0; n < 8; ++n) {
        VARIANTARG number{}; number.vt = VT_I2; number.iVal = n;
        DISPPARAMS parameters{&number, nullptr, 1, 0};
        ATL::CComVariant result;
        ORIGINAL_CLASS_CHECK(info->Invoke(outer.p, member, DISPATCH_METHOD,
            &parameters, &result, nullptr, nullptr) == S_OK && result.vt == VT_BSTR && NameIs(result.bstrVal, names[n]));
    }
    ATL::CComBSTR unused;
    ORIGINAL_CLASS_CHECK(outer->GetName(8, &unused.m_str) == DISP_E_EXCEPTION);
    ATL::CComPtr<IErrorInfo> error;
    ORIGINAL_CLASS_CHECK(::GetErrorInfo(0, &error.p) == S_OK && error.p);
    ATL::CComBSTR description;
    ORIGINAL_CLASS_CHECK(error->GetDescription(&description.m_str) == S_OK && NameIs(description, L"Interface type out of range"));
    VARIANTARG invalidNumber{}; invalidNumber.vt = VT_I2; invalidNumber.iVal = 8;
    DISPPARAMS invalidParameters{&invalidNumber, nullptr, 1, 0};
    ATL::CComVariant invalidResult;
    EXCEPINFO exception{};
    const HRESULT invokedError = info->Invoke(outer.p, member, DISPATCH_METHOD,
        &invalidParameters, &invalidResult, &exception, nullptr);
    ATL::CComBSTR exceptionText, exceptionSource, exceptionHelp;
    exceptionText.Attach(exception.bstrDescription);
    exceptionSource.Attach(exception.bstrSource);
    exceptionHelp.Attach(exception.bstrHelpFile);
    ORIGINAL_CLASS_CHECK(invokedError == DISP_E_EXCEPTION && NameIs(exceptionText, L"Interface type out of range"));
    ::SetErrorInfo(0, nullptr);
    failed = reinterpret_cast<void*>(1);
    ORIGINAL_CLASS_CHECK(outer->QueryInterface(IID_IStream, &failed) == E_NOINTERFACE && !failed);
    ORIGINAL_CLASS_CHECK(registrations.Revoke() == S_OK);
    first.Release(); another.Release(); repeated.Release(); canonical.Release(); outer.Release();
    ORIGINAL_CLASS_CHECK(module.DllCanUnloadNow() == S_FALSE);
    ATL::CComPtr<IOuter> retainedOwner;
    ORIGINAL_CLASS_CHECK(cached->QueryInterface(IID_IOuter, reinterpret_cast<void**>(&retainedOwner.p)) == S_OK);
    ATL::CComBSTR finalName;
    ORIGINAL_CLASS_CHECK(retainedOwner->GetName(0, &finalName.m_str) == S_OK && NameIs(finalName, L"IOuter"));
    retainedOwner.Release(); cached.Release();
    ORIGINAL_CLASS_CHECK(module.GetLockCount() == 0 && module.DllCanUnloadNow() == S_OK);
    // The native error object owns its copied description independently.
    ATL::CComBSTR survivingError;
    ORIGINAL_CLASS_CHECK(error->GetDescription(&survivingError.m_str) == S_OK && NameIs(survivingError, L"Interface type out of range"));
    return true;
}
}
bool RunMicrosoftCommapClassTests() {
    const bool result = OriginalClassBodies();
    return result && !ATL::_pAtlModule;
}
