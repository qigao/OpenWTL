# Official sample coverage

The complete patched WTL 10.0.10320 release (headers, all 11 original
Samples, AppWizard and MS-PL) now resides under `wtl/` in this repository.
Its 296-file source manifest proves all included bytes; no second ZIP
or vcpkg WTL installation is used. All original sample sources and
resources remain unchanged, with the same native CI gates.

Official samples are a separate acceptance boundary from the community SDI
fixture (`kkhaike/wtltest`) and OpenATL's own conformance tests. Passing that
community fixture does not qualify the official WTL or Microsoft ATL suites.

## Acceptance contract: ATL and WTL, without MFC

The requested acceptance boundary is all 11 samples in WTL 10.0.10320 and
every pure ATL project in the pinned Microsoft `VC2010Samples/ATL` directory,
on both MSVC and MinGW. Preserve the public ATL/WTL APIs and compile the
original sample sources and resources. The WTL Win32 build boundary has passed;
the Microsoft pure ATL boundary has **not yet passed**.
MFC implementations, headers and libraries are not dependencies of OpenATL.
Projects explicitly configured to use MFC are recorded separately, excluded
from pure ATL totals, and never counted as passing pure ATL tests.

The reference for public ATL signatures is MSVC 14.44.35207 (VS 2022).
Its ATL/MFC implementation is not a build dependency: MSVC qualification
uses `/X` and explicitly enumerates compiler/Windows SDK include directories
without `atlmfc`, so a missing repository header produces a real failure.

The original 11 examples are all preserved as immutable source/assets.
The legacy full Win32-only project compilation (MSVC x86 / llvm-i686) is
**historical evidence**, not part of the streamlined x64/ARM64 CI.
Some original samples (including MTPad/Wizard97Test) rely on 32-bit ABI
details and therefore are **not claimed** to pass on x64/ARM64. Current
CI still builds and natively executes suitable original examples
(GuidGen on both compilers; MemDlg under MSVC), plus real SDI and
original WTL/ATL API behavior on x64 and real ARM64. A historical
32-bit full-suite build may be requested explicitly, but it is not
a mandatory CI target.

For MinGW, the selected SDK's three Ribbon declaration headers are adapted in
the build directory and supplied through `OpenWTL::WTL`, including GUIDs and
the Property System import library. No consumer-specific Ribbon include roots,
additional OpenATL library or Microsoft ATL/MFC implementation is required.
These Windows SDK headers remain build inputs under the SDK's license.

Wizard97Test contains Windows-1252 characters in wide string literals. MSVC
uses an explicit source charset; Clang's compiler input reader decodes that
one file to UTF-8 in memory, preserving the original bytes on disk, C++ text,
line locations and include resolution. This is character decoding, not a
sample token/API patch. Existing compatibility changes are applied directly to the checked-in
WTL headers, not a generated adapter or an original sample.

Historical full Win32-suite build results (2026-10-28; outside current target matrix):

| Sample | MSVC 14.44 Win32 | llvm-mingw Win32 |
| --- | --- | --- |
| Aero | Built | Built |
| Alpha | Built | Built |
| BmpView | Built | Built |
| GuidGen | Built | Built |
| MDIDocVw | Built | Built |
| MemDlg | Built | Built with Windows SDK RC |
| MTPad | Built | Built |
| MTPad7 | Built | Built with SDK RC/UICC/Ribbon declarations |
| TabBrowser | Built | Built |
| Wizard97Test | Built | Built |
| WTLExplorer | Built | Built |

Both compilers have compiled and linked all 11 examples. Individual successful
links are compile evidence only; no blanket runtime qualification of every
example is claimed. Native conformance separately exercises real WebBrowser
activation/navigation, typed connection-point events, source identity,
argument errors, exception translation, window teardown, retained-host module
locks, disconnected queries, wrong-thread rejection and COM balance.

The Microsoft inventory is
[`microsoft-projects.json`](../tests/official_samples/microsoft-projects.json):
48 projects, of which 11 declare static/dynamic MFC and 37 do not. All 37
remain unqualified until their original sources, code generation and resources
are integrated into both compiler build graphs. The inventory records scope,
not success. In particular, MIDL, `#import`, registrar, dispatch, ActiveX server/composite controls and
OLE DB requirements are outstanding work within the pure ATL contract, rather
than reasons to silently omit those projects.

