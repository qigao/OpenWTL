#include <atlbase.h>
#include <atlcom.h>
#include <statreg.h>
#include <openatl/com_server.h>
#include <openatl/com_registration.h>
#include <openatl/rgs_resource.h>
#include "interfaces.h"
#include "fixture_registry_ids.h"
#include "openatl_rgs_fixtures.generated.h"
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
#include "COMMap.h"
extern "C" HRESULT WINAPI OpenATLCreateEmbeddedDispatch(IDispatch** output);
extern "C" HRESULT WINAPI OpenATLCreateRegisteredDispatch(IDispatch** output);
#endif
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
#include <sddl.h>

using namespace OpenATL::COM;
#define REQUIRE(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "COM_SERVER_FAIL line=%d condition=%s winerr=%lu\n", \
                 __LINE__, #condition, static_cast<unsigned long>(::GetLastError())); \
    return false; } } while (0)

bool RunAtlCompatibilityTests();
bool RunDllConnectionPointTests(IOpenATLValue* object, IEnumConnections** retained);
bool RunAtlAutoAggregateTests(bool multiThreaded);
bool RunAtlErrorInfoTests();
bool RunAtlDispatchTests();
bool RunAtlModuleTests();
int RunAtlModuleServer(const char* token, bool apartment);
bool RunRgsParserTests();
bool RunRgsRegistryTests();
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
bool RunOriginalCommapTypeLibTest(const char* path);
#endif
namespace {
std::atomic<int> live{0};
bool throwUnexpected = false;
class Value final : public Object<Value, Interface<IOpenATLDerived, &IID_OpenATLDerived>,
                                       InterfaceAlias<IOpenATLValue, &IID_OpenATLValue, IOpenATLDerived>,
                                       Interface<IOpenATLSecond, &IID_OpenATLSecond>> {
public:
    explicit Value(Module& module) noexcept : Object(module) { ++live; }
    ~Value() { --live; }
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 42; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSecond(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 17; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 99; return S_OK;
    }
};
class Throws final : public Object<Throws, Interface<IOpenATLValue, &IID_OpenATLValue>> {
public:
    explicit Throws(Module& module) : Object(module) {
        if (throwUnexpected) throw std::runtime_error("constructor failure");
        throw std::bad_alloc();
    }
    HRESULT STDMETHODCALLTYPE GetValue(LONG*) noexcept override { return E_UNEXPECTED; }
};

bool identities(IOpenATLValue* object) {
    ATL::CComPtr<IOpenATLSecond> second;
    REQUIRE(object->QueryInterface(IID_OpenATLSecond, reinterpret_cast<void**>(&second)) == S_OK);
    LONG value = 0;
    REQUIRE(second->GetSecond(&value) == S_OK && value == 17);
    ATL::CComPtr<IUnknown> a, b;
    REQUIRE(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a)) == S_OK);
    REQUIRE(second->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b)) == S_OK);
    REQUIRE(a.p == b.p);
    ATL::CComPtr<IOpenATLDerived> derived;
    REQUIRE(second->QueryInterface(IID_OpenATLDerived, reinterpret_cast<void**>(&derived)) == S_OK);
    REQUIRE(derived->GetExtra(&value) == S_OK && value == 99);
    ATL::CComPtr<IOpenATLValue> base;
    REQUIRE(derived->QueryInterface(IID_OpenATLValue, reinterpret_cast<void**>(&base)) == S_OK);
    REQUIRE(base.p == object);
    void* output = reinterpret_cast<void*>(1);
    REQUIRE(object->QueryInterface(IID_OpenATLUnknown, &output) == E_NOINTERFACE && output == nullptr);
    REQUIRE(object->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    return true;
}

bool core() {
    REQUIRE(RunAtlCompatibilityTests());
    const ClassEntry entries[] = {{CLSID_OpenATLValue, &Value::CreateInstance},
                                  {CLSID_OpenATLThrow, &Throws::CreateInstance}};
    Module module(entries, 2);
    REQUIRE(module.Status() == S_OK && module.CanUnloadNow() == S_OK);
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_IUnknown, nullptr) == E_POINTER);
    void* output = reinterpret_cast<void*>(1);
    REQUIRE(module.GetClassObject(CLSID_NULL, IID_IUnknown, &output) == CLASS_E_CLASSNOTAVAILABLE && !output);
    output = reinterpret_cast<void*>(1);
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_OpenATLUnknown, &output) == E_NOINTERFACE && !output);
    REQUIRE(module.CanUnloadNow() == S_OK);
    ATL::CComPtr<IClassFactory> factory;
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(module.CanUnloadNow() == S_FALSE);
    output = reinterpret_cast<void*>(1);
    REQUIRE(factory->CreateInstance(factory.p, IID_OpenATLValue, &output) == CLASS_E_NOAGGREGATION && !output);
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLValue, nullptr) == E_POINTER);
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLUnknown, &output) == E_NOINTERFACE && !output);
    REQUIRE(live == 0);
    ATL::CComPtr<IOpenATLValue> object;
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLValue,
                                    reinterpret_cast<void**>(&object)) == S_OK);
    REQUIRE(live == 1 && identities(object));
    factory.Release();
    REQUIRE(module.CanUnloadNow() == S_FALSE);
    object.Release();
    REQUIRE(live == 0 && module.CanUnloadNow() == S_OK);

    REQUIRE(module.GetClassObject(CLSID_OpenATLThrow, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory)) == S_OK);
    output = reinterpret_cast<void*>(1);
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLValue, &output) == E_OUTOFMEMORY && !output);
    throwUnexpected = true;
    output = reinterpret_cast<void*>(1);
    const HRESULT unexpected = factory->CreateInstance(nullptr, IID_OpenATLValue, &output);
    throwUnexpected = false;
    REQUIRE(unexpected == E_UNEXPECTED && !output);
    REQUIRE(factory->LockServer(FALSE) == E_UNEXPECTED);
    REQUIRE(factory->LockServer(TRUE) == S_OK);
    factory.Release();
    REQUIRE(module.CanUnloadNow() == S_FALSE);
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(factory->LockServer(FALSE) == S_OK);
    factory.Release();
    REQUIRE(module.CanUnloadNow() == S_OK);

    std::atomic<bool> okay{true};
    ATL::CComPtr<IOpenATLValue> shared;
    REQUIRE(Value::CreateInstance(module, IID_OpenATLValue,
                                  reinterpret_cast<void**>(&shared)) == S_OK);
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 500; ++j) {
            ATL::CComPtr<IClassFactory> f;
            ATL::CComPtr<IOpenATLValue> v;
            if (module.GetClassObject(CLSID_OpenATLValue, IID_IClassFactory,
                                      reinterpret_cast<void**>(&f)) != S_OK ||
                f->CreateInstance(nullptr, IID_OpenATLValue,
                                   reinterpret_cast<void**>(&v)) != S_OK ||
                f->LockServer(TRUE) != S_OK || f->LockServer(FALSE) != S_OK) {
                okay = false; return;
            }
            v.p->AddRef(); v.p->Release();
            shared.p->AddRef(); shared.p->Release();
        }
    });
    for (auto& worker : workers) worker.join();
    shared.Release();
    REQUIRE(okay && live == 0 && module.CanUnloadNow() == S_OK);
    const ClassEntry duplicate[] = {entries[0], entries[0]};
    Module invalid(duplicate, 2);
    REQUIRE(invalid.Status() == E_INVALIDARG && invalid.CanUnloadNow() == S_FALSE);
    output = reinterpret_cast<void*>(1);
    REQUIRE(invalid.GetClassObject(CLSID_OpenATLValue, IID_IUnknown, &output) == E_INVALIDARG && !output);
    return true;
}

struct RegistryRoot {
    HKEY key = nullptr;
    std::wstring path;
    bool owned = false;
    bool Open() {
        GUID guid{};
        if (FAILED(::CoCreateGuid(&guid))) return false;
        wchar_t id[40]{};
        if (!::StringFromGUID2(guid, id, 40)) return false;
        path = std::wstring(L"Software\\OpenATL.COMServerTest.") + id;
        DWORD disposition = 0;
        LSTATUS status = ::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                                           REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                                           &key, &disposition);
        owned = status == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY;
        return owned;
    }
    bool Close() {
        if (key) { ::RegCloseKey(key); key = nullptr; }
        if (!owned) return true;
        // This unique, newly created test root owns all of its contents,
        // including the deliberately foreign-to-registrar regression entries.
        LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        if (status == ERROR_SUCCESS) owned = false;
        return status == ERROR_SUCCESS;
    }
    ~RegistryRoot() { Close(); }
};

