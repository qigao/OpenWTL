// Clean-room modern ATL application module subset. Included by atlbase.h.
#ifndef OPENATL_ATL_MODULE_H
#define OPENATL_ATL_MODULE_H
#include "../atlbase.h"
#include "com_server.h"
#include "rgs_resource.h"
#include "../atliface.h"
#include <atomic>
#include <algorithm>
#include <mutex>
#include <optional>
#include <memory>
#include <type_traits>
#include <vector>
#include <limits>
#include <shellapi.h>

namespace OpenATL { namespace COM {
struct ClassObjectEntry { CLSID clsid; IUnknown* factory; };
}}

namespace ATL {
class CAtlModule;
// Construction/destruction require external quiescence, like ATL's image module.
inline CAtlModule* _pAtlModule = nullptr;
using _ATL_TERMFUNC = void (WINAPI*)(DWORD_PTR);
struct _ATL_REGMAP_ENTRY { LPCOLESTR szKey; LPCOLESTR szData; };
HRESULT WINAPI AtlCreateRegistrar(IRegistrar** output) noexcept;
namespace detail { inline HRESULT AtlModuleException() noexcept; }

class CAtlModule {
public:
    // Per-image ATL library identity. DECLARE_LIBID initializes this before
    // any TypeLib request on an actual application module; it is metadata,
    // not a registry registration or a second COM factory provider.
    inline static GUID m_libid{};
    // Same image-owned module lock as GetLockCount/Lock/Unlock.
    // Original WTL's CServerAppModule reads this counter.
    std::atomic<LONG> m_nLockCnt{0};
    CComAutoCriticalSection m_csStaticDataInitAndTypeInfo;
    CAtlModule() noexcept {
        if (_pAtlModule) std::terminate();
        _pAtlModule = this;
    }
    virtual ~CAtlModule() noexcept {
        Term();
        if (_pAtlModule != this) std::terminate();
        _pAtlModule = nullptr;
    }
    CAtlModule(const CAtlModule&) = delete;
    CAtlModule& operator=(const CAtlModule&) = delete;
    LONG GetLockCount() const noexcept { return m_nLockCnt.load(); }
    LONG Lock() noexcept {
        if (terminated_.load()) std::terminate();
        LONG value = m_nLockCnt.load();
        for (;;) {
            if (value == (std::numeric_limits<LONG>::max)()) std::terminate();
            if (m_nLockCnt.compare_exchange_weak(value, value + 1)) return value + 1;
        }
    }
    LONG Unlock() noexcept {
        LONG value = m_nLockCnt.load();
        for (;;) {
            if (!value) std::terminate();
            if (m_nLockCnt.compare_exchange_weak(value, value - 1)) return value - 1;
        }
    }
    HRESULT AddTermFunc(_ATL_TERMFUNC callback, DWORD_PTR argument) noexcept {
        if (!callback) return E_INVALIDARG;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (terminated_.load()) return E_UNEXPECTED;
            if (callbacks_.size() == 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            callbacks_.push_back({callback, argument});
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_UNEXPECTED; }
    }
    virtual HRESULT AddCommonRGSReplacements(IRegistrarBase* registrar) {
        return registrar ? S_OK : E_POINTER;
    }
    HRESULT WINAPI UpdateRegistryFromResourceS(LPCTSTR resource, BOOL install,
                                _ATL_REGMAP_ENTRY* entries = nullptr) noexcept;
    HRESULT WINAPI UpdateRegistryFromResourceS(UINT id, BOOL install,
                                _ATL_REGMAP_ENTRY* entries = nullptr) noexcept {
        if (!id || id > 0xffff) return E_INVALIDARG;
        return UpdateRegistryFromResourceS(MAKEINTRESOURCE(id), install, entries);
    }
    // OpenWTL always uses its built-in static registrar.
    HRESULT WINAPI UpdateRegistryFromResource(LPCTSTR name, BOOL install,
                                _ATL_REGMAP_ENTRY* entries = nullptr) noexcept {
        return UpdateRegistryFromResourceS(name, install, entries);
    }
    HRESULT WINAPI UpdateRegistryFromResource(UINT id, BOOL install,
                                _ATL_REGMAP_ENTRY* entries = nullptr) noexcept {
        return UpdateRegistryFromResourceS(id, install, entries);
    }
    void Term() noexcept {
        std::vector<Termination> callbacks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (terminated_.load()) return;
            if (m_nLockCnt.load()) std::terminate();
            terminated_.store(true);
            callbacks.swap(callbacks_);
        }
        for (auto i = callbacks.rbegin(); i != callbacks.rend(); ++i)
            i->callback(i->argument);
    }
protected:
    bool IsTerminated() const noexcept { return terminated_.load(); }
private:
    friend HRESULT WINAPI AtlCreateRegistrar(IRegistrar**) noexcept;
    struct Termination { _ATL_TERMFUNC callback; DWORD_PTR argument; };
    std::atomic<bool> terminated_{false};
    std::mutex mutex_;
    std::vector<Termination> callbacks_;
};

