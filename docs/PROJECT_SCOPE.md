# OpenWTL project charter — WTL frontend and ATL backend

**Canonical scope: OpenWTL includes WTL 10.0.10320 and its OpenATL backend implements ATL-compatible public headers and semantics for both COM Client and COM Server, Win32 windowing, ATL utility classes and Windows Template Library (WTL) consumers.** Previous UI-only or COM-client-only restrictions are superseded.

## Required capabilities

- **COM Client:** COM apartment ownership, system/component activation, interface identity and smart-pointer ownership, BSTR/VARIANT/SAFEARRAY, Automation, cross-apartment integration and event sinks.
- **COM Server:** class factories and IClassFactory, nonaggregated/aggregated objects, standard COM maps and object maps, CAtlModule/CAtlDllModuleT/CAtlExeModuleT, CLSID/ProgID/AppID registration and RGS, class-object activation, DLL/EXE lifecycle, IDispatch/type libraries and connection-point server components.
- **ATL/WTL:** source-compatible public ATL utility and windowing APIs with official WTL 10 applications and real downstream consumers.

These are **project goals, not claims of present implementation**. The [compatibility matrix](ATL_COMPATIBILITY.md) records what is available, missing and qualified. Both client and server API families are first-class; code from PR #17 is a supported starting point, not a subsystem to move out.

## Current evidence and gaps

- Repository-owned ATL headers under alt/include/ are the production implementation, not a silent Microsoft ATL fallback.
- The public source-tree target is OpenWTL::WTL, which includes the internal openatl_atl backend. That backend supports COM Client helpers and real explicit in-process COM Server factories, locks, registry registration and Windows native STA/MTA activation.
- Existing native tests exercise both sides; no-COM-init Win32/WTL UI continues to be a distinct supported case.
- Standard CAtlDllModuleT, basic/chained/explicit aggregate COM maps, basic/automatic aggregation, ordinary/cached tear-offs, native rich error reporting, OBJECT_ENTRY_AUTO, legacy CComModule object maps and bounded RGS resources have implemented subsets. Exact tear-off pointer/layout contracts, expanded maps, complete TypeLib registration/dispatch, full RGS, SAFEARRAY wrappers and portions of ActiveX/control support remain compatibility gaps.
- 11 official WTL samples have Win32 builds; 37 non-MFC Microsoft ATL projects remain a separate incomplete compatibility target.

## Acceptance

Original unmodified [ATL/WTL examples](OFFICIAL_SAMPLES.md) and downstream consumers are the conformance sources; compile/link is not a substitute for native behavior. Qualify real Windows COM activation, apartment lifetime, identity, module locks/unload and registry rollback with MSVC and llvm-mingw. Build only MSVC/llvm-mingw Windows x64/ARM64 in CI and bind all native CI claims to the exact HEAD.

Source ownership, safe lifetimes, provenance, exact-head CI and truthful unsupported HRESULTs remain essential engineering criteria, not limits excluding COM Server.

Build integration uses source CMake through `OpenWTL::WTL`; no installed SDK or installer/consumer project is supplied. UI code without COM activity is not required to initialize a COM apartment.

Tracking: [full ATL roadmap #20](https://github.com/qigao/OpenWTL/issues/20), [COM Server #3](https://github.com/qigao/OpenWTL/issues/3), [Weasel consumer #18](https://github.com/qigao/OpenWTL/issues/18).
