// Clean-room ATL SAFEARRAY ownership over native Windows OLEAUT32.
#ifndef OPENATL_ATLSAFE_H
#define OPENATL_ATLSAFE_H
#pragma once
#include "atlbase.h"
#include "atlcomcli.h"
#include <cstring>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace ATL {
template<class T> struct _ATL_AutomationType;
#define OPENATL_ARRAY_TYPE(cpp, automation) \
    template<> struct _ATL_AutomationType<cpp> { static constexpr VARTYPE type = automation; };
OPENATL_ARRAY_TYPE(char, VT_I1)
OPENATL_ARRAY_TYPE(signed char, VT_I1)
OPENATL_ARRAY_TYPE(unsigned char, VT_UI1)
OPENATL_ARRAY_TYPE(short, VT_I2)
OPENATL_ARRAY_TYPE(unsigned short, VT_UI2)
OPENATL_ARRAY_TYPE(int, VT_I4)
OPENATL_ARRAY_TYPE(unsigned int, VT_UI4)
OPENATL_ARRAY_TYPE(long, VT_I4)
OPENATL_ARRAY_TYPE(unsigned long, VT_UI4)
OPENATL_ARRAY_TYPE(long long, VT_I8)
OPENATL_ARRAY_TYPE(unsigned long long, VT_UI8)
OPENATL_ARRAY_TYPE(float, VT_R4)
OPENATL_ARRAY_TYPE(double, VT_R8)
OPENATL_ARRAY_TYPE(CY, VT_CY)
OPENATL_ARRAY_TYPE(DECIMAL, VT_DECIMAL)
OPENATL_ARRAY_TYPE(BSTR, VT_BSTR)
OPENATL_ARRAY_TYPE(VARIANT, VT_VARIANT)
OPENATL_ARRAY_TYPE(IUnknown*, VT_UNKNOWN)
OPENATL_ARRAY_TYPE(IDispatch*, VT_DISPATCH)
#undef OPENATL_ARRAY_TYPE

class CComSafeArrayBound : public SAFEARRAYBOUND {
public:
    CComSafeArrayBound(ULONG count = 0, LONG lower = 0) noexcept { cElements = count; lLbound = lower; }
    ULONG GetCount() const noexcept { return cElements; }
    LONG GetLowerBound() const noexcept { return lLbound; }
    LONG GetUpperBound() const noexcept { return static_cast<LONG>(static_cast<ULONG>(lLbound) + cElements - 1); }
    ULONG SetCount(ULONG count) noexcept { return cElements = count; }
    LONG SetLowerBound(LONG lower) noexcept { return lLbound = lower; }
    CComSafeArrayBound& operator=(ULONG count) noexcept { cElements = count; lLbound = 0; return *this; }
};

