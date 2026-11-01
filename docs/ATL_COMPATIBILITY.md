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
| `atlsafe.h`: `CComSafeArray<T, VARTYPE>`, `CComSafeArrayBound` | Native OLEAUT32 array creation, deep copy, attach/detach, checked borrowed element access, copied/transferred resource elements, append, multidimensional access and resize. Explicit destruction preserves ownership on native failure. See SAFEARRAY scope below. |
| `atlctl.h`: `IObjectSafetyImpl<T, flags>` | Native IObjectSafety QI and supported/enabled flags, masked updates, unsupported-interface/mask failures and public m_dwCurrentSafety state. No COM initialization or security validation is implied. |
| `atlctl.h`: property notifications | `IPropertyNotifySinkCP<T, CDV>` and `CFirePropNotifyEvent::FireOnChanged/FireOnRequestEdit`, with native sink QI across all points, edit vetoes, preflight/snapshot ownership and reentrant callbacks. See property-notification scope below. |
| `atlcom.h` / `atlctl.h` | `AtlAdvise`/`AtlUnadvise`, `IDispEventSimpleImpl`, typed `SINK_ENTRY_INFO` maps |
| `atlcom.h`: `IDispEventImpl` | Native registered/source-provided TypeLib metadata, `SINK_ENTRY`/`SINK_ENTRY_EX`/`SINK_ENTRY_EX_P`, independent sink identities, transactional binding and typed invocation. See TypeLib event scope below. |
| `atlcom.h`: server connection points | `IConnectionPointImpl`, `IConnectionPointContainerImpl`, connection-point maps (including `_P`), `CComDynamicUnkArray`/`CComUnkArray`, native `IEnumConnections`/`IEnumConnectionPoints` snapshots and clones. See connection-point scope below. |
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
| `CComCoClass::Error`, `AtlReportError` | Six text/ANSI/resource overloads each; native ICreateErrorInfo/IErrorInfo publication, IID/description/help/source metadata and standard default/explicit HRESULT mapping. Resource text uses the selected image without fixed-buffer truncation. See error reporting below. |
| `ISupportErrorInfoImpl<&IID>` | Native ISupportErrorInfo declaration with S_OK for the declared IID and S_FALSE otherwise; ordinary and cached tear-off error views share the owner identity. |
| `CComCoClass<T, &CLSID>`, `CComCreator<Object>`, `CComCreator2` | Default aggregatable creator selection, CLSID accessor, owned local creation and class-declared nonaggregated/aggregate-only/poly policies, including the typed outer overload. Explicit CComCreator<CComObject<T>> remains nonaggregated. |
| `CComContainedObject<T>`, `CComAggObject<T>` | Delegating contained interfaces and separate nondelegating inner IUnknown, zero-ref raw creation, construction failure cleanup and module pins through contained destruction. The outer identity is borrowed, not retained cyclically. |
| `DECLARE_AGGREGATABLE`, `DECLARE_NOT_AGGREGATABLE`, `DECLARE_ONLY_AGGREGATABLE`, `DECLARE_POLY_AGGREGATABLE` | Built-in creator policies honored by direct CComCoClass creation and OBJECT_ENTRY_AUTO factories; aggregate factory creation requires IID_IUnknown. Poly uses one CComPolyObject wrapper in both modes. Custom factory policies remain unqualified. |
| `CComPolyObject<T>` | One wrapper and public CComContainedObject<T> member for both modes. Nonaggregated public interfaces delegate to the wrapper IUnknown; aggregated public interfaces delegate to the borrowed outer, while private IUnknown owns the allocation. Zero-ref raw creation, protected construction, failure cleanup and existing module pins are retained. |
| `COM_INTERFACE_ENTRY_AGGREGATE`, `COM_INTERFACE_ENTRY_AGGREGATE_BLIND` | Delegate to an explicitly owned inner IUnknown, preserve the outer canonical identity and one owning QI reference, including through chained maps. |
| `COM_INTERFACE_ENTRY_AUTOAGGREGATE`, `COM_INTERFACE_ENTRY_AUTOAGGREGATE_BLIND` | Lazy native in-process CoCreateInstance with the controlling outer and IID_IUnknown. Serialized slot creation, activation failure/retry, retained inner after an unsupported IID, same-slot reentrancy rejection and blind fallback through the map. Caller initializes COM and releases the private slot in FinalRelease. |
| `CComTearOffObjectBase`, `CComTearOffObject`, `CComCachedTearOffObject` | Ordinary tear-offs retain the owner until their own last release; cached private IUnknown is owner-held and public interfaces retain the controlling owner. Supports ordinary and aggregated owner subobjects without layout casts. See the typed-owner limitation below. |
| `COM_INTERFACE_ENTRY_TEAR_OFF`, `COM_INTERFACE_ENTRY_CACHED_TEAR_OFF` | Lazy ordinary instances and serialized cached publication, one QI reference and canonical owner identity, construction failures/retry and same-entry reentrancy rejection. Cached slots must be released by the owner's FinalRelease. |
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