namespace detail {
// One bounded owned map implementation serves callback capabilities and
// persistent registrars. Each operation receives an independent snapshot.
class RgsReplacementMap final {
public:
    explicit RgsReplacementMap(std::vector<OpenATL::RGS::Replacement>&& map = {}) noexcept
        : map_(std::move(map)) {}
    HRESULT Add(LPCOLESTR name, LPCOLESTR value, bool allowModule = false) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_) return E_UNEXPECTED;
            if (!OpenATL::COM::registration_detail::bounded(
                    name, OpenATL::RGS::MaxReplacementNameUnits) ||
                !OpenATL::COM::registration_detail::bounded(
                    value, OpenATL::RGS::MaxValueUnits)) return E_INVALIDARG;
            if (allowModule && OpenATL::RGS::detail::Same(name, L"MODULE")) {
                if (!module_.empty()) return E_INVALIDARG;
                std::wstring_view raw(value);
                const bool quoted = raw.size() >= 2 && raw.front() == L'"' && raw.back() == L'"';
                if (quoted) { raw.remove_prefix(1); raw.remove_suffix(1); }
                std::wstring canonical;
                const HRESULT hr = OpenATL::COM::rgs_registration_detail::CanonicalModule(
                    raw, canonical);
                if (FAILED(hr)) return hr;
                module_ = std::move(canonical);
                quoteModule_ = quoted;
                return S_OK;
            }
            if (map_.size() == OpenATL::RGS::MaxReplacements)
                return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            auto staged = map_;
            staged.push_back({name, value});
            const HRESULT hr = OpenATL::COM::rgs_registration_detail::MapError(
                OpenATL::RGS::ValidateReplacements(staged));
            if (FAILED(hr)) return hr;
            map_.swap(staged);
            return S_OK;
        } catch (...) { return AtlModuleException(); }
    }
    HRESULT Clear() noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_) return E_UNEXPECTED;
            map_.clear();
            module_.clear();
            quoteModule_ = false;
            return S_OK;
        } catch (...) { return AtlModuleException(); }
    }
    HRESULT Snapshot(std::vector<OpenATL::RGS::Replacement>& output,
                     std::wstring& module, bool& quoteModule) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_) return E_UNEXPECTED;
            output = map_;
            module = module_;
            quoteModule = quoteModule_;
            return S_OK;
        } catch (...) { return AtlModuleException(); }
    }
    void Seal() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        map_.clear();
        module_.clear();
        quoteModule_ = false;
    }
    void Freeze(std::vector<OpenATL::RGS::Replacement>& output) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        output = std::move(map_);
        module_.clear();
        quoteModule_ = false;
    }
private:
    std::mutex mutex_;
    std::vector<OpenATL::RGS::Replacement> map_;
    std::wstring module_; // Persistent registrar's optional canonical MODULE binding.
    bool quoteModule_ = false;
    bool accepting_ = true;
};
class RegistrarModulePin final {
public:
    RegistrarModulePin(const RegistrarModulePin&) = delete;
    RegistrarModulePin& operator=(const RegistrarModulePin&) = delete;
    explicit RegistrarModulePin(CAtlModule* owner) noexcept : owner_(owner) {
        if (owner_) owner_->Lock();
    }
    ~RegistrarModulePin() { if (owner_) owner_->Unlock(); }
private:
    CAtlModule* owner_;
};

// Real COM ownership for one synchronous common-replacement callback.
class CommonRgsRegistrar final : public IRegistrarBase {
public:
    CommonRgsRegistrar(CAtlModule& owner,
                      std::vector<OpenATL::RGS::Replacement>&& map) noexcept
        : pin_(&owner), map_(std::move(map)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!::IsEqualIID(iid, IID_IUnknown) &&
            !::IsEqualIID(iid, IID_IRegistrarBase)) return E_NOINTERFACE;
        *output = static_cast<IRegistrarBase*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        if (!OpenATL::COM::detail::retain(references_)) std::terminate();
        return references_.load();
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = OpenATL::COM::detail::release(references_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE AddReplacement(LPCOLESTR name, LPCOLESTR value) noexcept override {
        return map_.Add(name, value);
    }
    HRESULT STDMETHODCALLTYPE ClearReplacements() noexcept override { return map_.Clear(); }
    void Seal() noexcept { map_.Seal(); }
    void Freeze(std::vector<OpenATL::RGS::Replacement>& output) noexcept { map_.Freeze(output); }
private:
    ~CommonRgsRegistrar() = default;
    RegistrarModulePin pin_; // Declared first: retained through map destruction.
    std::atomic<ULONG> references_{1};
    RgsReplacementMap map_;
};
} // namespace detail

inline HRESULT WINAPI CAtlModule::UpdateRegistryFromResourceS(
    LPCTSTR resource, BOOL install, _ATL_REGMAP_ENTRY* entries) noexcept {
    if (IsTerminated()) return E_UNEXPECTED;
    if (!resource || (!IS_INTRESOURCE(resource) &&
        (!OpenATL::COM::registration_detail::bounded(resource, OpenATL::RGS::MaxNameUnits) ||
         !*resource))) return E_INVALIDARG;
    // Capture policy before application code can change the image-local mode.
    const bool perUser = detail::perUserRegistration.load();
    const HMODULE image = _AtlBaseModule.GetModuleInstance();
    try {
        std::vector<OpenATL::RGS::Replacement> map;
        if (entries) {
            for (size_t i = 0;; ++i) {
                const auto& entry = entries[i];
                if (!entry.szKey) {
                    if (entry.szData) return E_INVALIDARG;
                    break;
                }
                if (i == OpenATL::RGS::MaxReplacements)
                    return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
                if (!OpenATL::COM::registration_detail::bounded(
                        entry.szKey, OpenATL::RGS::MaxReplacementNameUnits) ||
                    !OpenATL::COM::registration_detail::bounded(
                        entry.szData, OpenATL::RGS::MaxValueUnits)) return E_INVALIDARG;
                map.push_back({entry.szKey, entry.szData});
            }
        }
        HRESULT hr = OpenATL::COM::rgs_registration_detail::MapError(
            OpenATL::RGS::ValidateReplacements(map));
        if (FAILED(hr)) return hr;
        CComPtr<detail::CommonRgsRegistrar> registrar;
        registrar.Attach(new (std::nothrow) detail::CommonRgsRegistrar(*this, std::move(map)));
        if (!registrar.p) return E_OUTOFMEMORY;
        struct SealOnExit {
            detail::CommonRgsRegistrar* registrar;
            ~SealOnExit() { registrar->Seal(); }
        } seal{registrar.p};
        hr = AddCommonRGSReplacements(registrar.p); // Borrowed interface.
        if (FAILED(hr)) return hr;
        registrar.p->Freeze(map);
        if (OpenATL::COM::rgs_resource_detail::Current() ||
            OpenATL::COM::rgs_resource_detail::CurrentModule())
            return OpenATL::COM::UpdateRegistryResource(resource, install, &map);
        OpenATL::COM::ScopedModuleRgsRegistration scope(image, perUser);
        return OpenATL::COM::UpdateRegistryResource(resource, install, &map);
    } catch (...) { return detail::AtlModuleException(); }
}