bool exists(HKEY root, LPCWSTR path) {
    ATL::CRegKey key;
    return ::RegOpenKeyExW(root, path, 0, KEY_READ, &key.m_hKey) == ERROR_SUCCESS;
}
bool registry() {
    RegistryRoot root;
    REQUIRE(root.Open());
    ClassRegistration entry{CLSID_OpenATLValue, L"C:\\OpenATL Conformance\\server.dll",
                             L"OpenATL.Private.Conformance", L"Native fixture", ThreadingModel::Both};
    wchar_t guid[40]{};
    REQUIRE(::StringFromGUID2(entry.clsid, guid, 40));
    const std::wstring clsid = std::wstring(L"CLSID\\") + guid;
    ATL::CRegKey conflict;
    REQUIRE(::RegCreateKeyExW(root.key, entry.progId, 0, nullptr, 0, KEY_ALL_ACCESS,
                              nullptr, &conflict.m_hKey, nullptr) == ERROR_SUCCESS);
    REQUIRE(conflict.SetStringValue(nullptr, L"Existing application") == ERROR_SUCCESS);
    REQUIRE(RegisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    REQUIRE(!exists(root.key, clsid.c_str()) && exists(root.key, entry.progId));
    wchar_t foreign[64]{};
    ULONG capacity = 64;
    REQUIRE(conflict.QueryStringValue(nullptr, foreign, &capacity) == ERROR_SUCCESS &&
             std::wcscmp(foreign, L"Existing application") == 0);
    REQUIRE(::RegDeleteKeyW(root.key, entry.progId) == ERROR_SUCCESS);
    REQUIRE(conflict.Close() == ERROR_SUCCESS);

    REQUIRE(RegisterClass(root.key, entry) == S_OK);
    REQUIRE(exists(root.key, (clsid + L"\\InprocServer32").c_str()));
    REQUIRE(exists(root.key, L"OpenATL.Private.Conformance\\CLSID"));
    REQUIRE(RegisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    ClassRegistration other = entry;
    other.modulePath = L"C:\\Other\\server.dll";
    REQUIRE(UnregisterClass(root.key, other) == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(exists(root.key, clsid.c_str()));

    ATL::CRegKey key;
    REQUIRE(::RegOpenKeyExW(root.key, clsid.c_str(), 0, KEY_ALL_ACCESS, &key.m_hKey) == ERROR_SUCCESS);
    DWORD marker = 17;
    REQUIRE(::RegSetValueExW(key.m_hKey, L"Foreign", 0, REG_DWORD,
                             reinterpret_cast<BYTE*>(&marker), sizeof(marker)) == ERROR_SUCCESS);
    REQUIRE(UnregisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(exists(root.key, (clsid + L"\\InprocServer32").c_str()));
    REQUIRE(::RegDeleteValueW(key.m_hKey, L"Foreign") == ERROR_SUCCESS);
    HKEY child = nullptr;
    REQUIRE(::RegCreateKeyExW(key.m_hKey, L"ForeignChild", 0, nullptr, 0, KEY_ALL_ACCESS,
                              nullptr, &child, nullptr) == ERROR_SUCCESS);
    ::RegCloseKey(child);
    REQUIRE(UnregisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(::RegDeleteKeyW(key.m_hKey, L"ForeignChild") == ERROR_SUCCESS);
    REQUIRE(key.Close() == ERROR_SUCCESS);
    REQUIRE(UnregisterClass(root.key, entry) == S_OK);
    REQUIRE(!exists(root.key, clsid.c_str()) && !exists(root.key, entry.progId));
    REQUIRE(UnregisterClass(root.key, entry) == S_OK);
    other = entry; other.modulePath = L"relative.dll";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other = entry; other.progId = L"CLSID";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other.progId = L"OpenATL_Invalid";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other.progId = L"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other = entry; other.progId = nullptr; other.threadingModel = ThreadingModel::Apartment;
    REQUIRE(RegisterClass(root.key, other) == S_OK && UnregisterClass(root.key, other) == S_OK);
    // Multi-class source registration: validate all members before writes,
    // rollback a previously created class if a later one conflicts, and
    // preflight every owner before deleting any class from the batch.
    const ClassRegistration batch[] = {
        {CLSID_OpenATLValue, L"C:\\OpenATL Conformance\\server.dll",
         L"OpenATL.Private.BatchOne", L"Batch One", ThreadingModel::Both},
        {CLSID_OpenATLThrow, L"C:\\OpenATL Conformance\\server.dll",
         L"OpenATL.Private.BatchTwo", L"Batch Two", ThreadingModel::Apartment},
    };
    wchar_t batchGuid1[40]{}, batchGuid2[40]{};
    REQUIRE(::StringFromGUID2(batch[0].clsid, batchGuid1, 40));
    REQUIRE(::StringFromGUID2(batch[1].clsid, batchGuid2, 40));
    const std::wstring batchKey1 = std::wstring(L"CLSID\\") + batchGuid1;
    const std::wstring batchKey2 = std::wstring(L"CLSID\\") + batchGuid2;
    REQUIRE(RegisterClasses(root.key, batch, 0) == S_FALSE);
    REQUIRE(UnregisterClasses(root.key, batch, 0) == S_FALSE);
    REQUIRE(RegisterClasses(nullptr, batch, 2) == E_INVALIDARG);
    REQUIRE(RegisterClasses(root.key, nullptr, 2) == E_INVALIDARG);
    REQUIRE(RegisterClasses(root.key, batch, 1025) ==
            HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
    ClassRegistration invalid[] = {batch[0], batch[1]};
    invalid[1].modulePath = L"relative.dll";
    REQUIRE(RegisterClasses(root.key, invalid, 2) == E_INVALIDARG);
    invalid[1] = batch[0];
    REQUIRE(RegisterClasses(root.key, invalid, 2) == E_INVALIDARG);
    invalid[1] = batch[1];
    invalid[1].progId = L"openatl.private.batchone";
    REQUIRE(RegisterClasses(root.key, invalid, 2) == E_INVALIDARG);
    REQUIRE(!exists(root.key, batchKey1.c_str()) &&
            !exists(root.key, batchKey2.c_str()));

    ATL::CRegKey laterConflict;
    REQUIRE(::RegCreateKeyExW(root.key, batch[1].progId, 0, nullptr, 0,
                              KEY_ALL_ACCESS, nullptr, &laterConflict.m_hKey,
                              nullptr) == ERROR_SUCCESS);
    REQUIRE(laterConflict.SetStringValue(nullptr, L"Foreign application") == ERROR_SUCCESS);
    REQUIRE(RegisterClasses(root.key, batch, 2) ==
            HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    REQUIRE(!exists(root.key, batchKey1.c_str()) &&
            !exists(root.key, batchKey2.c_str()));
    REQUIRE(exists(root.key, batch[1].progId));
    REQUIRE(laterConflict.Close() == ERROR_SUCCESS);
    REQUIRE(::RegDeleteKeyW(root.key, batch[1].progId) == ERROR_SUCCESS);

    REQUIRE(RegisterClasses(root.key, batch, 2) == S_OK);
    REQUIRE(exists(root.key, batchKey1.c_str()) && exists(root.key, batchKey2.c_str()));
    REQUIRE(RegisterClasses(root.key, batch, 2) ==
            HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));

    ATL::CRegKey foreignInLater;
    REQUIRE(::RegOpenKeyExW(root.key, batchKey2.c_str(), 0, KEY_ALL_ACCESS,
                            &foreignInLater.m_hKey) == ERROR_SUCCESS);
    DWORD sentinel = 99;
    REQUIRE(::RegSetValueExW(foreignInLater.m_hKey, L"Foreign", 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&sentinel),
                             sizeof(sentinel)) == ERROR_SUCCESS);
    REQUIRE(UnregisterClasses(root.key, batch, 2) ==
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(exists(root.key, batchKey1.c_str()) && exists(root.key, batchKey2.c_str()));
    REQUIRE(::RegDeleteValueW(foreignInLater.m_hKey, L"Foreign") == ERROR_SUCCESS);
    REQUIRE(foreignInLater.Close() == ERROR_SUCCESS);
    REQUIRE(UnregisterClasses(root.key, batch, 2) == S_OK);
    REQUIRE(!exists(root.key, batchKey1.c_str()) &&
            !exists(root.key, batchKey2.c_str()) &&
            !exists(root.key, batch[0].progId) &&
            !exists(root.key, batch[1].progId));
    REQUIRE(UnregisterClasses(root.key, batch, 2) == S_OK);
    REQUIRE(root.Close());
    return true;
}

class Dll final {
public:
    using GetClass = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    using Operation = HRESULT (WINAPI*)();
    HMODULE image = nullptr;
    GetClass get = nullptr;
    Operation unload = nullptr, install = nullptr, uninstall = nullptr;
    bool installed = false;
    bool Open() {
        wchar_t path[32768]{};
        DWORD length = ::GetModuleFileNameW(nullptr, path, 32768);
        if (!length || length >= 32768) return false;
        wchar_t* leaf = std::wcsrchr(path, L'\\');
        const wchar_t name[] = L"openatl_com_server_fixture.dll";
        if (!leaf || size_t(leaf - path) + 1 + sizeof(name) / sizeof(wchar_t) > 32768) return false;
        std::memcpy(leaf + 1, name, sizeof(name));
        image = ::LoadLibraryW(path);
        if (!image) return false;
        get = reinterpret_cast<GetClass>(::GetProcAddress(image, "DllGetClassObject"));
        unload = reinterpret_cast<Operation>(::GetProcAddress(image, "DllCanUnloadNow"));
        install = reinterpret_cast<Operation>(::GetProcAddress(image, "DllRegisterServer"));
        uninstall = reinterpret_cast<Operation>(::GetProcAddress(image, "DllUnregisterServer"));
        return get && unload && install && uninstall;
    }
    bool Close() {
        if (installed) {
            if (uninstall() != S_OK) return false;
            installed = false;
        }
        if (image) {
            if (!unload || unload() != S_OK || !::FreeLibrary(image)) return false;
            image = nullptr;
        }
        return true;
    }
    ~Dll() { Close(); }
};

// Valid controlling outer in the client image; the DLL owns only the separate
// nondelegating inner IUnknown. No AddRef of the outer is allowed just because
// the inner was allocated or queried for its own IUnknown.
class DllAggregateOuter final : public IUnknown {
public:
    std::atomic<ULONG> refs{1};
    ATL::CComPtr<IUnknown> inner;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            *output = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(iid, output) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs; }
};
bool aggregateDll(Dll& dll, bool nativeCom) {
    DllAggregateOuter outer;
    ATL::CComPtr<IClassFactory> factory;
    REQUIRE(dll.get(CLSID_OpenATLValue, IID_IClassFactory,
        reinterpret_cast<void**>(&factory.p)) == S_OK && factory.p);
    void* invalid = reinterpret_cast<void*>(1);
    REQUIRE(factory->CreateInstance(&outer, IID_OpenATLValue, &invalid) ==
        CLASS_E_NOAGGREGATION && !invalid && outer.refs == 1);
    REQUIRE(factory->CreateInstance(&outer, IID_IUnknown, nullptr) == E_POINTER);
    HRESULT hr = nativeCom
        ? ::CoCreateInstance(CLSID_OpenATLValue, &outer, CLSCTX_INPROC_SERVER,
                             IID_IUnknown, reinterpret_cast<void**>(&outer.inner.p))
        : factory->CreateInstance(&outer, IID_IUnknown,
                                   reinterpret_cast<void**>(&outer.inner.p));
    REQUIRE(hr == S_OK && outer.inner.p && outer.refs == 1);
    factory.Release();
    REQUIRE(dll.unload() == S_FALSE);
    ATL::CComPtr<IUnknown> privateIdentity;
    REQUIRE(outer.inner->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&privateIdentity.p)) == S_OK &&
        privateIdentity.p == outer.inner.p && privateIdentity.p != &outer && outer.refs == 1);
    privateIdentity.Release();
    ATL::CComPtr<IOpenATLValue> value;
    REQUIRE(outer.QueryInterface(IID_OpenATLValue, reinterpret_cast<void**>(&value.p)) == S_OK && outer.refs == 2);
    LONG result = 0;
    REQUIRE(value->GetValue(&result) == S_OK && result == 42);
    REQUIRE(RunDllConnectionPointTests(value.p, nullptr));
    ATL::CComPtr<IOpenATLSecond> second;
    REQUIRE(value->QueryInterface(IID_OpenATLSecond, reinterpret_cast<void**>(&second.p)) == S_OK && outer.refs == 3);
    REQUIRE(second->GetSecond(&result) == S_OK && result == 17);
    ATL::CComPtr<IUnknown> canonical;
    REQUIRE(second->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK &&
        canonical.p == &outer && outer.refs == 4);
    canonical.Release(); second.Release(); value.Release();
    REQUIRE(outer.refs == 1);
    outer.inner.Release();
    // Exercise explicit DECLARE_NOT_AGGREGATABLE through the same auto-map
    // engine, not just its direct local helper.
    REQUIRE(dll.get(CLSID_OpenATLAuxValue, IID_IClassFactory,
        reinterpret_cast<void**>(&factory.p)) == S_OK);
    REQUIRE(factory->CreateInstance(&outer, IID_IUnknown, &invalid) == CLASS_E_NOAGGREGATION && !invalid);
    factory.Release();
    if (nativeCom) ::CoFreeUnusedLibrariesEx(0, 0);
    REQUIRE(dll.unload() == S_OK && outer.refs == 1);
    return true;
}

bool dllErrorInfoFields(IErrorInfo* error) {
    GUID iid{};
    DWORD help = 0;
    ATL::CComBSTR text, file, source;
    REQUIRE(error && error->GetGUID(&iid) == S_OK && IsEqualGUID(iid, IID_OpenATLValue));
    REQUIRE(error->GetHelpContext(&help) == S_OK && help == 711);
    REQUIRE(error->GetDescription(&text.m_str) == S_OK && text.m_str &&
        std::wcscmp(text.m_str, L"OpenWTL resource error \u4e2d\u6587") == 0);
    REQUIRE(error->GetHelpFile(&file.m_str) == S_OK && file.m_str &&
        std::wcscmp(file.m_str, L"fixture-help.chm") == 0);
    REQUIRE(error->GetSource(&source.m_str) == S_OK && source.m_str &&
        std::wcscmp(source.m_str, OpenATLTestProgId) == 0);
    return true;
}
bool captureDllError(IOpenATLValue* value, IErrorInfo** retained) {
    ATL::CComPtr<ISupportErrorInfo> support;
    REQUIRE(value->QueryInterface(IID_ISupportErrorInfo, reinterpret_cast<void**>(&support.p)) == S_OK);
    REQUIRE(support->InterfaceSupportsErrorInfo(IID_OpenATLValue) == S_OK &&
        support->InterfaceSupportsErrorInfo(IID_IStream) == S_FALSE);
    support.Release();
    REQUIRE(value->GetValue(nullptr) == E_POINTER);
    REQUIRE(::GetErrorInfo(0, retained) == S_OK && *retained);
    REQUIRE(dllErrorInfoFields(*retained));
    return true;
}

class DllAutoAggregateOwner : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IPersist {
public:
    ATL::CComPtr<IUnknown> inner;
    void FinalRelease() noexcept { inner.Release(); }
    BEGIN_COM_MAP(DllAutoAggregateOwner)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE(IID_OpenATLValue, inner.p, CLSID_OpenATLValue)
        COM_INTERFACE_ENTRY_AUTOAGGREGATE_BLIND(inner.p, CLSID_OpenATLValue)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = CLSID_NULL;
        return S_OK;
    }
};
bool autoAggregateDll(Dll& dll) {
    ATL::CComObject<DllAutoAggregateOwner>* owner = nullptr;
    REQUIRE(ATL::CComObject<DllAutoAggregateOwner>::CreateInstance(&owner) == S_OK && owner->AddRef() == 1);
    ATL::CComPtr<IUnknown> identity;
    REQUIRE(owner->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && !owner->inner.p);
    identity.Release();
    ATL::CComPtr<IOpenATLValue> value;
    REQUIRE(owner->QueryInterface(IID_OpenATLValue, reinterpret_cast<void**>(&value.p)) == S_OK && owner->inner.p);
    IUnknown* cached = owner->inner.p;
    ATL::CComPtr<IOpenATLSecond> second;
    REQUIRE(value->QueryInterface(IID_OpenATLSecond, reinterpret_cast<void**>(&second.p)) == S_OK && owner->inner.p == cached);
    REQUIRE(second->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == owner->GetUnknown());
    identity.Release();
    void* missing = reinterpret_cast<void*>(1);
    REQUIRE(owner->QueryInterface(IID_IStream, &missing) == E_NOINTERFACE && !missing && owner->inner.p == cached);
    REQUIRE(owner->AddRef() == 4 && owner->Release() == 3);
    LONG result = 0;
    REQUIRE(value->GetValue(&result) == S_OK && result == 42);
    REQUIRE(owner->Release() == 2);
    ::CoFreeUnusedLibrariesEx(0, 0);
    REQUIRE(dll.unload() == S_FALSE);
    REQUIRE(second->GetSecond(&result) == S_OK && result == 17);
    second.Release(); value.Release();
    ::CoFreeUnusedLibrariesEx(0, 0);
    REQUIRE(dll.unload() == S_OK);
    return true;
}

#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
// Original COMMap's library references IOuter/IChain, but not ITearOff1/2.
// Preserve that omission: direct tear-off calls work and absent type info fails.
template<class Face>
bool tearOffProperty(Face* object, const wchar_t* text) {
    ATL::CComBSTR value;
    REQUIRE(object->get_Name(&value.m_str) == S_OK && value.m_str &&
        std::wcscmp(value.m_str, text) == 0);
    ATL::CComPtr<ITypeInfo> absent;
    REQUIRE(object->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &absent.p) == TYPE_E_ELEMENTNOTFOUND && !absent.p);
    return true;
}
bool tearOffInvoke(IOuter* object, const wchar_t* text) {
    ATL::CComPtr<ITypeInfo> info;
    REQUIRE(object->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK && info.p);
    TYPEATTR* attr = nullptr;
    REQUIRE(info->GetTypeAttr(&attr) == S_OK && attr);
    const bool identity = IsEqualIID(attr->guid, IID_IOuter);
    info->ReleaseTypeAttr(attr);
    REQUIRE(identity);
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"GetName")};
    DISPID member = DISPID_UNKNOWN;
    REQUIRE(object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_SYSTEM_DEFAULT, &member) == S_OK && member != DISPID_UNKNOWN);
    VARIANTARG number{};
    number.vt = VT_I2; number.iVal = 7;
    DISPPARAMS arguments{&number, nullptr, 1, 0};
    ATL::CComVariant value;
    REQUIRE(object->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD,
        &arguments, &value, nullptr, nullptr) == S_OK);
    REQUIRE(value.vt == VT_BSTR && value.bstrVal && std::wcscmp(value.bstrVal, text) == 0);
    ATL::CComPtr<ISupportErrorInfo> support;
    REQUIRE(object->QueryInterface(IID_ISupportErrorInfo, reinterpret_cast<void**>(&support.p)) == S_OK);
    REQUIRE(support->InterfaceSupportsErrorInfo(IID_IOuter) == S_OK &&
        support->InterfaceSupportsErrorInfo(IID_IChain) == S_FALSE);
    support.Release();
    number.iVal = 8;
    EXCEPINFO exception{};
    ATL::CComVariant failedValue;
    const HRESULT failure = object->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD,
        &arguments, &failedValue, &exception, nullptr);
    ATL::CComBSTR errorDescription, errorSource, errorHelp;
    errorDescription.Attach(exception.bstrDescription);
    errorSource.Attach(exception.bstrSource);
    errorHelp.Attach(exception.bstrHelpFile);
    REQUIRE(failure == DISP_E_EXCEPTION && exception.scode == E_INVALIDARG && exception.dwHelpContext == 712);
    REQUIRE(errorDescription.m_str && std::wcscmp(errorDescription.m_str, L"Expected number seven") == 0);
    REQUIRE(errorHelp.m_str && std::wcscmp(errorHelp.m_str, L"tear-help.chm") == 0);
    ::SetErrorInfo(0, nullptr);
    return true;
}
bool verifyTearOffDispatch(IDispatch* owner, IOuter** retained) {
    REQUIRE(retained);
    *retained = nullptr;
    ATL::CComPtr<ITearOff1> first, another, same;
    ATL::CComPtr<ITearOff2> cached, repeated, cross;
    REQUIRE(owner->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&first.p)) == S_OK);
    REQUIRE(owner->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&another.p)) == S_OK && first.p != another.p);
    REQUIRE(first->QueryInterface(IID_ITearOff1, reinterpret_cast<void**>(&same.p)) == S_OK && first.p == same.p);
    REQUIRE(owner->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&cached.p)) == S_OK);
    REQUIRE(owner->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&repeated.p)) == S_OK && cached.p == repeated.p);
    REQUIRE(first->QueryInterface(IID_ITearOff2, reinterpret_cast<void**>(&cross.p)) == S_OK && cross.p == cached.p);
    ATL::CComPtr<IUnknown> canonical;
    REQUIRE(owner->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical.p)) == S_OK);
    for (IUnknown* item : {static_cast<IUnknown*>(first.p), static_cast<IUnknown*>(another.p),
                           static_cast<IUnknown*>(cached.p), static_cast<IUnknown*>(repeated.p)}) {
        ATL::CComPtr<IUnknown> identity;
        REQUIRE(item->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == canonical.p);
    }
    REQUIRE(tearOffProperty(first.p, L"OpenWTL tear one"));
    REQUIRE(tearOffProperty(cached.p, L"OpenWTL tear two"));
    ATL::CComPtr<IOuter> ordinaryDispatch, cachedDispatch, repeatedDispatch;
    REQUIRE(first->QueryInterface(IID_IOuter, reinterpret_cast<void**>(&ordinaryDispatch.p)) == S_OK);
    REQUIRE(cached->QueryInterface(IID_IOuter, reinterpret_cast<void**>(&cachedDispatch.p)) == S_OK);
    REQUIRE(owner->QueryInterface(IID_IOuter, reinterpret_cast<void**>(&repeatedDispatch.p)) == S_OK &&
        repeatedDispatch.p == cachedDispatch.p && ordinaryDispatch.p != cachedDispatch.p);
    for (IOuter* item : {ordinaryDispatch.p, cachedDispatch.p}) {
        ATL::CComPtr<IUnknown> identity;
        REQUIRE(item->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity.p)) == S_OK && identity.p == canonical.p);
    }
    REQUIRE(tearOffInvoke(ordinaryDispatch.p, L"OpenWTL tear one"));
    REQUIRE(tearOffInvoke(cachedDispatch.p, L"OpenWTL tear two"));
    *retained = cachedDispatch.Detach();
    return true;
}
bool embeddedDispatch(Dll& dll, const char* creator = "OpenATLCreateEmbeddedDispatch") {
    using Create = HRESULT (WINAPI*)(IDispatch**);
    using TypeInfo = HRESULT (WINAPI*)(REFIID, REFGUID, ITypeInfo**);
    const auto create = reinterpret_cast<Create>(
        ::GetProcAddress(dll.image, creator));
    const auto query = reinterpret_cast<TypeInfo>(
        ::GetProcAddress(dll.image, "OpenATLGetEmbeddedTypeInfo"));
    REQUIRE(create && query);
    ATL::CComPtr<ITypeLib> registeredBefore;
    const HRESULT registryBefore = ::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0,
        LOCALE_SYSTEM_DEFAULT, &registeredBefore.p);
    ATL::CComPtr<IDispatch> dispatch;
    REQUIRE(create(&dispatch.p) == S_OK && dispatch.p);
    REQUIRE(dll.unload() == S_FALSE);
    ATL::CComPtr<ITypeInfo> info;
    REQUIRE(dispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK && info.p);
    ATL::CComPtr<ITypeLib> library;
    UINT index = 0;
    REQUIRE(info->GetContainingTypeLib(&library.p, &index) == S_OK && library.p);
    TLIBATTR* attr = nullptr;
    REQUIRE(library->GetLibAttr(&attr) == S_OK && attr);
    const bool identity = IsEqualGUID(attr->guid, LIBID_COMMAPLib) &&
        attr->wMajorVerNum == 1 && attr->wMinorVerNum == 0 &&
        attr->syskind == (sizeof(void*) == 8 ? SYS_WIN64 : SYS_WIN32);
    library->ReleaseTLibAttr(attr);
    REQUIRE(identity);
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"Name2")};
    DISPID member = DISPID_UNKNOWN;
    REQUIRE(dispatch->GetIDsOfNames(IID_NULL, names, 1,
        LOCALE_SYSTEM_DEFAULT, &member) == S_OK && member == 0);
    DISPPARAMS arguments{};
    ATL::CComVariant result;
    REQUIRE(dispatch->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_PROPERTYGET, &arguments, &result, nullptr, nullptr) == S_OK);
    REQUIRE(result.vt == VT_BSTR && result.bstrVal &&
        ::SysStringLen(result.bstrVal) == 22 &&
        std::wcscmp(result.bstrVal, L"OpenWTL embedded chain") == 0);
    ITypeInfo* missing = reinterpret_cast<ITypeInfo*>(1);
    REQUIRE(query(IID_IStream, LIBID_COMMAPLib, &missing) == TYPE_E_ELEMENTNOTFOUND && !missing);
    missing = reinterpret_cast<ITypeInfo*>(1);
    REQUIRE(query(IID_IChain, GUID_NULL, &missing) == E_INVALIDARG && !missing);
    missing = reinterpret_cast<ITypeInfo*>(1);
    REQUIRE(query(IID_IChain, IID_IStream, &missing) == TYPE_E_ELEMENTNOTFOUND && !missing);
    REQUIRE(query(IID_IChain, LIBID_COMMAPLib, nullptr) == E_POINTER);
    ATL::CComPtr<ITypeLib> registeredAfter;
    REQUIRE(::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT,
        &registeredAfter.p) == registryBefore);
    ATL::CComPtr<IOuter> retainedTear;
    REQUIRE(verifyTearOffDispatch(dispatch.p, &retainedTear.p));
    dispatch.Release();
    info.Release();
    library.Release();
    REQUIRE(dll.unload() == S_FALSE);
    REQUIRE(tearOffInvoke(retainedTear.p, L"OpenWTL tear two"));
    retainedTear.Release();
    REQUIRE(dll.unload() == S_OK);
    std::puts("OPENATL_EMBEDDED_TYPELIB_OK image=dll dispatch=1 invoke=1 register=0");
    return true;
}

