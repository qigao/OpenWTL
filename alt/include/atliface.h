// OpenATL public containment and registrar interfaces. COM vtable order
// follows the published ATL IDL; no external ATL implementation is used.
#ifndef OPENATL_ATLIFACE_H
#define OPENATL_ATLIFACE_H
#pragma once
#include <olectl.h>
#ifndef __IRegistrarBase_INTERFACE_DEFINED__
#define __IRegistrarBase_INTERFACE_DEFINED__
inline constexpr IID IID_IRegistrarBase = {0xe21f8a85,0xb05d,0x4243,{0x81,0x83,0xc7,0xcb,0x40,0x55,0x88,0xf7}};
MIDL_INTERFACE("E21F8A85-B05D-4243-8183-C7CB405588F7") IRegistrarBase : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddReplacement(LPCOLESTR, LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearReplacements() = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IRegistrarBase,0xe21f8a85,0xb05d,0x4243,0x81,0x83,0xc7,0xcb,0x40,0x55,0x88,0xf7)
#endif
#endif
#ifndef __IRegistrar_INTERFACE_DEFINED__
#define __IRegistrar_INTERFACE_DEFINED__
inline constexpr IID IID_IRegistrar = {0x44ec053b,0x400f,0x11d0,{0x9d,0xcd,0x00,0xa0,0xc9,0x03,0x91,0xd3}};
MIDL_INTERFACE("44EC053B-400F-11D0-9DCD-00A0C90391D3") IRegistrar : IRegistrarBase {
    virtual HRESULT STDMETHODCALLTYPE ResourceRegisterSz(LPCOLESTR, LPCOLESTR, LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResourceUnregisterSz(LPCOLESTR, LPCOLESTR, LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE FileRegister(LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE FileUnregister(LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE StringRegister(LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE StringUnregister(LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResourceRegister(LPCOLESTR, UINT, LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResourceUnregister(LPCOLESTR, UINT, LPCOLESTR) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IRegistrar,0x44ec053b,0x400f,0x11d0,0x9d,0xcd,0x00,0xa0,0xc9,0x03,0x91,0xd3)
#endif
#endif
#ifndef __IDocHostUIHandlerDispatch_INTERFACE_DEFINED__
#define __IDocHostUIHandlerDispatch_INTERFACE_DEFINED__
// Public ATL containment IDL: Automation-safe parameters, in vtable order.
enum DOCHOSTUIDBLCLKDispatch {
    docHostUIDblClkDEFAULT = 0, docHostUIDblClkSHOWPROPERTIES = 1, docHostUIDblClkSHOWCODE = 2
};
enum DocHostUIFlagDispatch {
    docHostUIFlagDIALOG = 1, docHostUIFlagDISABLE_HELP_MENU = 2, docHostUIFlagNO3DBORDER = 4,
    docHostUIFlagSCROLL_NO = 8, docHostUIFlagDISABLE_SCRIPT_INACTIVE = 16,
    docHostUIFlagOPENNEWWIN = 32, docHostUIFlagDISABLE_OFFSCREEN = 64,
    docHostUIFlagFLAT_SCROLLBAR = 128, docHostUIFlagDIV_BLOCKDEFAULT = 256,
    docHostUIFlagACTIVATE_CLIENTHIT_ONLY = 512
};
inline constexpr IID IID_IDocHostUIHandlerDispatch = {0x425b5af0,0x65f1,0x11d1,{0x96,0x11,0x00,0x00,0xf8,0x1e,0x0d,0x0d}};
MIDL_INTERFACE("425B5AF0-65F1-11D1-9611-0000F81E0D0D") IDocHostUIHandlerDispatch : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD, DWORD, DWORD, IUnknown*, IDispatch*, HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHostInfo(DWORD*, DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ShowUI(DWORD, IUnknown*, IUnknown*, IUnknown*, IUnknown*, HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE HideUI() = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateUI() = 0;
    virtual HRESULT STDMETHODCALLTYPE EnableModeless(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnDocWindowActivate(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResizeBorder(long, long, long, long, IUnknown*, VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator(DWORD_PTR, DWORD, DWORD_PTR, DWORD_PTR, BSTR, DWORD, HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOptionKeyPath(BSTR*, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDropTarget(IUnknown*, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetExternal(IDispatch**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD, BSTR, BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE FilterDataObject(IUnknown*, IUnknown**) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IDocHostUIHandlerDispatch,0x425b5af0,0x65f1,0x11d1,0x96,0x11,0x00,0x00,0xf8,0x1e,0x0d,0x0d)
#endif
#endif
inline constexpr IID IID_IAxWinHostWindow = {0xb6ea2050,0x048a,0x11d1,{0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e}};
inline constexpr IID IID_IAxWinAmbientDispatch = {0xb6ea2051,0x048a,0x11d1,{0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e}};
MIDL_INTERFACE("B6EA2050-048A-11D1-82B9-00C04FB9942E") IAxWinHostWindow : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateControl(LPCOLESTR, HWND, IStream*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateControlEx(LPCOLESTR, HWND, IStream*, IUnknown**, REFIID, IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AttachControl(IUnknown*, HWND) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryControl(REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetExternalDispatch(IDispatch*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetExternalUIHandler(IDocHostUIHandlerDispatch*) = 0;
};
MIDL_INTERFACE("B6EA2051-048A-11D1-82B9-00C04FB9942E") IAxWinAmbientDispatch : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE put_AllowWindowlessActivation(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowWindowlessActivation(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BackColor(OLE_COLOR) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BackColor(OLE_COLOR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ForeColor(OLE_COLOR) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ForeColor(OLE_COLOR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_LocaleID(LCID) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LocaleID(LCID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UserMode(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UserMode(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DisplayAsDefault(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayAsDefault(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Font(IFontDisp*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Font(IFontDisp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageReflect(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MessageReflect(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowGrabHandles(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowHatching(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DocHostFlags(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DocHostFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DocHostDoubleClickFlags(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DocHostDoubleClickFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AllowContextMenu(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowContextMenu(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AllowShowUI(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowShowUI(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_OptionKeyPath(BSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OptionKeyPath(BSTR*) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IAxWinHostWindow,0xb6ea2050,0x048a,0x11d1,0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e)
__CRT_UUID_DECL(IAxWinAmbientDispatch,0xb6ea2051,0x048a,0x11d1,0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e)
#endif
#endif
