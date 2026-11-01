# ATL source compatibility — COM Client and COM Server

**OpenATL targets ATL source compatibility for both COM Client and COM
Server applications**, together with Win32/WTL. The current implementation
qualifies a subset; missing APIs are implementation backlog, not permanently
excluded. Microsoft's internal ATL ABI/layout is not currently claimed
compatible. See [the project charter](PROJECT_SCOPE.md).
Consumers link only `OpenWTL::WTL`; it exposes the complete patched repository WTL frontend and private ATL/COM backend.

The acceptance target is now all official WTL samples and all pure ATL
Microsoft samples, unchanged on both MSVC and MinGW, with no MFC dependency.
The implemented subset below is the current state, not a restriction on that
target. Missing standard headers and APIs remain compatibility gaps; see the
[full sample inventory and build results](OFFICIAL_SAMPLES.md).

| Header/API | Supported contract |
| --- | --- |
| `atlbase.h`, `atlcomcli.h` | Existing Win32 modules, threading policies, COM smart pointers, BSTR/VARIANT helpers |
| `atlwin.h`, `atlstr.h`, utility headers | Existing documented Windows/WTL subset; Unicode-only, C++17 |
| `atlhost.h`, `atliface.h` | Windowed OLE containment, `CAxWindow`/`CAxWindow2`, site/ambient interfaces and explicit control creation; see [ownership contract](ACTIVEX_HOST.md) |
| `atlcom.h` / `atlctl.h` | `AtlAdvise`/`AtlUnadvise`, `IDispEventSimpleImpl`, typed `SINK_ENTRY_INFO` maps |
| `atlbase.h`: `CAtlModule`, `CAtlModuleT`, `CAtlExeModuleT` | Module locks, termination callbacks, balanced EXE COM startup and automatic/explicit native class-factory registration. EXE activation shares the DLL factory engine and cookie rollback; explicit empty tables opt out. Common/EXE persistent RegisterServer/UnregisterServer share the automatic class map, CLSID selection, resource context and TypeLib preflight/rollback. RegisterAppId/UnregisterAppId use the derived resource hook; DLL exports/EXE registration commands order AppID and class work with one captured policy and rollback. Per-user commands enable the image-local mode and retain the TRUE TypeLib request; incompatible enclosing policies are rejected. EXE operations validate thread/initialization/termination. Existing tests cover HKCU LocalServer32 SCM launch, STA/MTA servers and native IDispatch/IPersist marshaling across processes. AppID security policy, arbitrary custom-interface marshaling and idle shutdown remain pending; see [module protocol](ATL_MODULE.md). |
| `atlbase.h`: `CAtlDllModuleT<T>` | Real DLL exports and one `OpenATL::COM::Module` engine. Supports bound tables and static `OBJECT_ENTRY_AUTO` discovery, plus class-directed `UpdateRegistry(BOOL)` callbacks with ordered rollback. Supplies a lazy RGS context using one captured per-user mode, with explicit context overrides. TypeLib=TRUE preloads the own-image TypeLib, registers it through OLEAUT32 after classes, and rolls class callbacks back if TypeLib registration fails. Automatic ProgID inference remains pending. |
| `OBJECT_ENTRY_AUTO(clsid, type)` | Static per-image class map, built-in class-declared creator policy and optional typed `UpdateRegistry(BOOL)` callback, bounded duplicate/invalid CLSID rejection, real `IClassFactory`/object/module-lock ownership and native STA/MTA activation. No COM, registry I/O or heap allocation in static constructor. |
| `DECLARE_NO_REGISTRY()` | Explicit ATL class opt-out of persistent registration; absent `UpdateRegistry` without this opt-out fails `E_NOTIMPL`. |
| `DECLARE_REGISTRY_RESOURCEID(id)`, `DECLARE_REGISTRY_RESOURCE(name)` | Class-static `UpdateRegistry(BOOL)` loads a real numeric/named `REGISTRY` resource, parses a bounded ATL RGS subset and executes the owner-verified plan. The active ATL module supplies the own-image/root context; explicit scoped contexts override it. Without an ATL module, standalone callbacks require an explicit context. |
| `DECLARE_REGISTRY_APPID_RESOURCEID(id, "{GUID}")` | Module-static GetAppId/UpdateRegistryAppId hook, validated non-null GUID and actual resource execution through the same common replacements/RGS engine. Value-free top-level NoRemove AppID is shared; GUID/EXE-alias child keys remain owned and verified. An undeclared AppID explicitly reports S_FALSE/no work; security policy and service registration remain unqualified. |
| `CAtlModule::UpdateRegistryFromResourceS`, `UpdateRegistryFromResource`, `_ATL_REGMAP_ENTRY` | Standard numeric/name overloads and bounded, owned sentinel replacement maps feed the same static registrar. Existing operation mode/root is retained through callbacks; direct calls establish a lazy default context. Key/name/string substitutions are data, with no recursive `%` expansion or grammar injection. Common replacement hooks feed this same path; dynamic registrar APIs remain pending. See [limits](ATL_MODULE.md). |
| `IRegistrarBase`, `CAtlModule::AddCommonRGSReplacements` | Published base IID/vtable, real QI/refcounts and a virtual common-map hook in static resource registration. Owned additions/clears obey the same map limits. Hook exit freezes or closes mutation on all paths; retained interfaces pin the module until Release. The hook capability rejects extended IRegistrar QI. Callback failures/exceptions propagate before default-root writes. |
| `IRegistrar`, `AtlCreateRegistrar`, `statreg.h` / `CRegObject` | Built-in owned COM registrar and static facade for string/file/PE-resource registration and unregistration. External PE resources are mapped without DLL execution; persistent MODULE bindings and maps are snapshotted for each operation. Automatic MODULE quotes EXE paths; MODULE_RAW and owner paths stay raw. Explicit bindings retain the caller's quote choice. The same parser/executor and ownership checks apply. Retained QI interfaces outlive the facade and pin the active ATL module. CRegObject inheritance ABI, CLSID_Registrar activation and dynamic APIs remain pending; see [limits](ATL_MODULE.md). |
| `IDispatchImpl<T, &IID, &LIBID, major, minor, holder>`, `CComTypeInfoHolder` | Real Windows type-info holder using `LoadRegTypeLib` and `GetTypeInfoOfGuid`; four IDispatch methods and `ITypeInfo::GetIDsOfNames/Invoke` delegation, with owned temporary pointers and no static type-info cache. Native OLEAUT32 tests cover real late binding. `GetTIFromFile` additionally loads **actual binary .tlb files** via `LoadTypeLibEx(REGKIND_NONE)`, verifies GUID/version/IID and exposes owned ITypeInfo without registry writes. Image TLB mode `0xffff/0xffff` loads this binary image with `REGKIND_NONE`, verifies LIBID/IID, and supports real dispatch over an embedded TypeLib. The image path is bounded to 8192 UTF-16 units; per-user registered TypeLib loading and real dispatch have native tests; machine-wide permission behavior remains a separate acceptance boundary. |
| `AtlLoadTypeLib`, `AtlRegisterTypeLib`, `AtlUnRegisterTypeLib`, `AtlGetPerUserRegistration`, `AtlSetPerUserRegistration` | Standard global signatures, owning BSTR/ITypeLib outputs, bounded numeric resource index, own-image loading, and native system/per-user OLEAUT32 registration. Per-user mode applies to helpers and module TypeLib/RGS work. EXE STA/MTA command tests now register the real embedded COMMap TypeLib, verify its EXE path and registered/embedded dispatch, and exercise native DACL-denial class/AppID rollback and cleanup. |
| `DECLARE_LIBID(id)` | Original ATL module `InitLibId()` pattern initializes `CAtlModule::m_libid` at module construction. A native derived DLL module test checks actual GUID identity and avoids type-library registration side effects. |
| `atlcom.h`: `CComObjectRootBase`, `CComObjectRootEx`, `CComObjectRoot` | Nonaggregated reference counting, thread-model critical section, construction/release hooks |
| `CComObject<T>` | Concrete IUnknown, raw creation with initial reference count **zero**, typed QI, last-release destruction |
| `CComCoClass<T, &CLSID>`, `CComCreator<Object>`, `CComCreator2` | Default aggregatable creator selection, CLSID accessor, owned local creation and class-declared nonaggregated/aggregate-only policies. Explicit CComCreator<CComObject<T>> remains nonaggregated. |
| `CComContainedObject<T>`, `CComAggObject<T>` | Delegating contained interfaces and separate nondelegating inner IUnknown, zero-ref raw creation, construction failure cleanup and module pins through contained destruction. The outer identity is borrowed, not retained cyclically. |
| `DECLARE_AGGREGATABLE`, `DECLARE_NOT_AGGREGATABLE`, `DECLARE_ONLY_AGGREGATABLE` | Built-in creator policies honored by direct CComCoClass creation and OBJECT_ENTRY_AUTO factories; aggregate factory creation requires IID_IUnknown. Custom factory policies and CComPolyObject remain unqualified. |
| `COM_INTERFACE_ENTRY_AGGREGATE`, `COM_INTERFACE_ENTRY_AGGREGATE_BLIND` | Delegate to an explicitly owned inner IUnknown, preserve the outer canonical identity and one owning QI reference, including through chained maps. Automatic aggregate creation and tear-offs remain pending. |
| `DECLARE_GET_CONTROLLING_UNKNOWN`, `OuterQueryInterface` / `OuterAddRef` / `OuterRelease` | Explicit outer identity access/delegation; no automatic COM initialization. |
| `BEGIN_COM_MAP` / `END_COM_MAP` | First simple entry supplies canonical IUnknown; typed pointer adjustments |
| `COM_INTERFACE_ENTRY`, `COM_INTERFACE_ENTRY_IID` | Interface type or explicit IID mapping |
| `COM_INTERFACE_ENTRY2`, `COM_INTERFACE_ENTRY2_IID` | Select the inheritance branch for an ambiguous base interface |
| `COM_INTERFACE_ENTRY_CHAIN(Base)` | Chain a base ATL COM map without double AddRef; preserve outer-first canonical `IUnknown` or use the base identity for a chain-only map. Native MI/QI/identity tests cover both forms. |
| `DECLARE_PROTECT_FINAL_CONSTRUCT` | Retain/release the construction guard reference |
| `CComModule` / WTL `CAppModule::Init` | Legacy object-map initialization, optional LIBID metadata, factory/cookie registration and object-main hooks are implemented. Full legacy module/registration API compatibility is still incomplete. |
| `atlconv.h`: `CA2WEX<N>`, `CW2AEX<N>`, `CA2W`, `CW2A` | Object-owned inline/heap buffers, explicit code page and default thread ACP, public buffer members and Unicode `CA2T`/`CT2A` aliases. Strict malformed/lossy conversion throws; see [encoding differences](UNICODE.md). |
| `openatl/com_server.h` / `com_registration.h` | Explicit in-process COM Server, IClassFactory, module/server locks, CLSID/ProgID registration and Windows STA/MTA activation (implemented subset) |