constexpr LPCWSTR typeLibKey = L"Software\\Classes\\TypeLib\\{4F9A68DD-DD9D-11CF-9462-00AA00BBAD7F}";
bool absentTypeLibRegistration() {
    const struct { LPCWSTR category; const GUID* id; } items[] = {
        {L"TypeLib", &LIBID_COMMAPLib}, {L"Interface", &IID_IChain},
        {L"Interface", &IID_IOuter}, {L"Interface", &IID_ITearOff1},
        {L"Interface", &IID_ITearOff2}
    };
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (const auto& item : items) {
            wchar_t id[40]{};
            REQUIRE(::StringFromGUID2(*item.id, id, 40));
            const std::wstring path = std::wstring(L"Software\\Classes\\") + item.category + L"\\" + id;
            HKEY key = nullptr;
            const LSTATUS state = ::RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
            if (key) ::RegCloseKey(key);
            REQUIRE(state == ERROR_FILE_NOT_FOUND || state == ERROR_PATH_NOT_FOUND);
        }
    }
    return true;
}

// Deny writes only to a newly-created test-owned TypeLib GUID. Keep the
// already-open security handle to restore its DACL on every exit path.
struct DeniedTypeLib {
    HKEY key = nullptr;
    bool owned = false, denied = false;
    std::vector<BYTE> security;
    bool Open() {
        DWORD disposition = 0;
        REQUIRE(::RegCreateKeyExW(HKEY_CURRENT_USER, typeLibKey, 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &key, &disposition) == ERROR_SUCCESS);
        owned = disposition == REG_CREATED_NEW_KEY;
        REQUIRE(owned);
        DWORD bytes = 0;
        REQUIRE(::RegGetKeySecurity(key, DACL_SECURITY_INFORMATION, nullptr,
            &bytes) == ERROR_INSUFFICIENT_BUFFER);
        security.resize(bytes);
        REQUIRE(::RegGetKeySecurity(key, DACL_SECURITY_INFORMATION,
            reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data()), &bytes) == ERROR_SUCCESS);
        PSECURITY_DESCRIPTOR deny = nullptr;
        REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;KW;;;WD)", SDDL_REVISION_1, &deny, nullptr));
        const LSTATUS state = ::RegSetKeySecurity(key,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, deny);
        ::LocalFree(deny);
        denied = state == ERROR_SUCCESS;
        REQUIRE(denied);
        return true;
    }
    bool Close() {
        if (denied) {
            if (::RegSetKeySecurity(key, DACL_SECURITY_INFORMATION,
                reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data())) != ERROR_SUCCESS) return false;
            denied = false;
        }
        if (key) { ::RegCloseKey(key); key = nullptr; }
        if (owned) {
            if (::RegDeleteKeyW(HKEY_CURRENT_USER, typeLibKey) != ERROR_SUCCESS) return false;
            owned = false;
        }
        return true;
    }
    ~DeniedTypeLib() { Close(); }
};

