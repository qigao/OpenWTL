// OpenATL - Clean-room ATL subset for WTL 10.0
// Main ATL module header

#ifndef __ATLBASE_H__
#define __ATLBASE_H__

#pragma once

#ifndef __cplusplus
#error ATL requires C++ compilation
#endif

#include "openatl/platform.h"

#include <windows.h>
#include <ole2.h>
#include <tchar.h>
#include <limits.h>

// Include sub-headers
#include "atldef.h"
#include "atltrace.h"
#include "atlcore.h"
#include "atlsimpcoll.h"
#include "atlalloc.h"
#include "atlreg.h"
#include "atlcomcli.h"
#include "atlconv.h"

#include <commctrl.h>
#include <shlwapi.h>

// The pinned llvm-mingw uxtheme.h declares GetThemePartSize(RECT*), although
// original WTL10's public method correctly accepts LPCRECT. Preserve WTL's
// untouched public API: provide one C++ const-input Win32 ABI overload only
// where the MinGW SDK signature requires a mutable pointer. Win32 consumes
// the rectangle as input; no borrowed const memory is ever cast away.
#if defined(__MINGW32__)
#include <uxtheme.h>
inline HRESULT WINAPI GetThemePartSize(HTHEME theme, HDC dc, int part, int state,
                                        const RECT* input, THEMESIZE size,
                                        SIZE* result) noexcept {
    RECT copied{};
    if (input) copied = *input;
    return ::GetThemePartSize(theme, dc, part, state,
                              input ? &copied : nullptr, size, result);
}
#endif

#if defined(__MINGW32__)
#include <type_traits>
#endif

namespace ATL {

#if defined(__MINGW32__)
// libc++ removes Win32's min/max macros. Preserve ordinary ATL sample calls,
// including mixed int/LONG operands, without restoring macros over the STL.
template<class A, class B>
constexpr std::common_type_t<A, B> (min)(A a, B b) { return b < a ? b : a; }
template<class A, class B>
constexpr std::common_type_t<A, B> (max)(A a, B b) { return a < b ? b : a; }
#endif

///////////////////////////////////////////////////////////////////////////////
// ATL::Checked namespace - secure CRT wrappers

namespace Checked {

inline void strcpy_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcpy_s(dest, size, src);
}

inline void wcscpy_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscpy_s(dest, size, src);
}

inline errno_t strncpy_s(char* dest, size_t size, const char* src, size_t count) noexcept
{
    return ::strncpy_s(dest, size, src, count);
}

inline errno_t wcsncpy_s(wchar_t* dest, size_t size, const wchar_t* src, size_t count) noexcept
{
    return ::wcsncpy_s(dest, size, src, count);
}

inline void strcat_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcat_s(dest, size, src);
}

inline void wcscat_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscat_s(dest, size, src);
}

inline void memcpy_s(void* dest, size_t destSize, const void* src, size_t count) noexcept
{
    ::memcpy_s(dest, destSize, src, count);
}

inline void memmove_s(void* dest, size_t destSize, const void* src, size_t count) noexcept
{
    ::memmove_s(dest, destSize, src, count);
}

// TCHAR variants
#ifdef _UNICODE
inline void tcscpy_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscpy_s(dest, size, src);
}

inline errno_t tcsncpy_s(wchar_t* dest, size_t size, const wchar_t* src, size_t count) noexcept
{
    return ::wcsncpy_s(dest, size, src, count);
}

inline void tcscat_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscat_s(dest, size, src);
}
#else
inline void tcscpy_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcpy_s(dest, size, src);
}

inline errno_t tcsncpy_s(char* dest, size_t size, const char* src, size_t count) noexcept
{
    return ::strncpy_s(dest, size, src, count);
}

inline void tcscat_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcat_s(dest, size, src);
}
#endif

} // namespace Checked

///////////////////////////////////////////////////////////////////////////////
// Threading models

class CComMultiThreadModelNoCS;
class CComMultiThreadModel {
public:
    using ThreadModelNoCS = CComMultiThreadModelNoCS;
    typedef CComAutoCriticalSection AutoCriticalSection;
    typedef CComAutoDeleteCriticalSection AutoDeleteCriticalSection;
    typedef CComCriticalSection CriticalSection;

    static ULONG WINAPI Increment(LONG volatile* p) noexcept
    {
        return ::InterlockedIncrement(p);
    }

    static ULONG WINAPI Decrement(LONG volatile* p) noexcept
    {
        return ::InterlockedDecrement(p);
    }
};

class CComSingleThreadModel {
public:
    using ThreadModelNoCS = CComSingleThreadModel;
    typedef CComFakeCriticalSection AutoCriticalSection;
    typedef CComFakeCriticalSection AutoDeleteCriticalSection;
    typedef CComFakeCriticalSection CriticalSection;

    static ULONG WINAPI Increment(LONG volatile* p) noexcept
    {
        return (ULONG)++(*p);
    }

    static ULONG WINAPI Decrement(LONG volatile* p) noexcept
    {
        return (ULONG)--(*p);
    }
};