namespace detail {
inline HRESULT UpdateAppIdResource(UINT id, LPCWSTR appId, BOOL install) noexcept {
    if (!OpenATL::COM::registration_detail::bounded(appId, 38) ||
        !OpenATL::RGS::detail::Guid(appId)) return E_INVALIDARG;
    GUID guid{};
    if (FAILED(::CLSIDFromString(appId, &guid)) || ::IsEqualGUID(guid, GUID_NULL))
        return E_INVALIDARG;
    return _pAtlModule ? _pAtlModule->UpdateRegistryFromResourceS(id, install) : E_UNEXPECTED;
}
// An enclosing application operation owns its policy/root until rollback ends.
// Nested AppID and class helpers must not recapture callback-mutated globals.
class ModuleRegistrationScope final {
public:
    ModuleRegistrationScope() noexcept {
        const auto* existing = OpenATL::COM::rgs_resource_detail::CurrentModule();
        perUser_ = existing ? existing->PerUserRegistration() : perUserRegistration.load();
        if (!existing) owned_.emplace(_AtlBaseModule.GetModuleInstance(), perUser_);
    }
    bool PerUserRegistration() const noexcept { return perUser_; }
private:
    bool perUser_ = false;
    std::optional<OpenATL::COM::ScopedModuleRgsRegistration> owned_;
};
}

// Original ATL object-map ABI. The source table remains owned by the caller;
// this module owns only the COM factory references and registration cookies.
// Null pclsid terminates the table; counts are bounded before any callback.
typedef HRESULT (WINAPI _ATL_CREATORFUNC)(void*, REFIID, LPVOID*);
typedef LPCTSTR (WINAPI _ATL_DESCRIPTIONFUNC)();
struct _ATL_CATMAP_ENTRY;
using _ATL_CATMAPFUNC = const _ATL_CATMAP_ENTRY* (*)();

struct _ATL_OBJMAP_ENTRY {
    const CLSID* pclsid = nullptr;
    HRESULT (WINAPI* pfnUpdateRegistry)(BOOL) = nullptr;
    _ATL_CREATORFUNC* pfnGetClassObject = nullptr;
    _ATL_CREATORFUNC* pfnCreateInstance = nullptr;
    IUnknown* pCF = nullptr;
    DWORD dwRegister = 0;
    _ATL_DESCRIPTIONFUNC* pfnGetObjectDescription = nullptr;
    _ATL_CATMAPFUNC pfnGetCategoryMap = nullptr;
    void (WINAPI* pfnObjectMain)(bool) = nullptr;
};

namespace detail {
// One cookie registration/retirement implementation shared with CAtlExeModuleT.
// Only the owning COM apartment can register or revoke its native classes.
inline HRESULT RegisterClassCookie(REFCLSID id, IUnknown* factory,
                                   DWORD context, DWORD flags, DWORD& cookie) noexcept {
    if (!factory || ::IsEqualCLSID(id, CLSID_NULL) || cookie) return E_INVALIDARG;
    DWORD created = 0;
    const HRESULT hr = ::CoRegisterClassObject(id, factory, context, flags, &created);
    if (SUCCEEDED(hr)) {
        if (!created) return E_UNEXPECTED;
        cookie = created;
    }
    return hr;
}
inline HRESULT RevokeClassCookie(DWORD& cookie) noexcept {
    if (!cookie) return S_FALSE;
    const HRESULT hr = ::CoRevokeClassObject(cookie);
    if (SUCCEEDED(hr)) cookie = 0;
    return hr;
}
}

class CComModule : public CAtlModule {
public:
    CComModule() noexcept = default;
    ~CComModule() noexcept { Term(); }
    CComModule(const CComModule&) = delete;
    CComModule& operator=(const CComModule&) = delete;