constexpr CLSID exeTypeLibClassId = {0xd19b8867, 0xa881, 0x47f6,
    {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
constexpr LPCWSTR exeTypeLibClassKey = L"OpenWTL.Exe.TypeLib";
constexpr LPCWSTR exeTypeLibAppId = L"AppID\\{DF014EC0-2477-450C-B47C-C493B26D9B90}";
class ExeTypeLibValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IPersist {
public:
    BEGIN_COM_MAP(ExeTypeLibValue)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept {
        if (!ATL::_pAtlModule) return E_UNEXPECTED;
        ATL::_ATL_REGMAP_ENTRY entries[] = {
            {L"KEY", exeTypeLibClassKey}, {L"FIELD", L"Description"},
            {L"VALUE", L"EXE TypeLib"}, {nullptr, nullptr}
        };
        return ATL::_pAtlModule->UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", install, entries);
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = exeTypeLibClassId;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(exeTypeLibClassId, ExeTypeLibValue)

template<bool Apartment>
class ExeTypeLibModule : public ATL::CAtlExeModuleT<ExeTypeLibModule<Apartment>> {
public:
    DECLARE_LIBID(LIBID_COMMAPLib)
    DECLARE_REGISTRY_APPID_RESOURCEID(IDR_OPENATL_APPID, "{DF014EC0-2477-450C-B47C-C493B26D9B90}")
    static HRESULT InitializeCom() noexcept {
        return ::CoInitializeEx(nullptr, Apartment ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    }
    HRESULT AddCommonRGSReplacements(IRegistrarBase* registrar) override {
        const auto* scope = rgs_resource_detail::CurrentModule();
        if (!scope || !scope->PerUserRegistration()) return E_UNEXPECTED;
        ATL::AtlSetPerUserRegistration(false); // The enclosing command must keep its original mode.
        return registrar->AddReplacement(L"APP_VALUE", L"EXE TypeLib");
    }
    HRESULT RegisterServer(BOOL library, const CLSID* = nullptr) noexcept {
        return ATL::CAtlExeModuleT<ExeTypeLibModule<Apartment>>::RegisterServer(library, &exeTypeLibClassId);
    }
    HRESULT UnregisterServer(BOOL library, const CLSID* = nullptr) noexcept {
        return ATL::CAtlExeModuleT<ExeTypeLibModule<Apartment>>::UnregisterServer(library, &exeTypeLibClassId);
    }
};

bool exeDispatch(bool registered) {
    ATL::CComPtr<IDispatch> dispatch;
    const HRESULT hr = registered ? OpenATLCreateRegisteredDispatch(&dispatch.p)
                                  : OpenATLCreateEmbeddedDispatch(&dispatch.p);
    REQUIRE(hr == S_OK && dispatch.p);
    ATL::CComPtr<ITypeInfo> info;
    REQUIRE(dispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK && info.p);
    ATL::CComPtr<ITypeLib> library;
    UINT index = 0;
    REQUIRE(info->GetContainingTypeLib(&library.p, &index) == S_OK && library.p);
    TLIBATTR* attributes = nullptr;
    REQUIRE(library->GetLibAttr(&attributes) == S_OK && attributes);
    const bool identity = ::IsEqualGUID(attributes->guid, LIBID_COMMAPLib) &&
        attributes->wMajorVerNum == 1 && attributes->wMinorVerNum == 0 &&
        attributes->syskind == (sizeof(void*) == 8 ? SYS_WIN64 : SYS_WIN32);
    library->ReleaseTLibAttr(attributes);
    REQUIRE(identity);
    LPOLESTR name = const_cast<LPOLESTR>(L"Name2");
    DISPID member = DISPID_UNKNOWN;
    REQUIRE(dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_SYSTEM_DEFAULT, &member) == S_OK && member == 0);
    DISPPARAMS parameters{};
    ATL::CComVariant result;
    REQUIRE(dispatch->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET,
                            &parameters, &result, nullptr, nullptr) == S_OK);
    REQUIRE(result.vt == VT_BSTR && result.bstrVal &&
            std::wstring_view(result.bstrVal, ::SysStringLen(result.bstrVal)) == L"OpenWTL embedded chain");
    ATL::CComPtr<IOuter> retainedTear;
    REQUIRE(verifyTearOffDispatch(dispatch.p, &retainedTear.p));
    REQUIRE(tearOffInvoke(retainedTear.p, L"OpenWTL tear two"));
    return true;
}

template<bool Apartment>
bool exeTypeLibRegistration() {
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED && !ATL::_pAtlModule);
    REQUIRE(absentTypeLibRegistration()); // No preexisting library or interface metadata may be replaced.
    bool previous = false;
    REQUIRE(ATL::AtlGetPerUserRegistration(&previous) == S_OK);
    struct Restore { bool value; ~Restore() { ATL::AtlSetPerUserRegistration(value); } } restore{previous};
    RegistryRoot root;
    REQUIRE(root.Open());
    {
        ExeTypeLibModule<Apartment> module;
        REQUIRE(module.GetInitHRESULT() == S_OK);
        ScopedRgsRegistrationContext context(root.key, ::GetModuleHandleW(nullptr), L"ExeTypeLibNative");
        REQUIRE(context.Status() == S_OK);
        struct Cleanup {
            ExeTypeLibModule<Apartment>& module;
            bool armed = false;
            ~Cleanup() {
                if (!armed) return;
                HRESULT hr = E_FAIL;
                module.ParseCommandLine(L"test.exe /UnregServerPerUser", &hr);
                if (FAILED(hr)) std::fprintf(stderr, "EXE_TYPELIB_CLEANUP_FAIL hr=0x%08lx\n",
                                             static_cast<unsigned long>(hr));
            }
        } cleanup{module};
        {
            ATL::CComBSTR path;
            ATL::CComPtr<ITypeLib> library;
            REQUIRE(ATL::AtlLoadTypeLib(::GetModuleHandleW(nullptr), L"\\1", &path.m_str, &library.p) == S_OK);
        }
        BSTR missingPath = reinterpret_cast<BSTR>(1);
        ITypeLib* missing = reinterpret_cast<ITypeLib*>(1);
        REQUIRE(FAILED(ATL::AtlLoadTypeLib(::GetModuleHandleW(nullptr), L"\\999", &missingPath, &missing)) &&
                !missingPath && !missing);
        REQUIRE(exeDispatch(false) && absentTypeLibRegistration()); // Embedded loading alone writes nothing.
        REQUIRE(ATL::AtlSetPerUserRegistration(false) == S_OK);
        HRESULT result = E_FAIL;
        cleanup.armed = true;
        REQUIRE(!module.ParseCommandLine(L"test.exe /RegServerPerUser", &result) && result == S_OK);
        REQUIRE(exists(root.key, exeTypeLibClassKey) && exists(root.key, exeTypeLibAppId) &&
                exists(root.key, L"AppID\\OpenWTL.AppId.Test.exe") && exists(HKEY_CURRENT_USER, typeLibKey));
        bool enabled = true;
        REQUIRE(ATL::AtlGetPerUserRegistration(&enabled) == S_OK && !enabled);
        {
            wchar_t image[OpenATL::RGS::MaxPathUnits + 2]{};
            REQUIRE(::GetModuleFileNameW(nullptr, image, static_cast<DWORD>(sizeof(image) / sizeof(image[0]))));
            ATL::CComBSTR registeredPath;
            REQUIRE(::QueryPathOfRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT,
                                            &registeredPath.m_str) == S_OK && registeredPath.m_str);
            REQUIRE(!::lstrcmpiW(image, registeredPath.m_str));
            ATL::CComPtr<ITypeLib> library;
            REQUIRE(::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT, &library.p) == S_OK && library.p);
            REQUIRE(exeDispatch(true) && exeDispatch(false));
        }
        REQUIRE(module.GetLockCount() == 0);
        REQUIRE(!module.ParseCommandLine(L"test.exe /UnregServerPerUser", &result) && result == S_OK);
        cleanup.armed = false;
        REQUIRE(!exists(root.key, exeTypeLibClassKey) && !exists(root.key, exeTypeLibAppId) &&
                !exists(root.key, L"AppID\\OpenWTL.AppId.Test.exe") && absentTypeLibRegistration());
        {
            ATL::CComPtr<ITypeLib> library;
            REQUIRE(FAILED(::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT, &library.p)) && !library.p);
        }
        {
            DeniedTypeLib denied;
            REQUIRE(denied.Open());
            REQUIRE(ATL::AtlSetPerUserRegistration(true) == S_OK);
            const HRESULT nativeFailure = ATL::AtlRegisterTypeLib(::GetModuleHandleW(nullptr), nullptr);
            REQUIRE(FAILED(nativeFailure) && nativeFailure != E_NOTIMPL);
            cleanup.armed = true;
            REQUIRE(!module.ParseCommandLine(L"test.exe /RegServerPerUser", &result) && result == nativeFailure);
            REQUIRE(!exists(root.key, exeTypeLibClassKey) && !exists(root.key, exeTypeLibAppId) &&
                    !exists(root.key, L"AppID\\OpenWTL.AppId.Test.exe"));
            cleanup.armed = false;
            REQUIRE(denied.Close() && absentTypeLibRegistration());
        }
        REQUIRE(module.GetLockCount() == 0);
    }
    REQUIRE(root.Close());
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED && !ATL::_pAtlModule &&
            !rgs_resource_detail::Current() && !rgs_resource_detail::CurrentModule());
    return true;
}

bool typeLibHelpers(Dll& dll) {
    bool perUser = true;
    REQUIRE(ATL::AtlGetPerUserRegistration(&perUser) == S_OK && !perUser);
    REQUIRE(ATL::AtlGetPerUserRegistration(nullptr) == E_POINTER);
    {
        ATL::CComBSTR path;
        ATL::CComPtr<ITypeLib> library;
        REQUIRE(ATL::AtlLoadTypeLib(dll.image, L"\\1", &path.m_str, &library.p) == S_OK);
        REQUIRE(path.m_str && library.p && ::SysStringLen(path.m_str) > 2);
        const UINT length = ::SysStringLen(path.m_str);
        REQUIRE(std::wcscmp(path.m_str + length - 2, L"\\1") == 0);
    }
    BSTR path = reinterpret_cast<BSTR>(1);
    ITypeLib* library = reinterpret_cast<ITypeLib*>(1);
    REQUIRE(ATL::AtlLoadTypeLib(dll.image, L"/1", &path, &library) == E_INVALIDARG && !path && !library);
    REQUIRE(FAILED(ATL::AtlLoadTypeLib(dll.image, L"\\999", &path, &library)) && !path && !library);
    REQUIRE(ATL::AtlLoadTypeLib(nullptr, nullptr, &path, &library) == E_INVALIDARG && !path && !library);
    REQUIRE(ATL::AtlLoadTypeLib(dll.image, nullptr, nullptr, &library) == E_POINTER && !library);
    return true;
}

bool typeLibRollback(Dll& dll) {
    REQUIRE(absentTypeLibRegistration()); // Never overwrite an installed sample.
    bool previous = false;
    REQUIRE(ATL::AtlGetPerUserRegistration(&previous) == S_OK);
    struct Restore { bool value; ~Restore() { ATL::AtlSetPerUserRegistration(value); } } restore{previous};
    REQUIRE(ATL::AtlSetPerUserRegistration(true) == S_OK);
    bool enabled = false;
    REQUIRE(ATL::AtlGetPerUserRegistration(&enabled) == S_OK && enabled);
    DeniedTypeLib denied;
    REQUIRE(denied.Open());
    const HRESULT nativeFailure = ATL::AtlRegisterTypeLib(dll.image, nullptr);
    REQUIRE(FAILED(nativeFailure) && nativeFailure != E_NOTIMPL);
    REQUIRE(dll.install() == nativeFailure);
    for (const GUID* id : {&CLSID_OpenATLValue, &CLSID_OpenATLAuxValue}) {
        wchar_t text[40]{};
        REQUIRE(::StringFromGUID2(*id, text, 40));
        const std::wstring path = std::wstring(L"Software\\Classes\\CLSID\\") + text;
        REQUIRE(!exists(HKEY_CURRENT_USER, path.c_str()));
    }
    REQUIRE(!exists(HKEY_CURRENT_USER, L"Software\\Classes\\OpenATL.Conformance.Server.1"));
    REQUIRE(!exists(HKEY_CURRENT_USER, L"Software\\Classes\\OpenATL.Conformance.Aux.1"));
    REQUIRE(denied.Close());
    REQUIRE(absentTypeLibRegistration());
    return true;
}
#endif