template<class T, VARTYPE _vartype = _ATL_AutomationType<T>::type>
class CComSafeArray {
    static_assert(std::is_trivially_copyable<T>::value, "Use native Automation element types");
    static_assert(_vartype == VT_I1 || _vartype == VT_UI1 || _vartype == VT_I2 || _vartype == VT_UI2 ||
        _vartype == VT_I4 || _vartype == VT_UI4 || _vartype == VT_I8 || _vartype == VT_UI8 ||
        _vartype == VT_INT || _vartype == VT_UINT || _vartype == VT_R4 || _vartype == VT_R8 ||
        _vartype == VT_BOOL || _vartype == VT_ERROR || _vartype == VT_DATE || _vartype == VT_CY ||
        _vartype == VT_DECIMAL || _vartype == VT_BSTR || _vartype == VT_VARIANT ||
        _vartype == VT_UNKNOWN || _vartype == VT_DISPATCH, "Unsupported SAFEARRAY element VARTYPE");
public:
    LPSAFEARRAY m_psa = nullptr;
    CComSafeArray() noexcept = default;
    explicit CComSafeArray(const SAFEARRAYBOUND& bound) { Check(Create(&bound)); }
    explicit CComSafeArray(ULONG count, LONG lower = 0) { Check(Create(count, lower)); }
    explicit CComSafeArray(const SAFEARRAYBOUND* bounds, UINT dimensions = 1) { Check(Create(bounds, dimensions)); }
    CComSafeArray(const CComSafeArray& other) { if (other.m_psa) Check(CopyFrom(other.m_psa)); }
    explicit CComSafeArray(const SAFEARRAY& other) { Check(CopyFrom(&other)); }
    explicit CComSafeArray(const SAFEARRAY* other) { Check(CopyFrom(other)); }
    ~CComSafeArray() noexcept { if (FAILED(Destroy())) std::terminate(); }
    CComSafeArray& operator=(const CComSafeArray& other) {
        if (this != &other) Check(other.m_psa ? CopyFrom(other.m_psa) : Destroy());
        return *this;
    }
    CComSafeArray& operator=(const SAFEARRAY* other) { Check(CopyFrom(other)); return *this; }
    operator LPSAFEARRAY() const noexcept { return m_psa; }
    LPSAFEARRAY* GetSafeArrayPtr() noexcept { return &m_psa; }
    UINT GetDimensions() const noexcept { return m_psa ? ::SafeArrayGetDim(m_psa) : 0; }
    VARTYPE GetType() const noexcept { return _vartype; }
    ULONG GetCount(UINT dimension = 0) const {
        if (!m_psa) return 0;
        const auto count = int64_t(GetUpperBound(dimension)) - GetLowerBound(dimension) + 1;
        return static_cast<ULONG>(count);
    }
    LONG GetLowerBound(UINT dimension = 0) const {
        LONG value = 0;
        if (!m_psa || dimension >= GetDimensions()) AtlThrow(E_INVALIDARG);
        Check(::SafeArrayGetLBound(m_psa, dimension + 1, &value));
        return value;
    }
    LONG GetUpperBound(UINT dimension = 0) const {
        LONG value = 0;
        if (!m_psa || dimension >= GetDimensions()) AtlThrow(E_INVALIDARG);
        Check(::SafeArrayGetUBound(m_psa, dimension + 1, &value));
        return value;
    }
    bool IsSizable() const noexcept {
        return m_psa && !(m_psa->fFeatures & (FADF_AUTO | FADF_STATIC | FADF_EMBEDDED | FADF_FIXEDSIZE));
    }
    HRESULT Create(ULONG count = 0, LONG lower = 0) noexcept {
        SAFEARRAYBOUND bound{count, lower};
        return Create(&bound);
    }
    HRESULT Create(const SAFEARRAYBOUND* bounds, UINT dimensions = 1) noexcept {
        if (!bounds) return E_POINTER;
        if (m_psa || !dimensions || dimensions > 65535) return E_INVALIDARG;
        size_t elements = 1;
        for (UINT n = 0; n < dimensions; ++n) {
            if (!ValidBound(bounds[n])) return E_INVALIDARG;
            if (bounds[n].cElements && elements > (std::numeric_limits<size_t>::max)() / bounds[n].cElements)
                return E_INVALIDARG;
            elements *= bounds[n].cElements;
        }
        if (elements > (std::numeric_limits<size_t>::max)() / sizeof(T)) return E_INVALIDARG;
        SAFEARRAY* created = ::SafeArrayCreate(_vartype, dimensions, const_cast<SAFEARRAYBOUND*>(bounds));
        if (!created) return E_OUTOFMEMORY;
        HRESULT hr = Attach(created);
        if (FAILED(hr)) ::SafeArrayDestroy(created);
        return hr;
    }
    HRESULT Attach(const SAFEARRAY* source) noexcept {
        if (!source) return E_INVALIDARG;
        if (source == m_psa) return S_OK;
        HRESULT hr = Validate(source);
        if (FAILED(hr)) return hr;
        auto* incoming = const_cast<SAFEARRAY*>(source);
        hr = ::SafeArrayLock(incoming);
        if (FAILED(hr)) return hr;
        hr = Destroy();
        if (FAILED(hr)) { ::SafeArrayUnlock(incoming); return hr; }
        m_psa = incoming;
        locked_ = true;
        return S_OK;
    }
    LPSAFEARRAY Detach() {
        if (m_psa && locked_) { Check(::SafeArrayUnlock(m_psa)); locked_ = false; }
        return std::exchange(m_psa, nullptr);
    }
    HRESULT Destroy() noexcept {
        if (!m_psa) return S_OK;
        const bool held = locked_;
        if (held) {
            const HRESULT hr = ::SafeArrayUnlock(m_psa);
            if (FAILED(hr)) return hr;
            locked_ = false;
        }
        const HRESULT hr = ::SafeArrayDestroy(m_psa);
        if (FAILED(hr)) {
            if (held) { if (FAILED(::SafeArrayLock(m_psa))) std::terminate(); locked_ = true; }
            return hr;
        }
        m_psa = nullptr;
        return S_OK;
    }
    HRESULT CopyFrom(const SAFEARRAY* source) noexcept {
        if (!source) return E_INVALIDARG;
        HRESULT hr = Validate(source);
        if (FAILED(hr)) return hr;
        SAFEARRAY* copy = nullptr;
        hr = ::SafeArrayCopy(const_cast<SAFEARRAY*>(source), &copy);
        if (FAILED(hr)) return hr;
        hr = Attach(copy);
        if (FAILED(hr)) ::SafeArrayDestroy(copy);
        return hr;
    }
    HRESULT CopyFrom(LPSAFEARRAY* source) noexcept { return source ? CopyFrom(*source) : E_POINTER; }
    HRESULT CopyFrom(std::nullptr_t) noexcept { return E_POINTER; }
    HRESULT CopyTo(LPSAFEARRAY* output) const noexcept {
        if (!output) return E_POINTER;
        if (output == &m_psa) return E_INVALIDARG;
        *output = nullptr;
        return m_psa ? ::SafeArrayCopy(m_psa, output) : S_OK;
    }
    T& GetAt(LONG index) const {
        if (GetDimensions() != 1) AtlThrow(E_INVALIDARG);
        Check(EnsureLocked());
        void* element = nullptr;
        Check(::SafeArrayPtrOfIndex(m_psa, &index, &element));
        return *static_cast<T*>(element);
    }
    T& operator[](LONG index) const { return GetAt(index); }
    T& operator[](int index) const { return GetAt(static_cast<LONG>(index)); }
    HRESULT SetAt(LONG index, const T& value, BOOL copy = TRUE) noexcept {
        if (GetDimensions() != 1) return E_INVALIDARG;
        HRESULT hr = EnsureLocked();
        if (FAILED(hr)) return hr;
        void* raw = nullptr;
        hr = ::SafeArrayPtrOfIndex(m_psa, &index, &raw);
        if (FAILED(hr)) return hr;
        auto* slot = static_cast<T*>(raw);
        if constexpr (_vartype == VT_BSTR || _vartype == VT_VARIANT) {
            if (!copy) {
                if (slot == &value) return S_OK;
                if constexpr (_vartype == VT_BSTR) {
                    if (*slot == value) return S_OK;
                    ::SysFreeString(*slot);
                } else {
                    hr = ::VariantClear(slot);
                    if (FAILED(hr)) return hr;
                }
                std::memcpy(slot, &value, sizeof(T)); // Ownership transfers on success.
                return S_OK;
            }
        }
        return ::SafeArrayPutElement(m_psa, &index, Input(value));
    }
    HRESULT MultiDimGetAt(const LONG* indices, T& value) const noexcept {
        if (!indices) return E_POINTER;
        if (!m_psa) return E_INVALIDARG;
        const HRESULT hr = EnsureLocked();
        return FAILED(hr) ? hr : ::SafeArrayGetElement(m_psa, const_cast<LONG*>(indices), &value);
    }
    HRESULT MultiDimSetAt(const LONG* indices, const T& value) noexcept {
        if (!indices) return E_POINTER;
        if (!m_psa) return E_INVALIDARG;
        const HRESULT hr = EnsureLocked();
        return FAILED(hr) ? hr : ::SafeArrayPutElement(m_psa, const_cast<LONG*>(indices), Input(value));
    }
    HRESULT Resize(ULONG count, LONG lower = 0) noexcept {
        SAFEARRAYBOUND bound{count, lower};
        return Resize(&bound);
    }
    HRESULT Resize(const SAFEARRAYBOUND* bound) noexcept {
        if (!bound) return E_POINTER;
        if (!ValidBound(*bound) || bound->cElements > (std::numeric_limits<size_t>::max)() / sizeof(T)) return E_INVALIDARG;
        if (!m_psa) return Create(bound);
        HRESULT hr = EnsureLocked();
        if (FAILED(hr)) return hr;
        hr = ::SafeArrayUnlock(m_psa);
        if (FAILED(hr)) return hr;
        locked_ = false;
        hr = ::SafeArrayRedim(m_psa, const_cast<SAFEARRAYBOUND*>(bound));
        if (FAILED(::SafeArrayLock(m_psa))) std::terminate();
        locked_ = true;
        return hr;
    }
    HRESULT Add(const T& value, BOOL copy = TRUE) noexcept { return Add(1, &value, copy); }
    HRESULT Add(ULONG count, const T* values, BOOL copy = TRUE) noexcept {
        if (!count) return S_OK;
        if (!values) return E_POINTER;
        if (m_psa && GetDimensions() != 1) return E_INVALIDARG;
        const ULONG oldCount = m_psa ? m_psa->rgsabound[0].cElements : 0;
        const LONG lower = m_psa ? m_psa->rgsabound[0].lLbound : 0;
        if (count > (std::numeric_limits<ULONG>::max)() - oldCount) return E_INVALIDARG;
        SAFEARRAYBOUND bound{oldCount + count, lower};
        if (!ValidBound(bound)) return E_INVALIDARG;
        constexpr bool resources = _vartype == VT_BSTR || _vartype == VT_VARIANT;
        // Staging also handles input aliases into our array before Redim moves data.
        if constexpr (resources) if (!copy && m_psa && oldCount) {
            const uintptr_t start = reinterpret_cast<uintptr_t>(m_psa->pvData);
            const uintptr_t input = reinterpret_cast<uintptr_t>(values);
            if (input >= start && input - start < size_t(oldCount) * sizeof(T)) return E_INVALIDARG;
        }
        CComSafeArray staged;
        const LONG stagedLower = count > ULONG((std::numeric_limits<LONG>::max)())
            ? (std::numeric_limits<LONG>::min)() : 0;
        HRESULT hr = staged.Create(count, stagedLower);
        if (FAILED(hr)) return hr;
        if constexpr (resources) {
            if (!copy) std::memcpy(staged.m_psa->pvData, values, size_t(count) * sizeof(T));
        }
        if (!resources || copy) for (ULONG n = 0; n < count; ++n) {
            hr = staged.SetAt(static_cast<LONG>(int64_t(stagedLower) + n), values[n]);
            if (FAILED(hr)) return hr;
        }
        hr = Resize(&bound);
        if (SUCCEEDED(hr)) {
            std::memcpy(static_cast<T*>(m_psa->pvData) + oldCount, staged.m_psa->pvData, size_t(count) * sizeof(T));
        }
        // On failure a no-copy caller still owns every input. On success all
        // staged resources now belong to our new slots. Neither path frees them.
        if (SUCCEEDED(hr) || (resources && !copy))
            std::memset(staged.m_psa->pvData, 0, size_t(count) * sizeof(T));
        return hr;
    }
    HRESULT Add(const SAFEARRAY* source) noexcept {
        if (!source || ::SafeArrayGetDim(const_cast<SAFEARRAY*>(source)) != 1) return E_INVALIDARG;
        CComSafeArray staged;
        HRESULT hr = staged.CopyFrom(source);
        if (FAILED(hr)) return hr;
        return Add(staged.GetCount(), static_cast<const T*>(staged.m_psa->pvData));
    }
private:
    mutable bool locked_ = false;
    static void Check(HRESULT hr) { if (FAILED(hr)) AtlThrow(hr); }
    static bool ValidBound(const SAFEARRAYBOUND& bound) noexcept {
        const int64_t upper = int64_t(bound.lLbound) + bound.cElements - 1;
        return upper >= (std::numeric_limits<LONG>::min)() && upper <= (std::numeric_limits<LONG>::max)();
    }
    static HRESULT Validate(const SAFEARRAY* source) noexcept {
        VARTYPE type = VT_EMPTY;
        const HRESULT hr = ::SafeArrayGetVartype(const_cast<SAFEARRAY*>(source), &type);
        if (FAILED(hr)) return hr;
        return type == _vartype && ::SafeArrayGetElemsize(const_cast<SAFEARRAY*>(source)) == sizeof(T)
            ? S_OK : DISP_E_TYPEMISMATCH;
    }
    HRESULT EnsureLocked() const noexcept {
        if (!m_psa) return E_INVALIDARG;
        if (locked_) return S_OK;
        const HRESULT hr = Validate(m_psa);
        if (FAILED(hr)) return hr;
        const HRESULT locked = ::SafeArrayLock(m_psa);
        if (SUCCEEDED(locked)) locked_ = true;
        return locked;
    }
    static void* Input(const T& value) noexcept {
        if constexpr (_vartype == VT_BSTR || _vartype == VT_UNKNOWN || _vartype == VT_DISPATCH)
            return const_cast<void*>(static_cast<const void*>(value));
        else return const_cast<T*>(&value);
    }
};
} // namespace ATL
#endif
