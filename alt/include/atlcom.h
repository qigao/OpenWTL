// OpenATL - clean-room ATL objects, aggregation and COM-map compatibility.
#ifndef __ATLCOM_H__
#define __ATLCOM_H__
#pragma once
#include "atlbase.h"
#include "atlcomcli.h"
#include "openatl/com_server.h"
#include "openatl/rgs_resource.h"
#include "openatl/atl_dispatch.h"
#include <ocidl.h>  // IObjectWithSite, native COM interface
#include <optional>

namespace ATL {

// Site ownership is a normal COM AddRef/Release contract; WTL's original
// PrintDlgEx callback uses this helper. No application COM apartment is
// created by storing or querying a site, and no second COM provider exists.
template<class T>
class IObjectWithSiteImpl : public IObjectWithSite {
public:
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) noexcept override {
        m_spUnkSite = site;  // AddRef new before releasing old; self-safe.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!m_spUnkSite.p) return E_FAIL;
        return m_spUnkSite.p->QueryInterface(iid, output);
    }
protected:
    CComPtr<IUnknown> m_spUnkSite;
};

// Native OLE error objects carry copied metadata independently of the server
// object's lifetime. No application apartment is initialized by these helpers.
namespace detail {
inline HRESULT ReportErrorWithoutInfo(HRESULT result) noexcept {
    ::SetErrorInfo(0, nullptr);
    return result;
}
inline HRESULT ReportErrorWide(REFCLSID clsid, LPCOLESTR description,
    DWORD helpContext, LPCOLESTR helpFile, REFIID iid, HRESULT result) noexcept {
    ::SetErrorInfo(0, nullptr); // Never leave an earlier error on a failed setup.
    CComPtr<ICreateErrorInfo> create;
    if (FAILED(::CreateErrorInfo(&create.p)) || !create.p) return result;
    if (FAILED(create->SetGUID(iid)) ||
        FAILED(create->SetDescription(const_cast<LPOLESTR>(description ? description : L""))) ||
        FAILED(create->SetHelpContext(helpContext)) ||
        FAILED(create->SetHelpFile(const_cast<LPOLESTR>(helpFile ? helpFile : L"")))) return result;
    LPOLESTR progId = nullptr;
    const HRESULT lookup = ::ProgIDFromCLSID(clsid, &progId);
    const HRESULT source = SUCCEEDED(lookup) && progId ? create->SetSource(progId) : S_OK;
    ::CoTaskMemFree(progId);
    if (FAILED(source)) return result;
    CComPtr<IErrorInfo> error;
    if (SUCCEEDED(create->QueryInterface(IID_IErrorInfo, reinterpret_cast<void**>(&error.p))) && error.p)
        ::SetErrorInfo(0, error.p);
    return result; // Reporting metadata must not replace the documented HRESULT.
}
inline bool ErrorTextFromAnsi(LPCSTR text, CComBSTR& output) noexcept {
    if (!text) return true;
    const int count = ::MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (count <= 0) return false;
    output.Attach(::SysAllocStringLen(nullptr, static_cast<UINT>(count - 1)));
    if (!output.m_str) return false;
    return ::MultiByteToWideChar(CP_ACP, 0, text, -1, output.m_str, count) == count;
}
} // namespace detail

inline HRESULT WINAPI AtlReportError(REFCLSID clsid, LPCOLESTR description,
    DWORD helpContext, LPCOLESTR helpFile, REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
    return detail::ReportErrorWide(clsid, description, helpContext, helpFile,
        iid, result ? result : DISP_E_EXCEPTION);
}
inline HRESULT WINAPI AtlReportError(REFCLSID clsid, LPCOLESTR description,
    REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
    return AtlReportError(clsid, description, 0, static_cast<LPCOLESTR>(nullptr), iid, result);
}
inline HRESULT WINAPI AtlReportError(REFCLSID clsid, LPCSTR description,
    DWORD helpContext, LPCSTR helpFile, REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
    const HRESULT returned = result ? result : DISP_E_EXCEPTION;
    CComBSTR wideDescription, wideHelp;
    if (!detail::ErrorTextFromAnsi(description, wideDescription) ||
        !detail::ErrorTextFromAnsi(helpFile, wideHelp)) return detail::ReportErrorWithoutInfo(returned);
    return detail::ReportErrorWide(clsid, wideDescription.m_str, helpContext, wideHelp.m_str, iid, returned);
}
inline HRESULT WINAPI AtlReportError(REFCLSID clsid, LPCSTR description,
    REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
    return AtlReportError(clsid, description, 0, static_cast<LPCSTR>(nullptr), iid, result);
}
inline HRESULT WINAPI AtlReportError(REFCLSID clsid, UINT id, DWORD helpContext,
    LPCOLESTR helpFile, REFIID iid = GUID_NULL, HRESULT result = 0,
    HINSTANCE image = _AtlBaseModule.GetResourceInstance()) noexcept {
    const HRESULT returned = result ? result : MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, id);
    ATLASSERT(id >= 0x200 && id <= 0xffff);
    LPCWSTR resource = nullptr;
    const int length = id >= 0x200 && id <= 0xffff
        ? ::LoadStringW(image, id, reinterpret_cast<LPWSTR>(&resource), 0) : 0;
    ATLASSERT(length > 0 && resource);
    if (length <= 0 || !resource)
        return detail::ReportErrorWide(clsid, L"Unknown Error.", helpContext, helpFile, iid, returned);
    // LoadString's pointer form has a length, but no trailing zero. Copy the
    // entire resource instead of truncating it into a fixed stack buffer.
    CComBSTR description;
    description.Attach(::SysAllocStringLen(resource, static_cast<UINT>(length)));
    if (!description.m_str) return detail::ReportErrorWithoutInfo(returned);
    return detail::ReportErrorWide(clsid, description.m_str, helpContext, helpFile, iid, returned);
}
inline HRESULT WINAPI AtlReportError(REFCLSID clsid, UINT id,
    REFIID iid = GUID_NULL, HRESULT result = 0,
    HINSTANCE image = _AtlBaseModule.GetResourceInstance()) noexcept {
    return AtlReportError(clsid, id, 0, static_cast<LPCOLESTR>(nullptr), iid, result, image);
}