// All test-induced foreign registry content is owned under a newly-created
// exact ProgID. No preexisting user entry is overwritten or deleted.
struct ForeignAuxProgId {
    HKEY key = nullptr;
    bool owned = false;
    static constexpr LPCWSTR path = L"Software\\Classes\\OpenATL.Conformance.Aux.1";
    bool Create() noexcept {
        DWORD disposition = 0;
        const LSTATUS state = ::RegCreateKeyExW(HKEY_CURRENT_USER, path, 0,
            nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
        if (state != ERROR_SUCCESS) return false;
        owned = disposition == REG_CREATED_NEW_KEY;
        return owned && ::RegSetValueExW(key, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(L"Foreign application"),
            sizeof(L"Foreign application")) == ERROR_SUCCESS;
    }
    bool Check() const noexcept {
        if (!key || !owned) return false;
        wchar_t value[64]{};
        DWORD type = 0, bytes = sizeof(value);
        return ::RegQueryValueExW(key, nullptr, nullptr, &type,
                   reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS &&
               type == REG_SZ && ::lstrcmpW(value, L"Foreign application") == 0;
    }
    bool Remove() noexcept {
        if (key) { ::RegCloseKey(key); key = nullptr; }
        if (!owned) return false;
        if (::RegDeleteKeyW(HKEY_CURRENT_USER, path) != ERROR_SUCCESS) return false;
        owned = false;
        return true;
    }
    ~ForeignAuxProgId() {
        if (key) ::RegCloseKey(key);
        if (owned) (void)::RegDeleteKeyW(HKEY_CURRENT_USER, path);
    }
};


bool verifyResourceDocuments(HMODULE image) {
    using namespace OpenATL;
    const struct { unsigned id; std::wstring_view original; } cases[] = {
        {IDR_MICROSOFT_CHAIN, OpenATLRgsSamples::Chain},
        {IDR_MICROSOFT_OUTER, OpenATLRgsSamples::Outer}
    };
    for (const auto& sample : cases) {
        RGS::Document expected, actual;
        REQUIRE(RGS::Parse(sample.original, expected) == RGS::Error::Ok);
        REQUIRE(COM::ReadRgsResource(image, sample.id, actual) == S_OK);
        REQUIRE(actual.keys.size() == expected.keys.size());
        for (size_t i = 0; i < expected.keys.size(); ++i) {
            const auto& a = actual.keys[i];
            const auto& b = expected.keys[i];
            REQUIRE(a.path == b.path && a.directive == b.directive);
            REQUIRE(a.values.size() == b.values.size());
            for (size_t k = 0; k < b.values.size(); ++k)
                REQUIRE(a.values[k].name == b.values[k].name &&
                        a.values[k].text == b.values[k].text);
        }
    }
    RGS::Document untouched;
    REQUIRE(RGS::Parse(L"HKCR { Sentinel = s 'unchanged' }", untouched) ==
            RGS::Error::Ok);
    REQUIRE(FAILED(COM::ReadRgsResource(image, IDR_WRONG_RESOURCE_TYPE,
                                        untouched)));
    REQUIRE(FAILED(COM::ReadRgsResource(image, 32767, untouched)));
    REQUIRE(COM::ReadRgsResource(nullptr, IDR_MICROSOFT_CHAIN,
                                 untouched) == E_INVALIDARG);
    REQUIRE(COM::ReadRgsResource(image, 0U, untouched) == E_INVALIDARG);
    REQUIRE(untouched.keys.size() == 1 &&
            untouched.keys[0].path == L"Sentinel");
    std::wstring decoded = L"untouched";
    const BYTE invalidUtf8[] = {0xef, 0xbb, 0xbf, 0xc3, 0x28};
    REQUIRE(COM::rgs_resource_detail::Decode(
        invalidUtf8, sizeof(invalidUtf8), decoded) ==
        HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
    REQUIRE(decoded == L"untouched");
    const BYTE invalidUtf16[] = {0xff, 0xfe, 0x00, 0xd8};
    REQUIRE(COM::rgs_resource_detail::Decode(
        invalidUtf16, sizeof(invalidUtf16), decoded) ==
        HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
    REQUIRE(decoded == L"untouched");
    const BYTE validUtf16[] = {0xff, 0xfe, 0x48, 0x00, 0x4b, 0x00,
                               0x43, 0x00, 0x52, 0x00, 0x20, 0x00,
                               0x7b, 0x00, 0x20, 0x00, 0x41, 0x00,
                               0x20, 0x00, 0x7d, 0x00};
    REQUIRE(COM::rgs_resource_detail::Decode(
        validUtf16, sizeof(validUtf16), decoded) == S_OK &&
        decoded == L"HKCR { A }");
    REQUIRE(COM::UpdateRegistryResource(IDR_MICROSOFT_CHAIN, TRUE) ==
            E_UNEXPECTED); // No ambient registration context.
    // Read-only resource admission must not create a COM apartment.
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}

class CommonRgsTestModule : public ATL::CAtlDllModuleT<CommonRgsTestModule> {
public:
    enum class Action { Add, Recover, Clear, Fail, AtlFailure, AllocationFailure,
                        Unexpected, Duplicate, Reserved, Limit } action = Action::Add;
    ATL::CComPtr<IRegistrarBase> retained;
    unsigned calls = 0;
    HRESULT AddCommonRGSReplacements(IRegistrarBase* registrar) override {
        ++calls;
        retained = registrar;
        switch (action) {
        case Action::Recover:
            if (registrar->AddReplacement(L"key", L"OpenATL.Bad") != E_INVALIDARG)
                return E_UNEXPECTED;
            break; // A rejected addition must leave the original map intact.
        case Action::Fail: return E_ABORT;
        case Action::AtlFailure: throw ATL::CAtlException(E_ACCESSDENIED);
        case Action::AllocationFailure: throw std::bad_alloc();
        case Action::Unexpected: throw std::runtime_error("common replacements");
        case Action::Duplicate: return registrar->AddReplacement(L"key", L"duplicate");
        case Action::Reserved: return registrar->AddReplacement(L"MODULE", L"C:\\other.dll");
        case Action::Limit:
            for (size_t i = 0; i <= OpenATL::RGS::MaxReplacements; ++i) {
                const std::wstring name = L"Entry" + std::to_wstring(i);
                const HRESULT hr = registrar->AddReplacement(name.c_str(), L"value");
                if (FAILED(hr)) return hr;
            }
            return E_UNEXPECTED;
        case Action::Clear: {
            HRESULT hr = registrar->ClearReplacements();
            if (FAILED(hr)) return hr;
            hr = registrar->AddReplacement(L"KEY", L"OpenATL.Common.Cleared");
            if (FAILED(hr)) return hr;
            hr = registrar->AddReplacement(L"VALUE", L"cleared");
            if (FAILED(hr)) return hr;
            break;
        }
        default: break;
        }
        wchar_t field[] = L"Description";
        const HRESULT hr = registrar->AddReplacement(L"FIELD", field);
        field[0] = L'X'; // The borrowed stack string must already be copied.
        return hr;
    }
};

bool commonResourceReplacements(HMODULE image, HKEY root) {
    CommonRgsTestModule registrar;
    registrar.action = CommonRgsTestModule::Action::Recover;
    ScopedRgsRegistrationContext context(root, image, L"CommonResourceTest");
    REQUIRE(context.Status() == S_OK);
    ATL::_ATL_REGMAP_ENTRY map[] = {
        {L"KEY", L"OpenATL.Common"}, {L"VALUE", L"O'Brien { literal }"}, {nullptr, nullptr}
    };
    REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, map) == S_OK);
    REQUIRE(registrar.calls == 1 && !exists(root, L"OpenATL.Bad") && registrar.GetLockCount() == 1 &&
            registrar.DllCanUnloadNow() == S_FALSE);
    ATL::CRegKey key;
    REQUIRE(key.Open(root, L"OpenATL.Common", KEY_READ) == ERROR_SUCCESS);
    wchar_t value[128]{};
    ULONG length = 128;
    REQUIRE(key.QueryStringValue(L"Description", value, &length) == ERROR_SUCCESS &&
            std::wstring_view(value) == L"prefix O'Brien { literal } suffix");
    key.Close();
    REQUIRE(registrar.retained.p &&
            registrar.retained->AddReplacement(L"LATE", L"data") == E_UNEXPECTED &&
            registrar.retained->ClearReplacements() == E_UNEXPECTED);
    ATL::CComPtr<IUnknown> identity;
    REQUIRE(registrar.retained->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&identity.p)) == S_OK);
    REQUIRE(identity.p == static_cast<IUnknown*>(registrar.retained.p));
    ATL::CComPtr<IRegistrarBase> again;
    REQUIRE(identity->QueryInterface(__uuidof(IRegistrarBase),
        reinterpret_cast<void**>(&again.p)) == S_OK && again.p == registrar.retained.p);
    void* unsupported = reinterpret_cast<void*>(1);
    REQUIRE(again->QueryInterface(IID_IStream, &unsupported) == E_NOINTERFACE && !unsupported);
    unsupported = reinterpret_cast<void*>(1);
    REQUIRE(again->QueryInterface(IID_IRegistrar, &unsupported) == E_NOINTERFACE && !unsupported);
    REQUIRE(again->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    identity.Release();
    again.Release();
    registrar.retained.Release();
    REQUIRE(registrar.GetLockCount() == 0 && registrar.DllCanUnloadNow() == S_OK);
    REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", FALSE, map) == S_OK);
    registrar.retained.Release();
    REQUIRE(!exists(root, L"OpenATL.Common") && registrar.GetLockCount() == 0);

    const struct { CommonRgsTestModule::Action action; HRESULT error; } failures[] = {
        {CommonRgsTestModule::Action::Fail, E_ABORT},
        {CommonRgsTestModule::Action::AtlFailure, E_ACCESSDENIED},
        {CommonRgsTestModule::Action::AllocationFailure, E_OUTOFMEMORY},
        {CommonRgsTestModule::Action::Unexpected, E_UNEXPECTED},
        {CommonRgsTestModule::Action::Duplicate, E_INVALIDARG},
        {CommonRgsTestModule::Action::Reserved, E_INVALIDARG},
        {CommonRgsTestModule::Action::Limit, HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)}
    };
    for (const auto& failure : failures) {
        registrar.action = failure.action;
        REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, map) == failure.error);
        REQUIRE(!exists(root, L"OpenATL.Common") && registrar.GetLockCount() == 1);
        REQUIRE(registrar.retained->AddReplacement(L"LATE", L"data") == E_UNEXPECTED &&
                registrar.retained->ClearReplacements() == E_UNEXPECTED);
        registrar.retained.Release();
        REQUIRE(registrar.GetLockCount() == 0);
    }
    registrar.action = CommonRgsTestModule::Action::Clear;
    REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, map) == S_OK);
    REQUIRE(!exists(root, L"OpenATL.Common") && exists(root, L"OpenATL.Common.Cleared"));
    REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", FALSE, map) == S_OK);
    registrar.retained.Release();
    REQUIRE(!exists(root, L"OpenATL.Common.Cleared") && registrar.GetLockCount() == 0);
    registrar.action = CommonRgsTestModule::Action::Add;
    REQUIRE(FAILED(registrar.UpdateRegistryFromResourceS(L"Missing.RGS", TRUE, map)));
    REQUIRE(registrar.retained->ClearReplacements() == E_UNEXPECTED);
    registrar.retained.Release();
    REQUIRE(registrar.GetLockCount() == 0);
    const unsigned calls = registrar.calls;
    REQUIRE(registrar.UpdateRegistryFromResourceS(static_cast<LPCTSTR>(nullptr), TRUE, map) ==
            E_INVALIDARG && registrar.calls == calls);
    ATL::_ATL_REGMAP_ENTRY invalid[] = {{nullptr, L"bad sentinel"}};
    REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, invalid) ==
            E_INVALIDARG && registrar.calls == calls);
    registrar.Term();
    REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, map) == E_UNEXPECTED &&
            registrar.calls == calls);
    return true;
}

