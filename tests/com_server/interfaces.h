#ifndef OPENATL_TEST_SERVER_INTERFACES_H
#define OPENATL_TEST_SERVER_INTERFACES_H
#include <windows.h>
#include <unknwn.h>
#include <oaidl.h>

inline constexpr IID IID_OpenATLValue =
    {0xd7022ae1, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr IID IID_OpenATLSecond =
    {0xd7022ae2, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr CLSID CLSID_OpenATLValue =
    {0xd7022ae3, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr CLSID CLSID_OpenATLThrow =
    {0xd7022ae4, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr CLSID CLSID_OpenATLAuxValue =
    {0xd7022af0, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr IID IID_OpenATLUnknown =
    {0xd7022ae5, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr IID IID_OpenATLDerived =
    {0xd7022ae6, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr IID IID_OpenATLValueEvents =
    {0xd7022ae7, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr IID IID_OpenATLProperty =
    {0xd7022ae8, 0x51ce, 0x49af, {0x98, 0x72, 0x11, 0x40, 0xd1, 0x82, 0x49, 0x61}};
inline constexpr DISPID DISPID_OpenATLProperty = 18;

struct IOpenATLValue : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetValue(LONG* value) = 0;
};
struct IOpenATLSecond : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetSecond(LONG* value) = 0;
};
struct IOpenATLDerived : IOpenATLValue {
    virtual HRESULT STDMETHODCALLTYPE GetExtra(LONG* value) = 0;
};
struct IOpenATLValueEvents : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnValue(LONG value) = 0;
};
struct IOpenATLProperty : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Property(LONG* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Property(LONG value) = 0;
};
inline constexpr wchar_t OpenATLTestProgId[] = L"OpenATL.Conformance.Server.1";
inline constexpr wchar_t OpenATLAuxProgId[] = L"OpenATL.Conformance.Aux.1";
#endif