Reference-count and ownership semantics follow the corresponding ATL APIs.
`CComObject<T>::CreateInstance(&raw)` returns zero references: immediately
`AddRef`, or obtain an owned interface through `QueryInterface`, then balance
every successful acquisition. `CComCoClass::CreateInstance` returns an owned
interface with one reference. `GetUnknown()` is borrowed; QI returns ownership.
Unsupported IIDs clear output and return `E_NOINTERFACE`; null outputs return
`E_POINTER`. A temporary reference protects QI/Release during `FinalConstruct`.
HRESULT failure or exceptions destroy partial objects and call `FinalRelease`;
a throwing constructor does not call it. `CAtlException` preserves a failure
HRESULT, allocation failure maps to `E_OUTOFMEMORY`, other exceptions to
`E_UNEXPECTED`. Destructors and `FinalRelease` must not throw.

Thread models have their usual separation: `CComSingleThreadModel` requires
external serialization; `CComMultiThreadModel` synchronizes the reference
count and provides explicit `Lock`/`Unlock`. This does not marshal interfaces
or synchronize application fields automatically.

When an application declares a `CAtlModule`-derived module, `CComObject`
automatically retains it through destruction. Without one, local creation is
independent of a module. The separate `OpenATL::COM::Module` lifetime still
requires its explicit creator overload for a DLL class table:

