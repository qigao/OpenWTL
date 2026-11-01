// Actual EXE/DLL-image Automation over unchanged Microsoft COMMap MIDL output.
// These are shared test-fixture entry points, not new OpenWTL public APIs.
#include <atlcom.h>
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
#include "COMMap.h"

namespace {
template<WORD Major, WORD Minor> class EmbeddedChain;
template<WORD Major, WORD Minor, bool Cached>
class EmbeddedTear : public ATL::CComTearOffObjectBase<EmbeddedChain<Major, Minor>>,
                     public ATL::IDispatchImpl<std::conditional_t<Cached, ITearOff2, ITearOff1>,
                         Cached ? &IID_ITearOff2 : &IID_ITearOff1, &LIBID_COMMAPLib, Major, Minor>,
                     public ATL::IDispatchImpl<IOuter, &IID_IOuter, &LIBID_COMMAPLib, Major, Minor> {
public:
    using Face = std::conditional_t<Cached, ITearOff2, ITearOff1>;
    BEGIN_COM_MAP(EmbeddedTear)
        COM_INTERFACE_ENTRY_IID(Cached ? IID_ITearOff2 : IID_ITearOff1, Face)
        COM_INTERFACE_ENTRY_IID(IID_IOuter, IOuter)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE get_Name(BSTR* output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!this->m_pOwner) return E_UNEXPECTED;
        *output = ::SysAllocString(Cached ? L"OpenWTL tear two" : L"OpenWTL tear one");
        return *output ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetName(short number, BSTR* output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return number == 7 ? get_Name(output) : E_INVALIDARG;
    }
};
template<WORD Major, WORD Minor>
class EmbeddedChain : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                      public ATL::IDispatchImpl<IChain, &IID_IChain,
                          &LIBID_COMMAPLib, Major, Minor> {
public:
    using OrdinaryTear = EmbeddedTear<Major, Minor, false>;
    using CachedTear = EmbeddedTear<Major, Minor, true>;
    ATL::CComPtr<IUnknown> cached;
    void FinalRelease() noexcept { cached.Release(); }
    BEGIN_COM_MAP(EmbeddedChain)
        COM_INTERFACE_ENTRY_IID(IID_IChain, IChain)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY_TEAR_OFF(IID_ITearOff1, OrdinaryTear)
        COM_INTERFACE_ENTRY_CACHED_TEAR_OFF(IID_ITearOff2, CachedTear, cached.p)
        COM_INTERFACE_ENTRY_CACHED_TEAR_OFF(IID_IOuter, CachedTear, cached.p)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE get_Name2(BSTR* output) noexcept override {
        if (!output) return E_POINTER;
        *output = ::SysAllocString(L"OpenWTL embedded chain");
        return *output ? S_OK : E_OUTOFMEMORY;
    }
};
}
#endif

template<WORD Major, WORD Minor>
HRESULT CreateDispatch(IDispatch** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    using Object = ATL::CComObject<EmbeddedChain<Major, Minor>>;
    Object* raw = nullptr;
    HRESULT hr = Object::CreateInstance(&raw);
    if (FAILED(hr)) return hr;
    raw->AddRef();
    hr = raw->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(output));
    raw->Release();
    return hr;
#else
    return E_NOTIMPL;
#endif
}

extern "C" HRESULT WINAPI OpenATLCreateEmbeddedDispatch(IDispatch** output) {
    return CreateDispatch<0xffff, 0xffff>(output);
}
extern "C" HRESULT WINAPI OpenATLCreateRegisteredDispatch(IDispatch** output) {
    return CreateDispatch<1, 0>(output);
}

extern "C" HRESULT WINAPI OpenATLGetEmbeddedTypeInfo(
    REFIID iid, REFGUID libid, ITypeInfo** output) {
    ATL::CComTypeInfoHolder holder;
    return holder.GetTI(&iid, &libid, 0xffff, 0xffff,
                        LOCALE_SYSTEM_DEFAULT, output);
}