struct TempRgsFile {
    std::wstring path;
    HANDLE writer = INVALID_HANDLE_VALUE;
    bool Open() {
        wchar_t directory[MAX_PATH]{}, name[MAX_PATH]{};
        const DWORD length = ::GetTempPathW(MAX_PATH, directory);
        if (!length || length >= MAX_PATH || !::GetTempFileNameW(directory, L"OWR", 0, name))
            return false;
        path = name;
        return true;
    }
    void CloseWriter() {
        if (writer != INVALID_HANDLE_VALUE) { ::CloseHandle(writer); writer = INVALID_HANDLE_VALUE; }
    }
    bool Write(const std::vector<BYTE>& bytes, bool keepOpen = false) {
        CloseWriter();
        writer = ::CreateFileW(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (writer == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool okay = bytes.empty() ||
            (::WriteFile(writer, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
             written == bytes.size());
        if (!keepOpen) CloseWriter();
        return okay;
    }
    ~TempRgsFile() { CloseWriter(); if (!path.empty()) ::DeleteFileW(path.c_str()); }
};

class StaticRegistrarTestModule final : public ATL::CAtlDllModuleT<StaticRegistrarTestModule> {};
bool staticRegistrarScripts(HMODULE image, HKEY root) {
    StaticRegistrarTestModule module;
    ScopedRgsRegistrationContext context(root, image, L"StaticRegistrarTest");
    REQUIRE(context.Status() == S_OK && module.DllCanUnloadNow() == S_OK);
    REQUIRE(ATL::AtlCreateRegistrar(nullptr) == E_POINTER);
    ATL::CComPtr<IRegistrar> retained;
    const wchar_t* source = L"HKCR { ForceRemove '%KEY%' = s '%VALUE%' "
                            L"{ val Origin = s '%MODULE%' } }";
    {
        ATL::CRegObject object;
        REQUIRE(module.GetLockCount() == 1 && module.DllCanUnloadNow() == S_FALSE);
        REQUIRE(object.QueryInterface(IID_IRegistrar,
                reinterpret_cast<void**>(&retained.p)) == S_OK);
        ATL::CComPtr<IRegistrarBase> base;
        REQUIRE(retained->QueryInterface(__uuidof(IRegistrarBase),
                reinterpret_cast<void**>(&base.p)) == S_OK);
        ATL::CComPtr<IRegistrar> extended;
        REQUIRE(base->QueryInterface(__uuidof(IRegistrar),
                reinterpret_cast<void**>(&extended.p)) == S_OK && extended.p == retained.p);
        ATL::CComPtr<IUnknown> first, second;
        REQUIRE(base->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&first.p)) == S_OK);
        REQUIRE(retained->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&second.p)) == S_OK &&
                first.p == second.p);
        void* unsupported = reinterpret_cast<void*>(1);
        REQUIRE(retained->QueryInterface(IID_IStream, &unsupported) == E_NOINTERFACE && !unsupported);
        REQUIRE(retained->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);

