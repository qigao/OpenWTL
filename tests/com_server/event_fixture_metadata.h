#ifndef OPENATL_EVENT_FIXTURE_METADATA_H
#define OPENATL_EVENT_FIXTURE_METADATA_H
#include <atlcom.h>
#include "EventFixture.h"
// Load the caller's own native MIDL resource without registry mutation.
inline HRESULT LoadEventFixtureType(REFGUID iid, ITypeInfo** output) noexcept {
    if (!output) return E_POINTER; *output = nullptr;
    try {
        wchar_t image[8193]{};
        const DWORD size = ::GetModuleFileNameW(ATL::_AtlBaseModule.GetModuleInstance(), image, 8193);
        if (!size || size >= 8193) return E_FAIL;
        const std::wstring path = std::wstring(image) + L"\\4";
        ATL::CComPtr<ITypeLib> library;
        HRESULT hr = ::LoadTypeLibEx(path.c_str(), REGKIND_NONE, &library.p);
        if (FAILED(hr)) return hr;
        if (!library.p) return E_UNEXPECTED;
        return library->GetTypeInfoOfGuid(iid, output);
    } catch (...) { return ATL::detail::AtlObjectException(); }
}
template<class Owner> class EventFixtureDispatch : public IDispatch {
public:
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* output) noexcept override {
        if (!output) return E_POINTER; *output = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID, ITypeInfo** output) noexcept override {
        if (!output) return E_POINTER; *output = nullptr;
        if (index) return DISP_E_BADINDEX;
        return static_cast<Owner*>(this)->GetFixtureDispatchTypeInfo(output);
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
        LCID locale, DISPID* output) noexcept override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !output) return E_POINTER;
        ATL::CComPtr<ITypeInfo> info;
        HRESULT hr = GetTypeInfo(0, locale, &info.p);
        return FAILED(hr) ? hr : info->GetIDsOfNames(names, count, output);
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID locale, WORD flags,
        DISPPARAMS* params, VARIANT* result, EXCEPINFO*, UINT*) noexcept override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!params || !result) return E_POINTER;
        ::VariantInit(result);
        LPOLESTR name = const_cast<LPOLESTR>(L"Current"); DISPID current = DISPID_UNKNOWN;
        HRESULT hr = GetIDsOfNames(IID_NULL, &name, 1, locale, &current);
        if (FAILED(hr)) return hr;
        if (member != current || flags != DISPATCH_PROPERTYGET) return DISP_E_MEMBERNOTFOUND;
        if (params->cArgs || params->cNamedArgs) return DISP_E_BADPARAMCOUNT;
        result->vt = VT_I4; result->lVal = static_cast<Owner*>(this)->FixtureCurrentValue();
        return S_OK;
    }
};
#endif