template<const IID* InterfaceId>
class ATL_NO_VTABLE ISupportErrorInfoImpl : public ISupportErrorInfo {
public:
    HRESULT STDMETHODCALLTYPE InterfaceSupportsErrorInfo(REFIID iid) noexcept override {
        return IsEqualIID(iid, *InterfaceId) ? S_OK : S_FALSE;
    }
};

class CComObjectRootBase {
public:
    HRESULT FinalConstruct() noexcept { return S_OK; }
    void FinalRelease() noexcept {}
protected:
    CComObjectRootBase() noexcept = default;
    ~CComObjectRootBase() = default;
};

template<class ThreadModel>
class CComObjectRootEx : public CComObjectRootBase {
public:
    using _ThreadModel = ThreadModel;
    virtual IUnknown* GetControllingUnknown() noexcept { return m_pOuterUnknown; }
    ULONG InternalAddRef() noexcept {
        ULONG count = ThreadModel::Increment(&m_dwRef);
        if (count == 0) std::terminate();
        return count;
    }
    ULONG InternalRelease() noexcept {
        ULONG count = ThreadModel::Decrement(&m_dwRef);
        if (count == (std::numeric_limits<ULONG>::max)()) std::terminate();
        return count;
    }
    HRESULT OuterQueryInterface(REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        return m_pOuterUnknown ? m_pOuterUnknown->QueryInterface(iid, output) : E_UNEXPECTED;
    }
    ULONG OuterAddRef() noexcept {
        if (!m_pOuterUnknown) std::terminate();
        return m_pOuterUnknown->AddRef();
    }
    ULONG OuterRelease() noexcept {
        if (!m_pOuterUnknown) std::terminate();
        return m_pOuterUnknown->Release();
    }
    void InternalFinalConstructAddRef() noexcept { InternalAddRef(); }
    void InternalFinalConstructRelease() noexcept { InternalRelease(); }
    void Lock() noexcept { m_criticalSection.Lock(); }
    void Unlock() noexcept { m_criticalSection.Unlock(); }
    CComObjectRootEx(const CComObjectRootEx&) = delete;
    CComObjectRootEx& operator=(const CComObjectRootEx&) = delete;
protected:
    CComObjectRootEx() noexcept = default;
    ~CComObjectRootEx() = default;
    LONG m_dwRef = 0;
    IUnknown* m_pOuterUnknown = nullptr; // Borrowed controlling identity; never AddRef here.
private:
    typename ThreadModel::AutoCriticalSection m_criticalSection;
};
using CComObjectRoot = CComObjectRootEx<CComObjectThreadModel>;

namespace detail {
// The first base is destroyed last, after the user's Base destructor.
class AtlObjectModulePin {
public:
    AtlObjectModulePin() noexcept : application_(_pAtlModule) {
        if (application_) application_->Lock();
    }
    ~AtlObjectModulePin() {
        reference_.reset();
        if (application_) application_->Unlock();
    }
protected:
    HRESULT BindModule(OpenATL::COM::Module& module) noexcept {
        reference_.emplace(module);
        return reference_->Status();
    }
private:
    CAtlModule* application_;
    std::optional<OpenATL::COM::ModuleReference> reference_;
};
inline HRESULT AtlObjectException() noexcept {
    try { throw; }
    catch (const CAtlException& e) { return FAILED(e.m_hr) ? e.m_hr : E_UNEXPECTED; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_UNEXPECTED; }
}
} // namespace detail

