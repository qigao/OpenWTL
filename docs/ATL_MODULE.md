# ATL application modules — current implementation and server roadmap

`CAtlModule` / `CAtlModuleT<T>` own module locks and termination callbacks;
`DECLARE_LIBID(LIBID_...)` supplies the original ATL `InitLibId()`
hook. The CRTP module constructor initializes the static per-image
`CAtlModule::m_libid` metadata; this neither initializes COM nor writes
type-library registration. The native source test validates GUID identity.

`CAtlExeModuleT<T>` additionally owns a COM initialization on its construction
thread and native automatic/explicit class-factory registrations.
`CAtlModuleT<T>` and its EXE/DLL derivatives share one frozen
`OBJECT_ENTRY_AUTO` map for persistent class registration. RegisterServer and
UnregisterServer invoke class-defined UpdateRegistry callbacks, with optional
CLSID selection and the existing resource/TypeLib ownership rules.
`CAtlDllModuleT<T>` now supports the real DLL class-factory/unload exports
through the same `OpenATL::COM::Module` engine. Standard static
`OBJECT_ENTRY_AUTO` class discovery is implemented; automatic registry
registration from class-defined `UpdateRegistry(BOOL)` callbacks is now
supported. Bounded resource-driven RGS uses a lazy module registration context
or a caller-supplied explicit context; full RGS and expanded ATL maps remain
compatibility work. Legacy
`CComModule` supports bounded object maps, class-factory registration cookies,
object-main hooks and same-thread retirement; its full API remains incomplete.

One application module may be alive per binary image. Construction publishes
the borrowed `_pAtlModule` pointer before application workers start. Module
destruction and explicit `Term()` require external quiescence and zero locks.
`Lock`/`Unlock` may have many concurrent callers; configuration, registration,
message-loop execution and destruction belong to the constructing thread.
`CComObject` retains the active application module until after its user-defined
destructor, independently of an explicitly supplied `OpenATL::COM::Module` pin.
Module construction/destruction cannot race with new object construction.

Termination callback records are owned; callback code and argument referents
must outlive invocation. They run once in reverse registration
order outside the callback-list mutex. Registration accepts at most 1024
callbacks, fails on capacity/allocation errors, and rejects additions after
termination begins. Callbacks must not throw or acquire new module locks.
`Term` is idempotent. Counter misuse and destruction with outstanding locks
are lifetime violations and terminate instead of authorizing unsafe retirement.

EXE module construction calls the CRTP `InitializeCom` hook; its successful
`S_OK` or `S_FALSE` acquisition is balanced by `UninitializeCom` on the same
thread. Failed initialization is exposed by `GetInitHRESULT` and prevents
startup/registration; it must not uninitialize somebody else's apartment.
Default initialization is MTA, or STA with `_ATL_APARTMENT_THREADED` /
`_ATL_SINGLE_THREADED`. Declaring this module opts into COM initialization;
including headers and ordinary WTL `CAppModule` construction do not.

Without a successful explicit table selection, RegisterClassObjects discovers
the same frozen OBJECT_ENTRY_AUTO map used for DLL factories and persistent
registration. It creates the existing engine's real IClassFactory instances
for every class before native publication, then registers them through the
shared CoRegisterClassObject cookie helper. Missing UpdateRegistry callbacks
do not prevent factory activation. No persistent registry operation is run.
Temporary references retire after the operation; the OS retains registered
factories until successful revocation. A duplicate/invalid map fails before
publication, and factory materialization failure publishes nothing.

`SetClassObjects` is an OpenATL extension accepting CLSID/IUnknown descriptors.
It copies metadata, queries/retains IClassFactory references, and admits at
most 1024 entries. Input descriptors are borrowed only during that call.
Replacing a table is exclusive and prohibited while OS registration cookies
remain active or Run is executing. A successful call selects the explicit table
exclusively; an empty explicit table intentionally disables automatic activation
for that module. Failed table updates preserve the previous selection.
`RegisterClassObjects` uses real `CoRegisterClassObject` calls;
partial failure revokes successful registrations, retaining failed-revocation
cookies for retry. `RevokeClassObjects` releases cookies; clearing the table
releases retained factories. Empty registration/revocation returns `S_FALSE`.
Rollback-revocation errors take priority and retain active cookies for retry.
Factory owners remain responsible for their own objects/server-lock lifetime.
Term and destruction revoke publication on the constructing thread before
termination callbacks and before balancing COM initialization. Explicit-table
references are released, and automatic factories, objects and server locks must
have fully retired; a remaining client reference is a lifetime violation.