class CComMultiThreadModelNoCS {
public:
    using ThreadModelNoCS = CComMultiThreadModelNoCS;
    typedef CComFakeCriticalSection AutoCriticalSection;
    typedef CComFakeCriticalSection AutoDeleteCriticalSection;
    typedef CComFakeCriticalSection CriticalSection;

    static ULONG WINAPI Increment(LONG volatile* p) noexcept
    {
        return ::InterlockedIncrement(p);
    }

    static ULONG WINAPI Decrement(LONG volatile* p) noexcept
    {
        return ::InterlockedDecrement(p);
    }
};

// Default thread model typedefs
#if defined(_ATL_SINGLE_THREADED)
typedef CComSingleThreadModel CComObjectThreadModel;
typedef CComSingleThreadModel CComGlobalsThreadModel;
#elif defined(_ATL_APARTMENT_THREADED)
typedef CComSingleThreadModel CComObjectThreadModel;
typedef CComMultiThreadModel CComGlobalsThreadModel;
#else // _ATL_FREE_THREADED
typedef CComMultiThreadModel CComObjectThreadModel;
typedef CComMultiThreadModel CComGlobalsThreadModel;
#endif

///////////////////////////////////////////////////////////////////////////////
// CComCritSecLock - RAII lock

template <typename TLock>
class CComCritSecLock {
public:
    CComCritSecLock(TLock& cs, bool bInitialLock = true)
        : m_cs(cs), m_bLocked(false)
    {
        if (bInitialLock) {
            HRESULT hr = Lock();
            if (FAILED(hr))
                AtlThrow(hr);
        }
    }

    ~CComCritSecLock()
    {
        if (m_bLocked)
            Unlock();
    }

    HRESULT Lock() noexcept
    {
        ATLASSERT(!m_bLocked);
        HRESULT hr = m_cs.Lock();
        if (SUCCEEDED(hr))
            m_bLocked = true;
        return hr;
    }

    void Unlock() noexcept
    {
        ATLASSERT(m_bLocked);
        m_cs.Unlock();
        m_bLocked = false;
    }

private:
    TLock& m_cs;
    bool m_bLocked;