template<class Base>
class CComObject : private detail::AtlObjectModulePin, public Base {
public:
    explicit CComObject(void* = nullptr) {}
    ~CComObject() { this->FinalRelease(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        return this->_InternalQueryInterface(iid, output);
    }
    template<class Q>
    HRESULT QueryInterface(Q** output) noexcept {
        return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(output));
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return this->InternalAddRef(); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = this->InternalRelease();
        if (!count) delete this;
        return count;
    }
    IUnknown* GetControllingUnknown() noexcept override { return this->GetUnknown(); }
    // ATL's local creator returns a raw object with reference count ZERO.
    static HRESULT WINAPI CreateInstance(CComObject** output) noexcept {
        return CreateObject(nullptr, output);
    }
    // Explicit DLL creator: the object pins this module through destruction.
    static HRESULT CreateInstance(OpenATL::COM::Module& module, REFIID iid,
                                  void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComObject* object = nullptr;
        HRESULT hr = CreateObject(&module, &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
private:
    static HRESULT CreateObject(OpenATL::COM::Module* module, CComObject** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComObject* object = nullptr;
        try { object = new CComObject; }
        catch (...) { return detail::AtlObjectException(); }
        HRESULT hr = module ? object->BindModule(*module) : S_OK;
        if (SUCCEEDED(hr)) {
            // A temporary reference protects QueryInterface/Release inside
            // FinalConstruct; it is removed without deleting the zero-ref object.
            object->InternalFinalConstructAddRef();
            try { hr = object->FinalConstruct(); }
            catch (...) { hr = detail::AtlObjectException(); }
            object->InternalFinalConstructRelease();
        }
        if (FAILED(hr)) { delete object; return hr; }
        *output = object;
        return S_OK;
    }
};

// The contained interfaces delegate identity/refcounts to the outer object.
// The separate CComAggObject IUnknown owns the inner allocation independently.
template<class Base>
class CComContainedObject : public Base {
public:
    explicit CComContainedObject(void* outer) noexcept(std::is_nothrow_default_constructible<Base>::value) {
        this->m_pOuterUnknown = static_cast<IUnknown*>(outer);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        return this->OuterQueryInterface(iid, output);
    }
    template<class Q> HRESULT QueryInterface(Q** output) noexcept {
        return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(output));
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return this->OuterAddRef(); }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return this->OuterRelease(); }
    IUnknown* GetControllingUnknown() noexcept override { return this->m_pOuterUnknown; }
};