`Run` dispatches PreMessageLoop / RunMessageLoop / PostMessageLoop, with owned
registrations revoked even on callback failure. The default pre-hook registers
automatic or explicitly selected classes and the post-hook revokes them;
no classes means `S_FALSE` and no loop. UI applications override the hooks
for their own lifecycle.
The post-hook runs when the pre-hook returns `S_OK`, including after a loop
exception. A failed pre-hook must unwind its application-owned side effects;
the module still revokes its own OS class registrations on every Run exit.
The default loop uses GetMessage/TranslateMessage/DispatchMessage and reports
GetMessage errors. Registration commands are limited to 32767 UTF-16 code
units including the terminator, then tokenized as Windows arguments;
the default RegServer/UnregServer commands request TypeLib registration, so a
missing embedded TypeLib fails before any class callback. Per-user registration
commands are supported as described below. Idle-shutdown monitoring,
service modules and complete ATL EXE-server behavior remain compatibility work.
No fake success is permitted.

The EXE wrapper checks its constructing thread, initialization HRESULT and
termination state before either persistent-registration operation. Base
CAtlModuleT registration does not initialize COM. The shared map validates
entries and duplicate CLSIDs once, sorts callbacks by CLSID, preflights every
selected callback, and rolls successful class registrations back in reverse
order after a later failure. Missing callbacks return E_NOTIMPL; a missing
selected CLSID returns CLASS_E_CLASSNOTAVAILABLE. The captured per-user policy,
explicit-root priority and TypeLib preflight/rollback are shared with DLLs.
Explicit DLL BindServer tables retain their separate registry limitation;
they do not acquire callbacks from the automatic map.

Existing native module tests register the real parameterized REGISTRY resource
through an EXE class's OBJECT_ENTRY_AUTO callback under a fresh GUID private
HKCU root. They check selected/all-callback preflight, actual quoted MODULE/raw
MODULE_RAW and owner values, preservation after external value changes,
unregistration, wrong-thread/failed-initialization/terminated rejection, and
balanced COM teardown. The common CAtlModuleT path runs the same resource
callback without initializing COM. EXE embedded-TypeLib and AppID registration
have their separate checks described below.

The same native module suite additionally publishes the automatic map in both
STA and MTA, uses CoGetClassObject and CoCreateInstance to create actual objects,
and checks class identity, object pins and LockServer. It verifies failed table
updates, duplicate publication, wrong-thread rejection, explicit-empty opt-out,
absence after revocation, a retained factory's independent lifetime, repeat Run,
loop exceptions, explicit Term and destructor cleanup. Automatic activation
must not run class registry callbacks. These are same-process native OLE
activation checks. There is no new executable, CMake target or workflow.

The existing executable also serves as a real out-of-process fixture. Each
client admission creates a fresh CLSID and named-event token, then uses the
built-in owner-verified registrar to install only that HKCU LocalServer32 entry.
The command uses quoted MODULE plus the server mode/token; ServerExecutable
uses unquoted MODULE_RAW. CoGetClassObject starts the same executable through
Windows SCM with -Embedding; the test never calls CreateProcess. The child
publishes the token's automatic class and pumps its owning STA or MTA.

