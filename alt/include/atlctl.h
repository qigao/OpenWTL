// OpenATL OLE control and event helpers. No MFC dependency.
#ifndef OPENATL_ATLCTL_H
#define OPENATL_ATLCTL_H
#pragma once
#include "atlcom.h"
#include "atlwin.h"
#include <olectl.h>
#include <objsafe.h>
#include <vector>

// mingw-w64's objsafe.h declares the IID but omits its C++ __uuidof binding.
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IObjectSafety,0xcb5bdc81,0x93c1,0x11cf,0x8f,0x20,0x00,0x80,0x5f,0x2c,0xd0,0x64)
#endif

namespace ATL {
template<class T, class CDV = CComDynamicUnkArray>
class IPropertyNotifySinkCP : public IConnectionPointImpl<T, &IID_IPropertyNotifySink, CDV> {};

class CFirePropNotifyEvent {
    static HRESULT Fire(IUnknown* object, DISPID member, bool request) noexcept {
        if (!object) return E_POINTER;
        try {
            CComPtr<IUnknown> sender(object);
            CComPtr<IConnectionPointContainer> container;
            HRESULT hr = object->QueryInterface(IID_IConnectionPointContainer,
                reinterpret_cast<void**>(&container.p));
            if (hr == E_NOINTERFACE) return S_OK; // A source without connection points is supported.
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !container.p) return E_UNEXPECTED;
            CComPtr<IEnumConnectionPoints> points;
            hr = container->EnumConnectionPoints(&points.p);
            if (FAILED(hr)) return hr;
            if (hr != S_OK || !points.p) return E_UNEXPECTED;

            // Capture every point before invoking any property sink. A sink
            // may also support property notifications through another IID.
            std::vector<CComPtr<IEnumConnections>> connections;
            for (;;) {
                CComPtr<IConnectionPoint> point;
                ULONG fetched = 0;
                hr = points->Next(1, &point.p, &fetched);
                if (hr == S_FALSE && !fetched && !point.p) break;
                if (FAILED(hr)) return hr;
                if (hr != S_OK || fetched != 1 || !point.p) return E_UNEXPECTED;
                CComPtr<IEnumConnections> snapshot;
                hr = point->EnumConnections(&snapshot.p);
                if (FAILED(hr)) return hr;
                if (hr != S_OK || !snapshot.p) return E_UNEXPECTED;
                connections.push_back(std::move(snapshot));
            }
            std::vector<CComPtr<IPropertyNotifySink>> targets;
            for (auto& snapshot : connections) for (;;) {
                CONNECTDATA data{};
                ULONG fetched = 0;
                hr = snapshot->Next(1, &data, &fetched);
                CComPtr<IUnknown> connected; connected.Attach(data.pUnk);
                if (hr == S_FALSE && !fetched && !connected.p) break;
                if (FAILED(hr)) return hr;
                if (hr != S_OK || fetched != 1 || !connected.p) return E_UNEXPECTED;
                CComPtr<IPropertyNotifySink> sink;
                hr = connected->QueryInterface(IID_IPropertyNotifySink,
                    reinterpret_cast<void**>(&sink.p));
                if (hr == E_NOINTERFACE) continue;
                if (FAILED(hr)) return hr;
                if (hr != S_OK || !sink.p) return E_UNEXPECTED;
                targets.push_back(std::move(sink));
            }
            for (auto& sink : targets) {
                if (request) {
                    hr = sink->OnRequestEdit(member);
                    if (hr != S_OK) return hr; // S_FALSE vetoes too; FAILED alone is insufficient.
                } else {
                    // OnChanged is advisory. Native IPropertyNotifySink
                    // requires S_OK even when a sink cannot act on the change.
                    sink->OnChanged(member);
                }
            }
            return S_OK;
        } catch (...) { return detail::AtlObjectException(); }
    }
public:
    static HRESULT FireOnChanged(IUnknown* object, DISPID member) noexcept { return Fire(object, member, false); }
    static HRESULT FireOnRequestEdit(IUnknown* object, DISPID member) noexcept { return Fire(object, member, true); }
};

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