## WTL 10.0.10320 release

The sources/resources below come directly from the `Samples/` directory in the
repository-owned WTL 10.0.10320 source and manifest. They are
built unchanged through `tests/official_samples/CMakeLists.txt`, linked only
to `OpenWTL::WTL`, and driven by native behavior tests registered in CTest.
The build supplies the common-controls v6 manifest as a separate resource;
upstream source files are never rewritten.

| Official sample | llvm-mingw | MSVC | Behavior verified |
| --- | --- | --- | --- |
| GuidGen | Build + native x64 test passed | Build + native x64 test passed | New GUID differs; four formats preserve its value; About controls; normal close |
| MemDlg | Build + native Win32 test passed using SDK RC; llvm-rc still lacks TOOLBAR support | Build + native x64 test passed | Resource About dialog, in-memory About dialog, text/buttons, normal frame exit |

GuidGen's normal close persists one user setting. Its test saves/restores the
preexisting `GUID Type` value and removes only newly created empty keys. It
does not invoke Copy or change the clipboard. MemDlg explicitly initializes
COM in its original application entry point; its success is not evidence of
a no-COM application. Neither sample modifies its original initialization.

These samples exposed fixes now owned by OpenATL and its repository WTL headers:

- Standard default `using namespace ATL`, with `_ATL_NO_AUTOMATIC_NAMESPACE`
  as the opt-out.
- Preserve WTL's real `CRegKeyEx` alias to `ATL::CRegKey`.
- Use a type-qualified CRTP enum for command-bar constant array bounds on Clang.
- Apply the documented Windows 7 minimum before Windows SDK inclusion, including
  when historical sample headers specify older versions. This is source
  compatibility with those samples, not a Windows XP runtime-support claim.

CI builds GuidGen for x64/ARM64 on both compilers and MemDlg for x64/ARM64 on
MSVC; the native Windows jobs run their behavior tests five times. ARM64 tests
require exact SHA256/PE/resource manifests (20 files / 27 cases for llvm-mingw,
21 files / 28 cases for MSVC). Linux/macOS cross-host jobs have been retired by the Windows x64/ARM64-only
policy; current-head native Windows CI is required for any new qualification.

The table above records the expanded compilation coverage. Runtime coverage
in this section remains limited to GuidGen and MemDlg.

## Pinned original Microsoft COMMap MIDL milestone

The repository now retains the **unchanged** `General/commap/COMMap.idl`
from `microsoft/VCSamples@9e1d4475555b76a17a3568369867f1d7b6cc6126`,
with Git blob SHA `dcf19ff69001c6f2f242d15c917a5598b03d70ba`
checked at CMake configure time on **all hosts**. The file is under
`tests/official_samples/microsoft_atl/commap/COMMap.idl`, with the
upstream MIT license and explicit Git checkout byte-preservation policy.

On **actual Windows** hosts, the selected Windows SDK `midl.exe` is a
required build tool. It generates `COMMap.h`, `COMMap_i.c`,
and a true binary `COMMap.tlb` from this exact original IDL.
The GUID implementation and generated header compile into the native
COM test executable under both MSVC and llvm-mingw against repository-owned
OpenATL headers. CTest loads the MIDL-generated TypeLib using
`LoadTypeLibEx(REGKIND_NONE)`, verifies the real COMMap LIBID/version
and original `IChain`/`IOuter` IID/member-name DISPIDs.
There is no registry installation or `regsvr32` post-build action.

Windows x64/ARM64 build hosts use their selected Windows SDK MIDL;
there are no Linux/macOS cross-host MIDL claims or jobs.

