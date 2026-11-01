// Clean-room server connection points over the native Windows COM interfaces.
#ifndef OPENATL_ATL_CONNECTION_POINTS_H
#define OPENATL_ATL_CONNECTION_POINTS_H
#pragma once
#include <ocidl.h>
#include <array>
#include <vector>
#include <memory>
#include <cstdint>
#include <algorithm>
#include <utility>

namespace ATL {
namespace detail {
// Slot cookies do not truncate pointers on 64-bit Windows. The arrays borrow
// interface pointers; IConnectionPointImpl owns their QI references.
inline DWORD ConnectionCookie(IUnknown** first, size_t size, IUnknown** find) noexcept {
    if (!find || !*find) return 0;
    const auto start = reinterpret_cast<uintptr_t>(first);
    const auto address = reinterpret_cast<uintptr_t>(find);
    if (address >= start && address - start < size * sizeof(IUnknown*) &&
        (address - start) % sizeof(IUnknown*) == 0)
        return static_cast<DWORD>((address - start) / sizeof(IUnknown*) + 1);
    for (size_t n = 0; n < size; ++n) if (first[n] == *find) return static_cast<DWORD>(n + 1);
    return 0;
}
}

class CComDynamicUnkArray {
public:
    CComDynamicUnkArray() = default;
    CComDynamicUnkArray(const CComDynamicUnkArray&) = delete;
    CComDynamicUnkArray& operator=(const CComDynamicUnkArray&) = delete;
    DWORD Add(IUnknown* value) noexcept {
        if (!value) return 0;
        for (size_t n = 0; n < slots_.size(); ++n)
            if (!slots_[n]) { slots_[n] = value; return static_cast<DWORD>(n + 1); }
        if (slots_.size() == static_cast<size_t>((std::numeric_limits<int>::max)())) return 0;
        try { slots_.push_back(value); } catch (...) { return 0; }
        return static_cast<DWORD>(slots_.size());
    }
    BOOL Remove(DWORD cookie) noexcept {
        if (!GetUnknown(cookie)) return FALSE;
        slots_[cookie - 1] = nullptr; return TRUE;
    }
    IUnknown* GetAt(int index) const noexcept {
        return index >= 0 && static_cast<size_t>(index) < slots_.size() ? slots_[index] : nullptr;
    }
    int GetSize() const noexcept { return static_cast<int>(slots_.size()); }
    IUnknown* WINAPI GetUnknown(DWORD cookie) const noexcept {
        return cookie && cookie <= slots_.size() ? slots_[cookie - 1] : nullptr;
    }
    DWORD WINAPI GetCookie(IUnknown** find) noexcept { return detail::ConnectionCookie(begin(), slots_.size(), find); }
    IUnknown** begin() noexcept { return slots_.data(); }
    IUnknown** end() noexcept { return slots_.empty() ? begin() : begin() + slots_.size(); }
    void clear() noexcept { std::vector<IUnknown*>().swap(slots_); }
private:
    std::vector<IUnknown*> slots_;
};

template<unsigned int Size>
class CComUnkArray {
    static_assert(Size > 0 && Size <= static_cast<unsigned int>((std::numeric_limits<int>::max)()), "Invalid connection capacity");
public:
    CComUnkArray() noexcept = default;
    CComUnkArray(const CComUnkArray&) = delete;
    CComUnkArray& operator=(const CComUnkArray&) = delete;
    DWORD Add(IUnknown* value) noexcept {
        if (value) for (unsigned int n = 0; n < Size; ++n)
            if (!slots_[n]) { slots_[n] = value; return n + 1; }
        return 0;
    }
    BOOL Remove(DWORD cookie) noexcept {
        if (!GetUnknown(cookie)) return FALSE;
        slots_[cookie - 1] = nullptr; return TRUE;
    }
    IUnknown* WINAPI GetUnknown(DWORD cookie) const noexcept { return cookie && cookie <= Size ? slots_[cookie - 1] : nullptr; }
    DWORD WINAPI GetCookie(IUnknown** find) noexcept { return detail::ConnectionCookie(begin(), Size, find); }
    IUnknown* GetAt(int index) const noexcept { return index >= 0 && static_cast<unsigned int>(index) < Size ? slots_[index] : nullptr; }
    int GetSize() const noexcept { return static_cast<int>(Size); }
    IUnknown** begin() noexcept { return slots_.data(); }
    IUnknown** end() noexcept { return begin() + Size; }
private:
    std::array<IUnknown*, Size> slots_{};
};

namespace detail {
struct ConnectionPointEnumeration {
    using Value = CComPtr<IConnectionPoint>;
    using Output = IConnectionPoint*;
    static REFIID IID() noexcept { return IID_IEnumConnectionPoints; }
    static void Copy(Output& output, const Value& value) noexcept { output = value.p; output->AddRef(); }
};
struct ConnectionEnumeration {
    struct Value { CComPtr<IUnknown> sink; DWORD cookie; };
    using Output = CONNECTDATA;
    static REFIID IID() noexcept { return IID_IEnumConnections; }
    static void Copy(Output& output, const Value& value) noexcept {
        output.pUnk = value.sink.p; output.dwCookie = value.cookie; output.pUnk->AddRef();
    }
};
template<class Policy> struct ConnectionSnapshot {
    // Keep the originating image/owner alive, including explicit module pins.
    CComPtr<IUnknown> owner;
    std::vector<typename Policy::Value> values;
};
template<class Interface, class Policy>
class ConnectionEnumerator final : public Interface {
    using Snapshot = ConnectionSnapshot<Policy>;
public:
    explicit ConnectionEnumerator(std::shared_ptr<const Snapshot> items, size_t position = 0)
        : items_(std::move(items)), position_(position) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != Policy::IID()) return E_NOINTERFACE;
        *output = static_cast<Interface*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        if (!OpenATL::COM::detail::retain(refs_)) std::terminate();
        return refs_.load();
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = OpenATL::COM::detail::release(refs_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, typename Policy::Output* output, ULONG* fetched) noexcept override {
        if (fetched) *fetched = 0;
        if ((count && !output) || (count > 1 && !fetched)) return E_POINTER;
        size_t start = 0, available = 0;
        {
            CComCritSecLock<CComAutoCriticalSection> lock(mutex_);
            start = position_;
            available = (std::min)(size_t(count), items_->values.size() - position_);
            position_ += available;
        }
        // Do not hold the cursor lock across external AddRef calls.
        for (size_t n = 0; n < available; ++n) Policy::Copy(output[n], items_->values[start + n]);
        if (fetched) *fetched = static_cast<ULONG>(available);
        return available == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) noexcept override {
        CComCritSecLock<CComAutoCriticalSection> lock(mutex_);
        const auto skipped = (std::min)(size_t(count), items_->values.size() - position_);
        position_ += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() noexcept override {
        CComCritSecLock<CComAutoCriticalSection> lock(mutex_); position_ = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(Interface** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        try {
            size_t position;
            { CComCritSecLock<CComAutoCriticalSection> lock(mutex_); position = position_; }
            *output = new ConnectionEnumerator(items_, position);
            return S_OK;
        } catch (...) { return AtlObjectException(); }
    }
private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<const Snapshot> items_;
    size_t position_;
    CComAutoCriticalSection mutex_;
};
}

// Typed lookup avoids fabricated base offsets and member-pointer ABI tricks.
template<const IID* InterfaceId> class _ICPLocator {
public:
    virtual IConnectionPoint* GetConnPoint() noexcept = 0;
protected:
    ~_ICPLocator() = default;
};

template<class T, const IID* InterfaceId, class CDV = CComDynamicUnkArray>
class IConnectionPointImpl : public _ICPLocator<InterfaceId> {
    // An embedded face keeps each point's IUnknown distinct. An owner's COM
    // wrapper must not accidentally override every connection point's QI.
    class Face final : public IConnectionPoint {
    public:
        explicit Face(IConnectionPointImpl& point) noexcept : point_(point) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
            if (!output) return E_POINTER;
            *output = nullptr;
            if (iid != IID_IUnknown && iid != IID_IConnectionPoint) return E_NOINTERFACE;
            *output = static_cast<IConnectionPoint*>(this); AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() noexcept override { return point_.Owner()->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() noexcept override { return point_.Owner()->Release(); }
        HRESULT STDMETHODCALLTYPE GetConnectionInterface(IID* output) noexcept override { return point_.GetConnectionInterface(output); }
        HRESULT STDMETHODCALLTYPE GetConnectionPointContainer(IConnectionPointContainer** output) noexcept override { return point_.GetConnectionPointContainer(output); }
        HRESULT STDMETHODCALLTYPE Advise(IUnknown* sink, DWORD* cookie) noexcept override { return point_.Advise(sink, cookie); }
        HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) noexcept override { return point_.Unadvise(cookie); }
        HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections** output) noexcept override { return point_.EnumConnections(output); }
    private:
        IConnectionPointImpl& point_;
    } face_{*this};
    T* Owner() noexcept { return static_cast<T*>(this); }
public:
    CDV m_vec;
    IConnectionPointImpl() = default;
    IConnectionPointImpl(const IConnectionPointImpl&) = delete;
    IConnectionPointImpl& operator=(const IConnectionPointImpl&) = delete;
    ~IConnectionPointImpl() {
        for (auto slot = m_vec.begin(); slot != m_vec.end(); ++slot)
            if (IUnknown* sink = std::exchange(*slot, nullptr)) sink->Release();
    }
    IConnectionPoint* GetConnPoint() noexcept override { return &face_; }
    HRESULT STDMETHODCALLTYPE GetConnectionInterface(IID* output) noexcept {
        if (!output) return E_POINTER;
        *output = *InterfaceId; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionPointContainer(IConnectionPointContainer** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        return Owner()->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(output));
    }
    HRESULT STDMETHODCALLTYPE Advise(IUnknown* sink, DWORD* cookie) noexcept {
        if (!cookie) return E_POINTER;
        *cookie = 0;
        if (!sink) return E_POINTER;
        CComPtr<IUnknown> protect(GetConnPoint()), connected;
        const HRESULT hr = sink->QueryInterface(*InterfaceId, reinterpret_cast<void**>(&connected.p));
        if (FAILED(hr)) return CONNECT_E_CANNOTCONNECT;
        if (hr != S_OK || !connected.p) return E_UNEXPECTED;
        try {
            detail::TearOffOwnerLock<T> lock(*Owner());
            const DWORD created = m_vec.Add(connected.p);
            if (!created) return CONNECT_E_ADVISELIMIT;
            connected.Detach(); *cookie = created;
            return S_OK;
        } catch (...) { return detail::AtlObjectException(); }
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) noexcept {
        CComPtr<IUnknown> protect(GetConnPoint()), removed;
        {
            detail::TearOffOwnerLock<T> lock(*Owner());
            IUnknown* sink = m_vec.GetUnknown(cookie);
            if (!sink || !m_vec.Remove(cookie)) return CONNECT_E_NOCONNECTION;
            removed.Attach(sink);
        }
        return S_OK; // Release the removed sink after dropping the owner lock.
    }
    HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        try {
            using Policy = detail::ConnectionEnumeration;
            auto snapshot = std::make_shared<detail::ConnectionSnapshot<Policy>>();
            snapshot->owner = GetConnPoint();
            {
                detail::TearOffOwnerLock<T> lock(*Owner());
                const int size = m_vec.GetSize();
                snapshot->values.reserve(size);
                for (int n = 0; n < size; ++n) {
                    auto slot = m_vec.begin() + n;
                    if (!*slot) continue;
                    const DWORD cookie = m_vec.GetCookie(slot);
                    CComPtr<IUnknown> retained(*slot);
                    snapshot->values.push_back({std::move(retained), cookie});
                }
            }
            *output = new detail::ConnectionEnumerator<IEnumConnections, Policy>(std::move(snapshot));
            return S_OK;
        } catch (...) { return detail::AtlObjectException(); }
    }
};

