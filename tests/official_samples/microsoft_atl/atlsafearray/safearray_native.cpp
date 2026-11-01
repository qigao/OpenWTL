// Native acceptance of unchanged original Microsoft ArrayManager sources.
#include "stdafx.h"
#include "AtlSafeArray.h"
#include "ArrayManager.h"
#include <cstdio>
#include <cwchar>
#include <string>

#define SAFE_SAMPLE_CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "MICROSOFT_SAFEARRAY_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
class SafeModule : public ATL::CAtlDllModuleT<SafeModule> {
public:
    DECLARE_LIBID(LIBID_AtlSafeArrayLib)
};
struct ClassRegistration {
    DWORD cookie = 0;
    ~ClassRegistration() { if (cookie && FAILED(::CoRevokeClassObject(cookie))) std::terminate(); }
};
struct TypeRegistration {
    bool previous = false, active = false, installed = false;
    bool Begin() {
        if (FAILED(ATL::AtlGetPerUserRegistration(&previous))) return false;
        active = true;
        return ATL::AtlSetPerUserRegistration(true) == S_OK;
    }
    HRESULT Install() {
        const HRESULT hr = ATL::AtlRegisterTypeLib(ATL::_AtlBaseModule.GetModuleInstance(), L"\\3");
        if (SUCCEEDED(hr)) installed = true;
        return hr;
    }
    HRESULT Remove() {
        const HRESULT hr = ATL::AtlUnRegisterTypeLib(ATL::_AtlBaseModule.GetModuleInstance(), L"\\3");
        if (SUCCEEDED(hr)) installed = false;
        return hr;
    }
    ~TypeRegistration() {
        if (!active) return;
        ATL::AtlSetPerUserRegistration(true);
        if (installed && FAILED(Remove())) std::terminate();
        ATL::AtlSetPerUserRegistration(previous);
    }
};
std::wstring GuidKey(const wchar_t* prefix, REFGUID guid) {
    wchar_t text[40]{};
    return ::StringFromGUID2(guid, text, 40) ? std::wstring(prefix) + text : std::wstring();
}
bool Absent(HKEY root, const std::wstring& path) {
    HKEY key = nullptr;
    const LSTATUS status = ::RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
    if (key) ::RegCloseKey(key);
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}
bool OriginalKeysAbsent() {
    SAFE_SAMPLE_CHECK(Absent(HKEY_CLASSES_ROOT, GuidKey(L"TypeLib\\", LIBID_AtlSafeArrayLib)));
    SAFE_SAMPLE_CHECK(Absent(HKEY_CLASSES_ROOT, GuidKey(L"Interface\\", IID_IArrayManager)));
    SAFE_SAMPLE_CHECK(Absent(HKEY_CLASSES_ROOT, GuidKey(L"CLSID\\", CLSID_ArrayManager)));
    ATL::CComPtr<ITypeLib> library;
    SAFE_SAMPLE_CHECK(::LoadRegTypeLib(LIBID_AtlSafeArrayLib, 1, 0, LOCALE_SYSTEM_DEFAULT, &library.p) == TYPE_E_LIBNOTREGISTERED && !library.p);
    return true;
}
bool RegistryCallback(SafeModule& module) {
    GUID unique{};
    SAFE_SAMPLE_CHECK(::CoCreateGuid(&unique) == S_OK);
    const auto path = GuidKey(L"Software\\OpenWTL\\Tests\\OriginalSafeArray-", unique);
    HKEY key = nullptr;
    DWORD disposition = 0;
    SAFE_SAMPLE_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
        KEY_ALL_ACCESS, nullptr, &key, &disposition) == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY);
    struct Root {
        HKEY key; const std::wstring& path;
        ~Root() { ::RegCloseKey(key); ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
    } root{key, path};
    OpenATL::COM::ScopedRgsRegistrationContext context(key,
        ATL::_AtlBaseModule.GetModuleInstance(), L"Original.ArrayManager");
    SAFE_SAMPLE_CHECK(context.Status() == S_OK);
    SAFE_SAMPLE_CHECK(module.RegisterServer(FALSE, &CLSID_ArrayManager) == S_OK);
    const auto classKey = GuidKey(L"CLSID\\", CLSID_ArrayManager);
    SAFE_SAMPLE_CHECK(!Absent(key, classKey + L"\\InprocServer32"));
    SAFE_SAMPLE_CHECK(module.UnregisterServer(FALSE, &CLSID_ArrayManager) == S_OK);
    SAFE_SAMPLE_CHECK(Absent(key, classKey));
    return true;
}
bool ArrayText(const VARIANT& array, std::initializer_list<const wchar_t*> expected) {
    SAFE_SAMPLE_CHECK(array.vt == (VT_ARRAY | VT_VARIANT) && array.parray && array.parray->cLocks == 0);
    LONG lower = 0, upper = -1;
    SAFE_SAMPLE_CHECK(::SafeArrayGetLBound(array.parray, 1, &lower) == S_OK && lower == 0);
    SAFE_SAMPLE_CHECK(::SafeArrayGetUBound(array.parray, 1, &upper) == S_OK && upper + 1 == static_cast<LONG>(expected.size()));
    LONG index = 0;
    for (auto* text : expected) {
        ATL::CComVariant element;
        SAFE_SAMPLE_CHECK(::SafeArrayGetElement(array.parray, &index, &element) == S_OK && element.vt == VT_BSTR);
        SAFE_SAMPLE_CHECK(element.bstrVal && std::wcscmp(element.bstrVal, text) == 0);
        ++index;
    }
    return true;
}
bool Safety(IArrayManager* object) {
    ATL::CComPtr<IObjectSafety> safety;
    SAFE_SAMPLE_CHECK(object->QueryInterface(IID_IObjectSafety, reinterpret_cast<void**>(&safety.p)) == S_OK);
    DWORD supported = 0, enabled = 0;
    SAFE_SAMPLE_CHECK(safety->GetInterfaceSafetyOptions(IID_IArrayManager, &supported, &enabled) == S_OK &&
        supported == INTERFACESAFE_FOR_UNTRUSTED_CALLER && enabled == supported);
    SAFE_SAMPLE_CHECK(safety->GetInterfaceSafetyOptions(IID_IArrayManager, nullptr, &enabled) == E_POINTER);
    SAFE_SAMPLE_CHECK(safety->SetInterfaceSafetyOptions(IID_IArrayManager, supported, 0) == S_OK);
    SAFE_SAMPLE_CHECK(safety->GetInterfaceSafetyOptions(IID_IDispatch, &supported, &enabled) == S_OK && enabled == 0);
    SAFE_SAMPLE_CHECK(safety->SetInterfaceSafetyOptions(IID_IDispatch, INTERFACESAFE_FOR_UNTRUSTED_DATA, 0) == E_FAIL);
    SAFE_SAMPLE_CHECK(safety->SetInterfaceSafetyOptions(IID_IArrayManager, supported, supported) == S_OK);
    supported = enabled = 0xffffffff;
    SAFE_SAMPLE_CHECK(safety->GetInterfaceSafetyOptions(IID_IStream, &supported, &enabled) == E_NOINTERFACE && !supported && !enabled);
    SAFE_SAMPLE_CHECK(safety->SetInterfaceSafetyOptions(IID_IStream, 0, 0) == E_NOINTERFACE);
    SAFE_SAMPLE_CHECK(safety->GetInterfaceSafetyOptions(IID_IArrayManager, &supported, &enabled) == S_OK && enabled == supported);
    return true;
}
bool Invoke(IArrayManager* object, const wchar_t* text, VARIANTARG* args, UINT count, VARIANT* result) {
    LPOLESTR name = const_cast<LPOLESTR>(text);
    DISPID id = DISPID_UNKNOWN;
    SAFE_SAMPLE_CHECK(object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_SYSTEM_DEFAULT, &id) == S_OK && id != DISPID_UNKNOWN);
    DISPPARAMS parameters{args, nullptr, count, 0};
    SAFE_SAMPLE_CHECK(object->Invoke(id, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD,
        &parameters, result, nullptr, nullptr) == S_OK);
    return true;
}
}
bool RunMicrosoftSafeArrayTests() {
    SAFE_SAMPLE_CHECK(!ATL::_pAtlModule && OriginalKeysAbsent());
    ATL::CComVariant snapshot, finalSnapshot;
    {
        SafeModule module;
        SAFE_SAMPLE_CHECK(RegistryCallback(module));
        ClassRegistration registration;
        ATL::CComPtr<IClassFactory> factory;
        SAFE_SAMPLE_CHECK(module.DllGetClassObject(CLSID_ArrayManager, IID_IClassFactory,
            reinterpret_cast<void**>(&factory.p)) == S_OK);
        SAFE_SAMPLE_CHECK(::CoRegisterClassObject(CLSID_ArrayManager, factory.p, CLSCTX_INPROC_SERVER,
            REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
        ATL::CComPtr<IArrayManager> object;
        SAFE_SAMPLE_CHECK(::CoCreateInstance(CLSID_ArrayManager, nullptr, CLSCTX_INPROC_SERVER,
            IID_IArrayManager, reinterpret_cast<void**>(&object.p)) == S_OK);
        SAFE_SAMPLE_CHECK(Safety(object.p));
        ATL::CComPtr<ITypeInfo> info;
        SAFE_SAMPLE_CHECK(object->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == TYPE_E_LIBNOTREGISTERED && !info.p);
        TypeRegistration types;
        SAFE_SAMPLE_CHECK(types.Begin() && types.Install() == S_OK);
        SAFE_SAMPLE_CHECK(object->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK && info.p);
        TYPEATTR* attributes = nullptr;
        SAFE_SAMPLE_CHECK(info->GetTypeAttr(&attributes) == S_OK && attributes);
        const bool correct = IsEqualGUID(attributes->guid, IID_IArrayManager);
        info->ReleaseTypeAttr(attributes);
        SAFE_SAMPLE_CHECK(correct);
        info.Release();
        ATL::CComVariant empty;
        SAFE_SAMPLE_CHECK(object->GetArray(&empty) == S_OK && empty.vt == VT_EMPTY);
        ATL::CComBSTR alpha(L"alpha"), beta(L"beta"), gamma(L"gamma"), changed(L"changed");
        SAFE_SAMPLE_CHECK(object->Add(alpha) == S_OK && object->Add(beta) == S_OK);
        alpha.m_str[0] = L'X';
        VARIANTARG argument{}; argument.vt = VT_BSTR; argument.bstrVal = gamma;
        SAFE_SAMPLE_CHECK(Invoke(object.p, L"Add", &argument, 1, nullptr));
        SAFE_SAMPLE_CHECK(Invoke(object.p, L"GetArray", nullptr, 0, &snapshot));
        SAFE_SAMPLE_CHECK(ArrayText(snapshot, {L"alpha", L"beta", L"gamma"}));
        VARIANTARG change[2]{};
        change[0].vt = VT_BSTR; change[0].bstrVal = changed;
        change[1].vt = VT_UI4; change[1].ulVal = 1;
        SAFE_SAMPLE_CHECK(Invoke(object.p, L"Change", change, 2, nullptr));
        ATL::CComVariant modified;
        SAFE_SAMPLE_CHECK(object->GetArray(&modified) == S_OK && ArrayText(modified, {L"alpha", L"changed", L"gamma"}));
        SAFE_SAMPLE_CHECK(object->Remove(1) == S_OK);
        argument.vt = VT_UI4; argument.ulVal = 1;
        SAFE_SAMPLE_CHECK(Invoke(object.p, L"Remove", &argument, 1, nullptr));
        SAFE_SAMPLE_CHECK(object->GetArray(&finalSnapshot) == S_OK && ArrayText(finalSnapshot, {L"alpha"}));
        SAFE_SAMPLE_CHECK(ArrayText(snapshot, {L"alpha", L"beta", L"gamma"}));
        SAFE_SAMPLE_CHECK(object->Remove(0) == S_OK);
        ATL::CComVariant emptied;
        SAFE_SAMPLE_CHECK(object->GetArray(&emptied) == S_OK && ArrayText(emptied, {}));
        SAFE_SAMPLE_CHECK(types.Remove() == S_OK && OriginalKeysAbsent());
        SAFE_SAMPLE_CHECK(object->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == TYPE_E_LIBNOTREGISTERED && !info.p);
        object.Release();
        SAFE_SAMPLE_CHECK(::CoRevokeClassObject(registration.cookie) == S_OK);
        registration.cookie = 0;
        factory.Release();
        SAFE_SAMPLE_CHECK(module.DllCanUnloadNow() == S_OK);
    }
    SAFE_SAMPLE_CHECK(!ATL::_pAtlModule);
    SAFE_SAMPLE_CHECK(ArrayText(snapshot, {L"alpha", L"beta", L"gamma"}) && ArrayText(finalSnapshot, {L"alpha"}));
    return true;
}