        wchar_t value[] = L"O'Brien { literal }";
        REQUIRE(object.AddReplacement(L"KEY", L"OpenATL.Static.String") == S_OK &&
                object.AddReplacement(L"VALUE", value) == S_OK);
        value[0] = L'X';
        REQUIRE(object.AddReplacement(L"key", L"OpenATL.Bad") == E_INVALIDARG);
        REQUIRE(object.StringRegister(source) == S_OK);
        ATL::CRegKey key;
        REQUIRE(key.Open(root, L"OpenATL.Static.String", KEY_READ) == ERROR_SUCCESS);
        wchar_t actual[OpenATL::RGS::MaxPathUnits + 2]{};
        ULONG length = static_cast<ULONG>(sizeof(actual) / sizeof(actual[0]));
        REQUIRE(key.QueryStringValue(nullptr, actual, &length) == ERROR_SUCCESS &&
                std::wstring_view(actual) == L"O'Brien { literal }");
        length = static_cast<ULONG>(sizeof(actual) / sizeof(actual[0]));
        REQUIRE(key.QueryStringValue(L"Origin", actual, &length) == ERROR_SUCCESS &&
                std::wstring_view(actual) == rgs_resource_detail::Current()->modulePath);
        key.Close();
        REQUIRE(!exists(root, L"OpenATL.Bad"));
        REQUIRE(object.StringRegister(source) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        REQUIRE(object.ClearReplacements() == S_OK &&
                object.AddReplacement(L"KEY", L"OpenATL.Static.String") == S_OK &&
                object.AddReplacement(L"VALUE", L"wrong") == S_OK);
        REQUIRE(FAILED(object.StringUnregister(source)) && exists(root, L"OpenATL.Static.String"));
        REQUIRE(object.ClearReplacements() == S_OK &&
                object.AddReplacement(L"KEY", L"OpenATL.Static.String") == S_OK &&
                object.AddReplacement(L"VALUE", L"O'Brien { literal }") == S_OK);
        REQUIRE(object.StringUnregister(source) == S_OK && !exists(root, L"OpenATL.Static.String"));
        REQUIRE(object.StringRegister(nullptr) == E_INVALIDARG);
        REQUIRE(FAILED(object.StringRegister(L"HKLM { A = s 'no' }")) && !exists(root, L"A"));
        REQUIRE(FAILED(object.StringRegister(L"HKCR { A = s '%UNKNOWN%' }")) && !exists(root, L"A"));

        TempRgsFile file;
        REQUIRE(file.Open());
        const std::string utf8 = "HKCR { ForceRemove OpenATL.Static.File = s 'file data' }";
        const std::vector<BYTE> bytes(utf8.begin(), utf8.end());
        REQUIRE(file.Write(bytes));
        REQUIRE(object.FileRegister(file.path.c_str()) == S_OK && exists(root, L"OpenATL.Static.File"));
        REQUIRE(object.FileUnregister(file.path.c_str()) == S_OK && !exists(root, L"OpenATL.Static.File"));
        // Both UTF-16 byte orders must resolve to the same registered plan.
        for (bool little : {true, false}) {
            std::vector<BYTE> encoded = little ? std::vector<BYTE>{0xff, 0xfe} :
                                                std::vector<BYTE>{0xfe, 0xff};
            for (unsigned char ch : utf8) {
                encoded.push_back(little ? ch : 0);
                encoded.push_back(little ? 0 : ch);
            }
            REQUIRE(file.Write(encoded) && retained->FileRegister(file.path.c_str()) == S_OK);
            REQUIRE(file.Write(bytes) && retained->FileUnregister(file.path.c_str()) == S_OK);
        }
        REQUIRE(file.Write(bytes, true));
        REQUIRE(object.FileRegister(file.path.c_str()) == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        file.CloseWriter();
        REQUIRE(!exists(root, L"OpenATL.Static.File"));
        REQUIRE(file.Write({0xef, 0xbb, 0xbf, 0xc3, 0x28}) &&
                object.FileRegister(file.path.c_str()) == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        REQUIRE(file.Write({0xff, 0xfe, 0x00}) &&
                object.FileRegister(file.path.c_str()) == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        REQUIRE(file.Write({}) && object.FileRegister(file.path.c_str()) == E_INVALIDARG);
        REQUIRE(file.Write(std::vector<BYTE>(OpenATL::RGS::MaxSourceUnits * 4 + 5, ' ')) &&
                object.FileRegister(file.path.c_str()) == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
        REQUIRE(object.FileRegister(L"relative.rgs") == E_INVALIDARG);
        REQUIRE(FAILED(object.FileRegister((file.path + L".missing").c_str())));
        REQUIRE(!exists(root, L"OpenATL.Static.File"));

        const auto& path = rgs_resource_detail::Current()->modulePath;
        REQUIRE(object.ResourceRegister(path.c_str(), IDR_MICROSOFT_CHAIN, L"REGISTRY") == S_OK &&
                exists(root, L"COMMap.Chain.1"));
        REQUIRE(object.ResourceUnregister(path.c_str(), IDR_MICROSOFT_CHAIN, L"REGISTRY") == S_OK &&
                !exists(root, L"COMMap.Chain.1"));
        REQUIRE(object.ClearReplacements() == S_OK &&
                object.AddReplacement(L"KEY", L"OpenATL.Static.Resource") == S_OK &&
                object.AddReplacement(L"VALUE", L"resource data") == S_OK &&
                object.AddReplacement(L"FIELD", L"Description") == S_OK);
        REQUIRE(object.ResourceRegisterSz(path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK &&
                exists(root, L"OpenATL.Static.Resource"));
        REQUIRE(retained->ResourceUnregisterSz(path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK &&
                !exists(root, L"OpenATL.Static.Resource"));
        REQUIRE(retained->ResourceRegisterSz(path.c_str(), MAKEINTRESOURCEW(IDR_MICROSOFT_CHAIN),
                                            L"registry") == S_OK);
        REQUIRE(object.ResourceUnregisterSz(path.c_str(), MAKEINTRESOURCEW(IDR_MICROSOFT_CHAIN),
                                             L"REGISTRY") == S_OK);
        wchar_t host[OpenATL::RGS::MaxPathUnits + 2]{};
        REQUIRE(::GetModuleFileNameW(nullptr, host, static_cast<DWORD>(sizeof(host) / sizeof(host[0]))));
        REQUIRE(object.ResourceRegister(host, IDR_MICROSOFT_CHAIN, L"REGISTRY") == S_OK);
        REQUIRE(object.ResourceUnregister(host, IDR_MICROSOFT_CHAIN, L"REGISTRY") == S_OK &&
                !exists(root, L"COMMap.Chain.1"));
        REQUIRE(object.ResourceRegister(path.c_str(), IDR_MICROSOFT_CHAIN, L"RCDATA") ==
                HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        REQUIRE(object.ResourceRegister(path.c_str(), 0, L"REGISTRY") == E_INVALIDARG);
        REQUIRE(FAILED(object.ResourceRegister(path.c_str(), 65535, L"REGISTRY")));

        REQUIRE(object.ClearReplacements() == S_OK &&
                object.AddReplacement(L"KEY", L"OpenATL.Static.Survives") == S_OK &&
                object.AddReplacement(L"VALUE", L"owned interface") == S_OK);
        REQUIRE(object.StringRegister(source) == S_OK);
    }
    REQUIRE(module.GetLockCount() == 1 && module.DllCanUnloadNow() == S_FALSE);
    REQUIRE(retained->StringUnregister(source) == S_OK && !exists(root, L"OpenATL.Static.Survives"));
    retained.Release();
    REQUIRE(module.GetLockCount() == 0 && module.DllCanUnloadNow() == S_OK);
    ATL::CComPtr<IRegistrar> created;
    REQUIRE(ATL::AtlCreateRegistrar(&created.p) == S_OK && created.p);
    REQUIRE(created->StringRegister(source) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    created.Release();
    REQUIRE(module.GetLockCount() == 0);
    module.Term();
    IRegistrar* rejected = reinterpret_cast<IRegistrar*>(1);
    REQUIRE(ATL::AtlCreateRegistrar(&rejected) == E_UNEXPECTED && !rejected);
    ATL::CRegObject stopped;
    REQUIRE(stopped.StringRegister(source) == E_UNEXPECTED);
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}

struct ResourceExecutionGate {
    std::wstring previous;
    bool existed = false;
    bool armed = false;
    bool Open() {
        ::SetLastError(ERROR_SUCCESS);
        const DWORD size = ::GetEnvironmentVariableW(OPENATL_RESOURCE_EXECUTION_GUARD, nullptr, 0);
        const DWORD error = ::GetLastError();
        existed = size != 0 || error == ERROR_SUCCESS;
        if (!size && error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND) return false;
        if (size) {
            std::vector<wchar_t> value(size);
            const DWORD length = ::GetEnvironmentVariableW(OPENATL_RESOURCE_EXECUTION_GUARD,
                                                           value.data(), size);
            if (!length || length >= size) return false;
            previous.assign(value.data(), length);
        }
        armed = ::SetEnvironmentVariableW(OPENATL_RESOURCE_EXECUTION_GUARD, L"1") != FALSE;
        return armed;
    }
    ~ResourceExecutionGate() {
        if (armed) ::SetEnvironmentVariableW(OPENATL_RESOURCE_EXECUTION_GUARD,
                                            existed ? previous.c_str() : nullptr);
    }
};

bool externalRegistrarResources(HMODULE image, HKEY root) {
    StaticRegistrarTestModule module;
    TempRgsFile copy;
    REQUIRE(copy.Open());
    wchar_t imagePath[OpenATL::RGS::MaxPathUnits + 2]{};
    REQUIRE(::GetModuleFileNameW(image, imagePath,
        static_cast<DWORD>(sizeof(imagePath) / sizeof(imagePath[0]))));
    REQUIRE(::CopyFileW(imagePath, copy.path.c_str(), FALSE));
    ResourceExecutionGate gate;
    REQUIRE(gate.Open());
    REQUIRE(!::GetModuleHandleW(copy.path.c_str()));
    HMODULE executable = ::LoadLibraryW(copy.path.c_str());
    const DWORD executionError = ::GetLastError();
    if (executable) ::FreeLibrary(executable);
    REQUIRE(!executable && executionError == ERROR_DLL_INIT_FAILED);
    ATL::CRegObject object;
    {
        ScopedRgsRegistrationContext context(root, image, L"ExternalRegistrarTest");
        REQUIRE(context.Status() == S_OK);
        REQUIRE(object.AddReplacement(L"KEY", L"OpenATL.External.Resource") == S_OK &&
                object.AddReplacement(L"VALUE", L"external resource") == S_OK &&
                object.AddReplacement(L"FIELD", L"Description") == S_OK);
        REQUIRE(object.ResourceRegisterSz(copy.path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
        ATL::CRegKey key;
        REQUIRE(key.Open(root, L"OpenATL.External.Resource", KEY_READ) == ERROR_SUCCESS);
        wchar_t value[OpenATL::RGS::MaxPathUnits + 2]{};
        ULONG count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        REQUIRE(key.QueryStringValue(L"Module", value, &count) == ERROR_SUCCESS &&
                std::wstring_view(value) == copy.path);
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        REQUIRE(key.QueryStringValue(L"RawModule", value, &count) == ERROR_SUCCESS &&
                std::wstring_view(value) == copy.path);
        key.Close();
        REQUIRE(!::GetModuleHandleW(copy.path.c_str()));
        REQUIRE(FAILED(object.ResourceUnregisterSz(imagePath, L"OPENATL_PARAMETERS", L"REGISTRY")) &&
                exists(root, L"OpenATL.External.Resource"));
        REQUIRE(object.ResourceUnregisterSz(copy.path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK &&
                !exists(root, L"OpenATL.External.Resource"));
        REQUIRE(object.ResourceRegister(copy.path.c_str(), IDR_MICROSOFT_CHAIN, L"REGISTRY") == S_OK &&
                exists(root, L"COMMap.Chain.1"));
        REQUIRE(object.ResourceUnregister(copy.path.c_str(), IDR_MICROSOFT_CHAIN, L"REGISTRY") == S_OK &&
                !exists(root, L"COMMap.Chain.1"));
        REQUIRE(FAILED(object.ResourceRegister(copy.path.c_str(), 65535, L"REGISTRY")));
        copy.writer = ::CreateFileW(copy.path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(copy.writer != INVALID_HANDLE_VALUE);
        REQUIRE(object.ResourceRegister(copy.path.c_str(), IDR_MICROSOFT_CHAIN, L"REGISTRY") ==
                HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        copy.CloseWriter();

        REQUIRE(object.AddReplacement(L"MODULE", L"relative.dll") == E_INVALIDARG);
        REQUIRE(object.AddReplacement(L"module", copy.path.c_str()) == S_OK);
        REQUIRE(object.AddReplacement(L"MODULE", imagePath) == E_INVALIDARG);
        const wchar_t* source = L"HKCR { ForceRemove '%KEY%' = s '%MODULE%' }";
        REQUIRE(object.StringRegister(source) == S_OK);
        REQUIRE(key.Open(root, L"OpenATL.External.Resource", KEY_READ) == ERROR_SUCCESS);
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        REQUIRE(key.QueryStringValue(nullptr, value, &count) == ERROR_SUCCESS &&
                std::wstring_view(value) == copy.path);
        key.Close();
        REQUIRE(object.StringUnregister(source) == S_OK);
        REQUIRE(object.ClearReplacements() == S_OK &&
                object.AddReplacement(L"KEY", L"OpenATL.External.Resource") == S_OK);
        REQUIRE(object.StringRegister(source) == S_OK);
        REQUIRE(key.Open(root, L"OpenATL.External.Resource", KEY_READ) == ERROR_SUCCESS);
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        REQUIRE(key.QueryStringValue(nullptr, value, &count) == ERROR_SUCCESS &&
                std::wstring_view(value) == rgs_resource_detail::Current()->modulePath);
        key.Close();
        REQUIRE(object.StringUnregister(source) == S_OK);
        REQUIRE(object.ClearReplacements() == S_OK &&
                object.AddReplacement(L"KEY", L"OpenATL.External.Resource") == S_OK &&
                object.AddReplacement(L"VALUE", L"explicit server identity") == S_OK &&
                object.AddReplacement(L"FIELD", L"Description") == S_OK &&
                object.AddReplacement(L"MODULE", imagePath) == S_OK);
        REQUIRE(object.ResourceRegisterSz(copy.path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
        REQUIRE(key.Open(root, L"OpenATL.External.Resource", KEY_READ) == ERROR_SUCCESS);
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        REQUIRE(key.QueryStringValue(L"Module", value, &count) == ERROR_SUCCESS &&
                std::wstring_view(value) == rgs_resource_detail::Current()->modulePath);
        key.Close();
        REQUIRE(object.ResourceUnregisterSz(copy.path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
    }
    // Exercise the default per-user root as well as the explicit private root.
    struct RestoreMode {
        bool previous = false;
        bool armed = false;
        ~RestoreMode() { if (armed) ATL::AtlSetPerUserRegistration(previous); }
    } mode;
    REQUIRE(ATL::AtlGetPerUserRegistration(&mode.previous) == S_OK);
    REQUIRE(ATL::AtlSetPerUserRegistration(true) == S_OK);
    mode.armed = true;
    GUID id{};
    wchar_t guid[40]{};
    REQUIRE(::CoCreateGuid(&id) == S_OK && ::StringFromGUID2(id, guid, 40));
    std::wstring name = L"OpenATL.External.";
    for (wchar_t ch : guid) if (OpenATL::RGS::detail::Hex(ch)) name += ch;
    const std::wstring path = L"Software\\Classes\\" + name;
    REQUIRE(object.ClearReplacements() == S_OK && object.AddReplacement(L"KEY", name.c_str()) == S_OK &&
            object.AddReplacement(L"FIELD", L"Description") == S_OK &&
            object.AddReplacement(L"VALUE", L"per-user external resource") == S_OK);
    struct CleanupRegistration {
        ATL::CRegObject& object;
        const std::wstring& image;
        bool installed = false;
        ~CleanupRegistration() {
            if (installed) object.ResourceUnregisterSz(image.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY");
        }
    } cleanup{object, copy.path};
    REQUIRE(object.ResourceRegisterSz(copy.path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
    cleanup.installed = true;
    REQUIRE(exists(HKEY_CURRENT_USER, path.c_str()));
    REQUIRE(FAILED(object.ResourceUnregisterSz(imagePath, L"OPENATL_PARAMETERS", L"REGISTRY")) &&
            exists(HKEY_CURRENT_USER, path.c_str()));
    REQUIRE(object.ResourceUnregisterSz(copy.path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
    cleanup.installed = false;
    REQUIRE(!exists(HKEY_CURRENT_USER, path.c_str()));
    REQUIRE(!rgs_resource_detail::Current() && !rgs_resource_detail::CurrentModule());
    REQUIRE(!::GetModuleHandleW(copy.path.c_str()));
    REQUIRE(::DeleteFileW(copy.path.c_str())); // Both file and mapping handles retired.
    copy.path.clear();
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}

bool exeModulePaths(HKEY root) {
    StaticRegistrarTestModule module;
    const HMODULE image = ::GetModuleHandleW(nullptr);
    ScopedRgsRegistrationContext context(root, image, L"ExeModulePaths");
    REQUIRE(context.Status() == S_OK);
    const std::wstring path = rgs_resource_detail::Current()->modulePath;
    const std::wstring quoted = L"\"" + path + L"\"";
    const wchar_t* name = L"OpenATL.Exe.ModulePaths";
    const auto verify = [&](std::wstring_view formatted, bool command) {
        ATL::CRegKey key;
        if (key.Open(root, name, KEY_READ) != ERROR_SUCCESS) return false;
        const auto valueIs = [&](LPCWSTR field, std::wstring_view expected) {
            wchar_t actual[OpenATL::RGS::MaxPathUnits + 32]{};
            ULONG count = static_cast<ULONG>(sizeof(actual) / sizeof(actual[0]));
            return key.QueryStringValue(field, actual, &count) == ERROR_SUCCESS &&
                   std::wstring_view(actual) == expected;
        };
        const std::wstring display = std::wstring(formatted) + (command ? L" /Embedding" : L"");
        return valueIs(L"Module", display) && valueIs(L"RawModule", path) &&
               valueIs(L"OpenATL.COMOwner", L"RGS1:ExeModulePaths:" + path);
    };
    ATL::_ATL_REGMAP_ENTRY entries[] = {
        {L"KEY", name}, {L"FIELD", L"Description"}, {L"VALUE", L"EXE resource"}, {nullptr, nullptr}
    };
    REQUIRE(module.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, entries) == S_OK);
    REQUIRE(verify(quoted, false));
    REQUIRE(module.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", FALSE, entries) == S_OK &&
            !exists(root, name));

    ATL::CRegObject object;
    const auto reset = [&]() {
        return object.ClearReplacements() == S_OK && object.AddReplacement(L"KEY", name) == S_OK &&
               object.AddReplacement(L"FIELD", L"Description") == S_OK &&
               object.AddReplacement(L"VALUE", L"EXE resource") == S_OK;
    };
    REQUIRE(reset());
    // A resource-only mapping of the EXE selects quoting from its PE header.
    REQUIRE(object.ResourceRegisterSz(path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
    REQUIRE(verify(quoted, false));
    REQUIRE(object.ResourceUnregisterSz(path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
    const wchar_t* script = L"HKCR { ForceRemove '%KEY%' "
                            L"{ val Module = s '%MODULE% /Embedding' "
                            L"val RawModule = s '%MODULE_RAW%' } }";
    REQUIRE(object.StringRegister(script) == S_OK && verify(quoted, true));
    // The owner path stays raw; changed value formatting still blocks deletion.
    REQUIRE(object.AddReplacement(L"MODULE", path.c_str()) == S_OK);
    REQUIRE(FAILED(object.StringUnregister(script)) && exists(root, name));
    REQUIRE(reset() && object.StringUnregister(script) == S_OK && !exists(root, name));
    REQUIRE(object.AddReplacement(L"MODULE", path.c_str()) == S_OK);
    REQUIRE(object.StringRegister(script) == S_OK && verify(path, true));
    REQUIRE(object.StringUnregister(script) == S_OK);
    REQUIRE(reset());
    REQUIRE(object.AddReplacement(L"MODULE", (L"\"" + path).c_str()) == E_INVALIDARG);
    REQUIRE(object.AddReplacement(L"MODULE", (quoted + L" /Embedding").c_str()) == E_INVALIDARG);
    REQUIRE(object.AddReplacement(L"MODULE_RAW", path.c_str()) == E_INVALIDARG);
    REQUIRE(object.AddReplacement(L"MODULE", quoted.c_str()) == S_OK);
    REQUIRE(object.StringRegister(script) == S_OK && verify(quoted, true));
    REQUIRE(object.StringUnregister(script) == S_OK);
    REQUIRE(object.ResourceRegisterSz(path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK &&
            verify(quoted, false));
    REQUIRE(object.ResourceUnregisterSz(path.c_str(), L"OPENATL_PARAMETERS", L"REGISTRY") == S_OK);
    REQUIRE(reset() && object.StringRegister(script) == S_OK && verify(quoted, true));
    REQUIRE(object.StringUnregister(script) == S_OK && !exists(root, name));
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}

bool scopedResourceRegistry(HMODULE image) {
    // The EXE can explicitly borrow its own GUID-isolated root for testing;
    // it does not inherit a DLL's TLS context or write to default HKCR.
    RegistryRoot root;
    REQUIRE(root.Open());
    using OpenATL::COM::ScopedRgsRegistrationContext;
    {
        ScopedRgsRegistrationContext invalid(HKEY_CURRENT_USER, image,
                                              L"RgsNativeTest");
        REQUIRE(invalid.Status() == E_INVALIDARG);
        ScopedRgsRegistrationContext invalidOwner(root.key, image,
                                                   L"illegal\\owner");
        REQUIRE(invalidOwner.Status() == E_INVALIDARG);
    }
    {
        // An invalid lazy module default must neither open a root nor prevent
        // an explicit isolated installer from selecting its own policy.
        OpenATL::COM::ScopedModuleRgsRegistration module(nullptr, false);
        REQUIRE(!OpenATL::COM::rgs_resource_detail::Current());
        {
            ScopedRgsRegistrationContext context(root.key, image,
                                                  L"RgsNativeTest");
            REQUIRE(context.Status() == S_OK);
            ScopedRgsRegistrationContext nested(root.key, image, L"Nested");
            REQUIRE(nested.Status() == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
            REQUIRE(OpenATL::COM::UpdateRegistryResource(
                IDR_MICROSOFT_CHAIN, TRUE) == S_OK);
            REQUIRE(exists(root.key, L"COMMap.Chain.1"));
            REQUIRE(OpenATL::COM::UpdateRegistryResource(
                IDR_MICROSOFT_CHAIN, FALSE) == S_OK);
            REQUIRE(!exists(root.key, L"COMMap.Chain.1"));
            REQUIRE(exists(root.key, L"CLSID")); // NoRemove container remains.
            REQUIRE(OpenATL::COM::UpdateRegistryResource(
                IDR_MICROSOFT_CHAIN, FALSE) == S_OK);
        }
        REQUIRE(OpenATL::COM::UpdateRegistryResource(
            IDR_MICROSOFT_CHAIN, TRUE) == E_INVALIDARG);
        REQUIRE(OpenATL::COM::UpdateRegistryResource(
            IDR_MICROSOFT_CHAIN, TRUE) == E_INVALIDARG);
    }
    REQUIRE(OpenATL::COM::UpdateRegistryResource(
        IDR_MICROSOFT_CHAIN, TRUE) == E_UNEXPECTED);
    {
        ATL::CAtlModule registrar;
        ScopedRgsRegistrationContext context(root.key, image, L"ModuleResourceTest");
        REQUIRE(context.Status() == S_OK);
        REQUIRE(registrar.UpdateRegistryFromResourceS(IDR_MICROSOFT_CHAIN, TRUE) == S_OK);
        REQUIRE(exists(root.key, L"COMMap.Chain.1"));
        REQUIRE(registrar.UpdateRegistryFromResource(
            MAKEINTRESOURCE(IDR_MICROSOFT_CHAIN), FALSE) == S_OK);
        REQUIRE(!exists(root.key, L"COMMap.Chain.1"));

        ATL::_ATL_REGMAP_ENTRY map[] = {
            {L"KEY", L"OpenATL.ModuleResource"}, {L"FIELD", L"Description"},
            {L"VALUE", L"O'Brien } Other = s 'literal' {"}, {nullptr, nullptr}
        };
        REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, map) == S_OK);
        ATL::CRegKey registered;
        REQUIRE(registered.Open(root.key, map[0].szData, KEY_READ) == ERROR_SUCCESS);
        wchar_t value[1024]{};
        ULONG length = 1024;
        REQUIRE(registered.QueryStringValue(nullptr, value, &length) == ERROR_SUCCESS &&
                std::wstring_view(value) == map[2].szData);
        length = 1024;
        REQUIRE(registered.QueryStringValue(map[1].szData, value, &length) == ERROR_SUCCESS &&
                std::wstring(value) == std::wstring(L"prefix ") + map[2].szData + L" suffix");
        length = 1024;
        REQUIRE(registered.QueryStringValue(L"Module", value, &length) == ERROR_SUCCESS &&
                std::wstring_view(value) == OpenATL::COM::rgs_resource_detail::Current()->modulePath);
        registered.Close();
        REQUIRE(!exists(root.key, L"Other"));
        REQUIRE(registrar.UpdateRegistryFromResource(L"OPENATL_PARAMETERS", TRUE, map) ==
                HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        map[2].szData = L"wrong unregister value";
        REQUIRE(FAILED(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", FALSE, map)));
        REQUIRE(exists(root.key, map[0].szData));
        map[2].szData = L"O'Brien } Other = s 'literal' {";
        REQUIRE(registrar.UpdateRegistryFromResource(L"OPENATL_PARAMETERS", FALSE, map) == S_OK);
        REQUIRE(!exists(root.key, map[0].szData));

        ATL::_ATL_REGMAP_ENTRY duplicate[] = {{L"KEY", L"A"}, {L"key", L"B"}, {nullptr, nullptr}};
        REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, duplicate) == E_INVALIDARG);
        ATL::_ATL_REGMAP_ENTRY unterminated[OpenATL::RGS::MaxReplacements + 1];
        for (auto& entry : unterminated) entry = {L"KEY", L"A"};
        REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, unterminated) ==
                HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
        ATL::_ATL_REGMAP_ENTRY badData[] = {{L"KEY", nullptr}, {nullptr, nullptr}};
        REQUIRE(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE, badData) == E_INVALIDARG);
        REQUIRE(FAILED(registrar.UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", TRUE)));
        REQUIRE(FAILED(registrar.UpdateRegistryFromResourceS(L"missing", TRUE)));
        REQUIRE(FAILED(registrar.UpdateRegistryFromResourceS(IDR_WRONG_RESOURCE_TYPE, TRUE)));
        REQUIRE(registrar.UpdateRegistryFromResourceS(0U, TRUE) == E_INVALIDARG);
        REQUIRE(registrar.UpdateRegistryFromResourceS(65536U, TRUE) == E_INVALIDARG);
        REQUIRE(registrar.UpdateRegistryFromResourceS(static_cast<LPCTSTR>(nullptr), TRUE) == E_INVALIDARG);
        REQUIRE(!exists(root.key, L"A") && !exists(root.key, L"B"));
        registrar.Term();
        REQUIRE(registrar.UpdateRegistryFromResourceS(IDR_MICROSOFT_CHAIN, TRUE) == E_UNEXPECTED);
    }
    REQUIRE(commonResourceReplacements(image, root.key));
    REQUIRE(staticRegistrarScripts(image, root.key));
    REQUIRE(externalRegistrarResources(image, root.key));
    REQUIRE(exeModulePaths(root.key));
    REQUIRE(root.Close());
    return true;
}

bool native(DWORD apartment) {
    Dll dll;
    REQUIRE(dll.Open());
    REQUIRE(verifyResourceDocuments(dll.image));
    REQUIRE(scopedResourceRegistry(dll.image));
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    REQUIRE(typeLibHelpers(dll));
    REQUIRE(typeLibRollback(dll));
#endif
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    REQUIRE(dll.unload() == S_OK);
    REQUIRE(aggregateDll(dll, false));
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    // Direct factory use is independent of COM initialization.
    ATL::CComPtr<IClassFactory> factory;
    REQUIRE(dll.get(CLSID_OpenATLValue, IID_IClassFactory,
                     reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(dll.unload() == S_FALSE);
    REQUIRE(factory->LockServer(TRUE) == S_OK);
    factory.Release();
    REQUIRE(dll.unload() == S_FALSE);
    REQUIRE(dll.get(CLSID_OpenATLValue, IID_IClassFactory,
                     reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(factory->LockServer(FALSE) == S_OK);
    REQUIRE(factory->LockServer(FALSE) == E_UNEXPECTED);
    factory.Release();
    REQUIRE(dll.unload() == S_OK);
    // First class is ordered before the auxiliary class by CLSID. A foreign
    // second ProgID forces failure after first-class installation, which
    // must be rolled back without modifying the foreign key.
    {
        ForeignAuxProgId foreign;
        REQUIRE(foreign.Create());
        REQUIRE(dll.install() == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        REQUIRE(foreign.Check());
        wchar_t guid[40]{};
        REQUIRE(::StringFromGUID2(CLSID_OpenATLValue, guid, 40));
        const std::wstring firstKey = std::wstring(L"Software\\Classes\\CLSID\\") + guid;
        REQUIRE(!exists(HKEY_CURRENT_USER, firstKey.c_str()));
        REQUIRE(foreign.Remove());
    }
    REQUIRE(dll.install() == S_OK);
    dll.installed = true;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    REQUIRE(dll.install() == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));

    HRESULT initialized = ::CoInitializeEx(nullptr, apartment);
    REQUIRE(initialized == S_OK);
    struct ApartmentOwner { ~ApartmentOwner() { ::CoUninitialize(); } } owner;
    REQUIRE(aggregateDll(dll, true));
    REQUIRE(RunAtlAutoAggregateTests(apartment == COINIT_MULTITHREADED));
    REQUIRE(RunAtlErrorInfoTests());
    REQUIRE(autoAggregateDll(dll));
    {
        ATL::CComPtr<IOpenATLValue> eventObject;
        REQUIRE(::CoCreateInstance(CLSID_OpenATLValue, nullptr, CLSCTX_INPROC_SERVER,
            IID_OpenATLValue, reinterpret_cast<void**>(&eventObject.p)) == S_OK);
        ATL::CComPtr<IEnumConnections> snapshot, clone;
        REQUIRE(RunDllConnectionPointTests(eventObject.p, &snapshot.p));
        REQUIRE(snapshot->Clone(&clone.p) == S_OK);
        eventObject.Release();
        ::CoFreeUnusedLibrariesEx(0, 0);
        REQUIRE(dll.unload() == S_FALSE); // Only the enumeration owns the source.
        CONNECTDATA data{};
        REQUIRE(snapshot->Next(1, &data, nullptr) == S_OK && data.pUnk && data.dwCookie);
        ATL::CComPtr<IUnknown> held; held.Attach(data.pUnk);
        ATL::CComPtr<IOpenATLValueEvents> sink;
        REQUIRE(held->QueryInterface(IID_OpenATLValueEvents, reinterpret_cast<void**>(&sink.p)) == S_OK);
        REQUIRE(sink->OnValue(42) == S_OK);
        sink.Release(); held.Release(); snapshot.Release();
        REQUIRE(dll.unload() == S_FALSE); // Clone retains the same snapshot.
        clone.Release();
        ::CoFreeUnusedLibrariesEx(0, 0);
        REQUIRE(dll.unload() == S_OK);
    }
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    REQUIRE(embeddedDispatch(dll));
    REQUIRE(embeddedDispatch(dll, "OpenATLCreateRegisteredDispatch"));
    REQUIRE(exists(HKEY_CURRENT_USER, typeLibKey));
    ATL::CComPtr<ITypeLib> registeredTypeLib;
    REQUIRE(::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT,
        &registeredTypeLib.p) == S_OK && registeredTypeLib.p);
    registeredTypeLib.Release();
#endif
    CLSID resolved{};
    REQUIRE(::CLSIDFromProgID(OpenATLTestProgId, &resolved) == S_OK &&
             IsEqualCLSID(resolved, CLSID_OpenATLValue));
    REQUIRE(::CLSIDFromProgID(OpenATLAuxProgId, &resolved) == S_OK &&
             IsEqualCLSID(resolved, CLSID_OpenATLAuxValue));
    ATL::CComPtr<IOpenATLValue> object;
    REQUIRE(::CoCreateInstance(CLSID_OpenATLValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, reinterpret_cast<void**>(&object)) == S_OK);
    LONG value = 0;
    REQUIRE(object->GetValue(&value) == S_OK && value == 42 && identities(object));
    ATL::CComPtr<IErrorInfo> retainedError;
    REQUIRE(captureDllError(object.p, &retainedError.p));
    ATL::CComPtr<IOpenATLValue> secondObject;
    REQUIRE(::CoCreateInstance(CLSID_OpenATLAuxValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, reinterpret_cast<void**>(&secondObject)) == S_OK);
    REQUIRE(secondObject->GetValue(&value) == S_OK && value == 84);
    REQUIRE(dll.unload() == S_FALSE);
    object.Release();
    secondObject.Release();
    // COM may cache a class factory until unused libraries are collected.
    ::CoFreeUnusedLibrariesEx(0, 0);
    REQUIRE(dll.unload() == S_OK);
    REQUIRE(dll.uninstall() == S_OK);
    dll.installed = false;
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    REQUIRE(absentTypeLibRegistration());
#endif
    void* missing = reinterpret_cast<void*>(1);
    REQUIRE(::CoCreateInstance(CLSID_OpenATLValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, &missing) == REGDB_E_CLASSNOTREG && !missing);
    missing = reinterpret_cast<void*>(1);
    REQUIRE(::CoCreateInstance(CLSID_OpenATLAuxValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, &missing) == REGDB_E_CLASSNOTREG && !missing);
    REQUIRE(dll.Close());
    REQUIRE(::GetModuleHandleW(L"openatl_com_server_fixture.dll") == nullptr);
    REQUIRE(dllErrorInfoFields(retainedError.p));
    retainedError.Release();
    return true;
}
} // namespace

#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
bool RunExeTypeLibTests() { return exeTypeLibRegistration<false>() && exeTypeLibRegistration<true>(); }
#endif

int main(int argc, char** argv) {
    if (argc == 4 && ::_stricmp(argv[3], "-Embedding") == 0) {
        if (std::strcmp(argv[1], "--module-server-sta") == 0) return RunAtlModuleServer(argv[2], true);
        if (std::strcmp(argv[1], "--module-server-mta") == 0) return RunAtlModuleServer(argv[2], false);
    }
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    if (argc == 3 && std::strcmp(argv[1], "--commap-tlb") == 0) {
        if (!RunOriginalCommapTypeLibTest(argv[2])) return 1;
        std::puts("OPENATL_ORIGINAL_COMMAP_MIDL_OK source=1 typelib=1 iids=1 dispids=1 register=0");
        return 0;
    }
#endif
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "--module") == 0) {
        if (!RunAtlModuleTests()) return 1;
        std::puts("ATL_MODULE_OK locks=1 termination=1 com_balance=1 class_objects=1 loop=1");
        return 0;
    }
    if (std::strcmp(argv[1], "--core") == 0) {
        ULONG_PTR token = 0;
        if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED ||
            !core() || !RunAtlDispatchTests() || !registry() || !RunRgsParserTests() ||
            !RunRgsRegistryTests() ||
            ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
        std::puts("OPENATL_COM_SERVER_CORE_OK identity=1 factory=1 locks=1 concurrent=1 rollback=1 registry=1 no_com=1");
        return 0;
    }
    const bool sta = std::strcmp(argv[1], "--sta") == 0;
    if (!sta && std::strcmp(argv[1], "--mta") != 0) return 2;
    if (!native(sta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED)) return 1;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    std::printf("OPENATL_COM_SERVER_NATIVE_OK mode=%s activation=1 identity=1 locks=1 unregister=1 unload=1\n",
                 sta ? "sta" : "mta");
    return 0;
}