template<class Base>
class CComAggObject : private detail::AtlObjectModulePin, public IUnknown,
    public CComObjectRootEx<typename Base::_ThreadModel::ThreadModelNoCS> {
public:
    explicit CComAggObject(void* outer) : m_contained(outer) {}
    ~CComAggObject() { FinalRelease(); }
    CComContainedObject<Base> m_contained;
    HRESULT FinalConstruct() { return m_contained.FinalConstruct(); }
    void FinalRelease() noexcept { m_contained.FinalRelease(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            *output = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return m_contained._InternalQueryInterface(iid, output);
    }
    template<class Q> HRESULT QueryInterface(Q** output) noexcept {
        return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(output));
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return this->InternalAddRef(); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        ULONG count = this->InternalRelease();
        if (!count) delete this;
        return count;
    }
    static HRESULT WINAPI CreateInstance(IUnknown* outer, CComAggObject** output) noexcept {
        return CreateObject(nullptr, outer, output);
    }
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!outer) return E_INVALIDARG;
        if (!IsEqualIID(iid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
        CComAggObject* object = nullptr;
        HRESULT hr = CreateObject(&module, outer, &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
private:
    static HRESULT CreateObject(OpenATL::COM::Module* module, IUnknown* outer,
                                CComAggObject** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!outer) return E_INVALIDARG;
        CComAggObject* object = nullptr;
        try { object = new CComAggObject(outer); }
        catch (...) { return detail::AtlObjectException(); }
        HRESULT hr = module ? object->BindModule(*module) : S_OK;
        if (SUCCEEDED(hr)) {
            object->InternalFinalConstructAddRef();
            try { hr = object->FinalConstruct(); }
            catch (...) { hr = detail::AtlObjectException(); }
            object->InternalFinalConstructRelease();
        }
        if (FAILED(hr)) { delete object; return hr; }
        *output = object; // Native ATL raw creator returns ZERO references.
        return S_OK;
    }
};

// One contained allocation handles both creation modes. In the ordinary
// case its public interfaces delegate to this wrapper's own IUnknown; with
// an outer they delegate to that borrowed controlling identity instead.
template<class Base>
class CComPolyObject : private detail::AtlObjectModulePin, public IUnknown,
    public CComObjectRootEx<typename Base::_ThreadModel::ThreadModelNoCS> {
public:
    explicit CComPolyObject(void* outer)
        : m_contained(outer ? outer : static_cast<IUnknown*>(this)) {}
    ~CComPolyObject() { FinalRelease(); }
    CComContainedObject<Base> m_contained;
    HRESULT FinalConstruct() { return m_contained.FinalConstruct(); }
    void FinalRelease() noexcept { m_contained.FinalRelease(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            *output = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return m_contained._InternalQueryInterface(iid, output);
    }
    template<class Q> HRESULT QueryInterface(Q** output) noexcept {
        return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(output));
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return this->InternalAddRef(); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = this->InternalRelease();
        if (!count) delete this;
        return count;
    }
    static HRESULT WINAPI CreateInstance(IUnknown* outer, CComPolyObject** output) noexcept {
        return CreateObject(nullptr, outer, output);
    }
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer && !IsEqualIID(iid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
        CComPolyObject* object = nullptr;
        HRESULT hr = CreateObject(&module, outer, &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
private:
    static HRESULT CreateObject(OpenATL::COM::Module* module, IUnknown* outer,
                                CComPolyObject** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComPolyObject* object = nullptr;
        try { object = new CComPolyObject(outer); }
        catch (...) { return detail::AtlObjectException(); }
        HRESULT hr = module ? object->BindModule(*module) : S_OK;
        if (SUCCEEDED(hr)) {
            object->InternalFinalConstructAddRef();
            try { hr = object->FinalConstruct(); }
            catch (...) { hr = detail::AtlObjectException(); }
            object->InternalFinalConstructRelease();
        }
        if (FAILED(hr)) { delete object; return hr; }
        *output = object; // Both raw modes start with zero private references.
        return S_OK;
    }
};

template<class Owner, class ThreadModel = CComObjectThreadModel>
class CComTearOffObjectBase : public CComObjectRootEx<ThreadModel> {
public:
    using _OpenATLOwner = Owner;
    // Store the actual Owner subobject, including inside CComContainedObject.
    // No cast to a fictitious CComObject<Owner> allocation is permitted.
    Owner* m_pOwner = nullptr;
};

namespace detail {
template<class Owner> IUnknown* TearOffControl(void* owner) {
    if (!owner) throw CAtlException(E_INVALIDARG);
    return static_cast<CComObject<Owner>*>(owner)->GetControllingUnknown();
}
template<class Tear> struct TearOffEntryTag { inline static const char value = 0; };
class TearOffOwnerReference {
public:
    explicit TearOffOwnerReference(IUnknown* owner) : owner_(owner) {
        if (!owner_) throw CAtlException(E_INVALIDARG);
        owner_->AddRef();
    }
    ~TearOffOwnerReference() { owner_->Release(); }
    IUnknown* _OpenATLOwnerUnknown() const noexcept { return owner_; }
private:
    IUnknown* owner_;
};
// Same-thread construction cycles fail before another allocation. Other
// threads serialize cached publication through the owner's normal lock.
struct TearOffConstruction {
    inline static thread_local const TearOffConstruction* current = nullptr;
    const TearOffConstruction* previous = nullptr;
    const void* owner;
    const void* key;
    bool active = false;
    TearOffConstruction(const void* object, const void* entry) noexcept
        : owner(object), key(entry) {
        size_t depth = 0;
        for (auto* item = current; item; item = item->previous) {
            if ((item->owner == owner && item->key == key) || ++depth >= 64) return;
        }
        previous = current;
        current = this;
        active = true;
    }
    ~TearOffConstruction() { if (active) current = previous; }
};
template<class Owner> struct TearOffOwnerLock {
    Owner& owner;
    explicit TearOffOwnerLock(Owner& value) noexcept : owner(value) { owner.Lock(); }
    ~TearOffOwnerLock() { owner.Unlock(); }
};
} // namespace detail

template<class Base>
class CComTearOffObject : private detail::AtlObjectModulePin,
                         private detail::TearOffOwnerReference, public Base {
public:
    using Owner = typename Base::_OpenATLOwner;
    explicit CComTearOffObject(void* owner)
        : CComTearOffObject(static_cast<Owner*>(static_cast<CComObject<Owner>*>(owner)),
                           detail::TearOffControl<Owner>(owner)) {}
    CComTearOffObject(Owner* owner, IUnknown* identity)
        : detail::TearOffOwnerReference(identity) {
        if (!owner) throw CAtlException(E_INVALIDARG);
        this->m_pOwner = owner;
        this->m_pOuterUnknown = identity;
    }
    ~CComTearOffObject() { this->FinalRelease(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (IsEqualIID(iid, IID_IUnknown))
            return _OpenATLOwnerUnknown()->QueryInterface(iid, output);
        const HRESULT hr = this->_InternalQueryInterface(iid, output);
        return hr == E_NOINTERFACE ? _OpenATLOwnerUnknown()->QueryInterface(iid, output) : hr;
    }
    template<class Q> HRESULT QueryInterface(Q** output) noexcept {
        return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(output));
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return this->InternalAddRef(); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = this->InternalRelease();
        if (!count) delete this;
        return count;
    }
    IUnknown* GetControllingUnknown() noexcept override { return _OpenATLOwnerUnknown(); }
    static HRESULT CreateInstance(Owner* owner, IUnknown* identity, REFIID iid,
                                  void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!owner || !identity) return E_INVALIDARG;
        CComTearOffObject* object = nullptr;
        try { object = new CComTearOffObject(owner, identity); }
        catch (...) { return detail::AtlObjectException(); }
        object->InternalFinalConstructAddRef();
        HRESULT hr;
        try { hr = object->FinalConstruct(); }
        catch (...) { hr = detail::AtlObjectException(); }
        if (SUCCEEDED(hr)) hr = object->QueryInterface(iid, output);
        // Keep an ordinary ref during QI and destroy on every failed path.
        object->Release();
        return hr;
    }
};

template<class Base>
class CComCachedTearOffObject : public CComAggObject<Base> {
public:
    using Owner = typename Base::_OpenATLOwner;
    explicit CComCachedTearOffObject(void* owner)
        : CComCachedTearOffObject(static_cast<Owner*>(static_cast<CComObject<Owner>*>(owner)),
                                 detail::TearOffControl<Owner>(owner)) {}
    CComCachedTearOffObject(Owner* owner, IUnknown* identity) : CComAggObject<Base>(identity) {
        if (!owner || !identity) throw CAtlException(E_INVALIDARG);
        this->m_contained.m_pOwner = owner;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = this->InternalRelease();
        if (!count) delete this;
        return count;
    }
    static HRESULT CreateInstance(Owner* owner, IUnknown* identity,
                                  CComCachedTearOffObject** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!owner || !identity) return E_INVALIDARG;
        CComCachedTearOffObject* object = nullptr;
        try { object = new CComCachedTearOffObject(owner, identity); }
        catch (...) { return detail::AtlObjectException(); }
        object->InternalFinalConstructAddRef();
        HRESULT hr;
        try { hr = object->FinalConstruct(); }
        catch (...) { hr = detail::AtlObjectException(); }
        object->InternalFinalConstructRelease();
        if (FAILED(hr)) { delete object; return hr; }
        *output = object; // Private cache IUnknown starts with zero refs.
        return S_OK;
    }
};

namespace detail {
inline HRESULT QueryAggregate(IUnknown*, REFIID, void**, bool&) noexcept;
template<class Tear, class Owner>
HRESULT QueryTearOff(Owner* owner, REFIID iid, void** output, bool& owned) noexcept {
    *output = nullptr;
    using DataOwner = typename Tear::_OpenATLOwner;
    auto* data = static_cast<DataOwner*>(owner);
    TearOffConstruction constructing(data, &TearOffEntryTag<Tear>::value);
    if (!constructing.active) return E_UNEXPECTED;
    auto* identity = owner->GetControllingUnknown();
    if (!identity) return E_UNEXPECTED;
    const HRESULT hr = CComTearOffObject<Tear>::CreateInstance(data, identity, iid, output);
    owned = hr == S_OK;
    return hr;
}
template<class Tear, class Owner>
HRESULT QueryCachedTearOff(Owner* owner, IUnknown*& cache, REFIID iid,
                            void** output, bool& owned) noexcept {
    *output = nullptr;
    auto* identity = owner->GetControllingUnknown();
    if (!identity) return E_UNEXPECTED;
    // Temporary protection only. A cached private IUnknown never owns the
    // controlling owner; public contained interfaces retain it instead.
    CComPtr<IUnknown> protect(identity);
    TearOffOwnerLock<Owner> lock(*owner);
    if (cache) return QueryAggregate(cache, iid, output, owned);
    TearOffConstruction constructing(static_cast<typename Tear::_OpenATLOwner*>(owner), &cache);
    if (!constructing.active) return E_UNEXPECTED;
    using Object = CComCachedTearOffObject<Tear>;
    Object* object = nullptr;
    const HRESULT created = Object::CreateInstance(
        static_cast<typename Tear::_OpenATLOwner*>(owner), identity, &object);
    if (FAILED(created)) return created;
    object->AddRef();
    const HRESULT hr = object->QueryInterface(iid, output);
    if (hr != S_OK || !*output) {
        object->Release();
        *output = nullptr;
        return SUCCEEDED(hr) ? E_UNEXPECTED : hr;
    }
    cache = static_cast<IUnknown*>(object); // Transfer one private owning ref.
    owned = true;
    return S_OK;
}
} // namespace detail

template<class ObjectType>
class CComCreator {
public:
    static HRESULT WINAPI CreateInstance(void* outer, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        ObjectType* object = nullptr;
        HRESULT hr = ObjectType::CreateInstance(&object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        return ObjectType::CreateInstance(module, iid, output);
    }
};

template<class Base>
class CComCreator<CComAggObject<Base>> {
public:
    static HRESULT WINAPI CreateInstance(void* outer, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!outer) return E_INVALIDARG;
        if (!IsEqualIID(iid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
        CComAggObject<Base>* object = nullptr;
        HRESULT hr = CComAggObject<Base>::CreateInstance(static_cast<IUnknown*>(outer), &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        return CComAggObject<Base>::CreateInstance(module, outer, iid, output);
    }
};

template<class Base>
class CComCreator<CComPolyObject<Base>> {
public:
    static HRESULT WINAPI CreateInstance(void* outer, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer && !IsEqualIID(iid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
        CComPolyObject<Base>* object = nullptr;
        HRESULT hr = CComPolyObject<Base>::CreateInstance(static_cast<IUnknown*>(outer), &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        return CComPolyObject<Base>::CreateInstance(module, outer, iid, output);
    }
};

template<class Base>
class CComCreator<CComTearOffObject<Base>> {
public:
    static HRESULT WINAPI CreateInstance(void* owner, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!owner) return E_INVALIDARG;
        using Owner = typename Base::_OpenATLOwner;
        auto* object = static_cast<CComObject<Owner>*>(owner);
        return CComTearOffObject<Base>::CreateInstance(static_cast<Owner*>(object),
            object->GetControllingUnknown(), iid, output);
    }
};
template<class Base>
class CComCreator<CComCachedTearOffObject<Base>> {
public:
    static HRESULT WINAPI CreateInstance(void* owner, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!owner) return E_INVALIDARG;
        // Only the owner may hold the cache's private IUnknown. Publishing a
        // public contained interface without its private anchor would dangle.
        if (!IsEqualIID(iid, IID_IUnknown)) return E_NOINTERFACE;
        using Owner = typename Base::_OpenATLOwner;
        auto* value = static_cast<CComObject<Owner>*>(owner);
        CComCachedTearOffObject<Base>* object = nullptr;
        HRESULT hr = CComCachedTearOffObject<Base>::CreateInstance(
            static_cast<Owner*>(value), value->GetControllingUnknown(), &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        *output = static_cast<IUnknown*>(object);
        return S_OK;
    }
};

template<HRESULT Failure>
class CComFailCreator {
public:
    static HRESULT WINAPI CreateInstance(void*, REFIID, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        return Failure;
    }
    static HRESULT CreateInstance(OpenATL::COM::Module&, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        return CreateInstance(outer, iid, output);
    }
};

template<class NonAggregated, class Aggregated>
class CComCreator2 {
public:
    static HRESULT WINAPI CreateInstance(void* outer, REFIID iid, void** output) noexcept {
        return outer ? Aggregated::CreateInstance(outer, iid, output)
                     : NonAggregated::CreateInstance(outer, iid, output);
    }
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        return outer ? Aggregated::CreateInstance(module, outer, iid, output)
                     : NonAggregated::CreateInstance(module, outer, iid, output);
    }
};

template<class T, const CLSID* ClassId = &CLSID_NULL>
class CComCoClass {
public:
    using _CreatorClass = CComCreator2<CComCreator<CComObject<T>>, CComCreator<CComAggObject<T>>>;
    static HRESULT WINAPI Error(LPCOLESTR text, REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
        return AtlReportError(GetObjectCLSID(), text, iid, result);
    }
    static HRESULT WINAPI Error(LPCOLESTR text, DWORD help, LPCOLESTR file,
                                 REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
        return AtlReportError(GetObjectCLSID(), text, help, file, iid, result);
    }
    static HRESULT WINAPI Error(LPCSTR text, REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
        return AtlReportError(GetObjectCLSID(), text, iid, result);
    }
    static HRESULT WINAPI Error(LPCSTR text, DWORD help, LPCSTR file,
                                 REFIID iid = GUID_NULL, HRESULT result = 0) noexcept {
        return AtlReportError(GetObjectCLSID(), text, help, file, iid, result);
    }
    static HRESULT WINAPI Error(UINT id, REFIID iid = GUID_NULL, HRESULT result = 0,
                                 HINSTANCE image = _AtlBaseModule.GetResourceInstance()) noexcept {
        return AtlReportError(GetObjectCLSID(), id, iid, result, image);
    }
    static HRESULT WINAPI Error(UINT id, DWORD help, LPCOLESTR file,
                                 REFIID iid = GUID_NULL, HRESULT result = 0,
                                 HINSTANCE image = _AtlBaseModule.GetResourceInstance()) noexcept {
        return AtlReportError(GetObjectCLSID(), id, help, file, iid, result, image);
    }
    static const CLSID& WINAPI GetObjectCLSID() noexcept { return *ClassId; }
    static HRESULT WINAPI CreateInstance(IUnknown* outer, REFIID iid, void** output) noexcept {
        using CreatorClass = typename T::_CreatorClass;
        return CreatorClass::CreateInstance(outer, iid, output);
    }
    template<class Q>
    static HRESULT WINAPI CreateInstance(Q** output) noexcept {
        return CreateInstance(nullptr, __uuidof(Q), reinterpret_cast<void**>(output));
    }
    template<class Q>
    static HRESULT WINAPI CreateInstance(IUnknown* outer, Q** output) noexcept {
        return CreateInstance(outer, __uuidof(Q), reinterpret_cast<void**>(output));
    }
};
namespace detail {
template<class T, class = void> struct AutoClassPolicy {
    using Type = CComCreator<CComObject<T>>;
};
template<class T> struct AutoClassPolicy<T, std::void_t<typename T::_CreatorClass>> {
    using Type = typename T::_CreatorClass;
};
template<class T> struct AutoClassCreator {
    static HRESULT CreateInstance(OpenATL::COM::Module& module, IUnknown* outer,
                                  REFIID iid, void** output) noexcept {
        using Policy = typename AutoClassPolicy<T>::Type;
        return Policy::CreateInstance(module, outer, iid, output);
    }
};
inline HRESULT QueryAggregate(IUnknown* inner, REFIID iid, void** output,
                               bool& owned) noexcept {
    *output = nullptr;
    if (!inner) return E_NOINTERFACE;
    HRESULT hr = inner->QueryInterface(iid, output);
    if (hr != S_OK || !*output) {
        *output = nullptr;
        return SUCCEEDED(hr) ? E_UNEXPECTED : hr;
    }
    owned = true;
    return S_OK;
}
template<class Owner>
HRESULT QueryAutoAggregate(Owner* owner, IUnknown*& cache, REFCLSID clsid,
                           REFIID iid, void** output, bool& owned) noexcept {
    *output = nullptr;
    auto* identity = owner->GetControllingUnknown();
    if (!identity) return E_UNEXPECTED;
    // Keep the owner alive until after its lock is dropped. The private
    // inner unknown itself borrows the controlling identity, avoiding cycles.
    CComPtr<IUnknown> protect(identity);
    TearOffOwnerLock<Owner> lock(*owner);
    if (!cache) {
        // The cache slot identifies this lazy entry even through chained maps.
        TearOffConstruction constructing(identity, &cache);
        if (!constructing.active) return E_UNEXPECTED;
        CComPtr<IUnknown> created;
        const HRESULT hr = ::CoCreateInstance(clsid, identity, CLSCTX_INPROC_SERVER,
            IID_IUnknown, reinterpret_cast<void**>(&created.p));
        if (FAILED(hr)) return hr;
        if (hr != S_OK || !created.p) return E_UNEXPECTED;
        // Successful activation owns the cache even if the requested interface
        // is missing. Activation failures leave a null slot and may retry.
        cache = created.Detach();
    }
    return QueryAggregate(cache, iid, output, owned);
}
template<class Owner>
HRESULT QueryAutoAggregate(Owner* owner, CComPtr<IUnknown>& cache, REFCLSID clsid,
                           REFIID iid, void** output, bool& owned) noexcept {
    return QueryAutoAggregate(owner, cache.p, clsid, iid, output, owned);
}
} // namespace detail
} // namespace ATL

// Maps use real typed casts, including pointer adjustment for multiple
// inheritance. The first simple entry defines the canonical IUnknown.
#define BEGIN_COM_MAP(x) \
public: \
    using _ComMapClass = x; \
    HRESULT _InternalQueryInterface(REFIID iid, void** output) noexcept { \
        bool owned = false; \
        HRESULT hr = _OpenATLFindInterface(iid, output, owned); \
        if (hr == S_OK && !owned) static_cast<IUnknown*>(*output)->AddRef(); \
        return hr; \
    } \
    IUnknown* GetUnknown() noexcept { \
        void* output = nullptr; \
        bool owned = false; \
        _OpenATLFindInterface(IID_IUnknown, &output, owned); \
        return static_cast<IUnknown*>(output); \
    } \
    HRESULT _OpenATLFindInterface(REFIID iid, void** output, bool& owned) noexcept { \
        owned = false; \
        if (!output) return E_POINTER; \
        *output = nullptr; \
        bool _atlFirst = true;

#define COM_INTERFACE_ENTRY_IID(id, x) \
    { x* _atlInterface = static_cast<x*>(this); \
      if ((_atlFirst && IsEqualIID(iid, IID_IUnknown)) || IsEqualIID(iid, id)) { \
          *output = IsEqualIID(iid, IID_IUnknown) ? \
              static_cast<void*>(static_cast<IUnknown*>(_atlInterface)) : static_cast<void*>(_atlInterface); \
          return S_OK; \
      } _atlFirst = false; }

#define COM_INTERFACE_ENTRY(x) COM_INTERFACE_ENTRY_IID(__uuidof(x), x)
#define COM_INTERFACE_ENTRY2_IID(id, x, via) \
    { x* _atlInterface = static_cast<x*>(static_cast<via*>(this)); \
      if ((_atlFirst && IsEqualIID(iid, IID_IUnknown)) || IsEqualIID(iid, id)) { \
          *output = IsEqualIID(iid, IID_IUnknown) ? \
              static_cast<void*>(static_cast<IUnknown*>(_atlInterface)) : static_cast<void*>(_atlInterface); \
          return S_OK; \
      } _atlFirst = false; }
#define COM_INTERFACE_ENTRY2(x, via) COM_INTERFACE_ENTRY2_IID(__uuidof(x), x, via)
// Chain a base map without adding a second reference. Simple entries return
// borrowed adjusted pointers; aggregate entries already own their QI result.
// Propagate that ownership through a chain to the final public QI boundary.
// In particular, the outer map's first simple entry still defines the
// canonical IUnknown; a chain-only outer map inherits its base identity.
#define COM_INTERFACE_ENTRY_CHAIN(base) \
    { \
        void* _atlChained = nullptr; \
        const HRESULT _atlChainHr = \
            static_cast<base*>(this)->_OpenATLFindInterface(iid, &_atlChained, owned); \
        if (_atlChainHr == S_OK && _atlChained) { \
            *output = _atlChained; \
            return S_OK; \
        } \
        if (SUCCEEDED(_atlChainHr)) return E_UNEXPECTED; \
        if (_atlChainHr != E_NOINTERFACE) return _atlChainHr; \
    }
// Inner QI already returns an owned interface; never AddRef it a second time.
// IUnknown always comes from a simple/chained entry of the outer map.
#define COM_INTERFACE_ENTRY_AGGREGATE(id, member) \
    if (!IsEqualIID(iid, IID_IUnknown) && IsEqualIID(iid, id)) \
        return ::ATL::detail::QueryAggregate(this->member, iid, output, owned);
#define COM_INTERFACE_ENTRY_AGGREGATE_BLIND(member) \
    if (!IsEqualIID(iid, IID_IUnknown)) { \
        const HRESULT _atlAggregateHr = \
            ::ATL::detail::QueryAggregate(this->member, iid, output, owned); \
        if (_atlAggregateHr == S_OK) return S_OK; \
    }
#define COM_INTERFACE_ENTRY_AUTOAGGREGATE(id, member, clsid) \
    if (!IsEqualIID(iid, IID_IUnknown) && IsEqualIID(iid, id)) \
        return ::ATL::detail::QueryAutoAggregate(this, this->member, clsid, iid, output, owned);
#define COM_INTERFACE_ENTRY_AUTOAGGREGATE_BLIND(member, clsid) \
    if (!IsEqualIID(iid, IID_IUnknown)) { \
        const HRESULT _atlAggregateHr = \
            ::ATL::detail::QueryAutoAggregate(this, this->member, clsid, iid, output, owned); \
        if (_atlAggregateHr == S_OK) return S_OK; \
    }
#define COM_INTERFACE_ENTRY_TEAR_OFF(id, type) \
    if (!IsEqualIID(iid, IID_IUnknown) && IsEqualIID(iid, id)) \
        return ::ATL::detail::QueryTearOff<type>(this, iid, output, owned);
#define COM_INTERFACE_ENTRY_CACHED_TEAR_OFF(id, type, member) \
    if (!IsEqualIID(iid, IID_IUnknown) && IsEqualIID(iid, id)) \
        return ::ATL::detail::QueryCachedTearOff<type>(this, this->member, iid, output, owned);
#define END_COM_MAP() \
        (void)_atlFirst; \
        return E_NOINTERFACE; \
    }
// Standard ATL-style automatic COM object-map declaration. The class-declared
// creator policy binds the existing single OpenATL::COM::Module,
// preserving object/factory/server-lock ownership. The registration record
// is static binary-owned metadata; no COM call or heap allocation occurs
// in its constructor under the Windows loader lock.
#define _OPENATL_AUTO_CAT_IMPL(a, b) a##b
#define _OPENATL_AUTO_CAT(a, b) _OPENATL_AUTO_CAT_IMPL(a, b)
#define OBJECT_ENTRY_AUTO(clsid, type) \
    namespace { \
        ATL::detail::AutoObjectEntry _OPENATL_AUTO_CAT(_openatlAutoEntry_, __COUNTER__)( \
            (clsid), nullptr, ATL::detail::AutoRegistryMethod<type>::callback, \
            &ATL::detail::AutoClassCreator<type>::CreateInstance, \
            &ATL::detail::AutoObjectType<type>::identity); \
    }

// Standard module registration supplies a lazy image/root context; explicit
// scoped installer contexts retain priority. Both macros use one registrar.
#define DECLARE_REGISTRY_RESOURCEID(id) \
public: \
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept { \
        if (::ATL::_pAtlModule) \
            return ::ATL::_pAtlModule->UpdateRegistryFromResource( \
                static_cast<UINT>(id), install); \
        return ::OpenATL::COM::UpdateRegistryResource( \
            static_cast<unsigned>(id), install); \
    }
#define DECLARE_REGISTRY_RESOURCE(name) \
public: \
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept { \
        if (::ATL::_pAtlModule) \
            return ::ATL::_pAtlModule->UpdateRegistryFromResource((name), install); \
        return ::OpenATL::COM::UpdateRegistryResource((name), install); \
    }

#define DECLARE_REGISTRY_APPID_RESOURCEID(id, appid) \
public: \
    static LPCTSTR GetAppId() noexcept { return _T(appid); } \
    static HRESULT WINAPI UpdateRegistryAppId(BOOL install) noexcept { \
        return ::ATL::detail::UpdateAppIdResource(static_cast<UINT>(id), GetAppId(), install); \
    }

// Original ATL module LIBID declaration. CAtlModuleT<T> calls the derived
// class's static InitLibId on construction; the source-compatible module
// identity is then available to defaulted ATL IDispatchImpl<T> templates.
// No type-library registry operation or implicit global COM initialization.
#define DECLARE_LIBID(libid) \
public: \
    static void InitLibId() noexcept { ATL::CAtlModule::m_libid = (libid); } \
    static HRESULT WINAPI GetLibID(GUID* output) noexcept { \
        if (!output) return E_POINTER; \
        *output = (libid); \
        return S_OK; \
    }

// ATL's deliberate declaration that this class performs no persistent
// registration. This is opt-in, unlike a missing/unsupported registrar.
#define DECLARE_NO_REGISTRY() \
public: \
    static HRESULT WINAPI UpdateRegistry(BOOL) noexcept { return S_OK; }

#define DECLARE_AGGREGATABLE(x) \
    using _CreatorClass = ATL::CComCreator2<ATL::CComCreator<ATL::CComObject<x>>, \
                                          ATL::CComCreator<ATL::CComAggObject<x>>>;
#define DECLARE_ONLY_AGGREGATABLE(x) \
    using _CreatorClass = ATL::CComCreator2<ATL::CComFailCreator<E_FAIL>, \
                                          ATL::CComCreator<ATL::CComAggObject<x>>>;
#define DECLARE_POLY_AGGREGATABLE(x) \
    using _CreatorClass = ATL::CComCreator<ATL::CComPolyObject<x>>;
#define DECLARE_GET_CONTROLLING_UNKNOWN() \
    IUnknown* GetControllingUnknown() noexcept override = 0;
#define DECLARE_NOT_AGGREGATABLE(x) \
    using _CreatorClass = ATL::CComCreator<ATL::CComObject<x>>;
#define DECLARE_PROTECT_FINAL_CONSTRUCT() \
    void InternalFinalConstructAddRef() noexcept { this->InternalAddRef(); } \
    void InternalFinalConstructRelease() noexcept { this->InternalRelease(); }
#include "openatl/atl_connection_points.h"
#include "openatl/atl_events.h"
#endif