template<class T>
class IConnectionPointContainerImpl : public IConnectionPointContainer {
public:
    HRESULT STDMETHODCALLTYPE FindConnectionPoint(REFIID iid, IConnectionPoint** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        static_cast<T*>(this)->_OpenATLVisitConnectionPoints([&](IConnectionPoint* point) {
            IID actual{};
            point->GetConnectionInterface(&actual);
            if (!*output && IsEqualIID(actual, iid)) { point->AddRef(); *output = point; }
        });
        return *output ? S_OK : CONNECT_E_NOCONNECTION;
    }
    HRESULT STDMETHODCALLTYPE EnumConnectionPoints(IEnumConnectionPoints** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        try {
            using Policy = detail::ConnectionPointEnumeration;
            auto snapshot = std::make_shared<detail::ConnectionSnapshot<Policy>>();
            static_cast<T*>(this)->_OpenATLVisitConnectionPoints([&](IConnectionPoint* point) {
                snapshot->values.emplace_back(point);
            });
            // Also retain empty-map owners and their module/image lifetime.
            snapshot->owner = static_cast<IConnectionPointContainer*>(this);
            *output = new detail::ConnectionEnumerator<IEnumConnectionPoints, Policy>(std::move(snapshot));
            return S_OK;
        } catch (...) { return detail::AtlObjectException(); }
    }
};
}

#define BEGIN_CONNECTION_POINT_MAP(x) \
public: \
    template<class Visitor> void _OpenATLVisitConnectionPoints(Visitor&& visitor) { \
        using _CPMapClass = x; \
        (void)visitor;
#define CONNECTION_POINT_ENTRY_P(piid) \
        visitor(static_cast<ATL::_ICPLocator<piid>*>(static_cast<_CPMapClass*>(this))->GetConnPoint());
#define CONNECTION_POINT_ENTRY(iid) CONNECTION_POINT_ENTRY_P(&(iid))
#define END_CONNECTION_POINT_MAP() }
#endif