The generated TypeLib is also embedded as TYPELIB resource 1 in the existing
native COM DLL fixture. MIDL uses Win64 metadata for both supported 64-bit
Windows CPUs (and Win32 metadata for an explicitly selected 32-bit build).
The fixture implements original IChain and uses standard
`IDispatchImpl<IChain, &IID_IChain, &LIBID_COMMAPLib, 0xffff, 0xffff>`:
STA/MTA tests resolve Name2 and invoke its BSTR property through real
OLEAUT32. They also check TypeLib identity, pointer width, missing IID and
LIBID failures, object/module lifetime, and unchanged registered-library
lookup results. CAtlBaseModule identifies its own PE image, including in a DLL.

The same fixture additionally registers/unregisters its actual TypeLib under
HKCU through standard module/global APIs and verifies registry-mode dispatch.
Native DACL denial rejects TypeLib writes and proves class-registration
rollback; tests refuse preexisting sample LIBID/interface keys. This is
per-user acceptance, not machine-wide permission or transactional-registry
qualification.

This qualifies original **IDL generation, embedded/registered TypeLib loading and one
real Automation property**, not original COMMap project compilation/link or
original COMMap class activation.
The unchanged COMMap still needs CComCoClass::Error, exact tear-off member/layout contracts, full registration/API compatibility, broader `IDispatchImpl` and
generated `aggreg/aggreg.idl`. All 37 original pure-ATL project
entries remain **not-qualified**.

## Original ATL RGS grammar fixtures (not sample-build evidence)

The exact, unchanged Chain/Outer, ArrayManager and MapCollection `.rgs`
resources from the pinned Microsoft ATL tree are now separately admitted
by a read-only, bounded RGS parser. Their original Git blob SHA-1 values
and MIT license are recorded in
[`tests/atl_rgs/README.md`](../tests/atl_rgs/README.md). Native Windows
COM tests compile these samples as wide string resources and check grammar,
key/value projection, `%MODULE%` substitution and fail-closed malformed
documents. Parsing does not execute registry instructions or qualify
the 37 original Microsoft pure-ATL applications.

## Microsoft's ATL samples

The source audit uses `microsoft/VCSamples` commit
`9e1d4475555b76a17a3568369867f1d7b6cc6126`, under
`VC2010Samples/ATL/` (all subdirectories). These are official archived samples, not passing
OpenATL build/runtime tests:

| Sample | Observed dependencies still missing in OpenATL |
| --- | --- |
| COMMap | CComCoClass::Error, exact tear-off member/layout contracts, complete registrar APIs and generated `aggreg/aggreg.idl`; implemented basic/automatic aggregation/tear-off/module/dispatch/map subsets do not qualify the whole project |
| ATLCollections | `ICollectionOnSTLImpl`, `CComEnumOnSTL`, complete registrar APIs and generated/imported type-library integration |
| ATLSafeArray | `atlsafe.h` / `CComSafeArray`, complete control/server support and whole-project generated TypeLib integration |

Adding `atlcom.h` alone does not implement these contracts. The implemented
object/map, basic aggregation and tear-off subsets remain covered by OpenATL's native tests;
see [ATL compatibility](ATL_COMPATIBILITY.md). `CComModule` has a legacy
object-map/lifetime subset. Numeric/named module resource helpers and bounded
replacement maps use the static registrar; dynamic registrar APIs and full
grammar remain incomplete. No standard ATL API is excluded merely because of
the obsolete UI-only scope decision. Source
compatibility with the original pure ATL samples is the target.

Sources:

- [Official WTL release](https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.0.10320%20Release/)
- [Microsoft sample catalog](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-samples)
- [Pinned Microsoft ATL sample sources](https://github.com/microsoft/VCSamples/tree/9e1d4475555b76a17a3568369867f1d7b6cc6126/VC2010Samples/ATL/General)

The original MIDL ITearOff1/ITearOff2 declarations additionally drive ordinary
and cached tear-off vtable/identity/lifetime tests. Their descriptions are
absent from the unchanged library because neither original coclass references
them; GetTypeInfo must fail TYPE_E_ELEMENTNOTFOUND. Each tear-off also implements
the original IOuter interface, whose real embedded/registered type information
drives native Invoke and a retained cached interface after DLL owner release.
This qualifies the mapped API subset, not the unchanged Outer.cpp/Chain.cpp or
the whole COMMap DLL. Exact Microsoft tear-off member pointer/layout compatibility
and complete original-project admission remain separate.
