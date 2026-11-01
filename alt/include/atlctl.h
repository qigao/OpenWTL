// OpenATL OLE control and event helpers. No MFC dependency.
#ifndef OPENATL_ATLCTL_H
#define OPENATL_ATLCTL_H
#pragma once
#include "atlcom.h"
#include "atlwin.h"
#include <olectl.h>
#include <objsafe.h>

namespace ATL {
template<class T, DWORD SupportedSafety>
class IObjectSafetyImpl : public IObjectSafety {
public:
    DWORD m_dwCurrentSafety = SupportedSafety;
    HRESULT STDMETHODCALLTYPE GetInterfaceSafetyOptions(REFIID iid, DWORD* supported,
                                                          DWORD* enabled) noexcept override {
        if (supported) *supported = 0;
        if (enabled) *enabled = 0;
        if (!supported || !enabled) return E_POINTER;
        CComPtr<IUnknown> face;
        if (FAILED(this->QueryInterface(iid, reinterpret_cast<void**>(&face.p))))
            return E_NOINTERFACE;
        *supported = SupportedSafety;
        *enabled = m_dwCurrentSafety;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInterfaceSafetyOptions(REFIID iid, DWORD mask,
                                                          DWORD enabled) noexcept override {
        CComPtr<IUnknown> face;
        if (FAILED(this->QueryInterface(iid, reinterpret_cast<void**>(&face.p))))
            return E_NOINTERFACE;
        if (mask & ~SupportedSafety) return E_FAIL;
        m_dwCurrentSafety = (m_dwCurrentSafety & ~mask) | (enabled & mask);
        return S_OK;
    }
};
}
#endif
