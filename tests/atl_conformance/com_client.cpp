/* OpenATL optional COM CLIENT: apartment ownership and actual Windows
 * system COM activation. No app-owned COM class, server, registrar or RGS.
 *
 * Each invocation is a fresh process/thread, making apartment checks
 * independent and permitting exact repetition on Windows x64 and ARM64.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <utility>
#include <atlbase.h>
#include <atlcomcli.h>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "This target must compile repository-owned OpenATL headers"
#endif

#if defined(__RATL__)
#error "COM client must use pinned clean-room OpenATL headers"
#endif

static int failure(int code, const char* step, HRESULT hr) {
    std::fprintf(stderr, "ATL_COM_CLIENT_FAIL code=%d step=%s hr=0x%08lx\n",
                 code, step, static_cast<unsigned long>(hr));
    return code;
}
static bool no_apartment() {
    ULONG_PTR context=0;
    return ::CoGetContextToken(&context)==CO_E_NOTINITIALIZED;
}


// In-process test double only: never registered, activated or published.
// It rejects QueryInterface to check failed COM identity claims.
struct BrokenIdentity final : IUnknown {
    ULONG refs = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};

static int com_client_ownership_regressions(IGlobalInterfaceTable* real) {
    ATL::CComPtr<IGlobalInterfaceTable> empty;
    IUnknown* stale = reinterpret_cast<IUnknown*>(static_cast<uintptr_t>(1));
    if (empty.QueryInterface(&stale) != E_POINTER || stale != nullptr ||
        empty.QueryInterface<IUnknown>(nullptr) != E_POINTER)
        return failure(50, "null_QueryInterface", E_FAIL);

    ATL::CComPtr<IGlobalInterfaceTable> git(real);
    IUnknown* id = nullptr;
    HRESULT hr = git.QueryInterface(&id);
    if (FAILED(hr) || id == nullptr)
        return failure(51, "valid_QueryInterface", hr);
    ATL::CComPtr<IUnknown> identity;
    identity.Attach(id);
    if (!git.IsEqualObject(id))
        return failure(52, "same_COM_identity", E_FAIL);
    git.Attach(git.p);
    if (!git.p || !git.IsEqualObject(id))
        return failure(53, "self_Attach_refcount", E_FAIL);

    BrokenIdentity b1, b2;
    ATL::CComPtr<IUnknown> broken(&b1);
    if (broken.IsEqualObject(&b2))
        return failure(54, "two_failed_QIs_are_not_equal", E_FAIL);
    ATL::CComPtr<IUnknown> moved(&b2);
    moved = std::move(broken);
    if (broken.p || moved.p != &b1 || b1.refs != 1 || b2.refs != 0)
        return failure(72, "CComPtr_move_replaces_owner", E_FAIL);
    auto& moveAlias = moved;
    moved = std::move(moveAlias);
    if (moved.p != &b1 || b1.refs != 1)
        return failure(73, "CComPtr_self_move_preserves_owner", E_FAIL);
    moved = std::move(broken);
    if (moved.p || b1.refs != 0 || b2.refs != 0)
        return failure(74, "CComPtr_move_empty_releases_owner", E_FAIL);

    ATL::CComPtr<IGlobalInterfaceTable> created;
    hr = created.CoCreateInstance(CLSID_StdGlobalInterfaceTable,
                                  nullptr, CLSCTX_INPROC_SERVER);
    if (FAILED(hr) || created.p == nullptr)
        return failure(55, "CComPtr_CoCreateInstance", hr);
    IGlobalInterfaceTable* original = created.p;
    hr = created.CoCreateInstance(CLSID_StdGlobalInterfaceTable,
                                  nullptr, CLSCTX_INPROC_SERVER);
    if (hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) ||
        created.p != original)
        return failure(56, "CComPtr_no_overwrite", hr);

    ATL::CComBSTR text(L"alpha");
    LPCOLESTR alias = text.m_str;
    text = alias;
    if (text.Length() != 5 || !text.m_str ||
        std::wcscmp(text.m_str,L"alpha") != 0)
        return failure(57, "BSTR_self_assignment", E_FAIL);
    if (text.Append(text.m_str, static_cast<int>(text.Length())) != S_OK ||
        text.Length() != 10 || std::wcscmp(text.m_str,L"alphaalpha") != 0)
        return failure(58, "BSTR_self_append", E_FAIL);
    if (text.Append(L"x", -1) != E_INVALIDARG ||
        text.Append(nullptr, 3) != E_POINTER ||
        text.Append(L"x", (std::numeric_limits<int>::max)()) != E_INVALIDARG ||
        text.Length() != 10)
        return failure(59, "BSTR_invalid_length", E_FAIL);

    const OLECHAR embeddedA[3] = {L'A',0,L'B'};
    const OLECHAR embeddedB[3] = {L'A',0,L'C'};
    ATL::CComBSTR a(3,embeddedA), same(3,embeddedA), b(3,embeddedB);
    if (!a.m_str || !same.m_str || !b.m_str ||
        a.Length() != 3 || !(a == same) || a == b || !(a < b))
        return failure(60, "binary_BSTR_comparison", E_FAIL);
    ATL::CComBSTR negative(-1,L"hello");
    if (negative.m_str != nullptr)
        return failure(61, "BSTR_negative_constructor", E_FAIL);

    ATL::CComVariant va;
    va.vt = VT_BSTR;
    va.bstrVal = ::SysAllocStringLen(embeddedA,3);
    ATL::CComVariant vb;
    vb.vt = VT_BSTR;
    vb.bstrVal = ::SysAllocStringLen(embeddedB,3);
    if (!va.bstrVal || !vb.bstrVal ||
        va == static_cast<const VARIANT&>(vb))
        return failure(62, "binary_VARIANT_BSTR_equality", E_FAIL);
    ATL::CComVariant copied(va);
    if (!(copied == static_cast<const VARIANT&>(va)) ||
        copied.Copy(static_cast<const VARIANT*>(&copied)) != S_OK ||
        copied.Copy(nullptr) != E_POINTER)
        return failure(63, "VARIANT_copy_and_alias", E_FAIL);
    copied = static_cast<const VARIANT&>(copied);
    if (!(copied == static_cast<const VARIANT&>(va)))
        return failure(64, "VARIANT_self_assignment", E_FAIL);
    if (copied.Detach(nullptr) != E_POINTER ||
        copied.Detach(static_cast<VARIANT*>(&copied)) != E_INVALIDARG ||
        copied.vt != VT_BSTR)
        return failure(65, "VARIANT_reject_self_Detach", E_FAIL);
    VARIANT transferred;
    ::VariantInit(&transferred);
    hr = copied.Detach(&transferred);
    if (FAILED(hr) || copied.vt != VT_EMPTY || transferred.vt != VT_BSTR ||
        ::SysStringLen(transferred.bstrVal) != 3) {
        (void)::VariantClear(&transferred);
        return failure(66, "VARIANT_transfer_ownership", hr);
    }
    (void)::VariantClear(&transferred);

    ATL::CComVariant live(L"another");
    LPCOLESTR source = live.bstrVal;
    live = source;
    if (live.vt != VT_BSTR || !live.bstrVal ||
        std::wcscmp(live.bstrVal,L"another") != 0)
        return failure(67, "VARIANT_BSTR_self_alias", E_FAIL);
    // VariantClear refuses to destroy a locked SAFEARRAY. Assignment
    // must retain its original tag/handle until the lock is released.
    SAFEARRAY* lockedArray = ::SafeArrayCreateVector(VT_UI1, 0, 3);
    if (lockedArray == nullptr)
        return failure(68, "VARIANT_locked_array_setup", E_OUTOFMEMORY);
    ATL::CComVariant guarded;
    guarded.vt = VT_ARRAY | VT_UI1;
    guarded.parray = lockedArray;
    hr = ::SafeArrayLock(lockedArray);
    if (FAILED(hr))
        return failure(69, "VARIANT_array_lock", hr);
    guarded = 314;
    const bool retained = guarded.vt == (VT_ARRAY | VT_UI1) &&
                          guarded.parray == lockedArray;
    hr = ::SafeArrayUnlock(lockedArray);
    if (FAILED(hr) || !retained) {
        if (!retained)
            (void)::SafeArrayDestroy(lockedArray);
        return failure(70, "VARIANT_locked_array_owner_preserved", hr);
    }
    guarded = 314;
    if (guarded.vt != VT_I4 || guarded.lVal != 314)
        return failure(71, "VARIANT_assignment_after_unlock", E_FAIL);
    return 0;
}

// IGlobalInterfaceTable is an OS-provided COM *client* target, not our own
// factory, service or registration. No COM object survives apartment exit.
static int system_component_client() {
    IGlobalInterfaceTable* raw=nullptr;
    HRESULT hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
        CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
        reinterpret_cast<void**>(&raw));
    if(FAILED(hr)||raw==nullptr)return failure(11,"CoCreateInstance_GIT",hr);
    {
        ATL::CComPtr<IGlobalInterfaceTable> git;
        git.Attach(raw);
        raw=nullptr;
        ATL::CComPtr<IGlobalInterfaceTable> shared(git);
        if(!shared.p || shared.p!=git.p)
            return failure(12,"CComPtr_copy",E_FAIL);

        IUnknown* identity=nullptr;
        hr=git->QueryInterface(IID_IUnknown,
               reinterpret_cast<void**>(&identity));
        if(FAILED(hr)||identity==nullptr)
            return failure(13,"QueryInterface_IUnknown",hr);
        ATL::CComPtr<IUnknown> owner;
        owner.Attach(identity);
        if(owner.p!=identity)return failure(14,"CComPtr_Attach",E_FAIL);
        IUnknown* detached=owner.Detach();
        if(owner.p!=nullptr||detached!=identity)
            return failure(15,"CComPtr_Detach",E_FAIL);
        detached->Release();

        ATL::CComBSTR name(L"Client");
        ATL::CComVariant number(42);
        if(name.Length()!=6 || name.m_str==nullptr ||
           number.vt!=VT_I4 || number.lVal!=42)
            return failure(16,"BSTR_and_VARIANT",E_FAIL);
        if (int result = com_client_ownership_regressions(git.p))
            return result;
    }
    return 0;
}

static int run_uninitialized() {
    if(!no_apartment())return failure(20,"unexpected_initial_apartment",E_FAIL);
    IGlobalInterfaceTable* object=nullptr;
    const HRESULT hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
        CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
        reinterpret_cast<void**>(&object));
    if(hr!=CO_E_NOTINITIALIZED || object!=nullptr) {
        if(object)object->Release();
        return failure(21,"activation_requires_apartment",hr);
    }
    if(!no_apartment())return failure(22,"negative_test_implicitly_initialized",E_FAIL);
    std::puts("ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1");
    return 0;
}

static int run_apartment(const char* mode,bool mta,bool changed) {
    if(!no_apartment())return failure(30,"fresh_thread_uninitialized",E_FAIL);
    const DWORD flags=mta?COINIT_MULTITHREADED:COINIT_APARTMENTTHREADED;
    const DWORD opposite=mta?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED;
    HRESULT hr=::CoInitializeEx(nullptr,flags);
    if(hr!=S_OK)return failure(31,"first_CoInitializeEx",hr);

    int result=0;
    ULONG_PTR context=0;
    hr=::CoGetContextToken(&context);
    if(hr!=S_OK)
        result=failure(32,"apartment_context_token",hr);

    if(!result&&changed) {
        hr=::CoInitializeEx(nullptr,opposite);
        if(hr!=RPC_E_CHANGED_MODE) {
            // Defensive cleanup if an unexpected successful initialization
            // was returned. A failed changed-mode call needs no Uninit.
            if(SUCCEEDED(hr))::CoUninitialize();
            result=failure(33,"changed_apartment_model_must_fail",hr);
        }
        if(!result && ::CoGetContextToken(&context)!=S_OK)
            result=failure(34,"changed_mode_damaged_apartment",E_FAIL);
    }

    if(!result) {
        // Same model returns S_FALSE and still needs its own CoUninitialize.
        hr=::CoInitializeEx(nullptr,flags);
        if(hr!=S_FALSE) {
            if(SUCCEEDED(hr))::CoUninitialize();
            result=failure(35,"same_model_nested_initialize",hr);
        } else {
            ::CoUninitialize();
            if(::CoGetContextToken(&context)!=S_OK)
                result=failure(36,"nested_CoUninitialize_unbalanced",E_FAIL);
        }
    }
    if(!result)result=system_component_client();

    ::CoUninitialize(); // Exactly once for the original S_OK
    if(!result && !no_apartment())
        result=failure(37,"COM_apartment_leaked",E_FAIL);
    if(result)return result;

    // Keep the original default output stable for legacy consumer tests.
    if(std::strcmp(mode,"default")==0)
        std::puts("ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0");
    else
        std::printf("ATL_COM_CLIENT_MODE_OK mode=%s apartment=1 activation=1 balance=1 changed=%d\n",
                    mode,changed?1:0);
    return 0;
}

int main(int argc,char** argv) {
    if(argc==1)return run_apartment("default",false,false);
    if(argc!=2)return failure(40,"unexpected_arguments",E_INVALIDARG);
    const char* mode=argv[1];
    if(std::strcmp(mode,"--sta")==0)
        return run_apartment("sta",false,false);
    if(std::strcmp(mode,"--mta")==0)
        return run_apartment("mta",true,false);
    if(std::strcmp(mode,"--changed-sta")==0)
        return run_apartment("changed-sta",false,true);
    if(std::strcmp(mode,"--changed-mta")==0)
        return run_apartment("changed-mta",true,true);
    if(std::strcmp(mode,"--uninitialized")==0)
        return run_uninitialized();
    return failure(41,"unsupported_mode",E_INVALIDARG);
}