    HRESULT Init(_ATL_OBJMAP_ENTRY* map, HINSTANCE image,
                 const GUID* libId = nullptr) noexcept {
        if (!image) return E_INVALIDARG;
        if (IsTerminated()) return E_UNEXPECTED;
        if (initialized_) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        size_t count = 0;
        if (map) {
            // A missing sentinel is an invalid caller-owned table, never an
            // invitation to walk arbitrarily large/unknown memory.
            for (; count < 1024 && map[count].pclsid; ++count) {
                const auto& entry = map[count];
                if (::IsEqualCLSID(*entry.pclsid, CLSID_NULL) ||
                    !entry.pfnGetClassObject || !entry.pfnCreateInstance ||
                    entry.pCF || entry.dwRegister) return E_INVALIDARG;
                for (size_t j = 0; j < count; ++j)
                    if (::IsEqualCLSID(*entry.pclsid, *map[j].pclsid))
                        return E_INVALIDARG;
            }
            if (count == 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        }
        m_hInst = image;
        m_hInstResource = image;
        m_pObjMap = map;
        mapCount_ = count;
        owner_ = ::GetCurrentThreadId();
        _AtlBaseModule.m_hInst = image;
        _AtlBaseModule.m_hInstResource = image;
        if (libId) m_libid = *libId;
        initialized_ = true;

        // Original ATL object maps invoke start/stop hooks on the owning
        // module thread. Revert already-started entries on a thrown hook.
        size_t started = 0;
        try {
            for (; started < count; ++started)
                if (map[started].pfnObjectMain)
                    map[started].pfnObjectMain(true);
        } catch (...) {
            while (started) {
                --started;
                if (map[started].pfnObjectMain) {
                    try { map[started].pfnObjectMain(false); }
                    catch (...) { std::terminate(); }
                }
            }
            m_pObjMap = nullptr;
            mapCount_ = 0;
            initialized_ = false;
            return E_UNEXPECTED;
        }
        return S_OK;
    }

    HRESULT RegisterClassObjects(DWORD context, DWORD flags) noexcept {
        if (!initialized_ || IsTerminated()) return E_UNEXPECTED;
        if (::GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        for (size_t i = 0; i < mapCount_; ++i)
            if (m_pObjMap[i].dwRegister || m_pObjMap[i].pCF)
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (!mapCount_) return S_FALSE;

        for (size_t i = 0; i < mapCount_; ++i) {
            auto& entry = m_pObjMap[i];
            IClassFactory* factoryRaw = nullptr;
            // Standard legacy creator ABI passes the class instance creator
            // as opaque context to the factory creator.
            HRESULT hr = E_UNEXPECTED;
            try {
                hr = entry.pfnGetClassObject(
                    reinterpret_cast<void*>(entry.pfnCreateInstance),
                    IID_IClassFactory,
                    reinterpret_cast<LPVOID*>(&factoryRaw));
            } catch (...) { hr = E_UNEXPECTED; }
            CComPtr<IClassFactory> factory;
            factory.Attach(factoryRaw);
            if (SUCCEEDED(hr) && !factory.p) hr = E_UNEXPECTED;
            if (SUCCEEDED(hr)) {
                DWORD cookie = 0;
                hr = detail::RegisterClassCookie(*entry.pclsid, factory.p,
                                                 context, flags, cookie);
                if (SUCCEEDED(hr)) {
                    entry.dwRegister = cookie;
                    entry.pCF = factory.Detach();
                }
            }
            if (FAILED(hr)) {
                const HRESULT rollback = RevokeClassObjects();
                return FAILED(rollback) ? rollback : hr;
            }
        }
        return S_OK;
    }

    HRESULT RevokeClassObjects() noexcept {
        if (!initialized_) return E_UNEXPECTED;
        if (::GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        bool any = false;
        HRESULT result = S_OK;
        for (size_t n = mapCount_; n > 0; --n) {
            auto& entry = m_pObjMap[n - 1];
            if (!entry.dwRegister) continue;
            any = true;
            HRESULT hr = detail::RevokeClassCookie(entry.dwRegister);
            if (SUCCEEDED(hr)) {
                // COM no longer owns the registered class factory. Release
                // our retained reference only after successful revocation.
                IUnknown* owned = entry.pCF;
                entry.pCF = nullptr;
                if (owned) owned->Release();
            } else if (SUCCEEDED(result)) result = hr;
        }
        return FAILED(result) ? result : (any ? S_OK : S_FALSE);
    }

    void Term() noexcept {
        if (initialized_) {
            if (::GetCurrentThreadId() != owner_) std::terminate();
            // Never retire a table or factory with leaked COM registration.
            if (FAILED(RevokeClassObjects()) || GetLockCount() != 0)
                std::terminate();
            for (size_t n = mapCount_; n > 0; --n)
                if (m_pObjMap[n - 1].pfnObjectMain) {
                    try { m_pObjMap[n - 1].pfnObjectMain(false); }
                    catch (...) { std::terminate(); }
                }
            initialized_ = false;
            m_pObjMap = nullptr;
            mapCount_ = 0;
        }
        CAtlModule::Term();
    }
    HINSTANCE GetModuleInstance() const noexcept { return m_hInst; }
    HINSTANCE GetResourceInstance() const noexcept {
        return _AtlBaseModule.GetResourceInstance();
    }
    HINSTANCE SetResourceInstance(HINSTANCE image) noexcept {
        m_hInstResource = image;
        return _AtlBaseModule.SetResourceInstance(image);
    }
    HINSTANCE m_hInst = nullptr;
    HINSTANCE m_hInstResource = nullptr;
    _ATL_OBJMAP_ENTRY* m_pObjMap = nullptr;
private:
    size_t mapCount_ = 0;
    DWORD owner_ = 0;
    bool initialized_ = false;
};

namespace detail {
inline HRESULT AtlModuleException() noexcept {
    try { throw; }
    catch (const CAtlException& e) { return FAILED(e.m_hr) ? e.m_hr : E_UNEXPECTED; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_UNEXPECTED; }
}

// One registered object-map entry per OBJECT_ENTRY_AUTO expansion, owned by
// its binary image. A class may provide static HRESULT UpdateRegistry(BOOL).
// Missing registration callbacks are errors, not implicit success.
// Constructors only link already-allocated static nodes;
// they neither allocate nor call COM while the loader lock is held.
// Registration completes before the first external class-object request.
using AutoRegistryCallback = HRESULT (*)(BOOL) noexcept;
template<class Class, class = void>
struct AutoRegistryMethod {
    static constexpr AutoRegistryCallback callback = nullptr;
};
template<class Class>
struct AutoRegistryMethod<Class,
    std::void_t<decltype(Class::UpdateRegistry(TRUE))>> {
    static HRESULT Invoke(BOOL install) noexcept {
        try { return static_cast<HRESULT>(Class::UpdateRegistry(install)); }
        catch (...) { return AtlModuleException(); }
    }
    static constexpr AutoRegistryCallback callback = &Invoke;
};

struct AutoObjectEntry;
inline std::atomic<AutoObjectEntry*> autoObjectEntries{nullptr};
struct AutoObjectEntry {
    const CLSID clsid;
    const OpenATL::COM::Creator creator;
    const OpenATL::COM::AggregatedCreator creatorWithOuter;
    const AutoRegistryCallback registrar;
    AutoObjectEntry* next = nullptr;

    AutoObjectEntry(REFCLSID id, OpenATL::COM::Creator function,
                    AutoRegistryCallback registry = nullptr,
                    OpenATL::COM::AggregatedCreator aggregate = nullptr) noexcept
        : clsid(id), creator(function), creatorWithOuter(aggregate), registrar(registry) {
        AutoObjectEntry* head = autoObjectEntries.load(std::memory_order_relaxed);
        do {
            next = head;
        } while (!autoObjectEntries.compare_exchange_weak(
            head, this, std::memory_order_release, std::memory_order_relaxed));
    }
    AutoObjectEntry(const AutoObjectEntry&) = delete;
    AutoObjectEntry& operator=(const AutoObjectEntry&) = delete;
};
struct OwnedClassFactory {
    CLSID clsid{};
    CComPtr<IClassFactory> factory;
};
// One immutable image object map supplies DLL/EXE factories and registration.
class AutoObjectMap final {
private:
    struct RegistryEntry {
        CLSID clsid;
        detail::AutoRegistryCallback callback;
    };
    static bool BeforeClsid(REFCLSID a, REFCLSID b) noexcept {
        if (a.Data1 != b.Data1) return a.Data1 < b.Data1;
        if (a.Data2 != b.Data2) return a.Data2 < b.Data2;
        if (a.Data3 != b.Data3) return a.Data3 < b.Data3;
        for (size_t i = 0; i < 8; ++i)
            if (a.Data4[i] != b.Data4[i]) return a.Data4[i] < b.Data4[i];
        return false;
    }
public:
    // Callbacks freeze on the first map initialization. Registration order is
    // CLSID-sorted, not dependent on loader/link order.
    HRESULT ApplyAutoRegistry(BOOL installing, const CLSID* selected) noexcept {
        HRESULT result = Initialize();
        if (FAILED(result)) return result;
        size_t matched = 0;
        for (const auto& item : autoRegistrars_) {
            if (selected && !IsEqualCLSID(*selected, item.clsid)) continue;
            ++matched;
            if (!item.callback) return E_NOTIMPL; // Preflight *all* callbacks.
        }
        if (!matched) return selected ? CLASS_E_CLASSNOTAVAILABLE : S_FALSE;

        if (installing) {
            for (size_t i = 0; i < autoRegistrars_.size(); ++i) {
                const auto& item = autoRegistrars_[i];
                if (selected && !IsEqualCLSID(*selected, item.clsid)) continue;
                result = item.callback(TRUE);
                if (FAILED(result)) {
                    HRESULT rollback = S_OK;
                    while (i != 0) {
                        --i;
                        const auto& previous = autoRegistrars_[i];
                        if (selected && !IsEqualCLSID(*selected, previous.clsid))
                            continue;
                        const HRESULT undone = previous.callback(FALSE);
                        if (FAILED(undone) && SUCCEEDED(rollback))
                            rollback = undone;
                    }
                    return FAILED(rollback) ? rollback : result;
                }
            }
            return S_OK;
        }
        for (size_t i = autoRegistrars_.size(); i-- != 0;) {
            const auto& item = autoRegistrars_[i];
            if (selected && !IsEqualCLSID(*selected, item.clsid)) continue;
            result = item.callback(FALSE);
            if (FAILED(result)) return result;
        }
        return S_OK;
    }

    HRESULT Initialize() noexcept {
        try {
            std::call_once(autoOnce_, [this]() noexcept {
                try {
                    std::vector<const detail::AutoObjectEntry*> entries;
                    for (const detail::AutoObjectEntry* entry =
                             detail::autoObjectEntries.load(std::memory_order_acquire);
                         entry; entry = entry->next) {
                        if (IsEqualCLSID(entry->clsid, CLSID_NULL) ||
                            (bool(entry->creator) == bool(entry->creatorWithOuter))) {
                            autoStatus_ = E_INVALIDARG;
                            return;
                        }
                        if (entries.size() == 1024) {
                            autoStatus_ = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
                            return;
                        }
                        for (const auto* existing : entries)
                            if (IsEqualCLSID(entry->clsid, existing->clsid)) {
                                autoStatus_ = E_INVALIDARG;
                                return;
                            }
                        entries.push_back(entry);
                    }
                    if (entries.empty()) {
                        autoStatus_ = CLASS_E_CLASSNOTAVAILABLE;
                        return;
                    }
                    std::sort(entries.begin(), entries.end(),
                        [](const detail::AutoObjectEntry* a,
                           const detail::AutoObjectEntry* b) {
                            return BeforeClsid(a->clsid, b->clsid);
                        });
                    for (const auto* entry : entries) {
                        autoEntries_.push_back({entry->clsid, entry->creator,
                                                entry->creatorWithOuter});
                        autoRegistrars_.push_back({entry->clsid, entry->registrar});
                    }
                    autoOwned_.reset(new (std::nothrow)
                        OpenATL::COM::Module(autoEntries_.data(), autoEntries_.size()));
                    if (!autoOwned_) {
                        autoStatus_ = E_OUTOFMEMORY;
                        return;
                    }
                    autoStatus_ = autoOwned_->Status();
                    if (SUCCEEDED(autoStatus_))
                        autoServer_.store(autoOwned_.get(), std::memory_order_release);
                } catch (...) {
                    autoStatus_ = detail::AtlModuleException();
                }
            });
            return autoStatus_;
        } catch (...) {
            return detail::AtlModuleException();
        }
    }

    OpenATL::COM::Module* Server() const noexcept {
        return autoServer_.load(std::memory_order_acquire);
    }
    HRESULT CreateFactories(std::vector<OwnedClassFactory>& output) noexcept {
        const HRESULT hr = Initialize();
        if (hr == CLASS_E_CLASSNOTAVAILABLE) return S_FALSE; // Empty image map.
        if (FAILED(hr)) return hr;
        try {
            std::vector<OwnedClassFactory> staged;
            staged.reserve(autoEntries_.size());
            for (const auto& entry : autoEntries_) {
                OwnedClassFactory item;
                item.clsid = entry.clsid;
                const HRESULT created = Server()->GetClassObject(entry.clsid, IID_IClassFactory,
                    reinterpret_cast<void**>(&item.factory.p));
                if (FAILED(created)) return created;
                if (!item.factory.p) return E_UNEXPECTED;
                staged.push_back(std::move(item));
            }
            output.swap(staged);
            return S_OK;
        } catch (...) { return AtlModuleException(); }
    }
private:
    // Member order: the server is destroyed before its immutable class table.
    std::once_flag autoOnce_;
    HRESULT autoStatus_ = CLASS_E_CLASSNOTAVAILABLE;
    std::vector<OpenATL::COM::ClassEntry> autoEntries_;
    std::vector<RegistryEntry> autoRegistrars_;
    std::unique_ptr<OpenATL::COM::Module> autoOwned_;
    std::atomic<OpenATL::COM::Module*> autoServer_{nullptr};
};

}

template<class T>
class CAtlModuleT : public CAtlModule {
public:
    CAtlModuleT() noexcept { InitLibIdIfPresent<T>(0); }
    static LPCTSTR GetAppId() noexcept { return nullptr; }
    static HRESULT WINAPI UpdateRegistryAppId(BOOL) noexcept { return S_FALSE; }
    HRESULT RegisterAppId() noexcept { return ApplyAppId(TRUE); }
    HRESULT UnregisterAppId() noexcept { return ApplyAppId(FALSE); }
    // Common persistent registration for EXE and DLL modules. One snapshot
    // supplies callbacks, resource ownership and the TypeLib mode policy.
    HRESULT RegisterServer(BOOL registerTypeLib = FALSE, const CLSID* selected = nullptr) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        detail::ModuleRegistrationScope context;
        const bool perUser = context.PerUserRegistration();
        if (!registerTypeLib) return autoMap_.ApplyAutoRegistry(TRUE, selected);
        detail::TypeLibRegistration registration(perUser);
        HRESULT hr = registration.Prepare(_AtlBaseModule.GetModuleInstance());
        if (FAILED(hr)) return hr; // No class callback before TypeLib preflight.
        hr = autoMap_.ApplyAutoRegistry(TRUE, selected);
        if (FAILED(hr)) return hr;
        hr = registration.Register();
        if (FAILED(hr)) {
            const HRESULT rollback = autoMap_.ApplyAutoRegistry(FALSE, selected);
            return FAILED(rollback) ? rollback : hr;
        }
        return S_OK;
    }
    HRESULT UnregisterServer(BOOL unregisterTypeLib = FALSE, const CLSID* selected = nullptr) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        detail::ModuleRegistrationScope context;
        const bool perUser = context.PerUserRegistration();
        if (!unregisterTypeLib) return autoMap_.ApplyAutoRegistry(FALSE, selected);
        detail::TypeLibRegistration registration(perUser);
        HRESULT hr = registration.Prepare(_AtlBaseModule.GetModuleInstance());
        if (FAILED(hr)) return hr;
        hr = autoMap_.ApplyAutoRegistry(FALSE, selected);
        return FAILED(hr) ? hr : registration.Unregister();
    }

protected:
    template<class Registration>
    HRESULT RegisterApplication(Registration registration) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        detail::ModuleRegistrationScope context;
        HRESULT app = E_UNEXPECTED;
        try { app = static_cast<T*>(this)->RegisterAppId(); }
        catch (...) { return detail::AtlModuleException(); }
        if (FAILED(app)) return app;
        HRESULT hr = E_UNEXPECTED;
        try { hr = registration(); }
        catch (...) { hr = detail::AtlModuleException(); }
        if (FAILED(hr) && app == S_OK) {
            HRESULT rollback = E_UNEXPECTED;
            try { rollback = static_cast<T*>(this)->UnregisterAppId(); }
            catch (...) { rollback = detail::AtlModuleException(); }
            return FAILED(rollback) ? rollback : hr;
        }
        return SUCCEEDED(hr) && app == S_OK ? S_OK : hr;
    }
    template<class Unregistration>
    HRESULT UnregisterApplication(Unregistration unregistration) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        detail::ModuleRegistrationScope context;
        HRESULT hr = E_UNEXPECTED;
        try { hr = unregistration(); }
        catch (...) { return detail::AtlModuleException(); }
        if (FAILED(hr)) return hr;
        HRESULT app = E_UNEXPECTED;
        try { app = static_cast<T*>(this)->UnregisterAppId(); }
        catch (...) { return detail::AtlModuleException(); }
        return FAILED(app) || app == S_OK ? app : hr;
    }
    detail::AutoObjectMap autoMap_;
