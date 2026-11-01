# Original Microsoft COMMap class and MIDL sources

The snapshot comes from `microsoft/VCSamples@9e1d4475555b76a17a3568369867f1d7b6cc6126`,
`VC2010Samples/ATL/General/commap`. `original_sources.json` records the exact Git
blob SHA of all 30 original files: COMMap.idl, Outer/Chain source and headers,
premap/resource headers, the nested aggreg classes/headers/Aggreg.idl, and all
six RGS scripts and both module .cpp/.rc/.def sets. Outer.rgs/Chain.rgs reuse the existing canonical fixtures.
`check_idl.py` checks every byte before native generation/build. Attributes
preserve source bytes and original whitespace; the upstream MIT license is
retained in `../MICROSOFT_LICENSE.txt`.

Windows SDK MIDL generates real COMMap/aggreg declarations, IID sources and
TypeLibs for the selected target architecture. The existing ActiveX events
case compiles the unchanged Outer.cpp/Chain.cpp/Agg.cpp/AggBlind.cpp/AutoAgg.cpp/
AutoAggB.cpp and their original headers against OpenWTL::WTL. A separate native
harness calls their actual class factories and methods; no original method,
macro or CLSID is renamed or replaced. The target uses the original apartment
threading definition consistently and runs those single-threaded classes in STA.
No new target, mode, test entry or workflow is added.

The harness verifies repeated header object entries collapse to six logical
classes, actual process-local CoCreateInstance, all eight original GetName
routes, canonical identity, independent/cached tear-offs, failed and partially
failed FinalConstruct cleanup, native ITypeInfo::Invoke from the real embedded
COMMap TypeLib, original Error/IErrorInfo and EXCEPINFO, and retirement after
class-factory revocation while a cached interface retains the owner.

The six original automatic UpdateRegistry callbacks execute through selected
module RegisterServer/UnregisterServer calls against a unique temporary HKCU
root. Original resource IDs overlap, so COMMap scripts live in the existing
ActiveX executable and Aggreg scripts in the existing COM fixture DLL. Tests
check the actual image paths (including EXE %MODULE% quoting), ProgIDs,
threading values, duplicate-install
rejection and reverse cleanup. No globally visible class registration is used.

Both native MIDL TypeLibs are embedded as resources 1 and 2. Public per-user
AtlRegisterTypeLib/AtlUnRegisterTypeLib calls exercise the unchanged classes'
registered-mode IDispatchImpl: IOuter GetName across eight routes, IChain and
four aggregate properties, independent library removal, and missing-library
errors with no embedded-image fallback. The original TypeLib omits both tear-off
IIDs, which correctly return TYPE_E_ELEMENTNOTFOUND while COMMap is registered.
Held native ITypeInfo remains invocable after unregister, while fresh object
lookups fail. Tests refuse preexisting sample LIBID/interface/CLSID keys, serialize
with existing server registration cases, and remove their TypeLib registrations.

The two existing unload DLLs additionally compile the unchanged COMMap.cpp
and Aggreg.cpp module entry points with their respective class bodies. The
original .rc files, including version/string/REGISTRY/TYPELIB resources, are
copied without byte changes beside the generated native MIDL output. Their
four original private COM exports are appended to each existing GUI export
list. Existing GUI unload/callback tests retain their no-COM behavior and
stdout markers. A separate test-only translation unit selects per-user
registration metadata before any export is called; it performs no registry
or COM work during DLL initialization.

After the host's process-local factories and module have retired, the same
STA events case loads both DLLs and calls their original registration exports.
Native CoCreateInstance executes all eight routes and registered dispatch
across both images. Interface vtable allocation bases must match their actual
DLLs, and each DLL rejects the other one's classes. Tests check real per-user
class paths and independent TypeLib paths, unregister both while a cached
tear-off retains the original owner, then retire all interfaces and COM caches
before FreeLibrary verifies both images are unmapped. Preexisting sample
CLSID/LIBID/interface/ProgID keys are rejected before installation.

This qualifies original class, module, resource and per-user two-DLL behavior
inside existing test images. Unchanged VCXPROJ/solution builds, original output
filenames/export layout, machine-wide deployment and exact Microsoft tear-off
member/layout compatibility remain separate. All 37 whole original pure-ATL
applications remain not-qualified. Non-Windows cross hosts do not generate
Windows SDK MIDL and do not count as original-module admission.

Native MIDL-generated headers additionally receive MinGW UUID template
bindings derived from their own annotations, reusing the existing Ribbon UUID
formatter. No original source or handwritten IID table is substituted. The
original COuter map puts its explicit blind aggregate before local
ISupportErrorInfo; QI therefore returns that aggregate's support view. The
harness checks this ordering and separately checks COuter's own support method.