**Tracked compatibility gaps:** exact Microsoft tear-off member/layout contracts and custom factory policies,
full RGS grammar and standalone registrar APIs, complete CAtlComModule/legacy TypeLib API coverage and machine-wide registration acceptance,
broader event-provider/signature compatibility, complete SAFEARRAY internal-wrapper/layout compatibility, windowless
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

## Tear-off source-contract boundary

CComTearOffObjectBase exposes m_pOwner as the actual typed Owner* subobject,
where Microsoft documents CComObject<Owner>*. This preserves owner-member
access and works safely for both CComObject and CComContainedObject owners;
it does not fabricate a CComObject pointer for an aggregated owner. Code that
requires the exact Microsoft member pointer type or object layout is not
qualified. The opaque void* constructors/creator helpers expect an actual
CComObject<Owner>*; map entries use explicit typed owner/control arguments
and do not depend on that cast.

Cached initialization uses the owner's existing Lock/Unlock policy. The
owner must remain externally referenced, SingleThreadModel access must stay
serialized, and user construction hooks must not wait for another thread
whose QI is blocked on the same owner lock. Same-thread construction cycles
fail E_UNEXPECTED before a second allocation (maximum nested construction
depth 64); failures leave a null cache so later requests can retry. Owners
release the private cached IUnknown during FinalRelease after external calls
retire. Concurrent manual replacement of the cache slot is not supported.


## Automatic aggregate activation

Auto-aggregate map entries use native CoCreateInstance with CLSCTX_INPROC_SERVER,
the outer controlling IUnknown and IID_IUnknown. They do not initialize COM or
substitute a local creator when activation fails. A populated IUnknown* or
CComPtr<IUnknown> slot is reused. Successful activation publishes one private
reference before the requested QI; a missing interface therefore leaves the
inner cached, while failed activation leaves the slot null for a later retry.

The owner's lock serializes initialization and QI; external calls must retain
the owner, SingleThreadModel use must be serialized, and activation hooks must
not wait on another thread blocked on that owner lock. Same-thread recursive
creation of the same slot fails E_UNEXPECTED before another activation, using
the same bounded construction guard as tear-offs. The owner releases the slot
in FinalRelease. Concurrent manual slot replacement is outside the contract.
Specific entries propagate failures; blind entries continue through the map
after any failed activation/interface query. IUnknown never triggers lazy
creation and always remains the owner's canonical identity.


## Rich COM error information

CComCoClass::Error forwards its class CLSID to AtlReportError. Both expose
wide/ANSI text, text with help metadata, resource ID and resource ID with help
metadata overloads. Text defaults to DISP_E_EXCEPTION; resource IDs default to
MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, id). A nonzero explicit HRESULT is
returned unchanged, including success statuses. Native ICreateErrorInfo stores
the supplied IID, copied description, help context/file and registered ProgID
source when ProgIDFromCLSID can resolve it. Missing registration leaves the
source empty; no substitute source or registry write is introduced.

The helpers replace the calling logical thread's native error object and clear
stale information if metadata preparation fails. Metadata is best effort and
does not replace the documented returned HRESULT. ANSI strings use Windows
CP_ACP. Resource IDs use the selected/default _AtlBaseModule resource image;
LoadStringW's pointer/length form is copied into an owned BSTR. Invalid/missing
resources assert in debug builds and report `Unknown Error.` in release builds.