private:
    HRESULT ApplyAppId(BOOL install) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        detail::ModuleRegistrationScope context;
        try { return static_cast<T*>(this)->UpdateRegistryAppId(install); }
        catch (...) { return detail::AtlModuleException(); }
    }
    template<class U>
    static auto InitLibIdIfPresent(int) noexcept
        -> decltype(U::InitLibId(), void()) { U::InitLibId(); }
    template<class U>
    static void InitLibIdIfPresent(...) noexcept {}
};

// DLL module lifetime and standard ATL export forwarding.
//
// BindServer() is the explicit-class-table bridge. The bound
// OpenATL::COM::Module and its static class table must outlive the DLL
// module and every factory/object/server lock. Binding is one-time; callers
// must coordinate DLL unload and configuration externally.
template<class T>
class ATL_NO_VTABLE CAtlDllModuleT : public CAtlModuleT<T> {
public:
    CAtlDllModuleT() noexcept = default;
    CAtlDllModuleT(const CAtlDllModuleT&) = delete;
    CAtlDllModuleT& operator=(const CAtlDllModuleT&) = delete;

    HRESULT BindServer(OpenATL::COM::Module& module) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        const HRESULT status = module.Status();
        if (FAILED(status)) return status;
        OpenATL::COM::Module* expected = nullptr;
        if (server_.compare_exchange_strong(expected, &module)) return S_OK;
        return expected == &module ? S_OK : HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    // No COM initialization/registry updates may run under the loader lock.
    BOOL WINAPI DllMain(DWORD /*reason*/, LPVOID /*reserved*/) noexcept {
        return TRUE;
    }