    CComCritSecLock(const CComCritSecLock&) = delete;
    CComCritSecLock& operator=(const CComCritSecLock&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// CHandle - HANDLE wrapper

class CHandle {
public:
    HANDLE m_h;

    CHandle() noexcept : m_h(NULL) {}
    CHandle(CHandle&& h) noexcept : m_h(h.m_h) { h.m_h = NULL; }
    explicit CHandle(HANDLE h) noexcept
        : m_h(h == INVALID_HANDLE_VALUE ? NULL : h) {}

    ~CHandle() { Close(); }

    CHandle& operator=(CHandle&& h) noexcept
    {
        if (this != &h) {
            // Failed CloseHandle must not lose the existing owner or
            // steal another handle into an already-occupied wrapper.
            Close();
            if (m_h == NULL) {
                m_h = h.m_h;
                h.m_h = NULL;
            }
        }
        return *this;
    }

    operator HANDLE() const noexcept { return m_h; }

    void Attach(HANDLE h) noexcept
    {
        // Never overwrite an owned handle in release builds.
        if (m_h == h) return;
        if (m_h != NULL) {
            ATLASSERT(m_h == NULL);
            return;
        }
        m_h = h == INVALID_HANDLE_VALUE ? NULL : h;
    }

    HANDLE Detach() noexcept
    {
        HANDLE h = m_h;
        m_h = NULL;
        return h;
    }

    void Close() noexcept
    {
        if (m_h == INVALID_HANDLE_VALUE) {
            m_h = NULL; // Invalid sentinel never owns a Win32 resource.
            return;
        }
        if (m_h != NULL && ::CloseHandle(m_h))
            m_h = NULL;
        // A failed CloseHandle leaves the original owner intact so an
        // explicit subsequent Close can retry or inspect GetLastError.
    }

private:
    CHandle(const CHandle&) = delete;
    CHandle& operator=(const CHandle&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// CComAllocator

class CComAllocator {
public:
    static void* Allocate(size_t nBytes) noexcept
    {
        return ::CoTaskMemAlloc(nBytes);
    }

    static void* Reallocate(void* p, size_t nBytes) noexcept
    {
        return ::CoTaskMemRealloc(p, nBytes);
    }

    static void Free(void* p) noexcept
    {
        ::CoTaskMemFree(p);
    }
};

///////////////////////////////////////////////////////////////////////////////
// InlineIsEqualUnknown

inline BOOL InlineIsEqualUnknown(REFGUID rguid) noexcept
{
    return (
        ((DWORD*)&rguid)[0] == 0 &&
        ((DWORD*)&rguid)[1] == 0 &&
        ((DWORD*)&rguid)[2] == 0x000000C0 &&
        ((DWORD*)&rguid)[3] == 0x46000000);
}

///////////////////////////////////////////////////////////////////////////////
// AtlHresultFromLastError

inline HRESULT AtlHresultFromLastError() noexcept
{
    DWORD dwErr = ::GetLastError();
    return HRESULT_FROM_WIN32(dwErr);
}

///////////////////////////////////////////////////////////////////////////////
// _ATL_RT_DLGINIT resource type

#ifndef _ATL_RT_DLGINIT
#define _ATL_RT_DLGINIT MAKEINTRESOURCE(240)
#endif

///////////////////////////////////////////////////////////////////////////////
// Pure Win32 packed dialog-resource layouts

// Plain extended-dialog layout required by WTL CMemDlgTemplate.
// These are Win32 UI resource structures, not the former ActiveX
// _DialogSplitHelper parser or any COM Server machinery.
namespace _DialogSplitHelper {
#pragma pack(push, 1)
struct DLGTEMPLATEEX {
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x, y, cx, cy;
};
struct DLGITEMTEMPLATEEX {
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    short x, y, cx, cy;
    DWORD id;
};
#pragma pack(pop)
static_assert(sizeof(DLGTEMPLATEEX) == 26, "Win32 DLGTEMPLATEEX packed layout");
static_assert(sizeof(DLGITEMTEMPLATEEX) == 24, "Win32 DLGITEMTEMPLATEEX packed layout");
} // namespace _DialogSplitHelper

///////////////////////////////////////////////////////////////////////////////
// Forward declarations

struct _AtlCreateWndData;
class CAtlWinModule;

///////////////////////////////////////////////////////////////////////////////
// _AtlCreateWndData

struct _AtlCreateWndData {
    void* m_pThis;
    DWORD m_dwThreadID;
    _AtlCreateWndData* m_pNext;
};

///////////////////////////////////////////////////////////////////////////////
// _ATL_WIN_MODULE

struct _ATL_WIN_MODULE70 {
    UINT cbSize;
    CComCriticalSection m_csWindowCreate;
    _AtlCreateWndData* m_pCreateWndList;
    CSimpleArray<ATOM> m_rgWindowClassAtoms;
};

typedef _ATL_WIN_MODULE70 _ATL_WIN_MODULE;

///////////////////////////////////////////////////////////////////////////////
// CAtlWinModule

class CAtlWinModule : public _ATL_WIN_MODULE {
public:
    CAtlWinModule() noexcept
    {
        cbSize = sizeof(_ATL_WIN_MODULE);
        m_pCreateWndList = NULL;
        m_csWindowCreate.Init();
    }

    ~CAtlWinModule()
    {
        m_csWindowCreate.Term();
    }

    void AddCreateWndData(_AtlCreateWndData* pData, void* pObject) noexcept
    {
        ATLASSERT(pData != NULL && pObject != NULL);
        pData->m_pThis = pObject;
        pData->m_dwThreadID = ::GetCurrentThreadId();

        CComCritSecLock<CComCriticalSection> lock(m_csWindowCreate, false);
        if (SUCCEEDED(lock.Lock())) {
            pData->m_pNext = m_pCreateWndList;
            m_pCreateWndList = pData;
        }
    }

    // Remove a stack-local pending creation record even when Win32 fails
    // before invoking StartWindowProc. A stale entry would otherwise be
    // consumed by a subsequent unrelated window on this thread.
    bool RemoveCreateWndData(_AtlCreateWndData* pending) noexcept
    {
        if (pending == NULL) return false;
        CComCritSecLock<CComCriticalSection> lock(m_csWindowCreate, false);
        if (FAILED(lock.Lock())) return false;
        _AtlCreateWndData** link = &m_pCreateWndList;
        while (*link != NULL) {
            if (*link == pending) {
                *link = pending->m_pNext;
                pending->m_pNext = NULL;
                return true;
            }
            link = &(*link)->m_pNext;
        }
        return false;
    }

    void* ExtractCreateWndData() noexcept
    {
        CComCritSecLock<CComCriticalSection> lock(m_csWindowCreate, false);
        if (FAILED(lock.Lock()))
            return NULL;

        void* pv = NULL;
        DWORD dwThreadID = ::GetCurrentThreadId();
        _AtlCreateWndData* pEntry = m_pCreateWndList;
        _AtlCreateWndData* pPrev = NULL;

        while (pEntry != NULL) {
            if (pEntry->m_dwThreadID == dwThreadID) {
                pv = pEntry->m_pThis;
                if (pPrev == NULL)
                    m_pCreateWndList = pEntry->m_pNext;
                else
                    pPrev->m_pNext = pEntry->m_pNext;
                break;
            }
            pPrev = pEntry;
            pEntry = pEntry->m_pNext;
        }
        return pv;
    }
};

__declspec(selectany) CAtlWinModule _AtlWinModule;

///////////////////////////////////////////////////////////////////////////////
// AtlGetCommCtrlVersion - forward declaration
// Full implementation is provided by WTL's atlapp.h for _ATL_VER >= 0x0B00

HRESULT AtlGetCommCtrlVersion(LPDWORD pdwMajor, LPDWORD pdwMinor);

} // namespace ATL

#include "openatl/atl_typelib.h"
#include "openatl/atl_module.h"

#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using namespace ATL;
#endif

// For headers that use the ATL:: prefix
using ATL::_AtlBaseModule;
using ATL::_AtlWinModule;

#endif // __ATLBASE_H__
