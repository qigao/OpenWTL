# Original Microsoft COMMap class and MIDL sources

The snapshot comes from `microsoft/VCSamples@9e1d4475555b76a17a3568369867f1d7b6cc6126`,
`VC2010Samples/ATL/General/commap`. `original_sources.json` records the exact Git
blob SHA of all 18 original files: COMMap.idl, Outer/Chain source and headers,
premap/resource headers, and the nested aggreg classes/headers/Aggreg.idl.
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
No new executable, mode, test entry, workflow or exported function is added.

The harness verifies repeated header object entries collapse to six logical
classes, actual process-local CoCreateInstance, all eight original GetName
routes, canonical identity, independent/cached tear-offs, failed and partially
failed FinalConstruct cleanup, native ITypeInfo::Invoke from the real embedded
COMMap TypeLib, original Error/IErrorInfo and EXCEPINFO, and retirement after
class-factory revocation while a cached interface retains the owner. It does
not write class/TypeLib registry keys. Registered-mode IDispatchImpl is not
replaced with a file/image provider; the harness explicitly invokes ITypeInfo.

This qualifies original **class source and behavior** in the existing test
image. Original COMMap.cpp/Aggreg.cpp DLL module entry points, their separate
binary layouts/resources, complete registration and whole VCXPROJ deployment
remain unqualified. All 37 original pure-ATL applications therefore remain
not-qualified. Exact Microsoft tear-off member/layout compatibility is also
separate. Non-Windows cross hosts do not run Windows SDK MIDL and do not count
as native original-class admission.


Native MIDL-generated headers additionally receive MinGW UUID template
bindings derived from their own annotations, reusing the existing Ribbon UUID
formatter. No original source or handwritten IID table is substituted. The
original COuter map puts its explicit blind aggregate before local
ISupportErrorInfo; QI therefore returns that aggregate's support view. The
harness checks this ordering and separately checks COuter's own support method.