    HRESULT DllCanUnloadNow() const noexcept {
        if (this->IsTerminated() || this->GetLockCount() != 0) return S_FALSE;
        const auto* server = server_.load(std::memory_order_acquire);
        const auto* autoServer = this->autoMap_.Server();
        if (server && server->CanUnloadNow() != S_OK) return S_FALSE;
        if (autoServer && autoServer->CanUnloadNow() != S_OK) return S_FALSE;
        return S_OK;
    }

    HRESULT DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (this->IsTerminated()) return E_UNEXPECTED;
        // Match ATL's CRTP hook: the DLL export delegates to T::GetClassObject,
        // allowing a derived module to provide a standard object-map lookup.
        // Pin the image throughout this synchronous callback.
        this->Lock();
        HRESULT result = E_UNEXPECTED;
        try {
            result = static_cast<T*>(this)->GetClassObject(clsid, iid, output);
        } catch (...) {
            result = detail::AtlModuleException();
        }
        // A successful COM lookup without an owned interface must never be
        // advertised as success, even by a custom derived ATL module.
        if (SUCCEEDED(result) && *output == nullptr) result = E_UNEXPECTED;
        if (FAILED(result)) *output = nullptr;
        this->Unlock();
        return result;
    }

    HRESULT GetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (this->IsTerminated()) return E_UNEXPECTED;
        // An explicitly bound legacy class table remains the single active
        // server engine for that module. With no binding, discover standard
        // ATL OBJECT_ENTRY_AUTO declarations and build that same engine once.
        auto* server = server_.load(std::memory_order_acquire);
        if (!server) {
            const HRESULT hr = this->autoMap_.Initialize();
            if (FAILED(hr)) return hr;
            server = this->autoMap_.Server();
        }
        return server ? server->GetClassObject(clsid, iid, output)
                      : CLASS_E_CLASSNOTAVAILABLE;
    }

    // ATL-compatible class-directed registration: each OBJECT_ENTRY_AUTO
    // class must expose a real static UpdateRegistry(BOOL), or explicitly
    // opt out with DECLARE_NO_REGISTRY. This path never derives ProgIDs or
    // installer privileges from the CLSID; resource scripts retain the
    // bounded parser and owner-verified registration policy.
    HRESULT RegisterServer(BOOL library = FALSE, const CLSID* selected = nullptr) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        if (server_.load(std::memory_order_acquire)) return E_NOTIMPL;
        return CAtlModuleT<T>::RegisterServer(library, selected);
    }
    HRESULT UnregisterServer(BOOL library = FALSE, const CLSID* selected = nullptr) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        if (server_.load(std::memory_order_acquire)) return E_NOTIMPL;
        return CAtlModuleT<T>::UnregisterServer(library, selected);
    }

    // Pin active registration exports against a cooperative DLL unload.
    HRESULT DllRegisterServer(BOOL registerTypeLib = TRUE) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        this->Lock();
        HRESULT result = E_UNEXPECTED;
        try {
            result = this->RegisterApplication([&] {
                return static_cast<T*>(this)->RegisterServer(registerTypeLib, nullptr);
            });
        } catch (...) {
            result = detail::AtlModuleException();
        }
        this->Unlock();
        return result;
    }
    HRESULT DllUnregisterServer(BOOL unregisterTypeLib = TRUE) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        this->Lock();
        HRESULT result = E_UNEXPECTED;
        try {
            result = this->UnregisterApplication([&] {
                return static_cast<T*>(this)->UnregisterServer(unregisterTypeLib, nullptr);
            });
        } catch (...) {
            result = detail::AtlModuleException();
        }
        this->Unlock();
        return result;
    }

