// Optional, source-owned in-process COM server primitives. No ATL module shim.
#ifndef OPENATL_COM_SERVER_H
#define OPENATL_COM_SERVER_H

#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <exception>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>

namespace OpenATL { namespace COM {

class Module;
using Creator = HRESULT (*)(Module&, REFIID, void**) noexcept;
using AggregatedCreator = HRESULT (*)(Module&, IUnknown*, REFIID, void**) noexcept;
struct ClassEntry {
    CLSID clsid;
    Creator create = nullptr;
    AggregatedCreator createWithOuter = nullptr;
};

namespace detail {
inline bool retain(std::atomic<ULONG>& count) noexcept {
    ULONG value = count.load();
    while (value != (std::numeric_limits<ULONG>::max)()) {
        if (count.compare_exchange_weak(value, value + 1)) return true;
    }
    return false;
}
inline ULONG release(std::atomic<ULONG>& count) noexcept {
    ULONG value = count.load();
    for (;;) {
        if (value == 0) std::terminate();
        if (count.compare_exchange_weak(value, value - 1)) return value - 1;
    }
}
} // namespace detail

// The immutable caller-owned class table and this module must outlive every
// factory, object and server lock. Destruction requires external quiescence.
// No COM apartment is initialized here; COM and its client own that policy.
class Module final {
public:
    Module(const ClassEntry* entries, size_t count) noexcept
        : entries_(entries), count_(count) {
        if (count != 0 && entries == nullptr) { status_ = E_INVALIDARG; return; }
        for (size_t i = 0; i < count; ++i) {
            if ((bool(entries[i].create) == bool(entries[i].createWithOuter)) ||
                IsEqualCLSID(entries[i].clsid, CLSID_NULL)) {
                status_ = E_INVALIDARG; return;
            }
            for (size_t j = 0; j < i; ++j)
                if (IsEqualCLSID(entries[i].clsid, entries[j].clsid)) {
                    status_ = E_INVALIDARG; return;
                }
        }
    }
    ~Module() {
        if (references_.load() != 0) std::terminate();
    }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    HRESULT Status() const noexcept { return status_; }
    HRESULT GetClassObject(REFCLSID clsid, REFIID iid, void** output) noexcept;
    HRESULT CanUnloadNow() const noexcept {
        return SUCCEEDED(status_) && references_.load() == 0 ? S_OK : S_FALSE;
    }
    HRESULT LockServer(BOOL lock) noexcept {
        if (FAILED(status_)) return status_;
        if (lock) {
            if (!detail::retain(references_)) return E_OUTOFMEMORY;
            if (detail::retain(serverLocks_)) return S_OK;
            detail::release(references_);
            return E_OUTOFMEMORY;
        }
        ULONG value = serverLocks_.load();
        for (;;) {
            if (value == 0) return E_UNEXPECTED;
            if (serverLocks_.compare_exchange_weak(value, value - 1)) break;
        }
        detail::release(references_);
        return S_OK;
    }

private:
    friend class ModuleReference;
    const ClassEntry* entries_;
    size_t count_;
    HRESULT status_ = S_OK;
    std::atomic<ULONG> references_{0};
    std::atomic<ULONG> serverLocks_{0};
};

// A concrete factory/object owns one module reference, independent of the
// number of interface pointers to that concrete instance.
class ModuleReference final {
public:
    explicit ModuleReference(Module& module) noexcept : module_(module) {
        status_ = module.Status();
        if (SUCCEEDED(status_) && !detail::retain(module.references_))
            status_ = E_OUTOFMEMORY;
    }
    ~ModuleReference() {
        if (SUCCEEDED(status_)) detail::release(module_.references_);
    }
    ModuleReference(const ModuleReference&) = delete;
    ModuleReference& operator=(const ModuleReference&) = delete;
    HRESULT Status() const noexcept { return status_; }
private:
    Module& module_;
    HRESULT status_;
};

// Explicit IID descriptors work on MSVC and MinGW without compiler-specific
// uuid attributes. Interfaces must derive directly/indirectly from IUnknown.
template<class T, const IID* Id> struct Interface {
    using Type = T;
    static const IID& iid() noexcept { return *Id; }
    template<class Owner> static T* cast(Owner* self) noexcept {
        return static_cast<T*>(self);
    }
};

// Expose an inherited interface through a specified implemented interface,
// avoiding a second subobject (and an ambiguous IUnknown identity).
template<class T, const IID* Id, class Via> struct InterfaceAlias {
    static_assert(std::is_base_of<T, Via>::value, "Alias must be a base of Via");
    using Type = T;
    static const IID& iid() noexcept { return *Id; }
    template<class Owner> static T* cast(Owner* self) noexcept {
        return static_cast<T*>(static_cast<Via*>(self));
    }
};
namespace detail {
template<class Entry> struct InterfaceBase : Entry::Type {};
template<class T, const IID* Id, class Via>
struct InterfaceBase<InterfaceAlias<T, Id, Via>> {};
} // namespace detail

template<class Derived, class... Interfaces>
class Object : public detail::InterfaceBase<Interfaces>... {
    static_assert(sizeof...(Interfaces) != 0, "At least one COM interface is required");
    static_assert((std::is_base_of<IUnknown, typename Interfaces::Type>::value && ...),
                  "Every exposed interface must derive from IUnknown");
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept final {
        if (!output) return E_POINTER;
        *output = nullptr;
        using First = typename std::tuple_element<0, std::tuple<Interfaces...>>::type;
        if (IsEqualIID(iid, IID_IUnknown))
            *output = static_cast<IUnknown*>(First::cast(this));
        else
            (query<Interfaces>(iid, output) || ...);
        if (!*output) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept final {
        if (!detail::retain(references_)) std::terminate();
        return references_.load();
    }
    ULONG STDMETHODCALLTYPE Release() noexcept final {
        const ULONG count = detail::release(references_);
        if (count == 0) delete static_cast<Derived*>(this);
        return count;
    }
    static HRESULT CreateInstance(Module& module, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (FAILED(module.Status())) return module.Status();
        try {
            // A derived constructor may throw. Its base's ModuleReference
            // then rolls back before the exception crosses this boundary.
            Derived* object = new (std::nothrow) Derived(module);
            if (!object) return E_OUTOFMEMORY;
            HRESULT hr = object->moduleReference_.Status();
            if (SUCCEEDED(hr)) hr = object->QueryInterface(iid, output);
            object->Release();
            return hr;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_UNEXPECTED;
        }
    }
protected:
    explicit Object(Module& module) noexcept : moduleReference_(module) {}
    ~Object() = default;
private:
    template<class Entry> bool query(REFIID iid, void** output) noexcept {
        if (!IsEqualIID(iid, Entry::iid())) return false;
        *output = Entry::cast(this);
        return true;
    }
    ModuleReference moduleReference_;
    std::atomic<ULONG> references_{1};
};

namespace detail {
class ClassFactory final : public IClassFactory {
public:
    ClassFactory(Module& module, const ClassEntry& entry) noexcept
        : module_(module), reference_(module), entry_(entry) {}
    HRESULT Status() const noexcept { return reference_.Status(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_IClassFactory))
            return E_NOINTERFACE;
        *output = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        if (!retain(references_)) std::terminate();
        return references_.load();
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = release(references_);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid,
                                             void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer && (!entry_.createWithOuter || !IsEqualIID(iid, IID_IUnknown)))
            return CLASS_E_NOAGGREGATION;
        const HRESULT hr = entry_.createWithOuter
            ? entry_.createWithOuter(module_, outer, iid, output)
            : entry_.create(module_, iid, output);
        return SUCCEEDED(hr) && !*output ? E_UNEXPECTED : hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) noexcept override {
        return module_.LockServer(lock);
    }
private:
    Module& module_;
    ModuleReference reference_;
    const ClassEntry entry_;
    std::atomic<ULONG> references_{1};
};
} // namespace detail

inline HRESULT Module::GetClassObject(REFCLSID clsid, REFIID iid,
                                      void** output) noexcept {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (FAILED(status_)) return status_;
    for (size_t i = 0; i < count_; ++i) {
        if (!IsEqualCLSID(clsid, entries_[i].clsid)) continue;
        auto* factory = new (std::nothrow) detail::ClassFactory(*this, entries_[i]);
        if (!factory) return E_OUTOFMEMORY;
        HRESULT hr = factory->Status();
        if (SUCCEEDED(hr)) hr = factory->QueryInterface(iid, output);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

}} // namespace OpenATL::COM
#endif