ISupportErrorInfoImpl advertises one supported IID; clients retrieve native
GetErrorInfo immediately after a failed call. GetErrorInfo transfers ownership
and clears that thread's pending error. This does not marshal error objects,
initialize an application apartment, or guarantee propagation across arbitrary
proxy calls/message loops. Existing tests cover the twelve public overloads,
metadata ownership, replacement, consumed state and native thread isolation;
actual DLL resource errors survive object release, unregistration and DLL
unload. Existing TypeLib cases verify a real IOuter Invoke failure and its
native EXCEPINFO description, help metadata and HRESULT. Arbitrary Automation
argument conversion, remote error propagation and all original COMMap sources
remain separate acceptance work.


## Repeated automatic object-map declarations

OBJECT_ENTRY_AUTO uses a shared per-type identity to coalesce identical
CLSID/type/creator/registrar records declared in multiple translation units.
This is needed by the unchanged COMMap Chain/Outer and aggreg headers. The
metadata list remains allocation-free during static construction. Untagged
explicit records and conflicting type/callback records are not coalesced;
the existing module duplicate-CLSID validation still rejects conflicts.

The original six class bodies are additionally compiled/executed by the
existing STA events case; see OFFICIAL_SAMPLES.md. This qualifies their used
API subset, not complete original DLL module/resource/deployment or full ATL
layout. The same existing case now admits six original RGS callbacks in an
isolated HKCU root and both real per-user TypeLib registrations. The unchanged
registered-mode IDispatchImpl executes IOuter/IChain and all four aggregates;
independent unregister rejects fresh lookups with no image fallback. Original
tear-off IIDs omitted from the TypeLib remain TYPE_E_ELEMENTNOTFOUND while the
library is registered. This does not qualify full original DLL deployment.


The original COMMap/Aggreg module .cpp and .rc inputs are also compiled into
two existing DLLs. The existing STA events case qualifies their original
registration exports, per-user paths, native activation/registered dispatch,
interface image ownership, cached-owner retirement and actual DLL unmapping.
This uses the existing production module/RGS/TypeLib engines unchanged.
Untouched original project builds and exact binary/type layouts remain open.


## Poly aggregation creator policy

CComPolyObject<T> and DECLARE_POLY_AGGREGATABLE select the same wrapper for both
aggregated and nonaggregated creation. The wrapper owns its private IUnknown
and counter; m_contained is the public CComContainedObject<T> member in both
cases. With no outer, contained interfaces delegate to the wrapper. With an
outer, they delegate to that borrowed identity while private IUnknown remains
independent. Aggregate factory creation accepts only IID_IUnknown. Raw creation
starts with zero references. The typed CComCoClass outer overload uses the
same class creator policy as the explicit-IID overload.

The existing core case covers exact member/creator types, both identity/refcount
modes, temporary public references during FinalConstruct, failure HRESULTs,
exceptions/allocation failure, unsupported interfaces, concurrent references,
factory retirement and ordinary/cached tear-offs on real contained owners.
The existing registered Value DLL uses the poly macro, so its native STA/MTA
activation, aggregation, methods, error metadata and unload cases use this
policy. Targets, modes, workflows and stdout markers remain unchanged.
Exact ATL binary layout and the documented tear-off m_pOwner type difference
remain separate; no CComObject pointer is fabricated for a contained owner.