Client calls use the system IClassFactory, IDispatch and IPersist proxy-stubs.
They create objects through both factory CreateInstance and CoCreateInstance,
check the remote ProcessId property against a different live process with the
same executable image, verify the unique CLSID and balance LockServer calls.
The registrar removes its unique CLSID before the child revokes publication.
Existing proxies must still call the same server; after client releases, the
child pumps until object/factory/server-lock references retire, exits normally
and balances COM. The client checks exit code zero and absence of the owned key.
Failure cleanup removes only verified registration, signals its token's stop
event and, if necessary, terminates only the validated child process handle.
Named waits and drain loops are bounded. All of this extends the existing
module admission, retaining its output contract and native CI matrix.

This qualifies native SCM launch and system-interface marshaling under the
current user's local COM policy. Custom proxy/stub generation, arbitrary
user-defined interface marshaling, AppID security policy, services,
idle shutdown
remain separate work.

`RegisterAppId()` / `UnregisterAppId()` now invoke the derived module's
`UpdateRegistryAppId(BOOL)` hook. `DECLARE_REGISTRY_APPID_RESOURCEID(id,
"{GUID}")` supplies that static hook and `GetAppId()` using the standard narrow
GUID-literal spelling. It validates a non-null GUID, then executes the actual
numeric REGISTRY resource through the existing static helper/common replacement
hook. Invalid GUIDs, missing resources and callback/exception failures propagate;
no alternate registrar or generated AppID metadata is selected. Without a
declaration/override, GetAppId returns null and the default hook/operations
return S_FALSE (explicitly no work), without opening a registry root.

DLL registration exports and EXE RegServer commands install AppID before class
registration. A subsequent class/TypeLib error removes the AppID only when that
hook reported S_OK; a rollback error takes priority. Unregistration removes
classes first and only then AppID, preserving AppID after a class error. Direct
RegisterServer/UnregisterServer still select class/TypeLib work separately.
An enclosing scope captures per-user mode once, keeps the lazy root alive, and
is reused by nested AppID, class and TypeLib helpers through rollback. Changing
the global mode from a hook cannot split that operation's policy. EXE AppID
operations also enforce the constructor-thread/init-HRESULT/termination checks.

The owner-verified executor now admits value-free top-level NoRemove AppID as a
shared container alongside CLSID. It never owns/removes either container or
their foreign values; all resource children retain exact owner/content checks.
Nested NoRemove AppID and scripted container values remain rejected.
Existing native tests use an embedded AppID resource with GUID and EXE alias
keys under a fresh GUID private HKCU test root. Base/EXE/DLL tests check actual
metadata/module/owner values, external-edit preservation, a foreign shared value,
exceptions and invalid/missing input, wrong-thread/failed-init/terminated access,
DLL/command ordering, rollback/error priority and callback-mutated global mode.
The class-registration hooks in these ordering tests are explicit probes;
positive native DLL TypeLib registration remains covered by the existing suites.
This is AppID resource/API support, not qualification of DCOM permissions,
RunAs, services or arbitrary AppID security configuration. No binary, project,
CMake target or workflow is added.