```cpp
const OpenATL::COM::ClassEntry classes[] = {
    {CLSID_MyObject, &ATL::CComObject<MyObject>::CreateInstance}
};
OpenATL::COM::Module module(classes, 1);
```

An object class uses the usual root + interfaces + COM-map pattern; it cannot
be `final` because `CComObject<T>` derives from it. The class may also inherit
`CComCoClass<T, &CLSID_MyObject>`. DLL exports and registration use the existing
[explicit server API](COM_SERVER.md). Direct local creation does not acquire a
DLL module reference; a DLL must use the module-aware creator or independently
retain its module across locally created objects and external callers.

The source-owned `openatl/rgs_parser.h` accepts a bounded, read-only
subset of actual Microsoft ATL RGS grammar, validated against four
untouched resources. The opt-in `openatl/rgs_registration.h` executes
these plans under an explicitly opened caller-owned Classes root using
the existing owner-verified registry plan/inspection machinery, with
fail-closed `ForceRemove`, reverse rollback and strict unregistration
preflight. The numeric and named resource macros support genuine Win32
`REGISTRY` resources with strict encoding, default module contexts and
explicit scoped owner/root overrides. Module static helpers additionally
support bounded sentinel replacement maps. **Full RGS grammar, complete standalone
registrar APIs and machine-wide registration acceptance remain incomplete.**
This isolated native admission does not qualify the original ATL applications'
complete compile/link/activation. See [server registration](COM_SERVER.md).