Public contracts: [CComPolyObject](https://learn.microsoft.com/en-us/cpp/atl/reference/ccompolyobject-class),
[aggregation macros](https://learn.microsoft.com/en-us/cpp/atl/reference/aggregation-and-class-factory-macros),
and [CComCoClass](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomcoclass-class).


## Native SAFEARRAY and original ArrayManager

CComSafeArray<T, VARTYPE> owns a genuine OLEAUT32 SAFEARRAY and one native lock
for stable borrowed GetAt references. Create/Attach/CopyFrom validate element
VARTYPE and size. Native bound/index rules support negative lower bounds,
zero-length arrays and multidimensional Get/Set/Resize. CComSafeArrayBound
exposes SAFEARRAYBOUND fields and standard count/lower/upper helpers.
Supported native types include integer/floating types, CY/DECIMAL, BSTR,
VARIANT, IUnknown* and IDispatch*. VT_BOOL/DATE/ERROR can be selected explicitly
with a correctly sized native representation. Record arrays are not admitted.

SetAt/Add copy BSTR/VARIANT values by default; bCopy=FALSE transfers caller-owned
resources only on success. Interface elements acquire native AddRef ownership.
Append stages inputs before resizing, including copied self-appends; failed
resize preserves both destination and no-copy caller ownership. Destroy and
replacement retain the old array when native external locks prevent release.
Detach removes only the wrapper's lock. External locks must balance before
destruction; a failed destructor cleanup terminates as an ownership violation.
GetAt/operator[] expose borrowed native T references; use SetAt for owning
resource updates. Internal _ATL_TYPEWRAPPER aliases and exact Microsoft layout
are not qualified. GetSafeArrayPtr is an output receiver for an empty wrapper;
callers must not overwrite or independently free an owned m_psa.

CComVariant(SAFEARRAY*) makes a native deep copy and owns the VT_ARRAY result;
a null array produces VT_EMPTY. IObjectSafetyImpl exposes its template-declared
supported/current flags and validates interfaces through real QI. Unsupported
masks fail without changing state; enabled bits outside the mask are ignored.
It implements the flag protocol; the application owns its actual safety policy.

The existing no-COM core case exercises native type/size metadata, bounds,
locks, resize/destroy/replacement failure, resource transfer/copy, interface
references and multidimensional behavior against OLEAUT32. The existing STA
events case additionally compiles unchanged Microsoft ArrayManager.cpp/.h and
its IDL through native MIDL. Real class factories/CoCreateInstance, original
RGS callbacks, IObjectSafety and registered GetTypeInfo/GetIDsOfNames/Invoke
execute Add/Change/Remove/GetArray. Returned snapshots survive class/module
retirement. Full original DLL/project deployment and all Automation/record/
internal-wrapper contracts remain separate.

References: [CComSafeArray](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomsafearray-class),
[CComSafeArrayBound](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomsafearraybound-class),
[IObjectSafetyImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iobjectsafetyimpl-class).

## Server connection points

IConnectionPointImpl<T, &iid, CDV> and IConnectionPointContainerImpl<T> expose
native IConnectionPoint/IConnectionPointContainer interfaces through standard
BEGIN_CONNECTION_POINT_MAP, CONNECTION_POINT_ENTRY, CONNECTION_POINT_ENTRY_P
and END_CONNECTION_POINT_MAP declarations. An embedded point face has its own
IUnknown identity and delegates lifetime to its actual ordinary/contained owner.
GetConnectionPointContainer uses real owner QI, including aggregation's outer.
FindConnectionPoint returns stable adjusted point interfaces by outgoing IID.

Advise queries the exact outgoing interface and owns that reference until
Unadvise or owner destruction. Failed Advise clears the cookie; missing sink
interfaces, fixed-capacity limits and invalid disconnects return actual errors.
Owner Lock/Unlock serializes connection changes. Sink QI and the final Release
of a disconnected sink occur outside that lock. Standard STDMETHOD overrides
of point methods are dispatched through the native face; thrown ATL/allocation/
unexpected exceptions from throwing C++ overrides become HRESULT failures at
that COM boundary. Standard STDMETHOD overrides must honor the SDK's nothrow
annotations and return failures as HRESULTs. The public m_vec supports
generated-proxy-style indexed/range access; callers must use the owner's lock
and retain interfaces before invoking them outside the lock. Firing policy is
the proxy/application's responsibility; these templates do not invent events.

CComDynamicUnkArray and CComUnkArray<N> store borrowed outgoing pointers, with
nonzero 32-bit slot cookies independent of pointer width. Removing a slot keeps
other live cookies stable; vacant slots may be reused. GetSize reports allocated
slots, including holes. Array methods do not AddRef/Release; connection points
manage those references. GetCookie accepts a slot address, or finds the first
matching value for a separately supplied pointer. Do not independently mutate
an owning point's m_vec behind its connection-management methods.

Both native enumerators own immutable snapshots with independent Next/Skip/
Reset/Clone cursors. Each returned pointer carries a caller-owned reference.
Snapshots retain sinks/points and the originating owner/image even after
Unadvise and client release. Native core tests cover maps, duplicate sinks,
capacity/failure cleanup, interface pointer adjustment, partial enumeration,
clones, callback/release reentrancy, typed sink interoperation and concurrent
direct free-threaded C++ use without COM initialization. Existing registered
Value DLL STA/MTA and aggregated cases exercise actual outgoing vtable calls;
snapshot/clone retirement gates DllCanUnloadNow on the existing module path.

The implementation uses typed embedded faces rather than Microsoft's internal
layout. Internal locator/map metadata, exact inheritance/binary layouts,
complete TypeLib event-provider/signature compatibility,
cross-apartment marshaling of custom interfaces and unchanged original event
proxy/full-project admission remain unqualified. No target, test mode, export
or workflow is added. All 36 pinned original Microsoft blobs remain unchanged.

References: [IConnectionPointImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iconnectionpointimpl-class),
[IConnectionPointContainerImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/iconnectionpointcontainerimpl-class),
[connection-point macros](https://learn.microsoft.com/en-us/cpp/atl/reference/connection-point-macros),
[CComDynamicUnkArray](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomdynamicunkarray-class),
[CComUnkArray](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomunkarray-class).

## Property notifications

IPropertyNotifySinkCP<T, CDV> exposes the native property-notification outgoing
interface through the same connection-point implementation. Its default
CComDynamicUnkArray or fixed CComUnkArray<N> policy retains ordinary Advise,
Unadvise, map lookup and enumeration behavior.

CFirePropNotifyEvent takes a sender IUnknown and DISPID. Both static helpers
enumerate every connection point and its native connections, then QI each
connected face for IPropertyNotifySink. A sink connected under another outgoing
IID is included; duplicate subscriptions are separate notification targets.
A valid sender without a connection-point container, an empty map or an empty
set of interested sinks returns S_OK. A null sender returns E_POINTER. Native
QI/enumeration failures propagate; only E_NOINTERFACE denotes an uninterested
source/sink. Malformed successful enumeration/QI results fail E_UNEXPECTED.

Connection snapshots and all target interfaces are collected before callbacks.
They retain source/image and sink ownership while callbacks unsubscribe, add
subscribers, release the caller's sender reference or reenter notification.
No owner lock is held across property callbacks. Subscribers added during a
callback join the next notification; a disconnected target in the current
snapshot remains valid for that notification. Each helper call captures its
own set; no atomic transaction across concurrent edits or connection points is
claimed. QI/enumeration/allocation preflight failures cause no notifications.

FireOnRequestEdit stops at the first result other than S_OK, preserving S_FALSE
as an edit veto as well as propagating errors. The setter owns the decision to
commit and must test hr != S_OK. FireOnChanged notifies every collected target;
callback return values are advisory, matching IPropertyNotifySink's requirement
to return S_OK even when it cannot act. DISPID_UNKNOWN and ordinary DISPIDs are
passed unchanged. These helpers do not validate or modify property values,
initialize COM, or implement CComControl/complete ActiveX control behavior.

Existing no-COM core cases verify absence/empty/capacity/error paths, adjusted
interfaces across different points, edit vetoes, advisory notifications,
preflight cleanup, reentrant subscriptions and sender/module retirement.
The existing Value DLL exposes an explicit test property: real direct/native
STA/MTA aggregated and registered activation cases verify old-value observation
on request, no commit/notification after denial, new-value observation after
commit, and notification to sinks reached through another outgoing IID.
Original event proxies/projects and broader TypeLib event compatibility remain open.
No target, test mode, export or workflow is added; all 36 original blobs remain
unchanged. Printing/hardware HSP remain outside CI.

References: [IPropertyNotifySinkCP](https://learn.microsoft.com/en-us/cpp/atl/reference/ipropertynotifysinkcp-class),
[CFirePropNotifyEvent](https://learn.microsoft.com/en-us/cpp/atl/reference/cfirepropnotifyevent-class),
[OnRequestEdit](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipropertynotifysink-onrequestedit),
[OnChanged](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nf-ocidl-ipropertynotifysink-onchanged).

## TypeLib-driven event sinks

IDispEventImpl<nID, T, pdiid, plibid, major, minor, tihclass> derives from the
typed simple sink and exposes _tihclass, GetTypeInfoCount/GetTypeInfo,
GetIDsOfNames, GetFuncInfoFromId and GetUserDefinedType. Explicit library
identity/version uses the existing CComTypeInfoHolder::GetTI contract, including
custom holders with that same contract. GUID_NULL library selection obtains the
native source TypeLib through IProvideClassInfo2/GetClassInfo and
GetContainingTypeLib. When that interface is absent, IProvideClassInfo supplies
the coclass metadata; if both providers are absent, IDispatch::GetTypeInfo supplies
the containing TypeLib and IPersist::GetClassID selects the coclass in that same
library. An explicit outgoing IID needs only the IDispatch library on this last
path. IID_NULL selects IProvideClassInfo2's default source IID, or the coclass
interface marked with both IMPLTYPEFLAG_FDEFAULT and IMPLTYPEFLAG_FSOURCE.
Only E_NOINTERFACE permits provider fallback; other provider/metadata errors
propagate. Missing default sources fail with CONNECT_E_NOCONNECTION; ambiguous
defaults and malformed type kinds fail rather than select an arbitrary source.
AtlGetObjectSourceInterface exposes this shared discovery and returns the actual
library GUID, outgoing IID and library version. It clears every supplied output
on entry and publishes metadata only after complete success. These fallbacks
apply to both IDispEventImpl and IDispEventSimpleImpl through this shared helper.
For the simple sink, Advise discovers the default outgoing IID even when the
template declares a different IID. DispEventAdvise uses its explicit/template
IID without requiring source type information; a null pointer or IID_NULL
requests discovery. Its invocation still uses SINK_ENTRY_INFO metadata, and its
GetTypeInfo/GetTypeInfoCount/GetIDsOfNames remain E_NOTIMPL. Successful simple
sink Unadvise restores the initial template IID and clears the cookie; failed
discovery/subscription restores the previous identity, while failed explicit
Unadvise preserves the active identity/cookie for retry. Unadvise disconnects
the stored default binding rather than rediscovering mutable source metadata.
Unavailable metadata returns its actual error, without implicit registration
or a registry/image fallback. There is no static owned ITypeInfo cache.

Native FUNCDESC/TYPEDESC data supplies calling convention and argument types.
Enum/alias and interface references are resolved through native referenced type
information with bounded recursion. Supported typed calls include scalar
integers/floats, BOOL/DATE representations, BSTR, VARIANT, COM interfaces and
single-level scalar/VARIANT BYREF pointers, up to eight parameters with void or
HRESULT handlers. Writable pointees must match the native type/width. Optional,
named/LCID/retval parameters, arrays/records, nested BYREF and non-void/HRESULT
return contracts are not admitted; unsupported metadata fails before a handler
is called. GetUserDefinedType returns VT_EMPTY for unresolved/unsupported types.

SINK_ENTRY, SINK_ENTRY_EX and SINK_ENTRY_EX_P take an explicit typed member
pointer such as &Owner::Handler. They can reference a handler base subobject:
the compiler performs its pointer adjustment before invocation. The existing
SINK_ENTRY_INFO shorthand remains available for manually specified metadata.
Legacy bare-handler spelling on the new macros, internal ATL sink-map/holder
layouts and the complete ATL macro/helper surface remain unqualified.

Each nID base is an independent IDispatch/IUnknown identity. Connection metadata
is staged before native Advise so synchronous callbacks can use it. Reentrant
binding changes fail; subscription failure restores the original identity and
releases staged metadata. Wrong-source/failed explicit Unadvise preserves the
connection for retry. Successful Unadvise releases its metadata and allows
rebinding. Destructor cleanup disconnects before destroying that metadata;
failure terminates rather than leave a source pointing at a destroyed sink.
The containing C++ object owns the sink's memory and must finish callbacks and
unadvise before destruction. Sink methods do not provide cross-thread marshaling.

Existing native core cases load an own event fixture generated by Windows SDK
MIDL (resource 4), with no replacement of pinned original IDLs or handwritten
type-info mocks. They exercise file/registered/source metadata, descriptors,
conversion/ownership/BYREF checks, unsupported signatures, independent bases,
eager callbacks, rollback/retry, rebinding and per-user registration cleanup.
Existing Value DLL direct/native STA/MTA aggregation and registered activation
exercise IProvideClassInfo-only discovery and emit actual dispatch events from
native metadata. Registered AuxValue activation exercises the IDispatch/IPersist
path without a class-info provider. Both expose a native described incoming
property and verify default event delivery and disconnection. Core cases cover
missing defaults, malformed coclass metadata, absent IPersist, metadata/class-ID
failures, cleared outputs, failed subscriptions and successful retry. Simple
sinks exercise the same native sources, including synchronous callbacks during
Advise, wrong-source/failed Unadvise, identity reset, explicit metadata-free
subscriptions and destructor cleanup. Existing Value/AuxValue DLL event cases
also bind manually described sinks through both discovery fallback paths.
The existing WebBrowser case
uses IDispEventImpl for real DocumentComplete events. No target/test mode/export/
workflow is added; all 36 pinned Microsoft blobs remain unchanged.

Wider Automation signature/locale behavior, full composite-control integration,
exact member layouts and whole original event proxy/project admission remain
open. This bounded sink implementation is not complete ActiveX control support.

References: [IDispEventImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/idispeventimpl-class),
[IDispEventSimpleImpl](https://learn.microsoft.com/en-us/cpp/atl/reference/idispeventsimpleimpl-class),
[source-discovery helper](https://learn.microsoft.com/en-us/cpp/atl/reference/composite-control-global-functions),
[IProvideClassInfo](https://learn.microsoft.com/en-us/windows/win32/api/ocidl/nn-ocidl-iprovideclassinfo),
[IPersist](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-ipersist),
[sink-map macros](https://learn.microsoft.com/en-us/cpp/atl/reference/composite-control-macros).

## Sink-map subscriptions

AtlAdviseSinkMap(T*, bool) binds the existing typed sink map through the owner's
GetDlgControl(int, REFIID, void**) method. With atlhost.h included,
CWindow::GetDlgControl retrieves the actual hosted child control through
AtlAxGetControl; absent children return E_FAIL, unsupported interfaces propagate
E_NOINTERFACE, and supplied output storage is cleared before lookup.

One nID selects one sink base. Compiler-checked base conversions select the
most-derived IDispEventImpl or IDispEventSimpleImpl implementation, preserving
per-connection TypeLib ownership and manually described invocation respectively.
Inherited maps adjust the declaring owner before invoking binding callbacks,
including when another polymorphic base precedes that owner.
Multiple handlers for the same control share one subscription. Wildcard IID
entries use default-source discovery; explicit IID entries select that source.
Conflicting explicit IIDs for one control fail E_INVALIDARG before lookup.
Multiple sink bases with the same nID are outside this bounded implementation.
The internal map representation and member layouts are not Microsoft ATL ABI.

The helper preflights all sources and rejects already connected bases before
starting subscriptions. A failure rolls back only connections established by
that call, in reverse order. If rollback itself fails, a rollback error is
returned and the retained connection can be retried through Unadvise. Unadvise
uses each sink's retained connection point, without querying changed or missing
child controls; it attempts all groups, returns the first failure and preserves
failed subscriptions for retry. Already disconnected groups are harmless.
Recursive map operations on one owner fail E_UNEXPECTED. No global binding
registry, implicit COM initialization or automatic owner deletion is added.

Existing native core cases cover mixed sink types, repeated entries, preflight,
subscription/rollback failures, reentrant callbacks, retained metadata, partial
Unadvise/retry and rebinding. Existing Value/AuxValue DLL aggregation/activation
cases deliver actual events to both map bases. The existing real WebBrowser
case now uses a hosted child control, CWindow::GetDlgControl and
AtlAdviseSinkMap for DocumentComplete, preserving its original runtime budget
and stdout contract. No target, test mode, export, workflow or original blob is
added or replaced; all 36 pinned original files remain byte-checked.

CAxDialogImpl automatic resource/control creation, full composite controls,
same-nID multiple-IID bases, exact internal layouts and whole original event
projects remain open. Applications must unadvise successfully before destroying
the C++ sink owner and must own COM/apartment and UI-thread lifetimes.

References: [AtlAdviseSinkMap](https://learn.microsoft.com/en-us/cpp/atl/reference/connection-point-global-functions),
[CWindow::GetDlgControl](https://learn.microsoft.com/en-us/cpp/atl/reference/cwindow-class).