private:
    std::atomic<OpenATL::COM::Module*> server_{nullptr};
};

template<class T>
class CAtlExeModuleT : public CAtlModuleT<T> {
public:
    CAtlExeModuleT() noexcept : owner_(::GetCurrentThreadId()), m_hr(T::InitializeCom()) {}
    ~CAtlExeModuleT() noexcept {
        this->Term();
        if (SUCCEEDED(m_hr)) T::UninitializeCom();
    }
    void Term() noexcept {
        if (::GetCurrentThreadId() != owner_ || running_ || FAILED(RevokeClassObjects()))
            std::terminate();
        classes_.clear();
        // Native registration owns factory references until successful revoke.
        // Client factories, objects and server locks must also have retired.
        const auto* server = this->autoMap_.Server();
        if (server && server->CanUnloadNow() != S_OK) std::terminate();
        CAtlModule::Term();
    }
    static HRESULT InitializeCom() noexcept {
#if defined(_ATL_APARTMENT_THREADED) || defined(_ATL_SINGLE_THREADED)
        return ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#else
        return ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    }
    static void UninitializeCom() noexcept { ::CoUninitialize(); }
    HRESULT GetInitHRESULT() const noexcept { return m_hr; }

    HRESULT RegisterAppId() noexcept {
        const HRESULT hr = CheckThread();
        return FAILED(hr) ? hr : CAtlModuleT<T>::RegisterAppId();
    }
    HRESULT UnregisterAppId() noexcept {
        const HRESULT hr = CheckThread();
        return FAILED(hr) ? hr : CAtlModuleT<T>::UnregisterAppId();
    }
    HRESULT RegisterServer(BOOL library = FALSE, const CLSID* selected = nullptr) noexcept {
        const HRESULT hr = CheckThread();
        return FAILED(hr) ? hr : CAtlModuleT<T>::RegisterServer(library, selected);
    }
    HRESULT UnregisterServer(BOOL library = FALSE, const CLSID* selected = nullptr) noexcept {
        const HRESULT hr = CheckThread();
        return FAILED(hr) ? hr : CAtlModuleT<T>::UnregisterServer(library, selected);
    }
    HRESULT SetClassObjects(const OpenATL::COM::ClassObjectEntry* entries, size_t count) noexcept {
        HRESULT hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (running_ || !cookies_.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (count > 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        if (count && !entries) return E_INVALIDARG;
        try {
            std::vector<BoundClass> staged;
            staged.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (!entries[i].factory || IsEqualCLSID(entries[i].clsid, CLSID_NULL)) return E_INVALIDARG;
                for (size_t j = 0; j < i; ++j)
                    if (IsEqualCLSID(entries[i].clsid, entries[j].clsid)) return E_INVALIDARG;
                BoundClass entry;
                entry.clsid = entries[i].clsid;
                hr = entries[i].factory->QueryInterface(IID_IClassFactory,
                    reinterpret_cast<void**>(&entry.factory));
                if (FAILED(hr)) return hr;
                if (!entry.factory) return E_UNEXPECTED;
                staged.push_back(std::move(entry));
            }
            classes_.swap(staged);
            explicitClasses_ = true;
            return S_OK;
        } catch (...) { return detail::AtlModuleException(); }
    }
    HRESULT RegisterClassObjects(DWORD context, DWORD flags) noexcept {
        HRESULT hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!cookies_.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        std::vector<BoundClass> automatic;
        const auto* selected = &classes_;
        if (!explicitClasses_) {
            hr = this->autoMap_.CreateFactories(automatic);
            if (hr != S_OK) return hr;
            selected = &automatic;
        }
        if (selected->empty()) return S_FALSE;
        try { cookies_.resize(selected->size()); }
        catch (...) { return detail::AtlModuleException(); }
        for (size_t i = 0; i < selected->size(); ++i) {
            DWORD cookie = 0;
            hr = detail::RegisterClassCookie((*selected)[i].clsid, (*selected)[i].factory.p,
                                             context, flags, cookie);
            if (FAILED(hr)) {
                const HRESULT rollback = RevokeClassObjects();
                return FAILED(rollback) ? rollback : hr;
            }
            cookies_[i] = cookie;
        }
        return S_OK;
    }
    HRESULT RevokeClassObjects() noexcept {
        if (cookies_.empty()) return S_FALSE;
        if (::GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        HRESULT result = S_OK;
        for (auto i = cookies_.rbegin(); i != cookies_.rend(); ++i) {
            if (!i->has_value()) continue;
            DWORD cookie = **i;
            HRESULT hr = detail::RevokeClassCookie(cookie);
            if (!cookie) i->reset();
            else if (SUCCEEDED(result)) result = hr;
        }
        if (SUCCEEDED(result)) cookies_.clear();
        return result;
    }
    HRESULT PreMessageLoop(int) noexcept {
        return static_cast<T*>(this)->RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
    }
    HRESULT PostMessageLoop() noexcept { return static_cast<T*>(this)->RevokeClassObjects(); }
    void RunMessageLoop() noexcept {
        MSG message{};
        BOOL result;
        while ((result = ::GetMessageW(&message, nullptr, 0, 0)) > 0) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (result == -1) {
            DWORD error = ::GetLastError();
            loopResult_ = error ? HRESULT_FROM_WIN32(error) : E_UNEXPECTED;
        }
    }
    HRESULT Run(int show = SW_HIDE) noexcept {
        HRESULT hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (running_) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        running_ = true;
        loopResult_ = S_OK;
        try {
            hr = static_cast<T*>(this)->PreMessageLoop(show);
            if (hr == S_OK) {
                try { static_cast<T*>(this)->RunMessageLoop(); }
                catch (...) { loopResult_ = detail::AtlModuleException(); }
                hr = static_cast<T*>(this)->PostMessageLoop();
                if (FAILED(loopResult_)) hr = loopResult_;
            }
        } catch (...) { hr = detail::AtlModuleException(); }
        HRESULT cleanup = RevokeClassObjects();
        if (SUCCEEDED(hr) && FAILED(cleanup)) hr = cleanup;
        running_ = false;
        return hr;
    }
    bool ParseCommandLine(LPCTSTR commandLine, HRESULT* result) noexcept {
        if (!result) return false;
        *result = CheckThread();
        if (FAILED(*result)) return false;
        *result = E_INVALIDARG;
        if (!commandLine) return false;
        size_t length = 0;
        while (length < 32767 && commandLine[length]) ++length;
        if (length == 32767) { *result = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW); return false; }
        int count = 0;
        LPWSTR* args = ::CommandLineToArgvW(commandLine, &count);
        if (!args) { *result = E_OUTOFMEMORY; return false; }
        bool run = true;
        *result = S_OK;
        try {
            for (int i = 1; i < count; ++i) {
                const wchar_t* option = args[i];
                if (*option != L'/' && *option != L'-') continue;
                ++option;
                const bool perUserInstall = ::_wcsicmp(option, L"RegServerPerUser") == 0;
                const bool perUserRemove = ::_wcsicmp(option, L"UnregServerPerUser") == 0;
                const bool install = perUserInstall || ::_wcsicmp(option, L"RegServer") == 0;
                const bool remove = perUserRemove || ::_wcsicmp(option, L"UnregServer") == 0;
                if (!install && !remove) continue;
                run = false;
                if (perUserInstall || perUserRemove) {
                    const auto* enclosing = OpenATL::COM::rgs_resource_detail::CurrentModule();
                    if (enclosing && !enclosing->PerUserRegistration()) {
                        *result = E_UNEXPECTED; break;
                    }
                    *result = AtlSetPerUserRegistration(true);
                    if (FAILED(*result)) break;
                }
                if (install) {
                    *result = this->RegisterApplication([&] {
                        return static_cast<T*>(this)->RegisterServer(TRUE);
                    });
                } else {
                    *result = this->UnregisterApplication([&] {
                        return static_cast<T*>(this)->UnregisterServer(TRUE);
                    });
                }
                break;
            }
        } catch (...) { *result = detail::AtlModuleException(); run = false; }
        ::LocalFree(args);
        return run;
    }
    int WinMain(int show) noexcept {
        const HRESULT state = CheckThread();
        if (FAILED(state)) return static_cast<int>(state);
        HRESULT result = S_OK;
        try {
            if (static_cast<T*>(this)->ParseCommandLine(::GetCommandLineW(), &result))
                result = static_cast<T*>(this)->Run(show);
        } catch (...) { result = detail::AtlModuleException(); }
        return static_cast<int>(result);
    }
private:
    HRESULT CheckThread() const noexcept {
        if (::GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        if (FAILED(m_hr)) return m_hr;
        return this->IsTerminated() ? E_UNEXPECTED : S_OK;
    }
    using BoundClass = detail::OwnedClassFactory;
    DWORD owner_;
    HRESULT m_hr;
    bool running_ = false;
    bool explicitClasses_ = false;
    HRESULT loopResult_ = S_OK;
    std::vector<BoundClass> classes_;
    std::vector<std::optional<DWORD>> cookies_;
};
} // namespace ATL
#include "atl_registrar.h"
#endif