Basic aggregation is exercised through raw inner creation, explicit engine
factories and the real auto-map DLL. Tests distinguish nondelegating inner
IUnknown from the outer identity, exact QI reference ownership, failed
construction/exception cleanup, class policies and factory-independent module
pins. The DLL is also activated through native CoCreateInstance with a valid
controlling outer in both STA and MTA. This is in-process aggregation; remote
aggregation and the complete original COMMap application are not qualified.

Original Microsoft COMMap `COMMap.idl` is pinned and submitted to native
Windows SDK `midl.exe` on Windows CI. The generated binary TypeLib,
interface header and IID implementation are checked through actual OLEAUT32
loading and native compilation. The same MIDL output is embedded in the existing native DLL fixture. Its
IChain implementation uses standard 0xffff/0xffff dispatch for real property
Invoke in STA/MTA. This is original-IDL and embedded-dispatch admission, not
the whole original COMMap DLL build/link or any of the 37 pure-ATL projects passing.

**Tracked compatibility gaps:** automatic aggregate creation, cached/uncached
tear-offs, CComPolyObject and custom factory policies,
full RGS grammar and standalone registrar APIs, complete CAtlComModule/legacy TypeLib API coverage and machine-wide registration acceptance,
connection-point server templates, SAFEARRAY wrappers, windowless
hosting, resource-based ActiveX dialogs and complete control/server support.
`CComModule` has an implemented legacy object-map subset; its presence is
not a claim that complete legacy ATL applications already pass. Windowed containment and typed simple
event sinks are implemented. Missing APIs must not claim fake success.
Real COM Server support already covers explicit class factories, module
locks, registration and native STA/MTA activation. No claim of complete
application compatibility is made yet.

Existing native COM core tests cover zero-ref creation, typed/IID queries,
multiple inheritance, canonical identity, construction failure and cleanup,
threaded references, single-thread policy, and explicit module pin retirement.
The pinned community WTL SDI fixture builds and runs without source patches, including
its existing `<atlcom.h>` include and `Init(nullptr, hInstance)` call.
It is not an official sample. [Official sample coverage](OFFICIAL_SAMPLES.md)
records separate WTL GuidGen/MemDlg tests and the gaps observed in Microsoft's
COMMap, ATLCollections and ATLSafeArray sources. The separate
[RATL sample admission](RATL_SAMPLES.md) records three unchanged applications
passing native behavior tests against OpenATL, plus the module API coverage.

Public behavior references:

- [CComObject](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomobject-class)
- [CComObjectRootEx](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomobjectrootex-class)
- [COM map entries](https://learn.microsoft.com/en-us/cpp/atl/reference/com-interface-entry-macros)
