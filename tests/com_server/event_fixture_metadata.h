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
#endif
