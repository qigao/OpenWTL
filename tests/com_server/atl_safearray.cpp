#include <atlsafe.h>
#include <atomic>
#include <cstdio>
#include <cwchar>

#define ARRAY_CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "ATL_SAFEARRAY_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
class ArrayUnknown : public IUnknown {
public:
    std::atomic<ULONG> refs{1};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown)) return E_NOINTERFACE;
        *out = static_cast<IUnknown*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};
template<class T, VARTYPE Type = ATL::_ATL_AutomationType<T>::type>
bool NativeType() {
    ATL::CComSafeArray<T, Type> array(1);
    VARTYPE actual = VT_EMPTY;
    ARRAY_CHECK(::SafeArrayGetVartype(array.m_psa, &actual) == S_OK && actual == Type);
    ARRAY_CHECK(::SafeArrayGetElemsize(array.m_psa) == sizeof(T));
    return true;
}
bool NumericBoundsAndLocks() {
    ATL::CComSafeArrayBound bounds(3, -4);
    ARRAY_CHECK(bounds.GetCount() == 3 && bounds.GetUpperBound() == -2);
    ARRAY_CHECK(bounds.SetLowerBound(7) == 7 && bounds.SetCount(2) == 2 && bounds.GetUpperBound() == 8);
    bounds = 3;
    ARRAY_CHECK(bounds.GetLowerBound() == 0 && bounds.GetCount() == 3);
    ATL::CComSafeArray<LONG> a(3, -4);
    ARRAY_CHECK(a.GetDimensions() == 1 && a.GetCount() == 3 && a.GetLowerBound() == -4 && a.GetUpperBound() == -2);
    ARRAY_CHECK(a.m_psa->cLocks == 1 && a.IsSizable());
    ARRAY_CHECK(a.SetAt(-4, 11) == S_OK && a.SetAt(-2, 33) == S_OK && a[-4] == 11 && a[-2] == 33);
    LONG index = -4, native = 0;
    ARRAY_CHECK(::SafeArrayGetElement(a.m_psa, &index, &native) == S_OK && native == 11);
    ARRAY_CHECK(a.SetAt(-5, 1) == DISP_E_BADINDEX);
    try { (void)a.GetAt(-1); ARRAY_CHECK(false); } catch (const ATL::CAtlException& e) { ARRAY_CHECK(e.m_hr == DISP_E_BADINDEX); }
    SAFEARRAY* original = a.m_psa;
    ARRAY_CHECK(::SafeArrayLock(original) == S_OK && original->cLocks == 2);
    ARRAY_CHECK(a.Resize(4, -4) == DISP_E_ARRAYISLOCKED && a.m_psa == original && a.GetCount() == 3);
    ARRAY_CHECK(a.Destroy() == DISP_E_ARRAYISLOCKED && a.m_psa == original && original->cLocks == 2);
    SAFEARRAY* incoming = ::SafeArrayCreateVector(VT_I4, 0, 1);
    ARRAY_CHECK(incoming && a.Attach(incoming) == DISP_E_ARRAYISLOCKED && incoming->cLocks == 0);
    ARRAY_CHECK(::SafeArrayDestroy(incoming) == S_OK);
    ATL::CComSafeArray<LONG> other(1);
    ARRAY_CHECK(other.SetAt(0, 99) == S_OK && a.CopyFrom(other.m_psa) == DISP_E_ARRAYISLOCKED && a[-4] == 11);
    ARRAY_CHECK(::SafeArrayUnlock(original) == S_OK && a.Resize(5, -4) == S_OK && a.m_psa == original);
    ARRAY_CHECK(a[-4] == 11 && a[-2] == 33 && a[0] == 0);
    ARRAY_CHECK(a.CopyTo(&a.m_psa) == E_INVALIDARG && a.m_psa == original);
    ARRAY_CHECK(a.CopyFrom(nullptr) == E_POINTER && a.CopyTo(nullptr) == E_POINTER);
    ARRAY_CHECK(a.Destroy() == S_OK && !a.m_psa);
    incoming = ::SafeArrayCreateVector(VT_I4, 0, 2);
    ARRAY_CHECK(incoming && a.Attach(incoming) == S_OK);
    ARRAY_CHECK(a.IsSizable() == !(incoming->fFeatures & (FADF_AUTO | FADF_STATIC | FADF_EMBEDDED | FADF_FIXEDSIZE)));
    incoming = a.Detach();
    ARRAY_CHECK(!a.m_psa && incoming->cLocks == 0 && ::SafeArrayDestroy(incoming) == S_OK);
    ARRAY_CHECK(a.Create(2) == S_OK && a.SetAt(0, 17) == S_OK);
    a.m_psa->fFeatures |= FADF_FIXEDSIZE;
    original = a.m_psa;
    ARRAY_CHECK(!a.IsSizable() && a.Resize(3) == E_INVALIDARG && a.Add(LONG(19)) == E_INVALIDARG);
    ARRAY_CHECK(a.m_psa == original && a.GetCount() == 2 && a[0] == 17 && original->cLocks == 1);
    ARRAY_CHECK(a.Destroy() == S_OK);
    ATL::CComSafeArray<LONG> out;
    SAFEARRAYBOUND bound{2, 1};
    *out.GetSafeArrayPtr() = ::SafeArrayCreate(VT_I4, 1, &bound);
    ARRAY_CHECK(out.m_psa && out.SetAt(1, 88) == S_OK && out[1] == 88 && out.m_psa->cLocks == 1);
    ATL::CComSafeArray<LONG> invalid;
    ARRAY_CHECK(invalid.Create(2, (std::numeric_limits<LONG>::max)()) == E_INVALIDARG && !invalid.m_psa);
    ATL::CComSafeArray<VARIANT_BOOL, VT_BOOL> boolean(1);
    ARRAY_CHECK(boolean.SetAt(0, VARIANT_TRUE) == S_OK && boolean[0] == VARIANT_TRUE);
    ATL::CComSafeArray<LONG, VT_BOOL> wrongSize;
    ARRAY_CHECK(wrongSize.Create(1) == DISP_E_TYPEMISMATCH && !wrongSize.m_psa);
    ARRAY_CHECK(NativeType<char>() && NativeType<signed char>() && NativeType<unsigned char>());
    ARRAY_CHECK(NativeType<short>() && NativeType<unsigned short>() && NativeType<int>() && NativeType<unsigned int>());
    ARRAY_CHECK(NativeType<long>() && NativeType<unsigned long>() && NativeType<long long>() && NativeType<unsigned long long>());
    ARRAY_CHECK(NativeType<float>() && NativeType<double>() && NativeType<CY>() && NativeType<DECIMAL>());
    ARRAY_CHECK(NativeType<BSTR>() && NativeType<VARIANT>() && NativeType<IUnknown*>() && NativeType<IDispatch*>());
    return true;
}
bool OwnedElements() {
    ATL::CComBSTR text;
    text.Attach(::SysAllocStringLen(L"A\0B", 3));
    ARRAY_CHECK(text.m_str);
    ATL::CComSafeArray<BSTR> strings(1, -2);
    ARRAY_CHECK(strings.SetAt(-2, text.m_str) == S_OK && strings[-2] != text.m_str && ::SysStringLen(strings[-2]) == 3);
    text.m_str[0] = L'X';
    ARRAY_CHECK(strings[-2][0] == L'A' && strings[-2][2] == L'B');
    BSTR adopted = ::SysAllocString(L"adopted");
    ARRAY_CHECK(adopted && strings.Add(adopted, FALSE) == S_OK && strings[-1] == adopted);
    adopted = nullptr; // Successful no-copy append transferred ownership.
    ARRAY_CHECK(strings.Add(2, &strings.GetAt(-2)) == S_OK && strings.GetCount() == 4);
    ARRAY_CHECK(strings[0] != strings[-2] && ::SysStringLen(strings[0]) == 3);
    ARRAY_CHECK(strings.Add(strings.m_psa) == S_OK && strings.GetCount() == 8);
    ARRAY_CHECK(strings.Add(1, &strings.GetAt(-2), FALSE) == E_INVALIDARG);
    ATL::CComSafeArray<VARIANT> empty;
    ARRAY_CHECK(empty.Create() == S_OK && empty.GetCount() == 0 && empty.GetDimensions() == 1);
    ATL::CComVariant emptySnapshot(empty.m_psa);
    ARRAY_CHECK(emptySnapshot.vt == (VT_ARRAY | VT_VARIANT) && emptySnapshot.parray);
    ATL::CComSafeArray<VARIANT> variants;
    ATL::CComVariant source(L"copied");
    ARRAY_CHECK(variants.Add(source) == S_OK && variants[0].vt == VT_BSTR && variants[0].bstrVal != source.bstrVal);
    ARRAY_CHECK(source.Clear() == S_OK && std::wcscmp(variants[0].bstrVal, L"copied") == 0);
    VARIANT transferred{}; transferred.vt = VT_BSTR; transferred.bstrVal = ::SysAllocString(L"transferred");
    ARRAY_CHECK(transferred.bstrVal && variants.SetAt(0, transferred, FALSE) == S_OK);
    ::VariantInit(&transferred);
    ATL::CComSafeArray<VARIANT> copy(variants);
    ARRAY_CHECK(copy[0].bstrVal != variants[0].bstrVal && std::wcscmp(copy[0].bstrVal, L"transferred") == 0);
    ATL::CComVariant snapshot(variants.m_psa);
    ARRAY_CHECK(snapshot.vt == (VT_ARRAY | VT_VARIANT) && snapshot.parray != variants.m_psa && snapshot.parray->cLocks == 0);
    ARRAY_CHECK(variants.Destroy() == S_OK);
    LONG zero = 0;
    ATL::CComVariant result;
    ARRAY_CHECK(::SafeArrayGetElement(snapshot.parray, &zero, &result) == S_OK && result.vt == VT_BSTR &&
        std::wcscmp(result.bstrVal, L"transferred") == 0);
    ArrayUnknown object;
    {
        ATL::CComSafeArray<IUnknown*> interfaces(1);
        IUnknown* face = &object;
        ARRAY_CHECK(interfaces.SetAt(0, face) == S_OK && object.refs == 2);
        ATL::CComSafeArray<IUnknown*> second(interfaces);
        ARRAY_CHECK(object.refs == 3 && second[0] == &object);
        ARRAY_CHECK(interfaces.Destroy() == S_OK && object.refs == 2);
    }
    ARRAY_CHECK(object.refs == 1);
    // No-copy append must retain the caller's resource when Redim is denied.
    ARRAY_CHECK(::SafeArrayLock(copy.m_psa) == S_OK);
    transferred.vt = VT_BSTR; transferred.bstrVal = ::SysAllocString(L"still caller owned");
    ARRAY_CHECK(copy.Add(transferred, FALSE) == DISP_E_ARRAYISLOCKED && copy.GetCount() == 1);
    ARRAY_CHECK(transferred.bstrVal && std::wcscmp(transferred.bstrVal, L"still caller owned") == 0);
    ARRAY_CHECK(::VariantClear(&transferred) == S_OK && ::SafeArrayUnlock(copy.m_psa) == S_OK);
    return true;
}
bool MultiDimensions() {
    SAFEARRAYBOUND bounds[] = {{2, -5}, {3, -5}};
    ATL::CComSafeArray<LONG> matrix(bounds, 2);
    SAFEARRAY* native = ::SafeArrayCreate(VT_I4, 2, bounds);
    ARRAY_CHECK(native);
    struct Cleanup { SAFEARRAY* array; ~Cleanup() { ::SafeArrayDestroy(array); } } cleanup{native};
    LONG indices[] = {-5, -5};
    LONG value = 42;
    ARRAY_CHECK(matrix.MultiDimSetAt(indices, value) == S_OK && ::SafeArrayPutElement(native, indices, &value) == S_OK);
    value = 0;
    ARRAY_CHECK(matrix.MultiDimGetAt(indices, value) == S_OK && value == 42);
    for (UINT dim = 0; dim < 2; ++dim) {
        LONG lower = 0, upper = 0;
        ARRAY_CHECK(::SafeArrayGetLBound(native, dim + 1, &lower) == S_OK && ::SafeArrayGetUBound(native, dim + 1, &upper) == S_OK);
        ARRAY_CHECK(matrix.GetLowerBound(dim) == lower && matrix.GetUpperBound(dim) == upper && matrix.GetCount(dim) == ULONG(upper - lower + 1));
    }
    ARRAY_CHECK(matrix.SetAt(-5, 1) == E_INVALIDARG && matrix.Add(value) == E_INVALIDARG);
    SAFEARRAYBOUND larger{4, -5};
    ARRAY_CHECK(matrix.Resize(&larger) == S_OK && ::SafeArrayRedim(native, &larger) == S_OK);
    ARRAY_CHECK(matrix.MultiDimGetAt(indices, value) == S_OK && value == 42);
    LONG upper = 0;
    ARRAY_CHECK(::SafeArrayGetUBound(native, 2, &upper) == S_OK && matrix.GetUpperBound(1) == upper);
    indices[0] = 100;
    ARRAY_CHECK(matrix.MultiDimGetAt(indices, value) == DISP_E_BADINDEX);
    ARRAY_CHECK(matrix.MultiDimSetAt(nullptr, value) == E_POINTER);
    return true;
}
}
bool RunAtlSafeArrayTests() {
    ULONG_PTR token = 0;
    ARRAY_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    ARRAY_CHECK(NumericBoundsAndLocks() && OwnedElements() && MultiDimensions());
    ARRAY_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