Public contracts: [module AppID helpers](https://learn.microsoft.com/en-us/cpp/atl/reference/catlmodulet-class),
[AppID resource macro](https://learn.microsoft.com/en-us/cpp/atl/reference/registry-macros#declare_registry_appid_resourceid).

The EXE parser recognizes RegServerPerUser and UnregServerPerUser with either
`/` or `-`, case-insensitively as complete Windows argument tokens. After the
thread/init/state and input checks, it enables AtlSetPerUserRegistration(true)
and calls the same AppID/class/TypeLib command workflow. Both commands retain
the TRUE TypeLib request. A command returns false (stop application startup),
with its actual operation HRESULT, on either success or failure. The enabled
image-local setting remains selected after failure or success unless application
code changes it; ordinary RegServer/UnregServer do not reset it. Callback changes
still cannot change the captured policy of the current operation.

An enclosing module scope already fixed to non-per-user mode rejects a nested
per-user command with E_UNEXPECTED before changing mode or running callbacks.
An existing per-user scope can be reused; an explicit opened-root context keeps
its existing priority. These OpenWTL scope rules prevent a mixed-policy operation.
Unknown/suffixed tokens, a token in the executable path, invalid/oversized input,
null outputs and wrong-thread/failed-init/terminated entry do not enable the mode.

The existing native module tests run both per-user commands through the AppID
resource ordering/rollback/mode-mutation cases. A separate application hook
registers and unregisters an actual uniquely named HKCU Software\\Classes RGS
key through the default scope and verifies quoted/raw module data, failure and
exception results, persistent setting selection, ordinary-command continuity,
input/state guards and cleanup. That hook checks the TRUE dispatch flag but is
not by itself evidence of EXE embedded-TypeLib registration. The default EXE's
incomplete class-map test still fails honestly after selecting per-user mode. No new
binary, project, CMake target or workflow is introduced.

The existing EXE now embeds resource 1 from the same Windows SDK MIDL output
already used by the DLL fixture, generated from the unchanged pinned Microsoft
COMMap.idl. Both images compile the existing embedded/registered IChain dispatch
fixture; no synthetic TypeLib or alternate registration path is introduced.
The module admission runs STA and MTA EXE modules with DECLARE_LIBID and the real
AppID resource, restricting the standard RegisterServer/UnregisterServer call to
one actual automatic class with a real resource callback. RegServerPerUser and
UnregServerPerUser therefore execute the complete AppID/class/OLEAUT32 TypeLib
workflow with TRUE, rather than an ordering-only hook.

Native checks prove embedded loading/dispatch alone does not register anything,
missing resource indexes fail with cleared outputs, QueryPathOfRegTypeLib points
to this EXE, LoadRegTypeLib loads its real library, LIBID/version/SYSKIND match,
and both embedded and registered dispatch perform a real ITypeInfo::Invoke.
Common hooks deliberately disable the global per-user setting, while the
captured operation still registers/unregisters in HKCU. Actual DACL denial of
the newly created test TypeLib key must return the native OLEAUT32 failure and
roll back class and AppID resources. Each cycle requires exact registry absence,
balanced COM and zero locks before teardown. Preexisting TypeLib/interface
metadata in HKCU or HKLM is rejected before mutation.

The former fixture assumption that the EXE had no TypeLib no longer applies.
Module preflight negatives now use an explicitly invalid image handle and
require E_INVALIDARG before callbacks; real missing-index loading is checked
separately. The same existing test targets, MIDL target and CI matrix are used.

## `CAtlDllModuleT<T>` — implemented explicit-class-table milestone

`ATL::CAtlDllModuleT<T>` is now available in `atlbase.h`. It inherits the
existing `CAtlModuleT<T>` lock/termination protocol and exposes the standard
ATL DLL-module entry points: `DllMain(DWORD, LPVOID)`,
`DllGetClassObject(REFCLSID, REFIID, LPVOID*)`,
`GetClassObject(...)`, `DllCanUnloadNow()`,
`DllRegisterServer(BOOL = TRUE)` and
`DllUnregisterServer(BOOL = TRUE)`. The application continues to export
its own unmangled `DllGetClassObject`, `DllCanUnloadNow`,
`DllRegisterServer` and `DllUnregisterServer` functions, delegating to
the module. This does not auto-export symbols.

**Current class map bridge:** call
`module.BindServer(OpenATL::COM::Module&)` with a valid, externally-owned,
immutable explicit COM class table. Binding succeeds exactly once (repeat
binding to the same module is idempotent); a second different table is
rejected without rebinding. The `OpenATL::COM::Module` and its class entries
must outlive all factories, objects, server locks and the bound DLL module.
An absent class table returns `CLASS_E_CLASSNOTAVAILABLE` and clears output;
invalid module descriptors cannot be bound.

`DllCanUnloadNow` returns `S_FALSE` when either the ATL module lock count
or the bound server's factory/object/server references are nonzero. Native
`DllGetClassObject` delegates through the derived `T::GetClassObject`
hook (as in ATL) and pins the ATL module during the synchronous lookup.
The default hook uses the explicit server table; a returned real factory
independently retains its server module.
This is a cooperative unload contract: it cannot make arbitrary
`FreeLibrary` safe during an active callback. The application must
quiesce every caller before unloading.

`DllMain` performs no COM activation, registry writes, or class registration
under the OS loader lock. Registration forwards through
`T::RegisterServer(BOOL, const CLSID*)` and
`T::UnregisterServer(BOOL, const CLSID*)`. A derived module can override
these methods; the inherited default now uses only the class-defined
`UpdateRegistry(BOOL)` methods attached to `OBJECT_ENTRY_AUTO` entries.
A missing class callback is **`E_NOTIMPL`**, not fake success.
`DECLARE_NO_REGISTRY()` deliberately opts a class out and explicitly
returns `S_OK` without installing keys. TypeLib registration uses the image-local Windows OLEAUT32 implementation.
For TRUE, the module preloads and validates the actual binary TypeLib before
any class callback, installs classes, then registers the library. Native
TypeLib registration failure reverses the successfully installed classes;
a class rollback failure takes priority over the original HRESULT. FALSE
continues to select class-only work. Unregistration preloads the TypeLib,
removes classes, then unregisters the exact LIBID/version/LCID/SYSKIND.
The Windows registry APIs are not an atomic transaction: general partial
TypeLib-write recovery and transactional unregistration are not claimed.

The global `AtlLoadTypeLib`, `AtlRegisterTypeLib` and `AtlUnRegisterTypeLib`
helpers support optional numeric \N resource indexes and counted owned outputs.
`AtlSetPerUserRegistration(true)` selects RegisterTypeLibForUser and
UnRegisterTypeLibForUser; the default selects their system-registration
counterparts. Set the image-local mode before registration and serialize
configuration/registration. Each operation snapshots its mode before class
callbacks and uses that same snapshot for RGS and TypeLib work. This does
not redirect arbitrary Win32 calls or another DLL. Native qualification
uses per-user registration and real DACL denial, without elevation or HKLM
writes. Machine-wide permission behavior remains separately unqualified.

Class-directed `DECLARE_REGISTRY_RESOURCEID` and `DECLARE_REGISTRY_RESOURCE`
load actual numeric/named `REGISTRY` resources from the module's own image.
`CAtlDllModuleT::RegisterServer` and
`UnregisterServer` supply a lazy synchronous RGS context: per-user mode opens
the current user's `Software\Classes`; default mode opens the current
token's HKCR merged Classes view with `RegOpenUserClassesRoot`. Root-access
errors propagate as HRESULTs. Custom and `DECLARE_NO_REGISTRY` callbacks do
not open this root. Once needed, the root and ownership context remain alive
through all class callbacks, TypeLib work and class rollback.

An explicit `ScopedRgsRegistrationContext` still takes priority, for example
when an installer needs an isolated root or custom owner tag. Automatic
contexts use `OpenATL.Module` plus the canonical module path as their ownership
identity and retain the existing parser and owner-verified executor. Direct
class callbacks use the active ATL module's static registrar; without an ATL
module they require an explicit context. Nested explicit contexts remain rejected. Installer writes
must be serialized; no work runs under `DllMain`.

`CAtlModule::UpdateRegistryFromResourceS(UINT/LPCTSTR, BOOL,
_ATL_REGMAP_ENTRY* = nullptr)` and the default `UpdateRegistryFromResource`
alias execute that same static registrar, including when called directly
outside `RegisterServer`. Existing explicit/operation contexts take priority;
otherwise the helper supplies an own-image context with the current per-user
setting. Resource names are bounded to 128 UTF-16 units and numeric IDs to
1..65535. Decoding, replacement validation and parsing finish before a default
registry root is acquired.

The replacement map is copied into owned strings before execution. Supply
at most 64 entries followed by `{nullptr, nullptr}`; the entire sentinel table
must remain readable for the call. Names are case-insensitive ASCII identifiers
of at most 64 units; data is limited to 8192 units and the complete map to
65536 units. Replacements occur only within key/name/REG_SZ data tokens.
Quotes and braces in data cannot introduce script instructions. Unknown or
duplicate names, reserved `MODULE`/`MODULE_RAW` overrides, recursive `%`
data, CR/LF and invalid expanded names fail before writes. Pointer-map strings
are NUL-terminated; counted parser-map data additionally rejects embedded NUL.
Unregistration must supply the same replacement values for ownership checks.

The static helper invokes virtual `AddCommonRGSReplacements(IRegistrarBase*)`
after copying and validating the explicit map, before resource parsing or
default-root acquisition. The default hook adds no extra parameters. A derived
module can use `AddReplacement` and `ClearReplacements`; explicit and common
entries share the same limits and duplicate/reserved-name rules. Each addition
owns its strings and commits only after validation; a failed addition leaves
the earlier map unchanged. Clearing removes the custom map, while built-in
MODULE ownership remains tied to the actual image. Hook failures propagate;
exceptions preserve failing `CAtlException` HRESULTs, map allocation failures
to `E_OUTOFMEMORY`, and map other exceptions to `E_UNEXPECTED`.

`IRegistrarBase` has its published IID/IUnknown vtable and real reference
counting. The hook receives a borrowed interface. It may retain an owned
reference with AddRef/QI; that reference pins the ATL module until final Release.
On every callback exit, including errors and exceptions, the capability is
closed. Later `AddReplacement`/`ClearReplacements` calls return `E_UNEXPECTED`;
QI and Release remain valid. Successful callbacks move an immutable owned map
into parsing, so retained pointers cannot change installation or rollback.
Mutation/closure are synchronized; callers still serialize installer operations
and coordinate module destruction/unload. The helper captures the per-user
setting before invoking application code, and preserves an enclosing module
operation's existing context/mode.

OpenWTL always uses its built-in static registrar. `AtlCreateRegistrar`
returns an owned `IRegistrar` with the published extended IID/vtable, real QI
identity and reference counting. String, file and numeric/named resource
registration/unregistration methods all feed the existing parser and
`ApplyRgsDocument` owner-verified execution path. The active ATL module is
pinned until final Release; without one, the caller owns image lifetime.
No COM apartment is initialized and no external ATL registrar is activated.

`#include <statreg.h>` supplies a noncopyable `CRegObject` static facade with
the same AddReplacement/ClearReplacements and eight script method signatures.
It owns the heap registrar; its QueryInterface returns an independent owned
interface that remains valid after the facade is destroyed. The facade itself
is not an IUnknown-derived object: direct `IRegistrar* p = &object` and
`CComObject<CRegObject>` patterns are not supported. Use QueryInterface or
AtlCreateRegistrar for retained COM interfaces.

Persistent registrar maps remain mutable between operations. Each script call
captures an owned map snapshot, resolves/parses its source and only then
acquires a default registration root. Explicit/operation contexts take priority;
otherwise the own image and captured per-user setting supply the default.
The replacement-only common-hook capability still rejects QI for IRegistrar;
it does not grant script execution. Callers serialize registry writes and keep
borrowed explicit HKEY/HMODULE contexts alive for synchronous calls.

String scripts are bounded to 65536 UTF-16 units. File methods accept absolute
paths within the existing 2048-unit canonical path policy (including its ban
on `%`); only disk files are accepted. They open read-only with FILE_SHARE_READ,
reject conflicting writers/delete access, bound input to 262148 bytes, read
through one owned handle, require EOF, and reuse the strict UTF-8/BOM UTF-16
LE/BE decoder. The decoded source retains the 65536-unit limit. Read/decode/parse
errors precede registry writes, and no source file is modified.

Resource methods accept REGISTRY resources in an explicitly named PE file,
including one not loaded for execution. The bounded absolute path is canonicalized;
a read-only FILE_SHARE_READ handle and a LoadLibraryEx mapping with
LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE stay owned
through parsing and execution. The loader reads resources without executing
the DLL entry point or loading static imports. Both handles retire on every
return path. Numeric IDs, named-resource, encoding and parser limits are unchanged.
The supplied path is retained directly; datafile handles are not passed to
GetModuleFileName.

Persistent IRegistrar/CRegObject additionally accept a case-insensitive
`AddReplacement(L"MODULE", absolutePath)` binding. The canonical owned path is
snapshotted with custom entries and supplies both MODULE expansion and the
owner marker. Duplicate bindings and invalid paths fail without changing state;
ClearReplacements clears the binding. Without a binding, resource operations
use their source PE path, while string/file operations use the active context's
module path. An explicit binding permits an external resource template to
describe a different server module. Root selection and owner tags still come
from the existing explicit/operation/default context, which is never rewritten.
Common replacement hooks and explicit module regmap tables retain their
reserved-MODULE policy; MODULE_RAW is always derived from the canonical path.

Both built-in tokens are case-insensitive. Automatically supplied `%MODULE%`
is quoted for the current EXE and unquoted for a DLL; `%MODULE_RAW%` is always
unquoted. Explicit resource-file calls inspect the source PE's DLL characteristic
through the held read-only handle, without executing it or guessing from its
filename. A persistent MODULE binding preserves the caller's choice: an unquoted
absolute path stays unquoted, while one enclosing pair of double quotes requests
quoted MODULE expansion. MODULE_RAW and owner markers use the canonical path
without those quotes. Malformed/interior quotes are rejected atomically. Clearing
the binding restores automatic selection. Quote characters count toward the
existing expanded-value limit. The low-level ResolveModule/RegisterRgs/
UnregisterRgs APIs retain their unquoted default and accept a final quoteModule
flag when the caller explicitly selects EXE formatting.

Native tests use a copied fixture whose DllMain is deliberately rejected by a
scoped test gate: executable loading fails with ERROR_DLL_INIT_FAILED while
resource registration/unregistration succeeds. They verify automatic and bound
MODULE values, wrong-owner unregister preservation, sharing denial, final file
deletion, and default HKCU per-user installation/cleanup with a fresh GUID key.
No new test executable or workflow is introduced.
The existing test EXE also embeds the same REGISTRY resource set to verify module
helpers and resource-only EXE calls, MODULE_RAW, explicit quoted/unquoted bindings,
reset behavior and content-verified unregister rejection when formatting differs.

CLSID_Registrar activation, dynamic UpdateRegistryFromResourceD, full CRegObject
inheritance/legacy ABI, complete EXE COM-server lifecycle compatibility and full
RGS grammar remain unimplemented. No second registrar is selected.

Resource mapping contract: [LoadLibraryExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw).
Built-in token formatting: [ATL replaceable parameters](https://learn.microsoft.com/en-us/cpp/atl/using-replaceable-parameters-the-registrar-s-preprocessor).

Public signature references: [CAtlModule registry helpers](https://learn.microsoft.com/en-us/cpp/atl/reference/catlmodule-class),
[registry macros](https://learn.microsoft.com/en-us/cpp/atl/reference/registry-macros),
and [replaceable parameters](https://learn.microsoft.com/en-us/cpp/atl/using-replaceable-parameters-the-registrar-s-preprocessor).

The existing `tests/com_server/fixture.cpp` now routes its actual COM DLL
exports through `CAtlDllModuleT`, without manually opening a root or supplying
an RGS context, and preserves real HKCU class registration,
STA/MTA activation, object identity, server locks and unload behavior. The
DLL's common hook supplies the auxiliary class's actual ThreadingModel resource
parameter and deliberately changes the global mode, verifying that registration,
TypeLib work and failure rollback retain the original per-user snapshot. Native
tests also cover QI identity, retained-capability module pins and closed mutation,
ClearReplacements, bounded additions, callback errors/exceptions and cleanup. The
`atl_module.cpp` native test exercises failed/unbound/duplicate binding,
IID rejection, factory and object lifetimes, registration forwarding and
ordinary no-COM initialization.

**Implemented next step:** `OBJECT_ENTRY_AUTO(clsid, type)` registers
an immutable per-binary COM class descriptor using static storage. The
registrar only performs atomic pointer linking, with no COM call or heap
allocation during DLL initialization. Without an explicit `BindServer`,
the DLL module composes these entries **once on the first class-object
request** into the existing `OpenATL::COM::Module`. A duplicate CLSID,
invalid entry or oversized map fails closed; no entry returns
`CLASS_E_CLASSNOTAVAILABLE`. Factory, object and server-lock ownership
continues to use the **same** module engine, including `DllCanUnloadNow`.

The native COM DLL fixture uses standard `OBJECT_ENTRY_AUTO` registration
and is activated through genuine Windows STA and MTA clients; the module
test covers lookup, failure paths, duplicated CLSIDs and live
`LockServer` retirement. The old explicit `BindServer` class-table API
remains supported for existing applications; it does not create a
second COM backend.

**Class-directed registration milestone:** `OBJECT_ENTRY_AUTO` records an
optional static class `UpdateRegistry(BOOL)` callback. The DLL module takes
the same one-time class-map snapshot used for factories, sorts its classes
by CLSID for deterministic installation, and invokes real callbacks in that
order. Missing hooks fail preflight before *any* class is installed. Later
registration failure invokes inverse callbacks for earlier successes in
reverse order, returning the first rollback error in preference to the
original failure. Unregistration also proceeds in reverse order. A
`RegisterServer(FALSE, &CLSID)` call restricts the operation to one class;
unknown CLSIDs return `CLASS_E_CLASSNOTAVAILABLE`.

The real two-class COM fixture uses per-class `UpdateRegistry(BOOL)` methods
calling the existing owner-verified HKCU registrar, with STA/MTA activation
and a foreign second-class ProgID to force rollback of the first. This is
not automatic inference of ProgIDs/paths, and **not** an atomic registry
transaction: callbacks must clean up their own partial failure and callers
must serialize installer writes. The existing explicit `BindServer` table
remains usable, but its default class registration is `E_NOTIMPL`; the
owning application can provide a derived registrar.

**Not yet complete:** `CAtlComModule`, extended object-map variants,
full arbitrary RGS registration grammar, complete legacy TypeLib APIs,
aggregated classes and complete EXE/service COM Server behavior remain
separate compatibility milestones. Bounded real `REGISTRY` resource-ID
handling is now available only under an explicit scoped registration policy. Complete Microsoft ATL source compatibility is not yet claimed.

## Automatic factory aggregation policy

OBJECT_ENTRY_AUTO now supplies the built-in class creator policy to the same
Module engine, including an explicit controlling-outer parameter. CComCoClass
provides the default aggregatable policy; NOT_AGGREGATABLE and
ONLY_AGGREGATABLE declarations select their respective creation paths. Classes
without CComCoClass or a declared policy retain nonaggregated CComObject
creation. Existing explicit Creator callbacks keep their nonaggregated contract.

Factories validate the aggregate IID_IUnknown request before calling a creator.
CComAggObject binds the module before FinalConstruct, keeps the outer pointer
borrowed, and separates nondelegating inner lifetime from interface references
that retain the outer. The object pins the engine and application module until
the contained Base destructor finishes, even after the factory is released.
See COM_SERVER.md for aggregate map ownership and the qualified native tests.


## Original Microsoft two-DLL integration

The existing unload fixture/probe images now also compile byte-pinned original
COMMap.cpp/Aggreg.cpp module entry points and their respective original .rc
resources/class sources. The original four private COM exports join the GUI
exports. Existing GUI cases still verify no implicit COM initialization.
The STA events case calls original per-user register/unregister exports,
native CoCreateInstance and registered dispatch, checks vtable image ownership,
retains a cached owner across unregister, and verifies actual DLL unmapping
once objects and COM caches retire. No target, test mode or workflow is added.
This qualifies the original module path in existing images; untouched VCXPROJ
builds, original output/export layouts and machine-wide policies remain open.


Automatic OBJECT_ENTRY_AUTO creation also honors DECLARE_POLY_AGGREGATABLE.
Both aggregated and nonaggregated requests select CComPolyObject on the same
factory engine. Its private IUnknown and contained public interfaces share the
existing application/COM module pin contract. The registered Value DLL now
uses this policy in the existing native STA/MTA activation and unload cases.
Custom factory policies and exact ATL binary layouts remain unqualified.
